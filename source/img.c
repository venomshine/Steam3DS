#include "img.h"
#include "net.h"
#include "ui.h"

#include <stdlib.h>
#include <string.h>

#define SLOTS 14
#define ICON_SLOTS 40   // aparte: los iconos pequeños (biblioteca, avatares) no echan a las imágenes grandes
#define ALL_SLOTS (SLOTS + ICON_SLOTS)

// Las imágenes se piden en cola, por orden de llegada, unas pocas a la vez: así la consola no
// descarga y decodifica muchas de golpe y no se echan unas a otras antes de llegar
#define BIG_AT_ONCE 2
#define ICON_AT_ONCE 4

typedef struct {
	char *url;
	int max_w;          // la misma imagen puede pedirse grande (captura) y pequeña (miniatura)
	Req *req;
	bool queued;        // pedida, esperando su turno para descargarse
	u64 asked;          // orden de la cola
	bool ready, failed;
	C3D_Tex tex;
	Tex3DS_SubTexture sub;
	Pic pic;
	u64 used;
} Slot;

static Slot slots[ALL_SLOTS];
static u64 frame, asked_seq;

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

// Arranca las que esperan en cola (las más antiguas primero) si hay hueco en su grupo
static void start_queued(int i0, int i1, int limit)
{
	int busy = 0;
	for (int i = i0; i < i1; i++) busy += slots[i].req != NULL;
	while (busy < limit) {
		Slot *next = NULL;
		for (int i = i0; i < i1; i++) {
			Slot *c = &slots[i];
			// Solo las que se siguen viendo (las demás esperan a que vuelvan a pedirse)
			if (c->queued && frame - c->used <= 2 && (!next || c->asked < next->asked)) next = c;
		}
		if (!next) break;
		next->queued = false;
		next->req = net_get_jpeg(next->url, next->max_w);
		if (!next->req) next->failed = true;
		busy++;
	}
}

void img_update(void)
{
	frame++;
	start_queued(0, SLOTS, BIG_AT_ONCE);
	start_queued(SLOTS, ALL_SLOTS, ICON_AT_ONCE);
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
		// Hueco libre o el que lleva más tiempo sin usarse (nunca uno de este frame); una que se
		// está descargando solo si no queda otra (si no, nunca llegaría a verse)
		Slot *best = NULL;
		int i0 = max_w <= 64 ? SLOTS : 0, i1 = max_w <= 64 ? ALL_SLOTS : SLOTS;
		for (int i = i0; i < i1; i++) {
			Slot *c = &slots[i];
			if (!c->url) { best = c; break; }
			if (c->used == frame) continue;
			bool cb = c->req != NULL, bb = best && best->req != NULL;
			if (!best || (bb && !cb) || (bb == cb && c->used < best->used)) best = c;
		}
		if (!best) return NULL;
		slot_clear(best);
		best->url = strdup(url);
		best->max_w = max_w;
		best->queued = true;
		best->asked = ++asked_seq;
		s = best;
	}
	s->used = frame;
	return s->ready ? &s->pic : NULL;
}

int img_pending(void)
{
	int n = 0;
	for (int i = 0; i < ALL_SLOTS; i++) n += slots[i].req != NULL || slots[i].queued;
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
