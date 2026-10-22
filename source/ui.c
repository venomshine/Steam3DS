#include "ui.h"
#include "qrcodegen.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

C2D_Font g_font = NULL;     // NULL = fuente del sistema
float g_time, g_dt;

static C2D_TextBuf tbuf;
static float line_feed = 30.0f;

// Caché de anchos por carácter: medir es lo que más se repite al maquetar
#define WCACHE 1024
static struct { u32 cp; float w; } wcache[WCACHE];

void ui_init(void)
{
	tbuf = C2D_TextBufNew(8192);
	FINF_s *fi = C2D_FontGetInfo(g_font);
	if (fi) line_feed = fi->lineFeed;
	memset(wcache, 0xff, sizeof wcache);
}

void ui_exit(void)
{
	C2D_TextBufDelete(tbuf);
}

void ui_begin_frame(void)
{
	C2D_TextBufClear(tbuf);
}

static int utf8_next(const char *s, u32 *cp)
{
	const unsigned char *u = (const unsigned char *)s;
	if (u[0] < 0x80) { *cp = u[0]; return 1; }
	if ((u[0] & 0xE0) == 0xC0 && u[1]) { *cp = ((u[0] & 0x1F) << 6) | (u[1] & 0x3F); return 2; }
	if ((u[0] & 0xF0) == 0xE0 && u[1] && u[2]) { *cp = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F); return 3; }
	if ((u[0] & 0xF8) == 0xF0 && u[1] && u[2] && u[3]) {
		*cp = ((u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F);
		return 4;
	}
	*cp = '?';
	return 1;
}

static float char_w(u32 cp)
{
	u32 h = (cp * 2654435761u) & (WCACHE - 1);
	if (wcache[h].cp == cp) return wcache[h].w;
	int gi = C2D_FontGlyphIndexFromCodePoint(g_font, cp);
	charWidthInfo_s *cw = C2D_FontGetCharWidthInfo(g_font, gi);
	float w = cw ? cw->charWidth : 0;
	wcache[h].cp = cp;
	wcache[h].w = w;
	return w;
}

// Kerning manual: tras un dígito, una letra mayúscula queda muy separada («Steam3 DS»)
#define KERN_DIGIT_UPPER (-2.5f)

static bool kern_at(const char *s, size_t i)
{
	return i > 0 && s[i - 1] >= '0' && s[i - 1] <= '9' && s[i] >= 'A' && s[i] <= 'Z';
}

static float text_w_n(const char *s, size_t n, float sz)
{
	float w = 0;
	size_t i = 0;
	while (i < n && s[i]) {
		u32 cp;
		int k = utf8_next(s + i, &cp);
		if (cp != '\n') w += char_w(cp);
		if (kern_at(s, i)) w += KERN_DIGIT_UPPER;
		i += k;
	}
	return w * sz;
}

float ui_text_w(const char *s, float sz)
{
	return s ? text_w_n(s, strlen(s), sz) : 0;
}

float ui_line_h(float sz)
{
	return line_feed * sz;
}

static void draw_raw(float x, float y, const char *s, size_t n, float sz, u32 col)
{
	char tmp[512];
	if (n >= sizeof tmp) n = sizeof tmp - 1;
	memcpy(tmp, s, n);
	tmp[n] = 0;
	C2D_Text t;
	C2D_TextFontParse(&t, g_font, tbuf, tmp);
	C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, sz, sz, col);
}

static void draw_n(float x, float y, const char *s, size_t n, float sz, u32 col)
{
	size_t start = 0;
	for (size_t i = 1; i < n; i++) {
		if (!kern_at(s, i)) continue;
		draw_raw(x, y, s + start, i - start, sz, col);
		x += text_w_n(s + start, i - start, sz) + KERN_DIGIT_UPPER * sz;
		start = i;
	}
	draw_raw(x, y, s + start, n - start, sz, col);
}

float ui_text(float x, float y, const char *s, float sz, u32 col, int align, float max_w)
{
	if (!s || !*s) return 0;
	size_t n = strlen(s);
	float w = text_w_n(s, n, sz);
	bool cut = false;
	if (max_w > 0 && w > max_w) {
		float ell = ui_text_w("…", sz);
		while (n > 0 && w + ell > max_w) {
			do n--; while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80);
			w = text_w_n(s, n, sz);
		}
		while (n > 0 && s[n - 1] == ' ') { n--; w = text_w_n(s, n, sz); }
		cut = true;
		w += ell;
	}
	float dx = align == AL_CENTER ? -w / 2 : align == AL_RIGHT ? -w : 0;
	if (cut) {
		char tmp[512];
		if (n > sizeof tmp - 4) n = sizeof tmp - 4;
		memcpy(tmp, s, n);
		memcpy(tmp + n, "…", 4);
		draw_n(x + dx, y, tmp, n + 3, sz, col);
	} else {
		draw_n(x + dx, y, s, n, sz, col);
	}
	return w;
}

static bool is_cjk(u32 cp)
{
	return (cp >= 0x3000 && cp <= 0x9FFF) || (cp >= 0xFF00 && cp <= 0xFFEF);
}

static bool no_line_start(u32 cp)
{
	return cp == 0x3002 || cp == 0x3001 || cp == 0xFF01 || cp == 0xFF1F || cp == 0x300D || cp == 0xFF09 || cp == 0x30FC;
}

// Calcula dónde termina la siguiente línea que cabe en w. Devuelve bytes consumidos; *len = bytes a dibujar.
size_t ui_next_line(const char *s, float sz, float w, size_t *len)
{
	float x = 0;
	size_t i = 0, last_break = 0, last_break_len = 0;
	while (s[i]) {
		u32 cp;
		int k = utf8_next(s + i, &cp);
		if (cp == '\n') { *len = i; return i + 1; }
		float cw = char_w(cp) * sz;
		if (x + cw > w && i > 0) {
			if (last_break > 0) { *len = last_break_len; return last_break; }
			// Palabra más larga que la línea (o texto CJK): corta aquí,
			// sin empezar la línea siguiente con un signo de cierre
			if (no_line_start(cp)) { *len = i + k; return i + k; }
			*len = i;
			return i;
		}
		x += cw;
		if (cp == ' ') { last_break = i + 1; last_break_len = i; }
		else if (is_cjk(cp)) {
			u32 nx = 0;
			if (s[i + k]) utf8_next(s + i + k, &nx);
			if (!no_line_start(nx)) { last_break = i + k; last_break_len = i + k; }
		}
		i += k;
	}
	*len = i;
	return i;
}

int ui_text_lines(const char *s, float sz, float w)
{
	int lines = 0;
	while (s && *s) {
		size_t len;
		s += ui_next_line(s, sz, w, &len);
		lines++;
	}
	return lines;
}

// Palabras cortas que no pueden quedar justo antes de «…» (la frase se quedaría colgando)
static bool is_connector(const char *w, size_t n)
{
	static const char *const list[] = {
		"y", "e", "o", "u", "a", "de", "del", "la", "el", "lo", "los", "las", "en", "con", "por", "para", "que",
		"al", "un", "una", "ni", "su", "sus", "se", "tu", "mi", "sin", "como", "más", "pero",
		"the", "of", "and", "to", "in", "an", "with", "for", "or", "on", "at", "by", "your", "its", "from", "as", "is",
	};
	for (size_t i = 0; i < sizeof list / sizeof *list; i++)
		if (strlen(list[i]) == n && !strncasecmp(w, list[i], n)) return true;
	return false;
}

// Última línea de un texto que no cabe entero: solo palabras completas. Si hay un final de frase
// en la segunda mitad de la línea, se corta ahí sin «…»; si no, en la última palabra con sentido
// (sin conectores ni comas al final) y con «…».
static void last_line(float x, float y, float w, const char *s, float sz, u32 col)
{
	char tmp[512];
	size_t n = strlen(s);
	if (n > sizeof tmp - 8) n = sizeof tmp - 8;
	memcpy(tmp, s, n);
	tmp[n] = 0;
	for (char *p = tmp; *p; p++) if (*p == '\n') *p = ' ';
	char *t = tmp;
	while (*t == ' ') t++;
	float ell = ui_text_w("…", sz);
	// Hasta dónde caben palabras enteras (dejando sitio para «…»)
	size_t fit = 0, i = 0, len = strlen(t);
	while (i <= len) {
		if (i == len || t[i] == ' ') {
			if (text_w_n(t, i, sz) + ell > w) break;
			fit = i;
		}
		i++;
	}
	if (fit == 0) {
		ui_text(x, y, t, sz, col, AL_LEFT, w);   // una sola palabra larguísima
		return;
	}
	// ¿Un final de frase en la segunda mitad?
	for (size_t k = fit; k > fit / 2; k--) {
		char c = t[k - 1];
		if (c == '.' || c == '!' || c == '?') {
			draw_n(x, y, t, k, sz, col);
			return;
		}
	}
	// Quita conectores y signos colgando del final
	size_t end = fit;
	for (int guard = 0; guard < 4; guard++) {
		while (end > 0 && (t[end - 1] == ' ' || t[end - 1] == ',' || t[end - 1] == ';' || t[end - 1] == ':' || t[end - 1] == '-'))
			end--;
		size_t ws = end;
		while (ws > 0 && t[ws - 1] != ' ') ws--;
		if (ws > 0 && is_connector(t + ws, end - ws)) end = ws;
		else break;
	}
	if (end == 0) end = fit;
	memcpy(t + end, "…", 4);
	draw_n(x, y, t, end + 3, sz, col);
}

// Como ui_text_wrap pero cada línea centrada en cx
int ui_text_wrap_center(float cx, float y, float w, const char *s, float sz, u32 col, int max_lines, float lh)
{
	if (lh <= 0) lh = ui_line_h(sz);
	int lines = 0;
	while (s && *s && (max_lines <= 0 || lines < max_lines)) {
		size_t len;
		size_t used = ui_next_line(s, sz, w, &len);
		if (!used) break;
		char tmp[256];
		size_t n = len < sizeof tmp - 1 ? len : sizeof tmp - 1;
		memcpy(tmp, s, n);
		tmp[n] = 0;
		ui_text(cx, y + lines * lh, tmp, sz, col, AL_CENTER, 0);
		lines++;
		s += used;
	}
	return lines;
}

int ui_text_wrap(float x, float y, float w, const char *s, float sz, u32 col, int max_lines, float lh)
{
	if (lh <= 0) lh = ui_line_h(sz);
	int lines = 0;
	while (s && *s) {
		size_t len;
		size_t used = ui_next_line(s, sz, w, &len);
		if (max_lines > 0 && lines == max_lines - 1 && s[used]) {
			// Última línea visible y sobra texto: se corta con sentido (ver last_line)
			last_line(x, y + lines * lh, w, s, sz, col);
			return lines + 1;
		}
		draw_n(x, y + lines * lh, s, len, sz, col);
		lines++;
		s += used;
	}
	return lines;
}


void ui_block_parse(TextBlock *b, C2D_TextBuf buf, const char *s, float sz, float w, int max_lines)
{
	if (max_lines > 4) max_lines = 4;
	b->n = 0;
	while (s && *s && b->n < max_lines) {
		size_t len;
		size_t used = ui_next_line(s, sz, w, &len);
		char tmp[512];
		if (b->n == max_lines - 1 && s[used]) {
			// Última línea: lo que quepa y «…»
			size_t n = len;
			float ell = ui_text_w("…", sz);
			while (n > 0 && text_w_n(s, n, sz) + ell > w) {
				do n--; while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80);
			}
			while (n > 0 && s[n - 1] == ' ') n--;
			if (n > sizeof tmp - 4) n = sizeof tmp - 4;
			memcpy(tmp, s, n);
			memcpy(tmp + n, "…", 4);
		} else {
			if (len > sizeof tmp - 1) len = sizeof tmp - 1;
			memcpy(tmp, s, len);
			tmp[len] = 0;
		}
		C2D_TextFontParse(&b->line[b->n], g_font, buf, tmp);
		C2D_TextOptimize(&b->line[b->n]);
		b->n++;
		s += used;
	}
}

void ui_block_draw(const TextBlock *b, float x, float y, float sz, u32 col, float lh)
{
	for (int i = 0; i < b->n; i++) C2D_DrawText(&b->line[i], C2D_WithColor, x, y + i * lh, 0.5f, sz, sz, col);
}

// fontGlyphIndexFromCodePoint recorre la tabla de la fuente para cada carácter que no es ASCII
// (tildes, eñes, japonés): con mucho texto en pantalla hundía los fps. El enlazador redirige
// aquí todas las llamadas (-Wl,--wrap=fontGlyphIndexFromCodePoint) y se guarda el resultado,
// como en Gemi3DS.
int __real_fontGlyphIndexFromCodePoint(CFNT_s *font, u32 codePoint);
int __wrap_fontGlyphIndexFromCodePoint(CFNT_s *font, u32 codePoint)
{
	if (codePoint < 0x80) return __real_fontGlyphIndexFromCodePoint(font, codePoint);
	static struct { CFNT_s *f; u32 cp; int gi; } cache[8192];
	unsigned h = ((codePoint * 2654435761u) ^ (unsigned)(uintptr_t)font) >> 19;
	for (int i = 0; i < 8; i++) {
		unsigned k = (h + i) & 8191;
		if (cache[k].f == font && cache[k].cp == codePoint) return cache[k].gi;
		if (!cache[k].f) {
			int gi = __real_fontGlyphIndexFromCodePoint(font, codePoint);
			cache[k].f = font;
			cache[k].cp = codePoint;
			cache[k].gi = gi;
			return gi;
		}
	}
	int gi = __real_fontGlyphIndexFromCodePoint(font, codePoint);
	cache[h & 8191].f = font;
	cache[h & 8191].cp = codePoint;
	cache[h & 8191].gi = gi;
	return gi;
}

// ----------------------------------------------------------------------------

void ui_rect(float x, float y, float w, float h, u32 col)
{
	C2D_DrawRectSolid(x, y, 0.5f, w, h, col);
}

void ui_vgrad(float x, float y, float w, float h, u32 top, u32 bottom)
{
	C2D_DrawRectangle(x, y, 0.5f, w, h, top, top, bottom, bottom);
}

// Cuarto de círculo en abanico (sin solaparse con nada: con colores semitransparentes
// los solapes se verían más oscuros)
static void quarter(float cx, float cy, float r, float a0, u32 col)
{
	const int N = 6;
	for (int i = 0; i < N; i++) {
		float b0 = a0 + i * (M_PI / 2) / N, b1 = a0 + (i + 1) * (M_PI / 2) / N;
		C2D_DrawTriangle(cx, cy, col, cx + cosf(b0) * r, cy + sinf(b0) * r, col, cx + cosf(b1) * r, cy + sinf(b1) * r, col, 0.5f);
	}
}

void ui_rrect(float x, float y, float w, float h, float r, u32 col)
{
	if (r > h / 2) r = h / 2;
	if (r > w / 2) r = w / 2;
	if (r <= 0.5f) { ui_rect(x, y, w, h, col); return; }
	ui_rect(x + r, y, w - 2 * r, h, col);
	ui_rect(x, y + r, r, h - 2 * r, col);
	ui_rect(x + w - r, y + r, r, h - 2 * r, col);
	quarter(x + r, y + r, r, M_PI, col);
	quarter(x + w - r, y + r, r, -M_PI / 2, col);
	quarter(x + w - r, y + h - r, r, 0, col);
	quarter(x + r, y + h - r, r, M_PI / 2, col);
}

// Rectángulo recto arriba y redondeado abajo (lo que cuelga de una barra)
void ui_rrect_bottom(float x, float y, float w, float h, float r, u32 col)
{
	if (r > h) r = h;
	if (r > w / 2) r = w / 2;
	ui_rect(x, y, w, h - r, col);
	ui_rect(x + r, y + h - r, w - 2 * r, r, col);
	quarter(x + w - r, y + h - r, r, 0, col);
	quarter(x + r, y + h - r, r, M_PI / 2, col);
}

void ui_hline(float x, float y, float w, u32 col)
{
	// En coordenadas enteras: una línea de 1 px entre dos filas puede no pintarse en la consola
	ui_rect(floorf(x), floorf(y), w, 1, col);
}

void ui_circle(float x, float y, float r, u32 col)
{
	C2D_DrawCircleSolid(x, y, 0.5f, r, col);
}

void ui_scissor(gfxScreen_t scr, float x, float y, float w, float h)
{
	C2D_Flush();
	int W = scr == GFX_TOP ? 400 : 320;
	int l = 240 - (int)(y + h), t = W - (int)(x + w), r = 240 - (int)y, b = W - (int)x;
	if (l < 0) l = 0;
	if (t < 0) t = 0;
	C3D_SetScissor(GPU_SCISSOR_NORMAL, l, t, r, b);
}

void ui_scissor_off(void)
{
	C2D_Flush();
	C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
}

// ----------------------------------------------------------------------------
// Íconos

static void tri(float x1, float y1, float x2, float y2, float x3, float y3, u32 c)
{
	C2D_DrawTriangle(x1, y1, c, x2, y2, c, x3, y3, c, 0.5f);
}

static void seg(float x1, float y1, float x2, float y2, float th, u32 c)
{
	C2D_DrawLine(x1, y1, c, x2, y2, c, th, 0.5f);
}

static void ring(float cx, float cy, float r, float th, u32 c)
{
	const int N = 20;
	for (int i = 0; i < N; i++) {
		float a0 = i * 2 * M_PI / N, a1 = (i + 1) * 2 * M_PI / N;
		seg(cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r, th, c);
	}
}

void ui_icon(Icon ic, float x, float y, float s, u32 c)
{
	float u = s / 12.0f;   // los íconos están pensados en una rejilla de 12×12
	// El texto de la fuente tiene su centro visual algo por debajo de su caja: los iconos se bajan
	// lo mismo para quedar alineados con el texto que llevan al lado
	y += s * 0.11f;
#define X(v) (x + (v) * u)
#define Y(v) (y + (v) * u)
	switch (ic) {
	case IC_CART:
		seg(X(0), Y(2), X(2.2), Y(2), 1.3f * u, c);
		seg(X(2.2), Y(2), X(3.8), Y(8), 1.3f * u, c);
		seg(X(3.8), Y(8), X(10), Y(8), 1.3f * u, c);
		seg(X(10), Y(8), X(11.5), Y(3.5), 1.3f * u, c);
		seg(X(11.5), Y(3.5), X(2.8), Y(3.5), 1.3f * u, c);
		ui_circle(X(4.6), Y(10.3), 1.1f * u, c);
		ui_circle(X(9.2), Y(10.3), 1.1f * u, c);
		break;
	case IC_HEART:
		ui_circle(X(3.6), Y(4.3), 2.7f * u, c);
		ui_circle(X(8.4), Y(4.3), 2.7f * u, c);
		tri(X(1.0), Y(5.3), X(11.0), Y(5.3), X(6), Y(11), c);
		break;
	case IC_PC:
		ui_rect(X(0.5), Y(1), 11 * u, 7.2f * u, c);
		ui_rect(X(1.7), Y(2.2), 8.6f * u, 4.8f * u, C2D_Color32(0, 0, 0, 0x90));
		ui_rect(X(5.3), Y(8.2), 1.4f * u, 2 * u, c);
		ui_rect(X(3.3), Y(10), 5.4f * u, 1.2f * u, c);
		break;
	case IC_IMAGE:
		seg(X(0.6), Y(1.2), X(11.4), Y(1.2), 1.2f * u, c);
		seg(X(0.6), Y(10.8), X(11.4), Y(10.8), 1.2f * u, c);
		seg(X(1.2), Y(1.2), X(1.2), Y(10.8), 1.2f * u, c);
		seg(X(10.8), Y(1.2), X(10.8), Y(10.8), 1.2f * u, c);
		tri(X(2), Y(9.6), X(5), Y(5.5), X(8), Y(9.6), c);
		tri(X(6), Y(9.6), X(8.2), Y(6.6), X(10.2), Y(9.6), c);
		ui_circle(X(8.4), Y(3.9), 1.1f * u, c);
		break;
	case IC_SEARCH:
		ring(X(5), Y(5), 3.8f * u, 1.5f * u, c);
		seg(X(7.8), Y(7.8), X(11), Y(11), 1.8f * u, c);
		break;
	case IC_USER:
		ui_circle(X(6), Y(3.6), 2.7f * u, c);
		ui_rrect(X(1.5), Y(7), 9 * u, 4.8f * u, 2.4f * u, c);
		break;
	case IC_USERS:
		ui_circle(X(8.4), Y(3.4), 2.1f * u, c & 0xB0FFFFFF);
		ui_rrect(X(6.2), Y(6.4), 5.6f * u, 4.6f * u, 2 * u, c & 0xB0FFFFFF);
		ui_circle(X(4.3), Y(4), 2.4f * u, c);
		ui_rrect(X(0.3), Y(7.2), 8 * u, 4.4f * u, 2.2f * u, c);
		break;
	case IC_TROPHY:
		ui_rect(X(3.2), Y(1), 5.6f * u, 4 * u, c);
		ui_circle(X(6), Y(5), 2.8f * u, c);
		ring(X(2.6), Y(3.6), 1.6f * u, 1 * u, c);
		ring(X(9.4), Y(3.6), 1.6f * u, 1 * u, c);
		ui_rect(X(5.2), Y(7.2), 1.6f * u, 2 * u, c);
		ui_rect(X(3.4), Y(9.2), 5.2f * u, 1.8f * u, c);
		break;
	case IC_CARD:
		ui_rrect(X(1), Y(1), 6.6f * u, 9 * u, 1 * u, c & 0x90FFFFFF);
		ui_rrect(X(4.2), Y(2.4), 6.8f * u, 9 * u, 1 * u, c);
		break;
	case IC_CLOUD:
		ui_circle(X(4), Y(7), 2.8f * u, c);
		ui_circle(X(7), Y(5.4), 3.4f * u, c);
		ui_circle(X(9.6), Y(7.6), 2.2f * u, c);
		ui_rect(X(4), Y(7.4), 5.6f * u, 2.4f * u, c);
		break;
	case IC_HOME:
		tri(X(0.4), Y(6), X(6), Y(1), X(11.6), Y(6), c);
		ui_rect(X(2), Y(5.8), 8 * u, 5.4f * u, c);
		ui_rect(X(4.9), Y(7.6), 2.2f * u, 3.6f * u, C2D_Color32(0, 0, 0, 0x90));
		break;
	case IC_PAD:
		ui_rrect(X(0.5), Y(2.5), 11 * u, 6 * u, 3 * u, c);
		ui_circle(X(2.6), Y(8.2), 1.8f * u, c);
		ui_circle(X(9.4), Y(8.2), 1.8f * u, c);
		ui_rect(X(2.4), Y(5.2), 2.6f * u, 0.9f * u, C2D_Color32(0, 0, 0, 0x90));
		ui_rect(X(3.25), Y(4.35), 0.9f * u, 2.6f * u, C2D_Color32(0, 0, 0, 0x90));
		ui_circle(X(8.6), Y(5.6), 0.7f * u, C2D_Color32(0, 0, 0, 0x90));
		break;
	case IC_THUMB_UP:
	case IC_THUMB_DOWN: {
		bool dn = ic == IC_THUMB_DOWN;
#define TY(v) (dn ? Y(12 - (v)) : Y(v))
		float y0 = dn ? Y(1) : Y(5.5), h0 = 5.5f * u;
		ui_rect(X(0.5), y0, 2.4f * u, h0, c);
		ui_rect(X(3.6), dn ? Y(1) : Y(5.4), 7.6f * u, 5.6f * u, c);
		tri(X(3.6), TY(5.6), X(6.4), TY(0.6), X(8), TY(5.6), c);
		ui_circle(X(7.0), TY(1.6), 1.2f * u, c);
#undef TY
		break;
	}
	case IC_WRENCH:
		ring(X(8.4), Y(3.6), 2.4f * u, 1.6f * u, c);
		seg(X(6.6), Y(5.4), X(1.6), Y(10.4), 2.2f * u, c);
		break;
	case IC_COIN:
		ring(X(6), Y(6), 4.8f * u, 1.4f * u, c);
		ui_rect(X(5.3), Y(3), 1.4f * u, 6 * u, c);
		break;
	case IC_SWORDS:
		seg(X(1), Y(1), X(10.5), Y(10.5), 1.5f * u, c);
		seg(X(11), Y(1), X(1.5), Y(10.5), 1.5f * u, c);
		seg(X(7.5), Y(9.5), X(9.5), Y(7.5), 1.5f * u, c);
		seg(X(2.5), Y(7.5), X(4.5), Y(9.5), 1.5f * u, c);
		break;
	case IC_PHONE:
		ui_rrect(X(2.8), Y(0.4), 6.4f * u, 11.2f * u, 1.2f * u, c);
		ui_rect(X(3.9), Y(1.8), 4.2f * u, 7.4f * u, C2D_Color32(0, 0, 0, 0x90));
		break;
	case IC_WIFI:
		for (int i = 0; i < 3; i++) {
			float r = (3 + i * 3) * u;
			for (int k = 0; k < 6; k++) {
				float a0 = (-135 + k * 15) * M_PI / 180, a1 = (-135 + (k + 1) * 15) * M_PI / 180;
				seg(X(6) + cosf(a0) * r, Y(10.5) + sinf(a0) * r, X(6) + cosf(a1) * r, Y(10.5) + sinf(a1) * r, 1.3f * u, c);
			}
		}
		ui_circle(X(6), Y(10.3), 1.2f * u, c);
		break;
	case IC_POWER:
		// Aro abierto por arriba y una raya: el símbolo de apagar
		for (int k = 0; k < 16; k++) {
			float a0 = (-60 + k * 18.75f) * M_PI / 180, a1 = (-60 + (k + 1) * 18.75f) * M_PI / 180;
			seg(X(6) + cosf(a0) * 4.6f * u, Y(6.6) + sinf(a0) * 4.6f * u, X(6) + cosf(a1) * 4.6f * u, Y(6.6) + sinf(a1) * 4.6f * u, 1.6f * u, c);
		}
		seg(X(6), Y(0.8), X(6), Y(6), 1.7f * u, c);
		break;
	case IC_LOCK:
		ring(X(6), Y(4.6), 2.8f * u, 1.3f * u, c);
		ui_rrect(X(2), Y(5.4), 8 * u, 6.2f * u, 1 * u, c);
		break;
	}
#undef X
#undef Y
}

// ----------------------------------------------------------------------------
// Componentes

void ui_badge_disc(float x, float y, float w, float h, int disc, float sz)
{
	char t[12];
	snprintf(t, sizeof t, "-%d%%", disc);
	ui_rect(x, y, w, h, COL_DGREEN);
	ui_text(x + w / 2, y + (h - ui_line_h(sz)) / 2 + 1, t, sz, COL_LIME, AL_CENTER, 0);
}

void ui_price_box(float x, float y, float w, float h, const char *orig, const char *price, float sz_old, float sz_new)
{
	ui_rect(x, y, w, h, COL_SHADE);
	float r = x + w - 4;
	if (orig && *orig) {
		float lo = ui_line_h(sz_old), ln = ui_line_h(sz_new);
		float oy = y + (h - lo * 0.78f - ln * 0.86f) / 2 - lo * 0.06f;
		float ow = ui_text(r, oy, orig, sz_old, COL_OLD, AL_RIGHT, 0);
		ui_rect(floorf(r - ow), floorf(oy + lo * 0.50f), ceilf(ow), 1, COL_OLD);
		ui_text(r, oy + lo * 0.78f, price, sz_new, COL_LIME, AL_RIGHT, 0);
	} else {
		ui_text(r, y + (h - ui_line_h(sz_new)) / 2 + 1, price, sz_new, COL_TXT, AL_RIGHT, 0);
	}
}

// Cargando: doce rayitas que se van apagando en círculo (como el indicador de iOS)
void ui_spinner(float cx, float cy, float r, u32 col)
{
	int head = (int)(g_time * 12) % 12;
	float th = r > 7 ? 2.0f : 1.6f;
	for (int i = 0; i < 12; i++) {
		float a = i * (float)M_PI / 6 - (float)M_PI / 2;
		int age = (head - i + 12) % 12;
		float k = 1 - age / 12.0f;
		u32 c = col_alpha(col, 0.18f + 0.82f * k * k);
		C2D_DrawLine(cx + cosf(a) * r * 0.48f, cy + sinf(a) * r * 0.48f, c, cx + cosf(a) * r, cy + sinf(a) * r, c, th, 0.5f);
	}
}

// Batería como en Cloud3DS: contorno redondeado, polo y relleno de color; rayo si carga.
// (x, y) es la esquina de arriba a la izquierda de una caja de 19×10
void ui_battery(float x, float y, int pct, bool charging)
{
	const float w = 19, h = 10;
	u32 line = C2D_Color32(0xc7, 0xd5, 0xe0, 0xc0);
	// Contorno de 1 px con esquinas recortadas
	ui_rect(x + 2, y, w - 4, 1, line);
	ui_rect(x + 2, y + h - 1, w - 4, 1, line);
	ui_rect(x, y + 2, 1, h - 4, line);
	ui_rect(x + w - 1, y + 2, 1, h - 4, line);
	ui_rect(x + 1, y + 1, 1, 1, line);
	ui_rect(x + w - 2, y + 1, 1, 1, line);
	ui_rect(x + 1, y + h - 2, 1, 1, line);
	ui_rect(x + w - 2, y + h - 2, 1, 1, line);
	ui_rrect(x + w + 0.5f, y + 3, 2, h - 6, 1, line);
	if (pct >= 0) {
		u32 c = charging ? COL_ACC : pct <= 15 ? C2D_Color32(0xe0, 0x4b, 0x3a, 0xff)
			: pct <= 30 ? C2D_Color32(0xe8, 0xc5, 0x47, 0xff) : C2D_Color32(0x7c, 0xc4, 0x3a, 0xff);
		float fw = (w - 4) * (pct > 100 ? 100 : pct) / 100.0f;
		if (fw < 1.5f) fw = 1.5f;
		ui_rrect(x + 2, y + 2, fw, h - 4, 1, c);
	}
	if (charging) {
		u32 b = C2D_Color32(0x10, 0x16, 0x20, 0xff);
		float cx = x + w / 2, cy = y + h / 2;
		tri(cx + 1, cy - 4, cx - 3, cy + 0.5f, cx, cy + 0.5f, b);
		tri(cx, cy - 0.5f, cx + 3, cy - 0.5f, cx - 1, cy + 4, b);
	}
}

// Señal Wi-Fi: tres arcos encendidos según la intensidad (0-3); -1 = sin conexión.
// (cx, by) es el punto de abajo de los arcos
void ui_wifi(float cx, float by, int bars)
{
	u32 on = C2D_Color32(0xc7, 0xd5, 0xe0, 0xe0), off = C2D_Color32(0xff, 0xff, 0xff, 0x30);
	for (int i = 0; i < 3; i++) {
		float r = 3.5f + i * 3.2f;
		u32 c = bars > i ? on : off;
		const int N = 6;
		for (int k = 0; k < N; k++) {
			float a0 = -M_PI * 0.75f + (M_PI * 0.5f) * k / N, a1 = -M_PI * 0.75f + (M_PI * 0.5f) * (k + 1) / N;
			seg(cx + cosf(a0) * r, by + sinf(a0) * r, cx + cosf(a1) * r, by + sinf(a1) * r, 1.5f, c);
		}
	}
	ui_circle(cx, by - 0.5f, 1.3f, bars > 0 ? on : off);
	if (bars < 0) seg(cx - 6, by - 11, cx + 6, by + 1, 1.4f, C2D_Color32(0xe0, 0x4b, 0x3a, 0xff));
}

// Volante de válvula (el logo): aro, seis radios y el eje; angle para que gire
void ui_wheel(float cx, float cy, float r, float angle, u32 col)
{
	const int N = 18;
	for (int i = 0; i < N; i++) {
		float a0 = i * 2 * M_PI / N, a1 = (i + 1) * 2 * M_PI / N;
		seg(cx + cosf(a0) * r, cy + sinf(a0) * r, cx + cosf(a1) * r, cy + sinf(a1) * r, r * 0.26f, col);
	}
	for (int i = 0; i < 6; i++) {
		float a = angle + i * M_PI / 3;
		seg(cx + cosf(a) * r * 0.3f, cy + sinf(a) * r * 0.3f, cx + cosf(a) * r * 0.9f, cy + sinf(a) * r * 0.9f, r * 0.18f, col);
	}
	ui_circle(cx, cy, r * 0.36f, col);
}

void ui_qr(const char *text, float x, float y, float size)
{
	static uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
	if (!qrcodegen_encodeText(text, tmp, qr, qrcodegen_Ecc_MEDIUM, 4, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true))
		return;
	int n = qrcodegen_getSize(qr);
	float m = floorf(size / (n + 4));
	float total = m * (n + 4);
	float ox = floorf(x + (size - total) / 2), oy = floorf(y + (size - total) / 2);
	// Tarjeta blanca redondeada con sombra suave
	ui_rrect(ox - 4, oy - 2, total + 8, total + 8, 12, C2D_Color32(0, 0, 0, 0x40));
	ui_rrect(ox - 4, oy - 4, total + 8, total + 8, 12, COL_WHITE);
	u32 black = C2D_Color32(0, 0, 0, 0xff);
	for (int j = 0; j < n; j++) {
		int i = 0;
		while (i < n) {
			if (!qrcodegen_getModule(qr, i, j)) { i++; continue; }
			int k = i;
			while (k < n && qrcodegen_getModule(qr, k, j)) k++;
			ui_rect(ox + (i + 2) * m, oy + (j + 2) * m, (k - i) * m, m, black);
			i = k;
		}
	}
}

// ----------------------------------------------------------------------------
// Animación

float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

float ease_out_cubic(float t)
{
	t = clampf(t, 0, 1);
	float u = 1 - t;
	return 1 - u * u * u;
}

float ease_out_back(float t)
{
	t = clampf(t, 0, 1);
	const float c1 = 1.70158f, c3 = c1 + 1;
	return 1 + c3 * powf(t - 1, 3) + c1 * powf(t - 1, 2);
}

float approach(float v, float target, float speed)
{
	float r = v + (target - v) * (1 - expf(-g_dt * speed));
	if (fabsf(r - target) < 0.01f) r = target;
	return r;
}

u32 col_alpha(u32 c, float a)
{
	u32 al = (u32)((c >> 24) * clampf(a, 0, 1));
	return (c & 0x00FFFFFF) | (al << 24);
}

u32 col_lerp(u32 a, u32 b, float t)
{
	t = clampf(t, 0, 1);
	u32 r = 0;
	for (int s = 0; s < 32; s += 8) {
		float ca = (a >> s) & 0xff, cb = (b >> s) & 0xff;
		r |= (u32)(ca + (cb - ca) * t) << s;
	}
	return r;
}

const char *ui_button_glyph(const char *key)
{
	static const struct { const char *k, *g; u32 cp; } map[] = {
		{"A", "\xEE\x80\x80", 0xE000}, {"B", "\xEE\x80\x81", 0xE001}, {"X", "\xEE\x80\x82", 0xE002},
		{"Y", "\xEE\x80\x83", 0xE003}, {"L", "\xEE\x80\x84", 0xE004}, {"R", "\xEE\x80\x85", 0xE005},
		{"+", "\xEE\x80\x86", 0xE006},
	};
	static int have = -1;
	if (have < 0) {
		// La fuente libre de Azahar no los trae: entonces se dibujan las teclas a mano
		FINF_s *fi = C2D_FontGetInfo(g_font);
		int gi = C2D_FontGlyphIndexFromCodePoint(g_font, 0xE000);
		have = fi && gi != fi->alterCharIndex && char_w(0xE000) > 0;
	}
	if (!have) return NULL;
	for (size_t i = 0; i < sizeof map / sizeof *map; i++)
		if (!strcmp(key, map[i].k)) return map[i].g;
	return NULL;
}
