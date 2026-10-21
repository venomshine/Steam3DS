#include "img.h"
#include "net.h"
#include "ui.h"

#include <stdlib.h>
#include <string.h>

#define SLOTS 14

typedef struct {
	char *url;
	int max_w;          // la misma imagen puede pedirse grande (captura) y pequeña (miniatura)
	Req *req;
	bool ready, failed;
	C3D_Tex tex;
	Tex3DS_SubTexture sub;
	Pic pic;
	u64 used;
} Slot;

static Slot slots[SLOTS];
static u64 frame;

void img_init(void) {}

static void slot_clear(Slot *s)
{
	if (s->req) net_release(s->req);
	if (s->ready) C3D_TexDelete(&s->tex);
	free(s->url);
	memset(s, 0, sizeof *s);
}

void img_exit(void)
{
	for (int i = 0; i < SLOTS; i++) slot_clear(&slots[i]);
}

static int pow2(int v)
{
	int p = 8;
	while (p < v) p <<= 1;
	return p;
}

// RGB → textura RGB8 en el orden en mosaico (8×8, Morton) que espera la GPU
static bool upload(Slot *s, const u8 *rgb, int w, int h)
{
	int tw = pow2(w), th = pow2(h);
	if (tw > 1024 || th > 1024) return false;
	if (!C3D_TexInit(&s->tex, tw, th, GPU_RGB8)) return false;
	u8 *dst = s->tex.data;
	memset(dst, 0, tw * th * 3);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			u32 off = ((((y >> 3) * (tw >> 3) + (x >> 3)) << 6) +
				((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3))) * 3;
			const u8 *p = rgb + (y * w + x) * 3;
			dst[off + 0] = p[2];
			dst[off + 1] = p[1];
			dst[off + 2] = p[0];
		}
	}
	C3D_TexFlush(&s->tex);
	C3D_TexSetFilter(&s->tex, GPU_LINEAR, GPU_LINEAR);
	s->sub.width = w;
	s->sub.height = h;
	s->sub.left = 0;
	s->sub.top = 1.0f;
	s->sub.right = (float)w / tw;
	s->sub.bottom = 1.0f - (float)h / th;
	s->pic.img.tex = &s->tex;
	s->pic.img.subtex = &s->sub;
	s->pic.w = w;
	s->pic.h = h;
	s->pic.born = g_time;
	return true;
}

void img_update(void)
{
	frame++;
	// Como mucho una subida por frame para no dar tirones
	for (int i = 0; i < SLOTS; i++) {
		Slot *s = &slots[i];
		if (!s->req) continue;
		if (s->req->state == REQ_DONE) {
			if (s->req->pixels && upload(s, s->req->pixels, s->req->w, s->req->h)) s->ready = true;
			else s->failed = true;
			net_release(s->req);
			s->req = NULL;
			break;
		}
		if (s->req->state == REQ_FAILED) {
			s->failed = true;
			net_release(s->req);
			s->req = NULL;
		}
	}
}

static Slot *find(const char *url, int max_w)
{
	for (int i = 0; i < SLOTS; i++)
		if (slots[i].url && slots[i].max_w == max_w && !strcmp(slots[i].url, url)) return &slots[i];
	return NULL;
}

const Pic *img_get(const char *url, int max_w)
{
	if (!url || !*url) return NULL;
	Slot *s = find(url, max_w);
	if (!s) {
		// Hueco libre o el que lleva más tiempo sin usarse (nunca uno de este frame)
		Slot *best = NULL;
		for (int i = 0; i < SLOTS; i++) {
			Slot *c = &slots[i];
			if (!c->url) { best = c; break; }
			if (c->used == frame) continue;
			if (!best || c->used < best->used) best = c;
		}
		if (!best) return NULL;
		slot_clear(best);
		best->url = strdup(url);
		best->max_w = max_w;
		best->req = net_get_jpeg(url, max_w);
		s = best;
	}
	s->used = frame;
	return s->ready ? &s->pic : NULL;
}

bool img_failed(const char *url)
{
	for (int i = 0; url && i < SLOTS; i++)
		if (slots[i].url && slots[i].failed && !strcmp(slots[i].url, url)) return true;
	return false;
}

static float fade_in(const Pic *p)
{
	float a = (g_time - p->born) / 0.25f;
	return a < 0 ? 0 : a > 1 ? 1 : a;
}

void img_draw_cover(const Pic *p, float x, float y, float w, float h, float focus_y)
{
	float scale = w / p->w;
	if (p->h * scale < h) scale = h / p->h;
	float vw = w / scale, vh = h / scale;            // parte visible, en píxeles de la imagen
	float sx = (p->w - vw) / 2;
	float sy = (p->h - vh) * focus_y;
	const Tex3DS_SubTexture *full = p->img.subtex;
	float tw = p->img.tex->width, th = p->img.tex->height;
	Tex3DS_SubTexture sub = {
		.width = (u16)w, .height = (u16)h,
		.left = sx / tw, .right = (sx + vw) / tw,
		.top = full->top - sy / th, .bottom = full->top - (sy + vh) / th,
	};
	C2D_Image im = { p->img.tex, &sub };
	C2D_ImageTint tint;
	C2D_AlphaImageTint(&tint, fade_in(p));
	C2D_DrawImageAt(im, x, y, 0.5f, &tint, w / sub.width, h / sub.height);
}

void img_draw_fit_a(const Pic *p, float x, float y, float w, float h, float alpha)
{
	float scale = w / p->w;
	if (p->h * scale > h) scale = h / p->h;
	float dw = p->w * scale, dh = p->h * scale;
	C2D_ImageTint tint;
	C2D_AlphaImageTint(&tint, fade_in(p) * alpha);
	C2D_DrawImageAt(p->img, x + (w - dw) / 2, y + (h - dh) / 2, 0.5f, &tint, scale, scale);
}

void img_draw_fit(const Pic *p, float x, float y, float w, float h)
{
	img_draw_fit_a(p, x, y, w, h, 1);
}
