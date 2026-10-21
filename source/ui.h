#pragma once
#include <3ds.h>
#include <citro2d.h>
#include <stdbool.h>

// Colores de la tienda de Steam
#define COL_BG      C2D_Color32(0x1b, 0x28, 0x38, 0xff)
#define COL_BAR     C2D_Color32(0x17, 0x1a, 0x21, 0xff)
#define COL_PANEL   C2D_Color32(0x22, 0x34, 0x47, 0xff)
#define COL_SEL     C2D_Color32(0x2a, 0x47, 0x5e, 0xff)
#define COL_LINE    C2D_Color32(0x16, 0x20, 0x2d, 0xff)
#define COL_TXT     C2D_Color32(0xc7, 0xd5, 0xe0, 0xff)
#define COL_WHITE   C2D_Color32(0xff, 0xff, 0xff, 0xff)
#define COL_DIM     C2D_Color32(0x8f, 0x98, 0xa0, 0xff)
#define COL_OLD     C2D_Color32(0x73, 0x88, 0x95, 0xff)
#define COL_ACC     C2D_Color32(0x66, 0xc0, 0xf4, 0xff)
#define COL_LIME    C2D_Color32(0xbe, 0xee, 0x11, 0xff)
#define COL_DGREEN  C2D_Color32(0x4c, 0x6b, 0x22, 0xff)
#define COL_TAG     C2D_Color32(0x2a, 0x47, 0x5e, 0xff)
#define COL_BTN2    C2D_Color32(0x21, 0x4b, 0x6b, 0xff)
#define COL_MIXED   C2D_Color32(0xb9, 0xa0, 0x74, 0xff)
#define COL_NEG     C2D_Color32(0xa3, 0x4c, 0x25, 0xff)
#define COL_SHADE   C2D_Color32(0x00, 0x00, 0x00, 0x40)

// Tamaños de letra (escala de la fuente del sistema)
#define SZ_TINY   0.36f
#define SZ_SMALL  0.40f
#define SZ_BODY   0.45f
#define SZ_LARGE  0.55f
#define SZ_TITLE  0.65f
#define SZ_HUGE   0.80f

enum { AL_LEFT = 0, AL_CENTER = 1, AL_RIGHT = 2 };

void ui_init(void);
void ui_exit(void);
void ui_begin_frame(void);    // limpia el búfer de texto

// Texto
float ui_text_w(const char *s, float sz);
float ui_line_h(float sz);
// Dibuja una línea; si no cabe en max_w (>0) la corta con «…». Devuelve el ancho dibujado.
float ui_text(float x, float y, const char *s, float sz, u32 col, int align, float max_w);
// Texto en varias líneas. Devuelve el número de líneas (dibuja como mucho max_lines, la última con «…»).
int ui_text_wrap(float x, float y, float w, const char *s, float sz, u32 col, int max_lines, float line_h);
int ui_text_lines(const char *s, float sz, float w);

// Formas
void ui_rect(float x, float y, float w, float h, u32 col);
void ui_vgrad(float x, float y, float w, float h, u32 top, u32 bottom);
void ui_rrect(float x, float y, float w, float h, float r, u32 col);
void ui_hline(float x, float y, float w, u32 col);
void ui_circle(float x, float y, float r, u32 col);
void ui_scissor(gfxScreen_t scr, float x, float y, float w, float h);
void ui_scissor_off(void);

// Íconos vectoriales (caja de s×s píxeles)
typedef enum {
	IC_CART, IC_HEART, IC_PC, IC_IMAGE, IC_SEARCH, IC_USER, IC_USERS, IC_TROPHY, IC_CARD,
	IC_CLOUD, IC_HOME, IC_PAD, IC_THUMB_UP, IC_THUMB_DOWN, IC_WRENCH, IC_COIN, IC_SWORDS,
	IC_PHONE, IC_WIFI, IC_LOCK,
} Icon;
void ui_icon(Icon ic, float x, float y, float s, u32 col);

// Componentes
void ui_badge_disc(float x, float y, float w, float h, int disc, float sz);
void ui_price_box(float x, float y, float w, float h, const char *orig, const char *price, float sz_old, float sz_new);
void ui_spinner(float cx, float cy, float r, u32 col);
void ui_battery(float x, float y, int pct, bool charging);
void ui_qr(const char *text, float x, float y, float size);

extern C2D_Font g_font;
extern float g_time;           // segundos desde el arranque (animaciones)
