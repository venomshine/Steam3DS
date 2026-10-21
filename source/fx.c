#include "fx.h"
#include "ui.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GRID 12          // separación de la rejilla de puntos

typedef struct {
	C3D_Tex tex;
	Tex3DS_SubTexture sub;
	C2D_Image img;
	bool ok;
} BgTex;

static BgTex bg[2];      // 0 arriba, 1 abajo
static gfxScreen_t cur_scr = GFX_TOP;

// ----------------------------------------------------------------------------
// Ruido determinista (mismo resultado en cada arranque)

static u32 hash(u32 x)
{
	x ^= x >> 16;
	x *= 0x7feb352d;
	x ^= x >> 15;
	x *= 0x846ca68b;
	x ^= x >> 16;
	return x;
}

static float hrand(u32 x) { return (hash(x) & 0xffff) / 65535.0f; }

// Mezcla «encima» (over) de un color con alfa sobre un píxel RGBA en coma flotante
static void over(float *px, float r, float g, float b, float a)
{
	float oa = a + px[3] * (1 - a);
	if (oa <= 0) return;
	for (int i = 0; i < 3; i++) {
		float src = i == 0 ? r : i == 1 ? g : b;
		px[i] = (src * a + px[i] * px[3] * (1 - a)) / oa;
	}
	px[3] = oa;
}

// Textura del fondo: brillo azul arriba, rejilla de puntos, líneas de escaneo, grano y viñeta.
// Se dibuja encima del degradado de base con una sola imagen.
static void build_bg(BgTex *t, int w, int h, u32 salt)
{
	const int tw = 512, th = 256;
	if (!C3D_TexInit(&t->tex, tw, th, GPU_RGBA8)) return;
	u32 *dst = t->tex.data;
	memset(dst, 0, tw * th * 4);
	for (int y = 0; y < h; y++) {
		for (int x = 0; x < w; x++) {
			float px[4] = {0, 0, 0, 0};
			float nx = (x - w * 0.5f) / (w * 0.5f), ny = (y - h * 0.5f) / (h * 0.5f);
			// Brillo frío que baja desde arriba
			float gy = y / (float)h;
			float glow = expf(-gy * 3.2f) * (1 - 0.6f * nx * nx) * 0.10f;
			over(px, 0.40f, 0.75f, 0.96f, glow);
			// Rejilla de puntos (cada GRID px), más visibles en el centro
			int gx = x % GRID, gyy = y % GRID;
			if ((gx == 0 || gx == 1) && (gyy == 0 || gyy == 1)) {
				float center = 1 - 0.55f * (nx * nx + ny * ny);
				float k = (gx == 0 && gyy == 0) ? 1.0f : 0.45f;
				over(px, 0.55f, 0.80f, 1.0f, 0.085f * k * (center > 0 ? center : 0));
			}
			// Líneas de escaneo muy suaves
			if (y % 3 == 0) over(px, 0, 0, 0, 0.045f);
			// Grano
			float n = hrand(x * 7919 + y * 104729 + salt);
			if (n > 0.5f) over(px, 1, 1, 1, (n - 0.5f) * 0.035f);
			else over(px, 0, 0, 0, (0.5f - n) * 0.06f);
			// Viñeta
			float v = nx * nx * 0.55f + ny * ny * 0.75f;
			over(px, 0, 0, 0, v > 0.18f ? (v - 0.18f) * 0.42f : 0);
			u32 off = (((y >> 3) * (tw >> 3) + (x >> 3)) << 6) +
				((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3));
			u8 r = (u8)(px[0] * 255), g = (u8)(px[1] * 255), b = (u8)(px[2] * 255), a = (u8)(px[3] * 255);
			dst[off] = ((u32)r << 24) | ((u32)g << 16) | ((u32)b << 8) | a;
		}
	}
	C3D_TexFlush(&t->tex);
	C3D_TexSetFilter(&t->tex, GPU_NEAREST, GPU_NEAREST);
	t->sub = (Tex3DS_SubTexture){(u16)w, (u16)h, 0, 1.0f, (float)w / tw, 1.0f - (float)h / th};
	t->img.tex = &t->tex;
	t->img.subtex = &t->sub;
	t->ok = true;
}

void fx_init(void)
{
	build_bg(&bg[0], 400, 240, 1);
	build_bg(&bg[1], 320, 240, 2);
}

void fx_exit(void)
{
	for (int i = 0; i < 2; i++)
		if (bg[i].ok) C3D_TexDelete(&bg[i].tex);
}

// ----------------------------------------------------------------------------
// Fondo

void fx_backdrop(gfxScreen_t scr, float w, float h)
{
	cur_scr = scr;
	ui_vgrad(0, 0, w, h, C2D_Color32(0x1d, 0x2c, 0x3e, 0xff), C2D_Color32(0x12, 0x1a, 0x25, 0xff));
	BgTex *t = &bg[scr == GFX_TOP ? 0 : 1];
	if (t->ok) C2D_DrawImageAt(t->img, 0, 0, 0.5f, NULL, 1, 1);
	// Rejilla que parpadea: unos pocos puntos se encienden y se apagan despacio
	for (int i = 0; i < 7; i++) {
		float period = 2.6f + i * 0.37f;
		int cycle = (int)((g_time + i * 0.9f) / period);
		float ph = fmodf(g_time + i * 0.9f, period) / period;
		u32 hsh = hash(cycle * 31 + i * 977 + (scr == GFX_TOP ? 0 : 5000));
		int cx = (hsh % (u32)(w / GRID)) * GRID, cy = ((hsh >> 12) % (u32)(h / GRID)) * GRID;
		float a = sinf(ph * (float)M_PI);
		ui_rect(cx - 0.5f, cy - 0.5f, 3, 3, col_alpha(COL_ACC, a * 0.55f));
		ui_rect(cx - 2.5f, cy - 2.5f, 7, 7, col_alpha(COL_ACC, a * 0.08f));
	}
}

// ----------------------------------------------------------------------------
// Paneles y barras

void fx_glow(float x, float y, float w, float h, float r, u32 col, float spread)
{
	for (int i = 3; i >= 1; i--) {
		float s = spread * i / 3;
		ui_rrect(x - s, y - s, w + 2 * s, h + 2 * s, r + s, col_alpha(col, 0.06f));
	}
}

void fx_panel(float x, float y, float w, float h, float r, float alpha)
{
	ui_rrect(x, y + 2, w, h, r, C2D_Color32(0, 0, 0, (u8)(0x50 * alpha)));
	ui_rrect(x, y, w, h, r, col_alpha(C2D_Color32(0x3a, 0x55, 0x6f, 0xff), alpha));
	ui_rrect(x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 0, col_alpha(C2D_Color32(0x1c, 0x2b, 0x3c, 0xff), alpha));
	float in = r > 2 ? r : 2;
	ui_vgrad(x + in, y + 1, w - 2 * in, h * 0.45f, col_alpha(C2D_Color32(0x7f, 0xb8, 0xe0, 0xff), 0.10f * alpha), col_alpha(C2D_Color32(0x7f, 0xb8, 0xe0, 0xff), 0));
	ui_rect(x + in, y + 1, w - 2 * in, 1, col_alpha(COL_WHITE, 0.10f * alpha));
}

void fx_bar(float x, float y, float w, float h, bool line_bottom)
{
	ui_vgrad(x, y, w, h, C2D_Color32(0x16, 0x1c, 0x26, 0xf4), C2D_Color32(0x10, 0x14, 0x1b, 0xf4));
	float ly = line_bottom ? y + h - 1 : y;
	// Línea de luz que se apaga hacia los lados
	ui_rect(x, ly, w, 1, C2D_Color32(0x2a, 0x3c, 0x50, 0xff));
	C2D_DrawRectangle(x, ly, 0.5f, w / 2, 1, col_alpha(COL_ACC, 0), col_alpha(COL_ACC, 0.55f), col_alpha(COL_ACC, 0), col_alpha(COL_ACC, 0.55f));
	C2D_DrawRectangle(x + w / 2, ly, 0.5f, w / 2, 1, col_alpha(COL_ACC, 0.55f), col_alpha(COL_ACC, 0), col_alpha(COL_ACC, 0.55f), col_alpha(COL_ACC, 0));
}

void fx_glow_line(float x, float y, float w, u32 col)
{
	ui_rect(x, y - 2, w, 1, col_alpha(col, 0.12f));
	ui_rect(x, y - 1, w, 1, col_alpha(col, 0.35f));
	ui_rect(x, y, w, 1, col);
	ui_rect(x, y + 1, w, 1, col_alpha(col, 0.35f));
	ui_rect(x, y + 2, w, 1, col_alpha(col, 0.12f));
}

void fx_brackets(float x, float y, float w, float h, float len, u32 col)
{
	ui_rect(x, y, len, 1, col);
	ui_rect(x, y, 1, len, col);
	ui_rect(x + w - len, y, len, 1, col);
	ui_rect(x + w - 1, y, 1, len, col);
	ui_rect(x, y + h - 1, len, 1, col);
	ui_rect(x, y + h - len, 1, len, col);
	ui_rect(x + w - len, y + h - 1, len, 1, col);
	ui_rect(x + w - 1, y + h - len, 1, len, col);
}

// ----------------------------------------------------------------------------
// Haz de luz por el borde

static void perim(float x, float y, float w, float h, float s, float *ox, float *oy)
{
	float P = 2 * (w + h);
	s = fmodf(s, P);
	if (s < 0) s += P;
	if (s < w) { *ox = x + s; *oy = y; return; }
	s -= w;
	if (s < h) { *ox = x + w; *oy = y + s; return; }
	s -= h;
	if (s < w) { *ox = x + w - s; *oy = y + h; return; }
	s -= w;
	*ox = x;
	*oy = y + h - s;
}

void fx_beam(float x, float y, float w, float h, u32 col, float speed, float len)
{
	float P = 2 * (w + h);
	for (int k = 0; k < 2; k++) {   // dos haces enfrentados
		float head = g_time * speed + k * P / 2;
		const int N = 10;
		for (int i = 0; i < N; i++) {
			float s0 = head - len * i / N, s1 = head - len * (i + 1) / N;
			float a = 1 - (float)i / N;
			// Si el tramo dobla una esquina se parte en dos por la esquina
			float x0, y0, x1, y1;
			perim(x, y, w, h, s0, &x0, &y0);
			perim(x, y, w, h, s1, &x1, &y1);
			u32 c = col_alpha(col, a * 0.95f);
			if (fabsf(x0 - x1) > 0.01f && fabsf(y0 - y1) > 0.01f) {
				// La esquina más cercana entre los dos puntos
				float qx = (fabsf(x0 - x) < 0.01f || fabsf(x0 - x - w) < 0.01f) ? x0 : x1;
				float qy = (fabsf(y0 - y) < 0.01f || fabsf(y0 - y - h) < 0.01f) ? y0 : y1;
				C2D_DrawLine(x0, y0, c, qx, qy, c, 1.6f, 0.5f);
				C2D_DrawLine(qx, qy, c, x1, y1, c, 1.6f, 0.5f);
			} else {
				C2D_DrawLine(x0, y0, c, x1, y1, c, 1.6f, 0.5f);
			}
		}
		float hx, hy;
		perim(x, y, w, h, head, &hx, &hy);
		ui_circle(hx, hy, 2.2f, col_alpha(COL_WHITE, 0.9f));
		ui_circle(hx, hy, 4.5f, col_alpha(col, 0.25f));
	}
}

// ----------------------------------------------------------------------------
// Botones

void fx_button(float x, float y, float w, float h, float r, const FxButton *b)
{
	float a = b->alpha;
	u32 top = b->top, bot = b->bot;
	float in = 0;
	if (b->pressed) {
		in = 1.5f;
		top = col_lerp(top, COL_WHITE, 0.18f);
		bot = col_lerp(bot, COL_WHITE, 0.12f);
	}
	x += in;
	y += in;
	w -= 2 * in;
	h -= 2 * in;
	if (b->focus && a >= 1) fx_glow(x, y, w, h, r, top, 5);
	if (!b->pressed) ui_rrect(x, y + 2, w, h, r, C2D_Color32(0, 0, 0, (u8)(0x55 * a)));
	// Borde claro y cuerpo con degradado
	ui_rrect(x, y, w, h, r, col_alpha(col_lerp(top, COL_WHITE, 0.30f), a));
	ui_rrect(x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 0, col_alpha(bot, a));
	float gi = r * 0.6f + 1;
	ui_vgrad(x + gi, y + 1, w - 2 * gi, h - 2, col_alpha(top, a), col_alpha(bot, a));
	ui_vgrad(x + 1, y + gi, gi, h - 2 * gi, col_alpha(top, a), col_alpha(bot, a));
	ui_vgrad(x + w - 1 - gi, y + gi, gi, h - 2 * gi, col_alpha(top, a), col_alpha(bot, a));
	// Bisel: luz arriba, sombra abajo
	ui_rect(x + r, y + 1, w - 2 * r, 1, col_alpha(COL_WHITE, 0.30f * a));
	ui_vgrad(x + r, y + 2, w - 2 * r, h * 0.4f, col_alpha(COL_WHITE, 0.10f * a), col_alpha(COL_WHITE, 0));
	ui_rect(x + r, y + h - 2, w - 2 * r, 1, col_alpha(C2D_Color32(0, 0, 0, 0xff), 0.22f * a));
	// Destello que cruza el botón cada pocos segundos
	if (b->shine && a >= 1) {
		const float period = 3.6f, dur = 0.75f;
		float t = fmodf(g_time + b->seed, period);
		if (t < dur) {
			float p = t / dur;
			float bw = 22, slant = h * 0.6f;
			float sx = x - bw - slant + (w + 2 * bw + 2 * slant) * p;
			ui_scissor(cur_scr, x + 1, y + 1, w - 2, h - 2);
			u32 c0 = col_alpha(COL_WHITE, 0), c1 = col_alpha(COL_WHITE, 0.28f);
			// Banda inclinada: dos triángulos con degradado de transparente a blanco y vuelta
			C2D_DrawTriangle(sx, y + h, c0, sx + slant, y, c0, sx + bw / 2, y + h, c1, 0.5f);
			C2D_DrawTriangle(sx + slant, y, c0, sx + bw / 2 + slant, y, c1, sx + bw / 2, y + h, c1, 0.5f);
			C2D_DrawTriangle(sx + bw / 2, y + h, c1, sx + bw / 2 + slant, y, c1, sx + bw, y + h, c0, 0.5f);
			C2D_DrawTriangle(sx + bw / 2 + slant, y, c1, sx + bw + slant, y, c0, sx + bw, y + h, c0, 0.5f);
			ui_scissor_off();
		}
	}
	if (b->focus && a >= 1) fx_beam(x, y, w, h, col_lerp(top, COL_WHITE, 0.5f), 140, 46);
}

// ----------------------------------------------------------------------------
// Ondas al tocar

typedef struct { float x, y, t; bool on; } Ripple;
static Ripple ripples[6];
static int next_ripple;

void fx_ripple(float x, float y)
{
	ripples[next_ripple] = (Ripple){x, y, 0, true};
	next_ripple = (next_ripple + 1) % 6;
}

void fx_ripples_draw(void)
{
	for (int i = 0; i < 6; i++) {
		Ripple *r = &ripples[i];
		if (!r->on) continue;
		float p = r->t / 0.45f;
		if (p >= 1) { r->on = false; continue; }
		float e = ease_out_cubic(p), rad = 6 + 34 * e;
		u32 c = col_alpha(COL_ACC, (1 - p) * 0.6f);
		const int N = 24;
		for (int k = 0; k < N; k++) {
			float a0 = k * 2 * (float)M_PI / N, a1 = (k + 1) * 2 * (float)M_PI / N;
			C2D_DrawLine(r->x + cosf(a0) * rad, r->y + sinf(a0) * rad, c, r->x + cosf(a1) * rad, r->y + sinf(a1) * rad, c, 1.5f, 0.5f);
		}
		ui_circle(r->x, r->y, rad * 0.5f, col_alpha(COL_ACC, (1 - p) * 0.10f));
	}
}

// ----------------------------------------------------------------------------
// Confeti

#define NCONF 70
typedef struct { float x, y, vx, vy, rot, vr, life; u32 col; float w, h; } Conf;
static Conf conf[NCONF];
static bool conf_on;

void fx_confetti(float x, float y)
{
	static const u32 cols[] = {0xff66c0f4, 0xffbeee11, 0xffffffff, 0xff7ad3ff, 0xff5c7e10, 0xffe8c547};
	for (int i = 0; i < NCONF; i++) {
		float a = -(float)M_PI / 2 + (hrand(i * 3 + 1 + (u32)(g_time * 1000)) - 0.5f) * 2.4f;
		float sp = 90 + hrand(i * 7 + 2 + (u32)(g_time * 997)) * 170;
		u32 c = cols[i % 6];
		// Los colores van como 0xAARRGGBB; citro2d quiere 0xAABBGGRR
		u32 cc = C2D_Color32((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, 0xff);
		conf[i] = (Conf){x, y, cosf(a) * sp, sinf(a) * sp, hrand(i + 11) * 6, (hrand(i + 23) - 0.5f) * 14,
			1.6f + hrand(i + 37) * 1.2f, cc, 3 + hrand(i + 41) * 3, 2 + hrand(i + 43) * 2};
	}
	conf_on = true;
}

void fx_confetti_draw(void)
{
	if (!conf_on) return;
	bool any = false;
	for (int i = 0; i < NCONF; i++) {
		Conf *c = &conf[i];
		if (c->life <= 0) continue;
		any = true;
		float a = c->life > 0.5f ? 1 : c->life / 0.5f;
		// Gira «de canto»: el ancho visible cambia con el giro
		float cw = c->w * fabsf(cosf(c->rot)), ch = c->h;
		float s = sinf(c->rot * 0.5f), co = cosf(c->rot * 0.5f);
		float dx1 = co * cw / 2, dy1 = s * cw / 2, dx2 = -s * ch / 2, dy2 = co * ch / 2;
		u32 col = col_alpha(c->col, a);
		C2D_DrawTriangle(c->x - dx1 - dx2, c->y - dy1 - dy2, col, c->x + dx1 - dx2, c->y + dy1 - dy2, col, c->x + dx1 + dx2, c->y + dy1 + dy2, col, 0.5f);
		C2D_DrawTriangle(c->x - dx1 - dx2, c->y - dy1 - dy2, col, c->x + dx1 + dx2, c->y + dy1 + dy2, col, c->x - dx1 + dx2, c->y - dy1 + dy2, col, 0.5f);
	}
	if (!any) conf_on = false;
}

// ----------------------------------------------------------------------------

void fx_update(float dt)
{
	for (int i = 0; i < 6; i++)
		if (ripples[i].on) ripples[i].t += dt;
	if (conf_on) {
		for (int i = 0; i < NCONF; i++) {
			Conf *c = &conf[i];
			if (c->life <= 0) continue;
			c->vy += 260 * dt;
			c->vx *= 1 - 1.2f * dt;
			c->vy *= 1 - 0.8f * dt;
			c->x += c->vx * dt;
			c->y += c->vy * dt;
			c->rot += c->vr * dt;
			c->life -= dt;
		}
	}
}

float fx_ticker(float *shown, float target, float speed)
{
	*shown = approach(*shown, target, speed);
	if (fabsf(*shown - target) < 0.5f) *shown = target;
	return *shown;
}
