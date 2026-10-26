#include "img.h"
#include "net.h"
#include "ui.h"

#include <stdlib.h>
#include <string.h>

#define SLOTS 14
#define ICON_SLOTS 40   // aparte: los iconos pequeños (biblioteca, avatares) no echan a las imágenes grandes
#define ALL_SLOTS (SLOTS + ICON_SLOTS)

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

static Slot slots[ALL_SLOTS];
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
	for (int i = 0; i < ALL_SLOTS; i++) slot_clear(&slots[i]);
}

// La imagen llega ya en el orden de la GPU (lo hace el hilo de red): solo se copia
static bool upload(Slot *s, const u8 *tiled, int w, int h, int tw, int th, bool alpha)
{
	if (!tiled || tw > 1024 || th > 1024) return false;
	if (!C3D_TexInit(&s->tex, tw, th, alpha ? GPU_RGBA8 : GPU_RGB8)) return false;
	memcpy(s->tex.data, tiled, (size_t)tw * th * (alpha ? 4 : 3));
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

static bool holding;

void img_hold(bool hold)
{
	holding = hold;
}

void img_update(void)
{
	frame++;
	if (holding) return;   // se suben al acabar la animación (siguen descargándose)
	// Como mucho una subida por frame para no dar tirones
	for (int i = 0; i < ALL_SLOTS; i++) {
		Slot *s = &slots[i];
		if (!s->req) continue;
		if (s->req->state == REQ_DONE) {
			if (s->req->pixels && upload(s, s->req->pixels, s->req->w, s->req->h, s->req->tw, s->req->th, s->req->alpha)) s->ready = true;
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
	for (int i = 0; i < ALL_SLOTS; i++)
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
		int i0 = max_w <= 64 ? SLOTS : 0, i1 = max_w <= 64 ? ALL_SLOTS : SLOTS;
		for (int i = i0; i < i1; i++) {
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

int img_pending(void)
{
	int n = 0;
	for (int i = 0; i < ALL_SLOTS; i++) n += slots[i].req != NULL;
	return n;
}

bool img_failed(const char *url)
{
	for (int i = 0; url && i < ALL_SLOTS; i++)
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
