#include "fx.h"
#include "ui.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define DRIFT 16         // margen de la textura para que el fondo se desplace sin mostrar el borde

typedef struct {
	C3D_Tex tex;
	Tex3DS_SubTexture sub;
	C2D_Image img;
	bool ok;
} BgTex;

static BgTex bg[2];      // 0 arriba, 1 abajo

typedef struct { float x, y, r, a; u8 cr, cg, cb; } Blob;

static float noise(u32 x)
{
	x ^= x >> 16;
	x *= 0x7feb352d;
	x ^= x >> 15;
	x *= 0x846ca68b;
	x ^= x >> 16;
	return (x & 0xffff) / 65535.0f;
}

// Fondo: degradado oscuro y manchas de color muy difuminadas, con un poco de grano para que
// el degradado no haga escalones. Es opaco: se dibuja con una sola imagen.
static void build_bg(BgTex *t, int w, int h, const Blob *b, int nb, u32 salt)
{
	const int tw = 512, th = 256;
	if (!C3D_TexInit(&t->tex, tw, th, GPU_RGBA8)) return;
	u32 *dst = t->tex.data;
	memset(dst, 0, tw * th * 4);
	for (int y = 0; y < h; y++) {
		float gy = y / (float)h;
		float br = 0x13 + (0x0a - 0x13) * gy, bgc = 0x1b + (0x10 - 0x1b) * gy, bb = 0x2a + (0x19 - 0x2a) * gy;
		for (int x = 0; x < w; x++) {
			float c[3] = {br, bgc, bb};
			for (int i = 0; i < nb; i++) {
				float dx = x - b[i].x, dy = y - b[i].y;
				float k = b[i].a * expf(-(dx * dx + dy * dy) / (b[i].r * b[i].r));
				c[0] += (b[i].cr - c[0]) * k;
				c[1] += (b[i].cg - c[1]) * k;
				c[2] += (b[i].cb - c[2]) * k;
			}
			float d = (noise(x * 7919 + y * 104729 + salt) - 0.5f) * 2.2f;
			u8 r = (u8)fminf(255, fmaxf(0, c[0] + d)), g = (u8)fminf(255, fmaxf(0, c[1] + d)), bl = (u8)fminf(255, fmaxf(0, c[2] + d)) ;
			u32 off = (((y >> 3) * (tw >> 3) + (x >> 3)) << 6) +
				((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3));
			dst[off] = ((u32)r << 24) | ((u32)g << 16) | ((u32)bl << 8) | 0xff;
		}
	}
	C3D_TexFlush(&t->tex);
	C3D_TexSetFilter(&t->tex, GPU_LINEAR, GPU_LINEAR);
	t->sub = (Tex3DS_SubTexture){(u16)w, (u16)h, 0, 1.0f, (float)w / tw, 1.0f - (float)h / th};
	t->img.tex = &t->tex;
	t->img.subtex = &t->sub;
	t->ok = true;
}

void fx_init(void)
{
	static const Blob top[] = {
		{70, 30, 170, 0.42f, 0x2a, 0x6c, 0xd8},
		{380, 70, 140, 0.26f, 0x1c, 0xa6, 0xbc},
		{250, 250, 170, 0.24f, 0x5a, 0x3e, 0xb6},
	};
	static const Blob bot[] = {
		{40, 220, 150, 0.32f, 0x2a, 0x6c, 0xd8},
		{320, 40, 140, 0.20f, 0x5a, 0x3e, 0xb6},
		{220, 150, 110, 0.12f, 0x1c, 0xa6, 0xbc},
	};
	build_bg(&bg[0], 400 + 2 * DRIFT, 240 + DRIFT, top, 3, 1);
	build_bg(&bg[1], 320 + 2 * DRIFT, 240 + DRIFT, bot, 3, 2);
}

void fx_exit(void)
{
	for (int i = 0; i < 2; i++)
		if (bg[i].ok) C3D_TexDelete(&bg[i].tex);
}

void fx_backdrop(gfxScreen_t scr, float w, float h)
{
	BgTex *t = &bg[scr == GFX_TOP ? 0 : 1];
	if (!t->ok) {
		ui_vgrad(0, 0, w, h, C2D_Color32(0x13, 0x1b, 0x2a, 0xff), C2D_Color32(0x0a, 0x10, 0x19, 0xff));
		return;
	}
	// Se mueve muy despacio, como un fondo vivo
	float dx = -DRIFT + DRIFT * 0.8f * sinf(g_time * 0.09f + (scr == GFX_TOP ? 0 : 2));
	float dy = -DRIFT / 2 + DRIFT * 0.4f * sinf(g_time * 0.07f + 1);
	C2D_DrawImageAt(t->img, dx, dy, 0.5f, NULL, 1, 1);
}

// ----------------------------------------------------------------------------
// Borde de luz

// Recorre el contorno de un rectángulo redondeado y pinta cada tramo según hacia dónde mira:
// la luz viene de arriba a la izquierda; el lado opuesto tiene un reflejo más débil.
void fx_rim(float x, float y, float w, float h, float r, float alpha, bool chroma)
{
	if (r > h / 2) r = h / 2;
	if (r > w / 2) r = w / 2;
	const float lx = -0.53f, ly = -0.85f;
	const int NC = 5;
	// Puntos del contorno, en sentido horario empezando arriba a la izquierda
	float px[4 * (NC + 1)], py[4 * (NC + 1)];
	int n = 0;
	const float cx[4] = {x + w - r, x + w - r, x + r, x + r}, cy[4] = {y + r, y + h - r, y + h - r, y + r};
	const float a0[4] = {-(float)M_PI / 2, 0, (float)M_PI / 2, (float)M_PI};
	for (int c = 0; c < 4; c++)
		for (int i = 0; i <= NC; i++) {
			float a = a0[c] + (float)M_PI / 2 * i / NC;
			px[n] = cx[c] + cosf(a) * r;
			py[n] = cy[c] + sinf(a) * r;
			n++;
		}
	for (int i = 0; i < n; i++) {
		int j = (i + 1) % n;
		float mx = (px[i] + px[j]) / 2, my = (py[i] + py[j]) / 2;
		// Normal hacia fuera: desde el centro de la forma
		float nx = mx - (x + w / 2), ny = my - (y + h / 2);
		// En los lados rectos la normal es la del lado
		if (fabsf(py[i] - py[j]) < 0.01f) nx = 0;
		if (fabsf(px[i] - px[j]) < 0.01f) ny = 0;
		float len = sqrtf(nx * nx + ny * ny);
		if (len < 0.001f) continue;
		nx /= len;
		ny /= len;
		float d = nx * lx + ny * ly;
		float lit = d > 0 ? d : 0, back = d < 0 ? -d : 0;
		float a = alpha * (0.10f + 0.55f * lit * lit + 0.16f * back);
		u32 c = col_alpha(COL_WHITE, a);
		C2D_DrawLine(px[i], py[i], c, px[j], py[j], c, 1.0f, 0.5f);
		if (chroma) {
			// Reflejo de color justo por dentro: cian donde da la luz, naranja en el lado opuesto
			float ix = px[i] - nx, iy = py[i] - ny, jx = px[j] - nx, jy = py[j] - ny;
			if (lit > 0.25f) {
				u32 cc = col_alpha(C2D_Color32(0x6c, 0xe6, 0xff, 0xff), alpha * 0.55f * lit);
				C2D_DrawLine(ix, iy, cc, jx, jy, cc, 1.0f, 0.5f);
			}
			if (back > 0.25f) {
				u32 cw = col_alpha(C2D_Color32(0xff, 0xa0, 0x58, 0xff), alpha * 0.45f * back);
				C2D_DrawLine(ix, iy, cw, jx, jy, cw, 1.0f, 0.5f);
			}
		}
	}
}

// ----------------------------------------------------------------------------
// Superficies

static void shadow(float x, float y, float w, float h, float r, float alpha)
{
	ui_rrect(x - 1, y + 1, w + 2, h + 2, r + 1, col_alpha(C2D_Color32(0, 0, 0, 0xff), 0.10f * alpha));
	ui_rrect(x, y + 2, w, h, r, col_alpha(C2D_Color32(0, 0, 0, 0xff), 0.16f * alpha));
}

void fx_glass(float x, float y, float w, float h, float r, float alpha, u32 tint)
{
	shadow(x, y, w, h, r, alpha);
	if (tint) {
		ui_rrect(x, y, w, h, r, col_alpha(tint, 0.82f * alpha));
		float in = r > 3 ? r * 0.7f : 2;
		ui_vgrad(x + in, y + 1, w - 2 * in, h * 0.5f, col_alpha(COL_WHITE, 0.16f * alpha), col_alpha(COL_WHITE, 0));
	} else {
		ui_rrect(x, y, w, h, r, col_alpha(COL_WHITE, 0.075f * alpha));
		float in = r > 3 ? r * 0.7f : 2;
		ui_vgrad(x + in, y + 1, w - 2 * in, h * 0.45f, col_alpha(COL_WHITE, 0.05f * alpha), col_alpha(COL_WHITE, 0));
	}
	fx_rim(x, y, w, h, r, alpha, false);
}

void fx_panel(float x, float y, float w, float h, float r, float alpha)
{
	shadow(x, y, w, h, r, alpha * 2);
	ui_rrect(x, y, w, h, r, col_alpha(C2D_Color32(0x1c, 0x26, 0x36, 0xff), 0.94f * alpha));
	float in = r > 3 ? r * 0.7f : 2;
	ui_vgrad(x + in, y + 1, w - 2 * in, h * 0.35f, col_alpha(COL_WHITE, 0.06f * alpha), col_alpha(COL_WHITE, 0));
	fx_rim(x, y, w, h, r, alpha, false);
}

void fx_bar(float x, float y, float w, float h, bool line_bottom)
{
	ui_rect(x, y, w, h, C2D_Color32(0x08, 0x0c, 0x13, 0x70));
	ui_rect(x, line_bottom ? y + h - 1 : y, w, 1, col_alpha(COL_WHITE, 0.08f));
}

void fx_button(float x, float y, float w, float h, float r, const FxButton *b)
{
	float a = b->alpha;
	u32 tint = b->tint;
	if (b->pressed) {
		// Se encoge un poco y se aclara, como al pulsar un botón de cristal
		x += 1.5f;
		y += 1.5f;
		w -= 3;
		h -= 3;
		r = r > 1.5f ? r - 1.5f : r;
		if (tint) tint = col_lerp(tint, COL_WHITE, 0.2f);
	}
	if (tint) fx_glass(x, y, w, h, r, a, tint);
	else {
		shadow(x, y, w, h, r, a);
		ui_rrect(x, y, w, h, r, col_alpha(COL_WHITE, (b->pressed ? 0.16f : 0.09f) * a));
		float in = r > 3 ? r * 0.7f : 2;
		ui_vgrad(x + in, y + 1, w - 2 * in, h * 0.5f, col_alpha(COL_WHITE, 0.06f * a), col_alpha(COL_WHITE, 0));
		fx_rim(x, y, w, h, r, a, false);
	}
	if (b->focus && a >= 1) {
		// Elegido con la cruceta: un poco más claro y con el reflejo de color en el borde
		ui_rrect(x, y, w, h, r, col_alpha(COL_WHITE, 0.10f));
		fx_rim(x, y, w, h, r, 1, true);
	}
}

void fx_select(float x, float y, float w, float h, float r, float alpha)
{
	ui_rrect(x, y, w, h, r, col_alpha(COL_WHITE, 0.11f * alpha));
	float in = r > 3 ? r * 0.7f : 2;
	ui_vgrad(x + in, y + 1, w - 2 * in, h * 0.5f, col_alpha(COL_WHITE, 0.06f * alpha), col_alpha(COL_WHITE, 0));
	fx_rim(x, y, w, h, r, alpha, true);
}

float fx_ticker(float *shown, float target, float speed)
{
	*shown = approach(*shown, target, speed);
	if (fabsf(*shown - target) < 0.5f) *shown = target;
	return *shown;
}
