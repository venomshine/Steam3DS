#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "app.h"
#include "auth.h"
#include "i18n.h"
#include "img.h"
#include "net.h"
#include "steam.h"
#include "ui.h"
#include "util.h"

// ============================================================================
// Estado

typedef enum { SC_SPLASH, SC_STORE, SC_GAME, SC_SHOTS, SC_LOGIN } Scene;
enum { TAB_HOME, TAB_OFFERS, TAB_DEMOS, TAB_BETAS, TAB_SEARCH, TAB_COUNT };
enum { SEC_SUMMARY, SEC_REQS, SEC_REVIEWS, SEC_EXTRAS, SEC_SIMILAR, SEC_COUNT };

#define ROW_H      29
#define LIST_Y     24
#define LIST_H     (SCR_H - LIST_Y - 18)
#define PAGE       30
#define MAX_REVIEWS 48
#define CACHE      6

// Fichas descargadas (para la vista previa de la lista y para volver atrás sin esperar)
typedef struct {
	u32 appid;
	AppInfo info;
	Req *req, *sum;
	bool done, failed;
	char err[112];
	u64 used, fail_at;
} InfoEnt;

static struct {
	Scene scene;
	bool quit, ask_exit;
	char toast[96];
	float toast_t;
	bool show_qr;

	// País e idioma de la tienda
	Req *country_req;
	bool ctx_ready;

	// Tienda
	int tab;
	bool dragging;
	int touch_y0, touch_x0, touch_moved;
	float drag_scroll0;
	bool want_kb;                // abrir el teclado (fuera del frame de dibujo)
	char recents[8][64];         // búsquedas recientes
	int nrecents;

	// Ficha
	u32 app;
	int sec;
	u32 stack_app[8];
	int stack_sec[8], stack_lsel[8];
	int depth;
	Review reviews[MAX_REVIEWS];
	TextBlock rtext[MAX_REVIEWS];   // reseñas ya maquetadas
	int nreviews;
	char rcursor[128];
	Req *rreq;
	bool rend;
	int rsel;
	float rscroll;
	RowList dlc, similar;
	u32 dlc_for, similar_for;
	int lsel;
	float lscroll;

	// Capturas
	int shot, shot_dir;
	float shot_anim, strip;

	// Animaciones
	float tab_x;                 // pestaña marcada (se desliza)
	float lsel_y;                // franja de selección de las listas de la ficha
	float seg_x;                 // apartado marcado en el selector
	float sec_anim; int sec_dir; // entrada del apartado
	float scene_anim; int scene_dir;
	float exit_anim;             // diálogo de salida (0 cerrado, 1 abierto)
	float quit_fade;             // fundido a negro al salir (-1 = no)
	int pressed;                 // botón táctil hundido (B_*)
	float toast_y;

	// Menú de START y sesión
	bool menu_open;
	float menu_anim;
	int menu_sel;
	float start_hold;            // START mantenido: a los 2 s pregunta si sales
	bool start_armed;
	Scene login_back;            // a dónde se vuelve al acabar de iniciar sesión
	float login_done;            // segundos desde que entró (para el saludo)
	AuthState last_auth;
} S;

enum { BT_NONE, BT_CART, BT_WISH, BT_SHOTS, BT_INST, BT_PREV, BT_NEXT, BT_EXIT_NO, BT_EXIT_YES, BT_SEARCH };

// Pestañas de la tienda
#define HOME_PARTS 4
typedef struct {
	RowList list;
	int sel;
	float scroll, scroll_target, sel_y;
	bool finger;                 // la lista la ha movido el dedo
	bool started;
	u64 fail_at, sel_changed;
	RowList part[HOME_PARTS];    // Inicio: los apartados se piden a la vez y se juntan
	char term[64];               // Buscar: lo que se buscó
} Tab;
static Tab tabs[TAB_COUNT];
#define CUR (&tabs[S.tab])

static InfoEnt cache[CACHE];
static u64 frame_no;
static C2D_SpriteSheet sprites, splash_bg;
static int sprites_free_in = -1;   // fotogramas que faltan para liberar los sprites de la carga
static C2D_TextBuf review_buf;
static float splash_t, splash_out = -1;

static void toast(const char *msg)
{
	snprintf(S.toast, sizeof S.toast, "%s", msg);
	S.toast_t = 2.6f;
}

static void go_scene(Scene sc, int dir)
{
	S.scene = sc;
	S.scene_anim = 0;
	S.scene_dir = dir;
}

// El error de red en palabras de persona (el código exacto queda en net.log)
static const char *human_err(const char *err)
{
	int code = 0;
	if (sscanf(err, "curl=%d", &code) == 1) {
		switch (code) {
		case 6: return _("No se encuentra el servidor de Steam");
		case 7: return _("No se pudo conectar con Steam");
		case 28: return _("Steam tarda demasiado en responder");
		case 35: case 51: case 58: case 60: case 77: return _("Falló la conexión segura con Steam");
		case 52: case 55: case 56: return _("Se cortó la conexión");
		}
		return _("Error de red");
	}
	if (sscanf(err, "HTTP %d", &code) == 1) {
		if (code == 429) return _("Steam pide esperar un poco");
		if (code >= 500) return _("Steam no responde bien ahora mismo");
		return _("Steam respondió con un error");
	}
	return _("Steam mandó una respuesta inesperada");
}

// Segundos de espera antes de reintentar solo
static int retry_delay(const char *err)
{
	return strstr(err, "HTTP 429") ? 30 : 8;
}


// ============================================================================
// Caché de fichas

static InfoEnt *info_get(u32 appid, bool start)
{
	InfoEnt *free_e = NULL;
	for (int i = 0; i < CACHE; i++) {
		if (cache[i].appid == appid) {
			cache[i].used = frame_no;
			return &cache[i];
		}
	}
	if (!start || !S.ctx_ready) return NULL;
	for (int i = 0; i < CACHE; i++) {
		InfoEnt *e = &cache[i];
		if (e->appid == S.app && S.scene != SC_STORE) continue;   // la ficha abierta no se tira
		if (!free_e || e->used < free_e->used) free_e = e;
	}
	if (!free_e) return NULL;
	if (free_e->req) net_release(free_e->req);
	if (free_e->sum) net_release(free_e->sum);
	appinfo_free(&free_e->info);
	memset(free_e, 0, sizeof *free_e);
	free_e->appid = appid;
	free_e->used = frame_no;
	free_e->req = steam_req_appdetails(appid);
	free_e->sum = steam_req_items(&appid, 1);
	return free_e;
}

static void info_poll(void)
{
	for (int i = 0; i < CACHE; i++) {
		InfoEnt *e = &cache[i];
		if (e->req && e->req->state >= REQ_DONE) {
			if (e->req->state == REQ_DONE && steam_parse_appdetails(e->req->data, e->appid, &e->info)) e->done = true;
			else {
				e->failed = true;
				e->fail_at = osGetTime();
				snprintf(e->err, sizeof e->err, "%s", e->req->state == REQ_DONE ? "json" : e->req->err);
			}
			net_release(e->req);
			e->req = NULL;
		}
		if (e->sum && e->sum->state >= REQ_DONE && !e->req) {
			if (e->sum->state == REQ_DONE && e->done) steam_parse_review_summary(e->sum->data, &e->info);
			net_release(e->sum);
			e->sum = NULL;
		}
	}
}

static void info_retry(InfoEnt *e)
{
	u32 id = e->appid;
	appinfo_free(&e->info);
	memset(e, 0, sizeof *e);
	info_get(id, true);
}

// ============================================================================
// Listas

static void list_poll(RowList *l, bool append)
{
	if (!l->req || l->req->state < REQ_DONE) return;
	if (l->req->state == REQ_DONE) steam_parse_rows(l->req->data, l, append);
	else snprintf(l->err, sizeof l->err, "%s", l->req->err);
	net_release(l->req);
	l->req = NULL;
}

static const StoreQuery home_q[HOME_PARTS] = {Q_TOP, Q_NEW, Q_SOON, Q_F2P};
static const char *home_title[HOME_PARTS] = {"Lo más vendido", "Novedades populares", "Próximamente", "Gratis para jugar"};

static bool tab_paged(int t)
{
	return t == TAB_OFFERS || t == TAB_DEMOS || t == TAB_BETAS;
}

// Espera lo que toque tras un error antes de volver a pedir
static bool tab_can_retry(Tab *t)
{
	if (!t->list.err[0]) return true;
	if (osGetTime() - t->fail_at < (u64)retry_delay(t->list.err) * 1000) return false;
	t->list.err[0] = 0;
	return true;
}

static void tab_more(int ti)
{
	Tab *t = &tabs[ti];
	if (!S.ctx_ready || t->list.req || !tab_can_retry(t)) return;
	if (ti == TAB_HOME) {
		if (t->list.count) return;
		for (int i = 0; i < HOME_PARTS; i++)
			if (!t->part[i].req && !t->part[i].count) {
				t->part[i].err[0] = 0;
				t->part[i].req = steam_req_query(home_q[i], 0, 10);
			}
	} else if (tab_paged(ti)) {
		if (t->list.end) return;
		static const StoreQuery q[TAB_COUNT] = {[TAB_OFFERS] = Q_OFFERS, [TAB_DEMOS] = Q_DEMOS, [TAB_BETAS] = Q_EARLY};
		t->list.req = steam_req_query(q[ti], t->list.count, PAGE);
	}
	t->started = true;
}

static void add_row(RowList *l, const StoreRow *r)
{
	if (l->count == l->cap) {
		int cap = l->cap ? l->cap * 2 : 48;
		StoreRow *n = realloc(l->rows, cap * sizeof(StoreRow));
		if (!n) return;
		l->rows = n;
		l->cap = cap;
	}
	l->rows[l->count++] = *r;
}

// Inicio: cuando llegan los cuatro apartados se juntan en una lista con títulos
static void home_poll(void)
{
	Tab *t = &tabs[TAB_HOME];
	bool busy = false, any = false;
	for (int i = 0; i < HOME_PARTS; i++) {
		list_poll(&t->part[i], false);
		if (t->part[i].req) busy = true;
		if (t->part[i].count) any = true;
	}
	if (busy || t->list.count || !t->started) return;
	if (!any) {
		// Todo falló: el error del primero, y se reintenta solo
		for (int i = 0; i < HOME_PARTS; i++)
			if (t->part[i].err[0] && !t->list.err[0]) snprintf(t->list.err, sizeof t->list.err, "%s", t->part[i].err);
		return;
	}
	for (int i = 0; i < HOME_PARTS; i++) {
		if (!t->part[i].count) continue;
		StoreRow hd = {0};
		hd.header = true;
		snprintf(hd.name, sizeof hd.name, "%s", _(home_title[i]));
		add_row(&t->list, &hd);
		for (int k = 0; k < t->part[i].count; k++) add_row(&t->list, &t->part[i].rows[k]);
	}
	t->list.total = t->list.count;
	t->list.end = true;
	t->list.err[0] = 0;
	t->sel = t->list.count > 1 ? 1 : 0;
	t->sel_y = t->sel;
	t->sel_changed = osGetTime();
}

// Buscar: las recientes se guardan en la SD, la última primero
static void recents_load(void)
{
	FILE *f = fopen(APP_DIR "/searches.txt", "r");
	if (!f) return;
	char line[64];   // las búsquedas tienen como mucho 60 caracteres (el teclado no deja más)
	while (S.nrecents < 8 && fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (line[0]) snprintf(S.recents[S.nrecents++], 64, "%s", line);
	}
	fclose(f);
}

static void recents_add(const char *term)
{
	char keep[8][64];
	int n = 0;
	snprintf(keep[n++], 64, "%s", term);
	for (int i = 0; i < S.nrecents && n < 8; i++)
		if (strcasecmp(S.recents[i], term)) snprintf(keep[n++], 64, "%s", S.recents[i]);
	memcpy(S.recents, keep, sizeof keep);
	S.nrecents = n;
	FILE *f = fopen(APP_DIR "/searches.tmp", "w");
	if (!f) return;
	for (int i = 0; i < n; i++) fprintf(f, "%s\n", S.recents[i]);
	fclose(f);
	remove(APP_DIR "/searches.txt");
	rename(APP_DIR "/searches.tmp", APP_DIR "/searches.txt");
}

// Sin búsqueda en marcha, la lista de Buscar son las recientes (filas sin appid)
static void search_show_recents(void)
{
	Tab *t = &tabs[TAB_SEARCH];
	if (t->list.req) return;
	t->list.count = 0;
	t->term[0] = 0;
	for (int i = 0; i < S.nrecents; i++) {
		StoreRow r = {0};
		snprintf(r.name, sizeof r.name, "%s", S.recents[i]);
		add_row(&t->list, &r);
	}
	t->list.total = t->list.count;
	t->list.end = true;
	t->sel = 0;
	t->sel_y = 0;
	t->scroll = t->scroll_target = 0;
}

static void search_start(const char *term)
{
	Tab *t = &tabs[TAB_SEARCH];
	if (!term[0] || !S.ctx_ready) return;
	recents_add(term);
	if (t->list.req) net_release(t->list.req);
	t->list.req = steam_req_search(term);
	t->list.count = 0;
	t->list.err[0] = 0;
	t->list.end = true;
	snprintf(t->term, sizeof t->term, "%s", term);
	t->sel = 0;
	t->sel_y = 0;
	t->scroll = t->scroll_target = 0;
	t->sel_changed = osGetTime();
}

// ============================================================================
// Dibujo común

static void draw_status(const char *user)
{
	ui_rect(0, 0, TOP_W, 18, COL_BAR);
	ui_text(8, 2, APP_NAME, SZ_SMALL, COL_ACC, AL_LEFT, 0);

	char clock[48], right[128];
	util_clock(clock, sizeof clock);
	bool chg;
	int bat = util_battery(&chg);
	char pct[8];
	snprintf(pct, sizeof pct, "%d%%", bat);
	float r = TOP_W - 8;
	r -= ui_text(r, 2, pct, SZ_SMALL, COL_DIM, AL_RIGHT, 0) + 4;
	ui_battery(r - 18, 5, bat, chg);
	r -= 24;
	if (user) snprintf(right, sizeof right, "%s · %s", user, clock);
	else snprintf(right, sizeof right, "%s", clock);
	ui_text(r, 2, right, SZ_SMALL, COL_DIM, AL_RIGHT, 230);
}

// Tecla de una pista: el glifo del sistema si la fuente lo tiene; si no, una tecla dibujada
static float hint_key(float x, float y, const char *key, bool draw)
{
	char g[16] = "";
	if (!strcmp(key, "L R")) {
		const char *l = ui_button_glyph("L"), *r = ui_button_glyph("R");
		if (l && r) snprintf(g, sizeof g, "%s%s", l, r);
	} else if (!strcmp(key, "↑↓") || !strcmp(key, "← →")) {
		const char *d = ui_button_glyph("+");
		if (d) snprintf(g, sizeof g, "%s", d);
	} else {
		const char *b = ui_button_glyph(key);
		if (b) snprintf(g, sizeof g, "%s", b);
	}
	if (g[0]) {
		float w = ui_text_w(g, SZ_SMALL);
		if (draw) ui_text(x, y + 1, g, SZ_SMALL, COL_WHITE, AL_LEFT, 0);
		return w;
	}
	float kw = ui_text_w(key, SZ_TINY) + 6;
	if (kw < 11) kw = 11;
	if (draw) {
		ui_rrect(x, y + 3.5f, kw, 11, 5.5f, C2D_Color32(0x3d, 0x44, 0x50, 0xff));
		ui_text(x + kw / 2, y + 3.5f, key, SZ_TINY, COL_WHITE, AL_CENTER, 0);
	}
	return kw;
}

static void draw_hints(const char *const *pairs, int n)
{
	float y = SCR_H - 18;
	ui_rect(0, y, BOT_W, 18, COL_BAR);
	float total = 0, widths[8];
	for (int i = 0; i < n; i++) {
		widths[i] = hint_key(0, y, pairs[i * 2], false) + 3 + ui_text_w(_(pairs[i * 2 + 1]), SZ_TINY);
		total += widths[i] + (i ? 12 : 0);
	}
	float x = (BOT_W - total) / 2;
	for (int i = 0; i < n; i++) {
		float kw = hint_key(x, y, pairs[i * 2], true);
		ui_text(x + kw + 3, y + 3.5f, _(pairs[i * 2 + 1]), SZ_TINY, COL_DIM, AL_LEFT, 0);
		x += widths[i] + 12;
	}
}

static void draw_net_wait(float cx, float cy, const char *what)
{
	const char *st = net_status_text();
	if (st[0]) {
		const char *msg = !strcmp(st, "wifi_off") ? _("Sin conexión Wi-Fi")
			: !strcmp(st, "wifi_noip") ? _("Conectando al Wi-Fi…") : _("La red de la consola no responde");
		ui_icon(IC_WIFI, cx - 10, cy - 30, 20, COL_DIM);
		ui_text(cx, cy, msg, SZ_BODY, COL_TXT, AL_CENTER, 0);
		ui_text(cx, cy + 16, _("Se reintenta solo"), SZ_SMALL, COL_DIM, AL_CENTER, 0);
	} else {
		ui_spinner(cx, cy - 6, 9, COL_ACC);
		if (what) ui_text(cx, cy + 10, what, SZ_SMALL, COL_DIM, AL_CENTER, 0);
	}
}

static void draw_tag(float *x, float y, const char *t)
{
	float w = ui_text_w(t, SZ_TINY) + 10;
	ui_rect(*x, y, w, 14, COL_TAG);
	ui_text(*x + 5, y + 1.5f, t, SZ_TINY, COL_ACC, AL_LEFT, 0);
	*x += w + 4;
}

static void draw_rating(float x, float y, float s, const AppInfo *a)
{
	if (!a->rating[0]) return;
	const char *sys = a->rating_sys;
	u32 bg = C2D_Color32(0xd8, 0x23, 0x2a, 0xff), fg = COL_WHITE;
	int age = atoi(a->rating);
	if (!strcmp(sys, "PEGI")) {
		bg = age >= 18 ? C2D_Color32(0xd8, 0x23, 0x2a, 0xff) : age >= 12 ? C2D_Color32(0xf2, 0x9a, 0x1b, 0xff) : C2D_Color32(0x5f, 0xa8, 0x32, 0xff);
	} else if (!strcmp(sys, "USK")) {
		bg = age >= 18 ? C2D_Color32(0xd8, 0x23, 0x2a, 0xff) : age >= 16 ? C2D_Color32(0x23, 0x7b, 0xd8, 0xff)
			: age >= 12 ? C2D_Color32(0x3a, 0xa0, 0x3a, 0xff) : age >= 6 ? C2D_Color32(0xf0, 0xd0, 0x20, 0xff) : COL_WHITE;
		if (age < 12) fg = C2D_Color32(0, 0, 0, 0xff);
	} else if (!strcmp(sys, "ESRB")) {
		bg = COL_WHITE;
		fg = C2D_Color32(0, 0, 0, 0xff);
	}
	float h = s * 1.2f;
	ui_rrect(x, y, s, h, 2, bg);
	const char *big = a->rating;
	char tmp[8];
	if (!strcmp(sys, "ESRB")) {
		// E, E10+, T, M, AO
		snprintf(tmp, sizeof tmp, "%s", !strcmp(big, "E10") ? "E10+" : big);
		big = tmp;
	}
	float bs = s * (strlen(big) > 2 ? 0.020f : 0.026f);
	ui_text(x + s / 2, y + h * 0.10f, big, bs, fg, AL_CENTER, s - 2);
	ui_text(x + s / 2, y + h * 0.66f, sys, s * 0.0085f, fg, AL_CENTER, s - 2);
}

static int feat_list(const AppInfo *a, Icon *ic, char (*lab)[32], int max)
{
	int n = 0;
#define ADD(i, t) do { if (n < max) { ic[n] = i; snprintf(lab[n], 32, "%s", t); n++; } } while (0)
	if (a->feats & FEAT_SINGLE) ADD(IC_USER, _("1 jugador"));
	if (a->feats & FEAT_MULTI) ADD(IC_USERS, _("Multijugador"));
	if (a->feats & FEAT_COOP) ADD(IC_USERS, _("Cooperativo"));
	if (a->feats & FEAT_PVP) ADD(IC_SWORDS, _("JcJ"));
	if (a->feats & FEAT_ACHIEVE) {
		char t[32];
		if (a->achievements > 0) snprintf(t, sizeof t, _("%d logros"), a->achievements);
		else snprintf(t, sizeof t, "%s", _("Logros"));
		ADD(IC_TROPHY, t);
	}
	if (a->feats & FEAT_CARDS) ADD(IC_CARD, _("Cromos"));
	if (a->feats & FEAT_CLOUD) ADD(IC_CLOUD, "Steam Cloud");
	if (a->feats & FEAT_FAMILY) ADD(IC_HOME, _("Préstamo familiar"));
	if (a->feats & FEAT_PAD_FULL) ADD(IC_PAD, _("Mando completo"));
	if (a->feats & FEAT_PAD_PART) ADD(IC_PAD, _("Mando parcial"));
	if (a->feats & FEAT_WORKSHOP) ADD(IC_WRENCH, "Workshop");
	if (a->feats & FEAT_INAPP) ADD(IC_COIN, _("Compras en el juego"));
#undef ADD
	return n;
}

// Chips de características en filas; devuelve la altura usada
static float draw_feats(float x, float y, float w, const AppInfo *a, int max_rows)
{
	Icon ic[12];
	char lab[12][32];
	int n = feat_list(a, ic, lab, 12);
	float cx = x, cy = y;
	int row = 0;
	for (int i = 0; i < n; i++) {
		float cw = 18 + ui_text_w(lab[i], SZ_TINY) + 6;
		if (cx + cw > x + w && cx > x) {
			if (++row >= max_rows) break;
			cx = x;
			cy += 18;
		}
		ui_rect(cx, cy, cw, 15, C2D_Color32(0x67, 0xc1, 0xf5, 0x1c));
		ui_icon(ic[i], cx + 4, cy + 2, 11, COL_ACC);
		ui_text(cx + 18, cy + 1.5f, lab[i], SZ_TINY, COL_TXT, AL_LEFT, 0);
		cx += cw + 4;
	}
	return n ? (cy - y) + 15 : 0;
}

static u32 review_col(int pct)
{
	return pct >= 70 ? COL_ACC : pct >= 40 ? COL_MIXED : COL_NEG;
}

static void draw_price_right(float r, float y, float h, const StoreRow *row, float wd, float wp)
{
	if (row->disc > 0) {
		ui_badge_disc(r - wp - wd, y, wd, h, row->disc, SZ_SMALL);
		ui_price_box(r - wp, y, wp, h, row->orig, row->price, SZ_TINY, SZ_SMALL);
	} else {
		const char *p = row->free ? _("Gratis") : row->price[0] ? row->price : "";
		ui_text(r - 4, y + (h - ui_line_h(SZ_SMALL)) / 2 + 1, p, SZ_SMALL, row->free ? COL_LIME : COL_TXT, AL_RIGHT, 0);
	}
}

// ============================================================================
// Tienda

static const char *tab_name(int t)
{
	static const char *n[] = {"Inicio", "Ofertas", "Demos", "Betas", ""};
	return _(n[t]);
}

#define SEARCH_BAR 30   // alto de la barra de búsqueda (solo en Buscar)

static float list_top(void)
{
	return LIST_Y + (S.tab == TAB_SEARCH ? SEARCH_BAR : 0);
}

static float list_h(void)
{
	return SCR_H - 18 - list_top();
}

static const StoreRow *cur_row(void)
{
	Tab *t = CUR;
	if (t->sel < 0 || t->sel >= t->list.count) return NULL;
	const StoreRow *r = &t->list.rows[t->sel];
	return r->header ? NULL : r;
}

// Lo que pone abajo a la derecha de la vista previa
static void footer_text(char *out, size_t n)
{
	Tab *t = CUR;
	switch (S.tab) {
	case TAB_HOME:
		out[0] = 0;
		for (int i = t->sel; i >= 0; i--)
			if (t->list.rows[i].header) { snprintf(out, n, "%s", t->list.rows[i].name); break; }
		break;
	case TAB_OFFERS: snprintf(out, n, _("Oferta %d de %d"), t->sel + 1, t->list.total); break;
	case TAB_DEMOS: snprintf(out, n, _("Demo %d de %d"), t->sel + 1, t->list.total); break;
	case TAB_BETAS: snprintf(out, n, "%s", _("Acceso anticipado")); break;   // el total de Steam cuenta también los que ya salieron
	default:
		if (t->term[0]) snprintf(out, n, _("%d resultados para «%s»"), t->list.count, t->term);
		else out[0] = 0;
		break;
	}
}

static void draw_preview(const StoreRow *row)
{
	InfoEnt *e = info_get(row->appid, false);
	const AppInfo *a = (e && e->done) ? &e->info : NULL;

	// Imagen (o hueco mientras llega)
	const Pic *p = a ? img_get(a->header, 460) : NULL;
	if (p) img_draw_cover(p, 12, 28, 200, 94, 0.5f);
	else {
		ui_rect(12, 28, 200, 94, COL_PANEL);
		if (!(a && img_failed(a->header))) ui_spinner(112, 75, 7, COL_DIM);
	}

	float x = 224, w = TOP_W - 10 - x;
	int nl = ui_text_wrap(x, 26, w, row->name, SZ_LARGE, COL_WHITE, 2, 17);
	float y = 26 + nl * 17 + 4;
	if (a) {
		float tx = x;
		for (int i = 0; i < a->ngenres && i < 2; i++) draw_tag(&tx, y, a->genres[i]);
		if (a->ngenres) y += 19;
	}
	if (row->review[0] && row->review_pct > 0) {
		char t[96];
		float rx = x + ui_text(x, y, _("Reseñas: "), SZ_SMALL, COL_DIM, AL_LEFT, 0);
		snprintf(t, sizeof t, "%s", row->review);
		ui_text(rx, y, t, SZ_SMALL, review_col(row->review_pct), AL_LEFT, x + w - rx);
		y += 13;
		if (a && a->metacritic) snprintf(t, sizeof t, "%d %% · Metacritic %d", row->review_pct, a->metacritic);
		else snprintf(t, sizeof t, "%d %%", row->review_pct);
		ui_text(x, y, t, SZ_SMALL, COL_DIM, AL_LEFT, w);
		y += 13;
	}
	if (a && (a->release[0] || a->developer[0])) {
		char t[112];
		snprintf(t, sizeof t, "%s%s%s", a->release, a->release[0] && a->developer[0] ? " · " : "", a->developer);
		ui_text(x, y, t, SZ_SMALL, COL_DIM, AL_LEFT, w);
	}

	if (a) {
		if (a->desc) ui_text_wrap(12, 128, TOP_W - 24, a->desc, SZ_SMALL, COL_TXT, 2, 13);
		float fx = 12;
		if (a->rating[0]) {
			draw_rating(12, 160, 30, a);
			fx = 50;
		}
		draw_feats(fx, 160, TOP_W - 12 - fx, a, 2);
	} else if (e && e->failed) {
		ui_text(12, 140, _("No se pudo cargar la ficha"), SZ_SMALL, COL_DIM, AL_LEFT, 0);
	} else {
		ui_spinner(TOP_W / 2, 165, 7, COL_DIM);
	}
}

static void draw_store_top(void)
{
	Tab *t = CUR;
	const StoreRow *row = cur_row();
	if (S.tab == TAB_SEARCH && !t->term[0]) {
		// Buscar, sin búsqueda: qué se puede hacer
		ui_icon(IC_SEARCH, TOP_W / 2 - 18, 62, 36, COL_ACC);
		ui_text(TOP_W / 2, 112, _("Busca juegos, DLC y demos"), SZ_LARGE, COL_WHITE, AL_CENTER, 380);
		ui_text(TOP_W / 2, 136, _("Toca la barra de abajo o pulsa Y para escribir"), SZ_SMALL, COL_DIM, AL_CENTER, 380);
		if (S.nrecents) ui_text(TOP_W / 2, 154, _("Abajo, tus búsquedas recientes"), SZ_SMALL, COL_DIM, AL_CENTER, 380);
		return;
	}
	if (!row || (row->appid == 0)) {
		if (t->list.err[0] && !t->list.req) {
			int left = retry_delay(t->list.err) - (int)((osGetTime() - t->fail_at) / 1000);
			char tt[64];
			snprintf(tt, sizeof tt, _("Reintentando en %d s · A para ya"), left < 0 ? 0 : left);
			ui_text(TOP_W / 2, 92, _("No se pudo cargar esta lista"), SZ_LARGE, COL_TXT, AL_CENTER, 0);
			ui_text(TOP_W / 2, 116, human_err(t->list.err), SZ_BODY, COL_TXT, AL_CENTER, 380);
			ui_text(TOP_W / 2, 136, tt, SZ_SMALL, COL_ACC, AL_CENTER, 0);
			ui_text(TOP_W / 2, 200, t->list.err, SZ_TINY, COL_DIM, AL_CENTER, 380);
		} else if (S.tab == TAB_SEARCH && !t->list.req) {
			char tt[112];
			snprintf(tt, sizeof tt, _("No hay resultados para «%s»"), t->term);
			ui_text(TOP_W / 2, 110, tt, SZ_LARGE, COL_TXT, AL_CENTER, 380);
			ui_text(TOP_W / 2, 134, _("Prueba con otras palabras o con el nombre en inglés"), SZ_SMALL, COL_DIM, AL_CENTER, 380);
		} else {
			draw_net_wait(TOP_W / 2, 120, _("Cargando…"));
		}
		return;
	}
	draw_preview(row);

	float fe = clampf((osGetTime() - t->sel_changed) / 160.0f, 0, 1);
	if (fe < 1) ui_rect(0, 22, TOP_W, SCR_H - 40, col_alpha(COL_BG, (1 - ease_out_cubic(fe)) * 0.85f));

	char cnt[112];
	footer_text(cnt, sizeof cnt);
	ui_text(TOP_W - 12, SCR_H - 18, cnt, SZ_SMALL, COL_DIM, AL_RIGHT, TOP_W - 24);
}

static void draw_tabs(void)
{
	ui_rect(0, 0, BOT_W, LIST_Y, COL_BAR);
	float w = BOT_W / 4.6f;
	// El fondo y la raya de la pestaña marcada se deslizan hasta su sitio
	float sx = S.tab_x * w, sw = S.tab_x > TAB_COUNT - 1.5f ? BOT_W - sx : w;
	ui_rect(sx, 0, sw, LIST_Y, COL_PANEL);
	ui_rect(sx, LIST_Y - 2, sw, 2, COL_ACC);
	for (int t = 0; t < TAB_COUNT; t++) {
		float x = t * w, tw = t == TAB_SEARCH ? BOT_W - x : w;
		float on = 1 - clampf(fabsf(S.tab_x - t), 0, 1);
		u32 c = col_lerp(COL_DIM, COL_WHITE, on);
		if (t == TAB_SEARCH) ui_icon(IC_SEARCH, x + tw / 2 - 6, 6, 12, c);
		else ui_text(x + tw / 2, 5, tab_name(t), SZ_SMALL, c, AL_CENTER, tw - 4);
	}
}

static void draw_search_bar(void)
{
	Tab *t = CUR;
	float y = LIST_Y + 4;
	bool pressed = S.pressed == BT_SEARCH;
	ui_rrect(8, y, BOT_W - 16, SEARCH_BAR - 8, 11, pressed ? COL_SEL : C2D_Color32(0x31, 0x4a, 0x61, 0xff));
	ui_icon(IC_SEARCH, 16, y + 5, 12, COL_ACC);
	if (t->term[0]) ui_text(34, y + 3, t->term, SZ_SMALL, COL_WHITE, AL_LEFT, BOT_W - 60);
	else ui_text(34, y + 3, _("Buscar en la tienda…"), SZ_SMALL, COL_DIM, AL_LEFT, BOT_W - 60);
}

static void draw_store_bottom(void)
{
	Tab *t = CUR;
	ui_rect(0, 0, BOT_W, SCR_H, COL_BG);
	draw_tabs();
	if (S.tab == TAB_SEARCH) draw_search_bar();
	float top = list_top(), lh = list_h();
	ui_scissor(GFX_BOTTOM, 0, top, BOT_W, lh);
	int first = (int)(t->scroll / ROW_H);
	if (t->list.count && cur_row()) {
		float hy = top + t->sel_y * ROW_H - t->scroll;
		ui_rect(0, hy, BOT_W, ROW_H, COL_SEL);
		ui_rect(0, hy, 3, ROW_H, COL_ACC);
	}
	for (int i = first; i < t->list.count && i < first + lh / ROW_H + 2; i++) {
		const StoreRow *r = &t->list.rows[i];
		float y = top + i * ROW_H - t->scroll;
		if (r->header) {
			// Título de apartado (Inicio)
			ui_rect(0, y, BOT_W, ROW_H, C2D_Color32(0x15, 0x20, 0x2c, 0xff));
			ui_rect(8, y + ROW_H - 7, 18, 2, COL_ACC);
			ui_text(8, y + 7, r->name, SZ_SMALL, COL_ACC, AL_LEFT, BOT_W - 16);
			continue;
		}
		bool on = i == t->sel;
		ui_hline(0, y + ROW_H - 1, BOT_W, COL_LINE);
		if (r->appid == 0) {
			// Búsqueda reciente
			ui_icon(IC_SEARCH, 10, y + 9, 11, COL_DIM);
			ui_text(28, y + 7, r->name, SZ_BODY - 0.02f, on ? COL_WHITE : COL_TXT, AL_LEFT, BOT_W - 40);
			continue;
		}
		float pw = r->disc > 0 ? 36 + 50 : 56;
		float nx = 8;
		if (r->type == 1 || r->type == 4) {
			// Etiqueta pequeña para demos y DLC (en Buscar salen mezclados)
			const char *tag = r->type == 1 ? _("DEMO") : "DLC";
			float tw = ui_text_w(tag, SZ_TINY) + 8;
			ui_rect(nx, y + 8, tw, 13, COL_TAG);
			ui_text(nx + 4, y + 8.5f, tag, SZ_TINY, COL_ACC, AL_LEFT, 0);
			nx += tw + 5;
		}
		ui_text(nx, y + 7, r->name, SZ_BODY - 0.02f, on ? COL_WHITE : COL_TXT, AL_LEFT, BOT_W - 10 - nx - pw);
		if (r->coming_soon && !r->price[0]) {
			ui_text(BOT_W - 10, y + 8, _("Próximamente"), SZ_TINY, COL_DIM, AL_RIGHT, 0);
		} else {
			draw_price_right(BOT_W - 6, y + 2, ROW_H - 5, r, 36, 50);
		}
	}
	float end_y = top + t->list.count * ROW_H - t->scroll;
	if (t->list.req && end_y < SCR_H) ui_spinner(BOT_W / 2, end_y + 14, 6, COL_DIM);
	if (S.tab == TAB_SEARCH && !t->term[0] && !t->list.count)
		ui_text(BOT_W / 2, top + 30, _("Aún no has buscado nada"), SZ_SMALL, COL_DIM, AL_CENTER, 0);
	ui_scissor_off();
	// Barra de scroll
	float total = t->list.count * ROW_H;
	if (total > lh) {
		float hh = lh * lh / total;
		if (hh < 12) hh = 12;
		float y = top + (lh - hh) * (t->scroll / (total - lh));
		ui_rect(BOT_W - 2, y, 2, hh, C2D_Color32(0x3d, 0x5a, 0x73, 0xff));
	}
	if (S.tab == TAB_SEARCH) {
		if (t->term[0]) {
			static const char *hs[] = {"Y", "Buscar", "A", "Ficha", "B", "Borrar", "L R", "Pestaña"};
			draw_hints(hs, 4);
		} else {
			static const char *hs[] = {"Y", "Buscar", "A", "Abrir", "L R", "Pestaña"};
			draw_hints(hs, 3);
		}
	} else {
		static const char *hh2[] = {"A", "Ficha", "L R", "Pestaña", "START", "Menú"};
		draw_hints(hh2, 3);
	}
}

// Elige una fila de la pestaña actual, saltándose los títulos de apartado
static void store_select(int sel, int dir)
{
	Tab *t = CUR;
	if (t->list.count == 0) return;
	if (sel < 0) sel = 0;
	if (sel >= t->list.count) sel = t->list.count - 1;
	while (t->list.rows[sel].header) {
		int n = sel + (dir ? dir : 1);
		if (n < 0 || n >= t->list.count) {
			dir = -(dir ? dir : 1);
			n = sel + dir;
			if (n < 0 || n >= t->list.count) return;
		}
		sel = n;
	}
	if (sel != t->sel) {
		t->sel = sel;
		t->sel_changed = osGetTime();
	}
	t->finger = false;
	float y = t->sel * ROW_H, lh = list_h();
	// Si encima hay un título de apartado, que se vea también
	float ytop = (t->sel > 0 && t->list.rows[t->sel - 1].header) ? y - ROW_H : y;
	if (ytop < t->scroll_target) t->scroll_target = ytop;
	if (y + ROW_H > t->scroll_target + lh) t->scroll_target = y + ROW_H - lh;
	if (tab_paged(S.tab) && t->sel >= t->list.count - 8) tab_more(S.tab);
}

// ============================================================================
// Ficha

static void open_game(u32 appid, bool push)
{
	if (push && S.scene == SC_GAME && S.depth < 8) {
		S.stack_app[S.depth] = S.app;
		S.stack_sec[S.depth] = S.sec;
		S.stack_lsel[S.depth] = S.lsel;
		S.depth++;
	}
	if (!push) S.depth = 0;
	go_scene(SC_GAME, 1);
	S.app = appid;
	S.sec = SEC_SUMMARY;
	S.show_qr = false;
	for (int i = 0; i < S.nreviews; i++) free(S.reviews[i].text);
	S.nreviews = 0;
	S.rcursor[0] = 0;
	C2D_TextBufClear(review_buf);
	if (S.rreq) net_release(S.rreq);
	S.rreq = NULL;
	S.rend = false;
	S.rsel = 0;
	S.rscroll = 0;
	S.lsel = 0;
	S.lscroll = 0;
	S.lsel_y = 0;
	S.seg_x = 0;
	S.sec_anim = 1;
	InfoEnt *e = info_get(appid, true);
	if (e && e->failed) info_retry(e);
}

static void game_back(void)
{
	if (S.show_qr) { S.show_qr = false; return; }
	if (S.depth > 0) {
		S.depth--;
		u32 a = S.stack_app[S.depth];
		int sec = S.stack_sec[S.depth], lsel = S.stack_lsel[S.depth], keep = S.depth;
		open_game(a, false);
		S.depth = keep;
		S.sec = sec;
		S.seg_x = sec;
		S.lsel = lsel;
		S.lsel_y = lsel;
		S.scene_dir = -1;
		return;
	}
	go_scene(SC_STORE, -1);
}

static void reviews_more(void)
{
	if (S.rreq || S.rend || S.nreviews >= MAX_REVIEWS) return;
	S.rreq = steam_req_reviews(S.app, S.rcursor);
}

static void game_poll(const AppInfo *a)
{
	if (S.rreq && S.rreq->state >= REQ_DONE) {
		if (S.rreq->state == REQ_DONE) {
			int n = steam_parse_reviews(S.rreq->data, S.reviews + S.nreviews, MAX_REVIEWS - S.nreviews, S.rcursor, sizeof S.rcursor);
			if (n <= 0) S.rend = true;
			for (int i = 0; i < n; i++) {
				int k = S.nreviews + i;
				ui_block_parse(&S.rtext[k], review_buf, S.reviews[k].text, SZ_TINY, TOP_W - 36, 3);
			}
			if (n > 0) S.nreviews += n;
		} else {
			S.rend = true;
		}
		net_release(S.rreq);
		S.rreq = NULL;
	}
	if (!a) return;
	if (S.sec == SEC_REVIEWS && S.nreviews == 0 && !S.rend) reviews_more();
	if (S.sec == SEC_EXTRAS && S.dlc_for != a->appid) {
		rowlist_free(&S.dlc);
		S.dlc_for = a->appid;
		// En un DLC, Extras enseña el juego base que hace falta para usarlo
		if (a->fullgame) S.dlc.req = steam_req_items(&a->fullgame, 1);
		else if (a->ndlc) S.dlc.req = steam_req_items(a->dlc, a->ndlc > 50 ? 50 : a->ndlc);
	}
	if (S.sec == SEC_SIMILAR && S.similar_for != a->appid) {
		rowlist_free(&S.similar);
		S.similar_for = a->appid;
		S.similar.req = steam_req_morelike(a->appid);
	}
	list_poll(&S.dlc, false);
	list_poll(&S.similar, false);
}

static void draw_summary_top(const AppInfo *a)
{
	const Pic *p = img_get(a->header, 460);
	if (p) img_draw_cover(p, 0, 18, TOP_W, 110, 0.3f);
	else ui_rect(0, 18, TOP_W, 110, COL_PANEL);
	ui_vgrad(0, 78, TOP_W, 51, C2D_Color32(0x1b, 0x28, 0x38, 0x00), COL_BG);
	ui_text(12, 100, a->name, SZ_TITLE, COL_WHITE, AL_LEFT, TOP_W - 24);

	float x = 12, y = 126;
	for (int i = 0; i < a->ngenres; i++) draw_tag(&x, y, a->genres[i]);
	if (a->review_label[0]) {
		x += 2;
		x += ui_text(x, y, _("Reseñas: "), SZ_SMALL, COL_DIM, AL_LEFT, 0);
		x += ui_text(x, y, a->review_label, SZ_SMALL, review_col(a->review_pct), AL_LEFT, TOP_W - 12 - x);
		if (a->review_pct) {
			char t[16];
			snprintf(t, sizeof t, " (%d %%)", a->review_pct);
			ui_text(x, y, t, SZ_SMALL, COL_DIM, AL_LEFT, 0);
		}
	}

	y = 148;
	float fx = 12;
	if (a->rating[0]) {
		draw_rating(12, y, 32, a);
		fx = 52;
	}
	if (a->ndesc) {
		char d[160] = "";
		for (int i = 0; i < a->ndesc; i++) {
			if (i) strcat(d, " · ");
			strcat(d, _(a->descriptors[i]));
		}
		ui_text(fx, y, d, SZ_TINY, COL_DIM, AL_LEFT, TOP_W - 12 - fx);
		y += 15;
	}
	draw_feats(fx, y, TOP_W - 12 - fx, a, a->ndesc ? 2 : 3);

	char t[112];
	snprintf(t, sizeof t, "%s%s%s", a->release, a->release[0] && a->developer[0] ? " · " : "", a->developer);
	ui_text(TOP_W - 12, SCR_H - 18, t, SZ_SMALL, COL_DIM, AL_RIGHT, TOP_W - 24);
}

static float draw_req_col(float x, float y, float w, const char *title, const ReqLine *l, int n)
{
	ui_text(x, y, title, SZ_SMALL, COL_ACC, AL_LEFT, w);
	y += 15;
	for (int i = 0; i < n; i++) {
		char t[140];
		snprintf(t, sizeof t, "%s: %s", l[i].key, l[i].val);
		// La clave en gris y el valor en claro: se dibuja todo en claro y encima la clave
		int lines = ui_text_wrap(x, y, w, t, SZ_TINY, COL_TXT, 3, 12);
		char k[32];
		snprintf(k, sizeof k, "%s:", l[i].key);
		ui_rect(x, y, ui_text_w(k, SZ_TINY), 12, COL_BG);
		ui_text(x, y, k, SZ_TINY, COL_DIM, AL_LEFT, 0);
		y += lines * 12 + 1;
	}
	return y;
}

static void draw_reqs_top(const AppInfo *a)
{
	float x = 12 + ui_text(12, 24, a->name, SZ_LARGE, COL_WHITE, AL_LEFT, 220);
	ui_text(x, 27, _(" · Requisitos (Windows)"), SZ_SMALL, COL_DIM, AL_LEFT, TOP_W - 12 - x);
	if (!a->nmin && !a->nrec) {
		ui_text(TOP_W / 2, 110, _("Steam no indica requisitos para este juego"), SZ_BODY, COL_DIM, AL_CENTER, 0);
		return;
	}
	if (a->nmin) draw_req_col(12, 46, a->nrec ? 182 : 376, _("MÍNIMO"), a->rmin, a->nmin);
	if (a->nrec) draw_req_col(a->nmin ? 206 : 12, 46, a->nmin ? 182 : 376, _("RECOMENDADO"), a->rrec, a->nrec);
	if (a->mac || a->lnx) {
		const char *t = a->mac && a->lnx ? _("También en macOS y Linux") : a->mac ? _("También en macOS") : _("También en Linux");
		ui_text(12, SCR_H - 18, t, SZ_SMALL, COL_DIM, AL_LEFT, 0);
	}
}

static void draw_reviews_top(const AppInfo *a)
{
	if (a->review_label[0]) {
		ui_text(12, 24, a->review_label, SZ_BODY, COL_WHITE, AL_LEFT, 0);
		char t[64];
		snprintf(t, sizeof t, _("%d %% de %d reseñas"), a->review_pct, a->review_count);
		ui_text(TOP_W - 12, 25, t, SZ_SMALL, COL_DIM, AL_RIGHT, 0);
		ui_rect(12, 42, TOP_W - 24, 4, COL_NEG);
		ui_rect(12, 42, (TOP_W - 24) * a->review_pct / 100.0f, 4, COL_ACC);
	}
	float top = 52, bottom = SCR_H - 20;
	if (S.nreviews == 0) {
		if (S.rend) ui_text(TOP_W / 2, 120, _("Todavía no hay reseñas en tu idioma"), SZ_BODY, COL_DIM, AL_CENTER, 0);
		else draw_net_wait(TOP_W / 2, 125, NULL);
		return;
	}
	ui_scissor(GFX_TOP, 0, top, TOP_W, bottom - top);
	float y = top - S.rscroll;
	for (int i = 0; i < S.nreviews; i++) {
		const Review *r = &S.reviews[i];
		float h = 20 + 3 * 12 + 6;
		if (y + h > top && y < bottom) {
			ui_rect(12, y, TOP_W - 24, h, i == S.rsel ? COL_SEL : C2D_Color32(0, 0, 0, 0x2e));
			ui_icon(r->voted_up ? IC_THUMB_UP : IC_THUMB_DOWN, 18, y + 5, 11, r->voted_up ? COL_ACC : COL_NEG);
			ui_text(34, y + 4, r->voted_up ? _("Recomendado") : _("No recomendado"), SZ_SMALL, COL_WHITE, AL_LEFT, 0);
			char hrs[32];
			snprintf(hrs, sizeof hrs, _("%d h jugadas"), r->hours);
			ui_text(TOP_W - 18, y + 4, hrs, SZ_SMALL, COL_DIM, AL_RIGHT, 0);
			ui_block_draw(&S.rtext[i], 18, y + 20, SZ_TINY, COL_TXT, 12);
		}
		y += h + 5;
	}
	ui_scissor_off();
	if (S.rreq) ui_spinner(TOP_W / 2, SCR_H - 10, 5, COL_DIM);
}

static void draw_list_top(const char *title, const RowList *l, const char *empty)
{
	ui_text(12, 24, title, SZ_SMALL, COL_ACC, AL_LEFT, TOP_W - 24);
	float top = 42, bottom = SCR_H - 4, rh = 22;
	if (l->count == 0) {
		if (l->req) draw_net_wait(TOP_W / 2, 125, NULL);
		else ui_text(TOP_W / 2, 120, l->err[0] ? l->err : empty, SZ_BODY, COL_DIM, AL_CENTER, 380);
		return;
	}
	ui_scissor(GFX_TOP, 0, top, TOP_W, bottom - top);
	float hy = top + S.lsel_y * rh - S.lscroll;
	ui_rect(12, hy, TOP_W - 24, rh - 1, COL_SEL);
	ui_rect(12, hy, 3, rh - 1, COL_ACC);
	for (int i = 0; i < l->count; i++) {
		float y = top + i * rh - S.lscroll;
		if (y + rh < top || y > bottom) continue;
		const StoreRow *r = &l->rows[i];
		bool on = i == S.lsel;
		float pw = r->disc > 0 ? 36 + 52 : 56;
		float nw = ui_text(20, y + 4, r->name, SZ_SMALL, on ? COL_WHITE : COL_TXT, AL_LEFT, TOP_W - 40 - pw);
		if (r->review[0] && r->review_pct > 0 && 20 + nw + 20 < TOP_W - 40 - pw) {
			char t[56];
			snprintf(t, sizeof t, "· %s", r->review);
			ui_text(26 + nw, y + 5, t, SZ_TINY, review_col(r->review_pct), AL_LEFT, TOP_W - 40 - pw - nw - 8);
		}
		draw_price_right(TOP_W - 16, y + 1, rh - 3, r, 36, 52);
	}
	ui_scissor_off();
}

static void draw_game_top(void)
{
	InfoEnt *e = info_get(S.app, false);
	if (!e || !e->done) {
		if (e && e->failed) {
			int left = retry_delay(e->err) - (int)((osGetTime() - e->fail_at) / 1000);
			char t[64];
			snprintf(t, sizeof t, _("Reintentando en %d s · A para ya"), left < 0 ? 0 : left);
			ui_text(TOP_W / 2, 92, _("No se pudo cargar la ficha"), SZ_LARGE, COL_TXT, AL_CENTER, 0);
			ui_text(TOP_W / 2, 116, human_err(e->err), SZ_BODY, COL_TXT, AL_CENTER, 380);
			ui_text(TOP_W / 2, 136, t, SZ_SMALL, COL_ACC, AL_CENTER, 0);
			ui_text(TOP_W / 2, 200, e->err, SZ_TINY, COL_DIM, AL_CENTER, 380);
		} else {
			draw_net_wait(TOP_W / 2, 120, _("Cargando ficha…"));
		}
		return;
	}
	const AppInfo *a = &e->info;
	if (S.show_qr) {
		char url[96];
		snprintf(url, sizeof url, "https://store.steampowered.com/app/%lu/", (unsigned long)a->appid);
		ui_qr(url, 20, 34, 170);
		ui_text(206, 50, _("Comprar en el móvil"), SZ_LARGE, COL_WHITE, AL_LEFT, 180);
		ui_text_wrap(206, 74, 180, _("Escanea el código con la cámara: se abre este juego en la tienda de Steam y lo pagas desde ahí."), SZ_SMALL, COL_TXT, 6, 14);
		ui_text(206, 170, a->name, SZ_SMALL, COL_DIM, AL_LEFT, 180);
		ui_text(TOP_W / 2, SCR_H - 18, _("B para volver"), SZ_SMALL, COL_DIM, AL_CENTER, 0);
		return;
	}
	C3D_Mtx view;
	C2D_ViewSave(&view);
	float se = ease_out_cubic(S.sec_anim);
	C2D_ViewTranslate(S.sec_dir * (1 - se) * 22, 0);
	switch (S.sec) {
	case SEC_SUMMARY: draw_summary_top(a); break;
	case SEC_REQS: draw_reqs_top(a); break;
	case SEC_REVIEWS: draw_reviews_top(a); break;
	case SEC_EXTRAS: {
		if (a->fullgame) {
			draw_list_top(a->is_demo ? _("JUEGO COMPLETO") : _("CONTENIDO NECESARIO"), &S.dlc, "");
			if (S.dlc.count)
				ui_text_wrap(12, 72, TOP_W - 24, a->is_demo ? _("Esta demo es una muestra de este juego.")
					: _("Para usar este contenido descargable necesitas tener el juego base en tu biblioteca."),
					SZ_SMALL, COL_DIM, 3, 14);
			break;
		}
		char t[64];
		snprintf(t, sizeof t, _("CONTENIDO DESCARGABLE (%d)"), a->ndlc);
		draw_list_top(t, &S.dlc, _("Este juego no tiene contenido descargable"));
		break;
	}
	case SEC_SIMILAR: {
		char t[160];
		snprintf(t, sizeof t, _("MÁS COMO %s"), a->name);
		draw_list_top(t, &S.similar, _("Steam no tiene juegos parecidos para este"));
		break;
	}
	}
	C2D_ViewRestore(&view);
	if (se < 1) ui_rect(0, 18, TOP_W, SCR_H - 18, col_alpha(COL_BG, (1 - se) * 0.9f));
}

// Botones de la ficha (también se usan para el toque)
typedef struct { float x, y, w, h; } Box;
static const Box B_SEG = {12, 6, 296, 22};
static const Box B_CART = {12, 76, 296, 26};
static const Box B_WISH = {12, 108, 145, 26};
static const Box B_SHOTS = {163, 108, 145, 26};
static const Box B_INST = {12, 140, 296, 26};

static bool hit(const Box *b, int x, int y)
{
	return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

static void draw_button(const Box *b, Icon ic, const char *label, u32 top, u32 bot, u32 fg, float alpha, bool pressed)
{
	// Hundido mientras se toca: un poco más pequeño y más claro
	float in = pressed ? 2 : 0;
	if (pressed) {
		top = col_lerp(top, COL_WHITE, 0.15f);
		bot = col_lerp(bot, COL_WHITE, 0.15f);
	}
	ui_vgrad(b->x + in, b->y + in, b->w - 2 * in, b->h - 2 * in, col_alpha(top, alpha), col_alpha(bot, alpha));
	float tw = ui_text_w(label, SZ_SMALL) + 18;
	float x = b->x + (b->w - tw) / 2;
	u32 f = col_alpha(fg, alpha < 1 ? 0.5f : 1);
	ui_icon(ic, x, b->y + (b->h - 12) / 2, 12, f);
	ui_text(x + 18, b->y + (b->h - ui_line_h(SZ_SMALL)) / 2 + 1, label, SZ_SMALL, f, AL_LEFT, b->w - 24);
}

static void draw_game_bottom(void)
{
	ui_rect(0, 0, BOT_W, SCR_H, COL_BG);
	InfoEnt *e = info_get(S.app, false);
	const AppInfo *a = (e && e->done) ? &e->info : NULL;

	// Selector de apartados
	ui_rrect(B_SEG.x, B_SEG.y, B_SEG.w, B_SEG.h, 3, COL_BAR);
	static const char *secs[] = {"Resumen", "Requisitos", "Opiniones", "Extras", "Similares"};
	float sw = (B_SEG.w - 4) / SEC_COUNT;
	// La píldora del apartado se desliza hasta su sitio
	ui_rrect(B_SEG.x + 2 + S.seg_x * sw, B_SEG.y + 2, sw, B_SEG.h - 4, 2, COL_SEL);
	for (int i = 0; i < SEC_COUNT; i++) {
		float x = B_SEG.x + 2 + i * sw;
		float on = 1 - clampf(fabsf(S.seg_x - i), 0, 1);
		ui_text(x + sw / 2, B_SEG.y + 5, _(secs[i]), SZ_TINY, col_lerp(COL_DIM, COL_WHITE, on), AL_CENTER, sw - 4);
	}

	if (a) {
		ui_text(12, 36, a->is_free ? _("Jugar") : _("Comprar"), SZ_SMALL, COL_DIM, AL_LEFT, 0);
		ui_text(12, 50, a->name, SZ_BODY, COL_WHITE, AL_LEFT, a->disc ? 160 : 190);
		StoreRow pr = {0};
		pr.disc = a->disc;
		snprintf(pr.price, sizeof pr.price, "%s", a->price);
		snprintf(pr.orig, sizeof pr.orig, "%s", a->orig);
		pr.free = a->is_free;
		if (a->disc > 0) {
			ui_badge_disc(308 - 64 - 52, 36, 52, 32, a->disc, SZ_LARGE);
			ui_price_box(308 - 64, 36, 64, 32, a->orig, a->price, SZ_SMALL, SZ_BODY + 0.05f);
		} else {
			const char *p = a->is_free ? _("Gratis") : a->has_price ? a->price : a->coming_soon ? _("Próximamente") : _("No disponible");
			float w = ui_text_w(p, SZ_BODY + 0.05f) + 16;
			ui_rect(308 - w, 36, w, 32, COL_SHADE);
			ui_text(308 - 8, 44, p, SZ_BODY + 0.05f, a->is_free ? COL_LIME : COL_TXT, AL_RIGHT, 0);
		}
		// Sin sesión: carrito, deseados e instalar se ven apagados
		draw_button(&B_CART, IC_CART, a->is_free ? _("Añadir a la biblioteca") : _("Añadir al carrito"),
			C2D_Color32(0x75, 0xb0, 0x22, 0xff), C2D_Color32(0x58, 0x8a, 0x1b, 0xff), C2D_Color32(0xd2, 0xef, 0xa9, 0xff), 0.45f, S.pressed == BT_CART);
		draw_button(&B_WISH, IC_HEART, _("Deseados"), COL_BTN2, COL_BTN2, COL_ACC, 0.45f, S.pressed == BT_WISH);
		char shots[40];
		snprintf(shots, sizeof shots, _("Capturas (%d)"), a->nshots);
		draw_button(&B_SHOTS, IC_IMAGE, shots, COL_BTN2, COL_BTN2, COL_ACC, a->nshots ? 1 : 0.45f, S.pressed == BT_SHOTS);
		draw_button(&B_INST, IC_PC, _("Instalar en mi PC"), C2D_Color32(0x47, 0xbf, 0xff, 0xff), C2D_Color32(0x1a, 0x44, 0xc2, 0xff), COL_WHITE, 0.4f, S.pressed == BT_INST);
		ui_text(BOT_W / 2, 172, _("Inicia sesión para usar el carrito, deseados e instalar"), SZ_TINY, COL_DIM, AL_CENTER, 300);
	}

	if (S.sec >= SEC_EXTRAS) {
		static const char *h[] = {"↑↓", "Elegir", "A", "Abrir ficha", "L R", "Apartado", "B", "Volver"};
		draw_hints(h, 4);
	} else {
		static const char *h[] = {"L R", "Apartado", "B", "Volver", "X", "Comprar en el móvil"};
		draw_hints(h, 3);
	}
}

// ============================================================================
// Capturas

static void draw_shots_top(void)
{
	InfoEnt *e = info_get(S.app, false);
	ui_rect(0, 0, TOP_W, SCR_H, C2D_Color32(0, 0, 0, 0xff));
	if (!e || !e->done || e->info.nshots == 0) return;
	const char *url = e->info.shots[S.shot];
	const Pic *p = img_get(url, 600);
	// La nueva entra deslizándose desde el lado hacia el que vas
	float se = ease_out_cubic(S.shot_anim);
	if (p) {
		C3D_Mtx view;
		C2D_ViewSave(&view);
		C2D_ViewTranslate(S.shot_dir * (1 - se) * 50, 0);
		img_draw_fit_a(p, 0, 0, TOP_W, SCR_H, 0.25f + 0.75f * se);
		C2D_ViewRestore(&view);
	} else if (img_failed(url)) {
		ui_text(TOP_W / 2, 112, _("No se pudo cargar la captura"), SZ_BODY, COL_DIM, AL_CENTER, 0);
	} else {
		ui_spinner(TOP_W / 2, SCR_H / 2, 9, COL_DIM);
	}
	// Las de al lado, ya pedidas para que el cambio sea inmediato
	if (S.shot + 1 < e->info.nshots) img_get(e->info.shots[S.shot + 1], 600);
	if (S.shot > 0) img_get(e->info.shots[S.shot - 1], 600);
}

#define THUMB_W 92
#define THUMB_H 52
#define THUMB_STEP 100
#define STRIP_Y 76
static const Box B_PREV = {12, 156, 140, 34};
static const Box B_NEXT = {168, 156, 140, 34};

static void draw_round_button(const Box *b, const char *label, bool enabled, bool pressed)
{
	float in = pressed ? 2 : 0;
	u32 bg = pressed ? col_lerp(COL_BTN2, COL_WHITE, 0.15f) : COL_BTN2;
	ui_rrect(b->x + in, b->y + in, b->w - 2 * in, b->h - 2 * in, (b->h - 2 * in) / 2, col_alpha(bg, enabled ? 1 : 0.45f));
	ui_text(b->x + b->w / 2, b->y + (b->h - ui_line_h(SZ_BODY)) / 2 + 1, label, SZ_BODY, col_alpha(COL_ACC, enabled ? 1 : 0.4f), AL_CENTER, b->w - 12);
}

static void draw_shots_bottom(void)
{
	ui_vgrad(0, 0, BOT_W, SCR_H, C2D_Color32(0x1e, 0x2d, 0x3e, 0xff), C2D_Color32(0x14, 0x19, 0x21, 0xff));
	InfoEnt *e = info_get(S.app, false);
	if (e && e->done) {
		const AppInfo *a = &e->info;
		ui_text(BOT_W / 2, 10, a->name, SZ_SMALL, COL_DIM, AL_CENTER, 296);
		// «3 / 19», con el total más apagado
		char cur[16], tot[16];
		snprintf(cur, sizeof cur, "%d", S.shot + 1);
		snprintf(tot, sizeof tot, " / %d", a->nshots);
		float w1 = ui_text_w(cur, SZ_TITLE), w2 = ui_text_w(tot, SZ_LARGE);
		float x = (BOT_W - w1 - w2) / 2;
		ui_text(x, 28, cur, SZ_TITLE, COL_WHITE, AL_LEFT, 0);
		ui_text(x + w1, 32, tot, SZ_LARGE, COL_DIM, AL_LEFT, 0);

		// Tira de miniaturas: se desliza hasta la actual, que va marcada
		for (int i = 0; i < a->nshots; i++) {
			float cx = BOT_W / 2 + (i - S.strip) * THUMB_STEP;
			if (cx < -THUMB_W || cx > BOT_W + THUMB_W) continue;
			float on = 1 - clampf(fabsf(S.strip - i), 0, 1);
			float sc = 1 + 0.14f * on;
			float w = THUMB_W * sc, hh = THUMB_H * sc;
			float x0 = cx - w / 2, y0 = STRIP_Y + (THUMB_H * 1.14f - hh) / 2;
			if (i == S.shot) ui_rrect(x0 - 3, y0 - 3, w + 6, hh + 6, 4, col_alpha(COL_ACC, on));
			ui_rect(x0, y0, w, hh, C2D_Color32(0x0b, 0x10, 0x16, 0xff));
			const Pic *p = img_get(a->shots[i], 150);
			if (p) img_draw_fit_a(p, x0, y0, w, hh, 0.45f + 0.55f * on);
			else ui_spinner(cx, y0 + hh / 2, 5, COL_DIM);
		}
		draw_round_button(&B_PREV, _("‹ Anterior"), S.shot > 0, S.pressed == BT_PREV);
		draw_round_button(&B_NEXT, _("Siguiente ›"), S.shot + 1 < a->nshots, S.pressed == BT_NEXT);
	}
	static const char *hh[] = {"← →", "Cambiar", "B", "Volver"};
	draw_hints(hh, 2);
}

// ============================================================================
// Diálogo de salida y avisos

static const Box B_EXIT_NO = {42, 138, 112, 34};
static const Box B_EXIT_YES = {166, 138, 112, 34};

static void draw_exit_dialog(void)
{
	float a = ease_out_cubic(S.exit_anim);
	ui_rect(0, 0, BOT_W, SCR_H, C2D_Color32(0, 0, 0, (u8)(0xa8 * a)));
	// Aparece con un pequeño rebote, como en Gemi3DS y Cloud3DS
	float s = 0.88f + 0.12f * (S.ask_exit ? ease_out_back(S.exit_anim) : a);
	float cx = BOT_W / 2, cy = 112;
	C3D_Mtx view;
	C2D_ViewSave(&view);
	C2D_ViewTranslate(cx, cy);
	C2D_ViewScale(s, s);
	C2D_ViewTranslate(-cx, -cy);
	float x0 = cx - 132, y0 = cy - 78;
	ui_rrect(x0, y0 + 2, 264, 156, 10, C2D_Color32(0, 0, 0, (u8)(0x60 * a)));
	ui_rrect(x0, y0, 264, 156, 10, col_alpha(COL_PANEL, a));
	ui_rect(x0 + 10, y0, 244, 2, col_alpha(C2D_Color32(0xe0, 0x4b, 0x3a, 0xff), a));
	ui_circle(cx, y0 + 30, 17, col_alpha(C2D_Color32(0xe0, 0x4b, 0x3a, 0x40), a));
	ui_icon(IC_POWER, cx - 9, y0 + 21, 18, col_alpha(C2D_Color32(0xff, 0x7a, 0x6a, 0xff), a));
	ui_text(cx, y0 + 54, _("¿Salir de Steam3DS?"), SZ_LARGE, col_alpha(COL_WHITE, a), AL_CENTER, 240);
	ui_text(cx, y0 + 78, _("Volverás al menú HOME."), SZ_SMALL, col_alpha(COL_DIM, a), AL_CENTER, 240);

	const char *gb = ui_button_glyph("B"), *ga = ui_button_glyph("A");
	char no[48], yes[48];
	snprintf(no, sizeof no, "%s%s%s", gb ? gb : "", gb ? " " : "", _("Cancelar"));
	snprintf(yes, sizeof yes, "%s%s%s", ga ? ga : "", ga ? " " : "", _("Salir"));
	bool pn = S.pressed == BT_EXIT_NO, py = S.pressed == BT_EXIT_YES;
	float in = pn ? 2 : 0;
	ui_rrect(B_EXIT_NO.x + in, B_EXIT_NO.y + in, B_EXIT_NO.w - 2 * in, B_EXIT_NO.h - 2 * in, 17 - in,
		col_alpha(pn ? col_lerp(COL_BTN2, COL_WHITE, 0.15f) : COL_BTN2, a));
	ui_text(B_EXIT_NO.x + B_EXIT_NO.w / 2, B_EXIT_NO.y + 9, no, SZ_BODY, col_alpha(COL_ACC, a), AL_CENTER, 104);
	in = py ? 2 : 0;
	u32 red = C2D_Color32(0xc8, 0x3b, 0x2e, 0xff);
	ui_rrect(B_EXIT_YES.x + in, B_EXIT_YES.y + in, B_EXIT_YES.w - 2 * in, B_EXIT_YES.h - 2 * in, 17 - in,
		col_alpha(py ? col_lerp(red, COL_WHITE, 0.15f) : red, a));
	ui_text(B_EXIT_YES.x + B_EXIT_YES.w / 2, B_EXIT_YES.y + 9, yes, SZ_BODY, col_alpha(COL_WHITE, a), AL_CENTER, 104);
	C2D_ViewRestore(&view);
}

static void draw_overlays_bottom(void)
{
	if (S.toast_t > 0) {
		// Sube desde abajo y se apaga al final
		float in = ease_out_cubic((2.6f - S.toast_t) / 0.25f);
		float a = clampf(S.toast_t / 0.3f, 0, 1) * in;
		float w = ui_text_w(S.toast, SZ_SMALL) + 44;
		if (w > 300) w = 300;
		float x = (BOT_W - w) / 2, y = 190 + (1 - in) * 16;
		ui_rrect(x, y, w, 26, 13, col_alpha(C2D_Color32(0x0e, 0x14, 0x1b, 0xf0), a));
		ui_icon(IC_LOCK, x + 12, y + 7, 12, col_alpha(COL_ACC, a));
		ui_text(x + 30, y + 5, S.toast, SZ_SMALL, col_alpha(COL_WHITE, a), AL_LEFT, w - 40);
	}
	if (S.exit_anim > 0.01f) draw_exit_dialog();
}



// ============================================================================
// Menú de START e inicio de sesión

static bool key_repeat(u32 down, u32 held, u32 mask);

enum { MI_LOGIN, MI_PROFILE, MI_LIBRARY, MI_WISHLIST, MI_LOGOUT, MI_EXIT };
#define MENU_ROW 30
#define MENU_HEAD 44

static int menu_items(int *out)
{
	int n = 0;
	if (!auth_logged()) {
		out[n++] = MI_LOGIN;
	} else {
		out[n++] = MI_PROFILE;
		out[n++] = MI_LIBRARY;
		out[n++] = MI_WISHLIST;
		out[n++] = MI_LOGOUT;
	}
	out[n++] = MI_EXIT;
	return n;
}

static float menu_y(int n)
{
	return SCR_H - 18 - (MENU_HEAD + n * MENU_ROW + 8);
}

static void open_login(void)
{
	S.login_back = S.scene == SC_LOGIN ? SC_STORE : S.scene;
	S.login_done = 0;
	go_scene(SC_LOGIN, 1);
	if (auth_info()->state != AUTH_QR) auth_begin_qr();
}

static void menu_choose(int item)
{
	S.menu_open = false;
	switch (item) {
	case MI_LOGIN: open_login(); break;
	case MI_LOGOUT:
		auth_logout();
		toast(_("Sesión cerrada"));
		break;
	case MI_EXIT:
		S.ask_exit = true;
		S.exit_anim = 0;
		break;
	default:
		toast(_("Llega en la siguiente parte de esta fase"));
		break;
	}
}

static void draw_menu(void)
{
	float a = ease_out_cubic(S.menu_anim);
	if (a <= 0.01f) return;
	int items[8], n = menu_items(items);
	ui_rect(0, 0, BOT_W, SCR_H, C2D_Color32(0, 0, 0, (u8)(0x90 * a)));
	float y0 = menu_y(n) + (1 - a) * 60;
	float hgt = SCR_H - y0;
	ui_rrect(6, y0, BOT_W - 12, hgt + 10, 10, col_alpha(COL_PANEL, a));
	ui_rect(BOT_W / 2 - 16, y0 + 5, 32, 3, col_alpha(COL_DIM, a * 0.6f));
	// Cabecera: tu cuenta (o «sin sesión»)
	const AuthInfo *ai = auth_info();
	float hx = 18, hy = y0 + 12;
	if (auth_logged()) {
		const Pic *p = ai->avatar[0] ? img_get(ai->avatar, 64) : NULL;
		ui_rect(hx, hy, 26, 26, col_alpha(COL_BG, a));
		if (p) img_draw_cover(p, hx, hy, 26, 26, 0.5f);
		else ui_icon(IC_USER, hx + 5, hy + 5, 16, col_alpha(COL_DIM, a));
		ui_text(hx + 34, hy, ai->persona, SZ_BODY, col_alpha(COL_WHITE, a), AL_LEFT, 200);
		char lv[72];
		if (ai->level) snprintf(lv, sizeof lv, _("Nivel %d"), ai->level);
		else snprintf(lv, sizeof lv, "%s", ai->account);
		ui_text(hx + 34, hy + 14, lv, SZ_TINY, col_alpha(COL_DIM, a), AL_LEFT, 200);
	} else {
		ui_icon(IC_USER, hx + 3, hy + 4, 18, col_alpha(COL_DIM, a));
		ui_text(hx + 34, hy, APP_NAME, SZ_BODY, col_alpha(COL_WHITE, a), AL_LEFT, 200);
		ui_text(hx + 34, hy + 14, _("Sin iniciar sesión"), SZ_TINY, col_alpha(COL_DIM, a), AL_LEFT, 200);
	}
	static const struct { Icon ic; const char *label; } info[] = {
		[MI_LOGIN] = {IC_PHONE, "Iniciar sesión con el móvil"},
		[MI_PROFILE] = {IC_USER, "Mi perfil"},
		[MI_LIBRARY] = {IC_PAD, "Biblioteca"},
		[MI_WISHLIST] = {IC_HEART, "Lista de deseos"},
		[MI_LOGOUT] = {IC_LOCK, "Cerrar sesión"},
		[MI_EXIT] = {IC_POWER, "Salir de Steam3DS"},
	};
	for (int i = 0; i < n; i++) {
		float y = y0 + MENU_HEAD + i * MENU_ROW;
		bool on = i == S.menu_sel;
		if (on) ui_rrect(12, y, BOT_W - 24, MENU_ROW - 2, 5, col_alpha(COL_SEL, a));
		u32 c = items[i] == MI_EXIT || items[i] == MI_LOGOUT ? C2D_Color32(0xff, 0x8a, 0x7a, 0xff) : COL_ACC;
		ui_icon(info[items[i]].ic, 22, y + 8, 12, col_alpha(c, a));
		ui_text(42, y + 6, _(info[items[i]].label), SZ_BODY - 0.02f, col_alpha(on ? COL_WHITE : COL_TXT, a), AL_LEFT, BOT_W - 60);
	}
}

static void input_menu(u32 kd, u32 kh, u32 ku, touchPosition *tp)
{
	int items[8], n = menu_items(items);
	if (S.menu_sel >= n) S.menu_sel = n - 1;
	if (key_repeat(kd, kh, KEY_DOWN)) S.menu_sel = (S.menu_sel + 1) % n;
	if (key_repeat(kd, kh, KEY_UP)) S.menu_sel = (S.menu_sel + n - 1) % n;
	if (kd & KEY_B) S.menu_open = false;
	if (kd & KEY_A) menu_choose(items[S.menu_sel]);
	if (kd & KEY_TOUCH) {
		float y0 = menu_y(n);
		if (tp->py < y0) {
			S.menu_open = false;   // tocar fuera lo cierra
		} else {
			int i = (int)((tp->py - y0 - MENU_HEAD) / MENU_ROW);
			if (tp->py >= y0 + MENU_HEAD && i >= 0 && i < n) {
				S.menu_sel = i;
				menu_choose(items[i]);
			}
		}
	}
}

static void draw_login_top(void)
{
	const AuthInfo *ai = auth_info();
	if (ai->state == AUTH_IN) {
		// Dentro: saludo con el avatar
		float e = ease_out_back(clampf(S.login_done / 0.5f, 0, 1));
		const Pic *p = ai->avatar[0] ? img_get(ai->avatar, 64) : NULL;
		float s = 64 * e;
		ui_rrect(TOP_W / 2 - s / 2 - 3, 60 - 3 + (64 - s) / 2, s + 6, s + 6, 6, COL_ACC);
		if (p) img_draw_cover(p, TOP_W / 2 - s / 2, 60 + (64 - s) / 2, s, s, 0.5f);
		else ui_rect(TOP_W / 2 - s / 2, 60 + (64 - s) / 2, s, s, COL_PANEL);
		char t[96];
		snprintf(t, sizeof t, _("Hola, %s"), ai->persona[0] ? ai->persona : ai->account);
		ui_text(TOP_W / 2, 140, t, SZ_TITLE, col_alpha(COL_WHITE, e), AL_CENTER, 380);
		ui_text(TOP_W / 2, 166, _("Ya tienes tu sesión de Steam en la consola"), SZ_SMALL, col_alpha(COL_DIM, e), AL_CENTER, 380);
		return;
	}
	if (!ai->qr_url[0]) {
		if (ai->err[0]) {
			ui_text(TOP_W / 2, 100, _("No se pudo pedir el código a Steam"), SZ_LARGE, COL_TXT, AL_CENTER, 380);
			ui_text(TOP_W / 2, 124, human_err(ai->err), SZ_BODY, COL_DIM, AL_CENTER, 380);
			ui_text(TOP_W / 2, 144, _("Se reintenta solo"), SZ_SMALL, COL_ACC, AL_CENTER, 0);
		} else {
			draw_net_wait(TOP_W / 2, 120, _("Pidiendo el código a Steam…"));
		}
		return;
	}
	ui_qr(ai->qr_url, 14, 30, 176);
	float x = 204, w = TOP_W - 12 - x;
	ui_text(x, 30, _("Inicia sesión con Steam"), SZ_LARGE, COL_WHITE, AL_LEFT, w);
	static const char *steps[] = {
		"Abre la app de Steam en el móvil",
		"Toca el escudo de Steam Guard",
		"Elige «Escanear un código QR» y apunta aquí",
	};
	float y = 56;
	for (int i = 0; i < 3; i++) {
		char num[4];
		snprintf(num, sizeof num, "%d", i + 1);
		ui_circle(x + 7, y + 7, 7, COL_SEL);
		ui_text(x + 7, y + 1, num, SZ_TINY, COL_ACC, AL_CENTER, 0);
		int nl = ui_text_wrap(x + 20, y, w - 20, _(steps[i]), SZ_SMALL, COL_TXT, 3, 13);
		y += nl * 13 + 8;
	}
	// Estado: esperando, o ya escaneado y falta aprobar
	float sy = 186;
	if (ai->qr_scanned) {
		float bh = 14 + 13 * ui_text_lines(_("Escaneado: apruébalo en el móvil"), SZ_SMALL, w - 20);
		ui_rrect(x, sy - (bh - 26), w, bh, 5, C2D_Color32(0x2d, 0x5a, 0x1e, 0xff));
		ui_text_wrap(x + 10, sy - (bh - 26) + 7, w - 20, _("Escaneado: apruébalo en el móvil"), SZ_SMALL, COL_LIME, 2, 13);
	} else {
		ui_spinner(x + 10, sy + 12, 5, COL_DIM);
		ui_text(x + 24, sy + 5, _("Esperando al móvil…"), SZ_SMALL, COL_DIM, AL_LEFT, w - 30);
	}
}

static void draw_login_bottom(void)
{
	ui_rect(0, 0, BOT_W, SCR_H, COL_BG);
	ui_icon(IC_LOCK, BOT_W / 2 - 10, 34, 20, COL_ACC);
	ui_text(BOT_W / 2, 64, _("Tu contraseña nunca pasa por la consola"), SZ_BODY, COL_WHITE, AL_CENTER, 300);
	ui_text_wrap(24, 88, BOT_W - 48, _("Apruebas la entrada desde la app oficial de Steam. La sesión se guarda en la tarjeta SD; puedes cerrarla cuando quieras desde el menú (START) o desde Steam, en Dispositivos autorizados."),
		SZ_SMALL, COL_DIM, 6, 14);
	static const char *hh[] = {"B", "Volver"};
	draw_hints(hh, 1);
}

static void input_login(u32 kd, float dt)
{
	const AuthInfo *ai = auth_info();
	if (ai->state == AUTH_IN) {
		S.login_done += dt;
		if (S.login_done > 1.8f || (kd & (KEY_A | KEY_B))) go_scene(S.login_back, -1);
		return;
	}
	if (kd & KEY_B) {
		auth_cancel_qr();
		go_scene(S.login_back, -1);
	}
}

// ============================================================================
// Pantalla de carga: la rueda de válvula gira, sale vapor y una barra de carga

enum { SPR_WHEEL, SPR_STEAM, SPR_GLOW, SPR_FLIP };   // SPR_FLIP..+23: flipbook del vapor
#define FLIP_FRAMES 24



static void draw_sprite(int idx, float cx, float cy, float scale, float angle, float alpha)
{
	if (!sprites || alpha <= 0) return;
	C2D_Image img = C2D_SpriteSheetGetImage(sprites, idx);
	C2D_ImageTint tint;
	C2D_AlphaImageTint(&tint, clampf(alpha, 0, 1));
	C2D_DrawImageAtRotated(img, cx, cy, 0.5f, angle, &tint, scale, scale);
}

// Con tinte: color mezcla el del sprite (blend 0 = tal cual, 1 = todo color) y su alfa multiplica
static void draw_sprite_ex(int idx, float cx, float cy, float sx, float sy, float angle, u32 color, float blend)
{
	if (!sprites || (color >> 24) == 0) return;
	C2D_Image img = C2D_SpriteSheetGetImage(sprites, idx);
	C2D_ImageTint tint;
	C2D_PlainImageTint(&tint, color, blend);
	C2D_DrawImageAtRotated(img, cx, cy, 0.5f, angle, &tint, sx, sy);
}

// Número pseudoaleatorio estable por partícula (sin estado: la misma imagen cada frame)
static float hrand(u32 i, u32 salt)
{
	u32 h = i * 2654435761u ^ salt * 2246822519u;
	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	return (h & 0xffff) / 65535.0f;
}

// Una voluta del chorro de vapor en su edad age (s) y vida life
typedef struct { float x, y, scale, angle, alpha; int frame; float cool, stretch, shade; } Puff;

#define JET_LIFE 3.4f

static bool jet_puff(int j, float since, Puff *o)
{
	const float life = JET_LIFE;
	if (since < 0) return false;
	float age = fmodf(since, life);
	u32 gen = (u32)(since / life);
	u32 id = j * 131 + gen * 7919;
	float r1 = hrand(id, 1), r2 = hrand(id, 2), r3 = hrand(id, 3), r4 = hrand(id, 4);
	float p = age / life;
	// Sale con poca fuerza y frena enseguida; a partir de ahí lo sube la flotación, despacio
	const float k = 3.0f;
	float v0 = 95 + 35 * r1;
	float push = v0 / k * (1 - expf(-k * age));
	float rise = push + (22 + 10 * r3) * powf(age, 1.3f);
	// Se abre hacia los lados al subir, una brisa suave curva la columna y las volutas ruedan
	float spread = (r2 - 0.5f) * 2 * (10 + 40 * p);
	float wind = 20 * p * p;
	float roll = sinf(age * 1.1f + r4 * 6.28f) * 9 * p;
	o->x = spread + wind + roll;
	o->y = -rise;
	o->scale = 0.35f + 2.2f * (1 - expf(-age * 0.9f)) * (0.8f + 0.4f * r4);
	float speed = v0 * expf(-k * age);
	o->stretch = 1 + speed / 400;
	o->angle = r1 * 6.28f + age * (r2 - 0.5f) * 0.5f;
	o->frame = (int)(p * (FLIP_FRAMES - 1) + 0.5f);
	float in = clampf(age / 0.25f, 0, 1);
	o->alpha = in * powf(1 - p, 1.1f) * (0.6f + 0.4f * r3);
	o->cool = clampf(p * 1.2f, 0, 1);
	o->shade = 0.7f + 0.3f * r4;      // unas volutas más en sombra que otras: volumen
	return true;
}

static void draw_puff(const Puff *pf, float cx, float cy, float a, bool back)
{
	// El vapor real es translúcido y gris azulado, no blanco: solo se ve denso donde se acumula
	u32 hot = C2D_Color32(0xe2, 0xea, 0xf2, 0xff), cold = C2D_Color32(0x9a, 0xac, 0xbf, 0xff);
	u32 c = col_lerp(hot, cold, pf->cool);
	float shade = pf->shade > 0 ? pf->shade : 1;
	c = col_lerp(c, C2D_Color32(0x4e, 0x62, 0x77, 0xff), (1 - shade) * 0.7f);
	// Lo que está cerca de la válvula recoge su luz azul
	float near = clampf(1 - hypotf(pf->x, pf->y) / 90, 0, 1);
	c = col_lerp(c, C2D_Color32(0x8c, 0xcc, 0xf5, 0xff), near * 0.35f);
	if (back) c = col_lerp(c, C2D_Color32(0x5a, 0x70, 0x88, 0xff), 0.4f);
	float al = pf->alpha * a * (back ? 0.30f : 0.42f);
	float st = pf->stretch > 0 ? pf->stretch : 1;
	draw_sprite_ex(SPR_FLIP + pf->frame, cx + pf->x, cy + pf->y, pf->scale / sqrtf(st), pf->scale * st, pf->angle,
		col_alpha(c, al), 0.65f);
}

// Ambiente de sala de máquinas: neblina oscura que sube por los lados y por abajo,
// y motas de polvo y humedad que brillan al pasar cerca de la luz de la válvula
static void draw_ambient(float t, float w, float h, float lx, float ly, float a)
{
	for (int i = 0; i < 10; i++) {
		const float life = 7.0f;
		float since = t + i * (life / 10);
		float age = fmodf(since, life), p = age / life;
		u32 id = i + (u32)(since / life) * 23;
		bool side = i < 7;
		float x = side ? ((i & 1) ? w - 22 - hrand(id, 1) * 40 : 22 + hrand(id, 1) * 40) : w * (0.2f + 0.6f * hrand(id, 1));
		float y = h + 30 - p * (h * 0.75f);
		x += sinf(age * 0.6f + i) * 10 + ((i & 1) ? -1 : 1) * p * 25;
		float al = sinf(p * 3.14159f) * 0.24f * a;
		Puff pf = {x, y, 2.0f + p * 1.6f, hrand(id, 2) * 6.28f + age * 0.08f, al,
			8 + (int)(p * 12), 1, 1, 0.75f};
		draw_puff(&pf, 0, 0, 1, true);
	}
	for (int i = 0; i < 34; i++) {
		float sp = 6 + hrand(i, 3) * 10;
		float x = fmodf(hrand(i, 1) * w + sinf(t * 0.5f + i) * 14 + t * (hrand(i, 4) - 0.5f) * 8 + w, w);
		float y = fmodf(hrand(i, 2) * h - t * sp + h * 4, h);
		float d = hypotf(x - lx, (y - ly) * 1.2f);
		float lit = clampf(1 - d / 150, 0, 1);
		float tw = 0.6f + 0.4f * sinf(t * (1.5f + hrand(i, 5) * 2) + i);
		float al = (0.05f + 0.55f * lit * lit) * tw * a;
		float r = 0.6f + hrand(i, 6) * 1.0f + lit * 0.4f;
		ui_circle(x, y, r, col_alpha(col_lerp(C2D_Color32(0x8a, 0x9c, 0xb0, 0xff), C2D_Color32(0xd8, 0xf0, 0xff, 0xff), lit), al));
	}
}

static void draw_splash_bg(int idx, float w, float h)
{
	if (splash_bg) C2D_DrawImageAt(C2D_SpriteSheetGetImage(splash_bg, idx), 0, 0, 0.5f, NULL, 1, 1);
	else ui_vgrad(0, 0, w, h, C2D_Color32(0x24, 0x3b, 0x50, 0xff), C2D_Color32(0x17, 0x1a, 0x21, 0xff));
}

static bool splash_ready(void)
{
	return tabs[S.tab].list.count > 0 || tabs[S.tab].list.err[0] || net_status_text()[0];
}

static void draw_splash_top(void)
{
	float t = splash_t;
	draw_splash_bg(0, TOP_W, SCR_H);
	float cx = TOP_W / 2, cy = 100;
	draw_ambient(t, TOP_W, SCR_H, cx, cy, clampf(t / 0.8f, 0, 1));
	float pop = ease_out_back(clampf(t / 0.55f, 0, 1));
	const float t0 = 0.62f;      // la válvula se abre
	float flash = t > t0 ? expf(-(t - t0) * 3.0f) : 0;
	draw_sprite(SPR_GLOW, cx, cy, (1.7f + 0.08f * sinf(t * 2.4f) + 0.5f * flash) * pop, 0, (0.55f + 0.35f * flash) * pop);

	// Neblina muy tenue alrededor: ambiente
	if (t > t0) {
		float hz = clampf((t - t0) / 1.2f, 0, 1);
		for (int i = 0; i < 3; i++) {
			float a = hz * (0.10f + 0.04f * sinf(t * 0.7f + i * 2));
			draw_sprite_ex(SPR_STEAM, cx + (i - 1) * 70 + sinf(t * 0.3f + i) * 12, cy + 30 - i * 18, 2.6f, 1.6f, t * 0.05f * (i - 1),
				C2D_Color32(0xc0, 0xd4, 0xe8, (u8)(255 * a)), 0.6f);
		}
	}
	// Capa de atrás del chorro (por detrás de la válvula): da profundidad
	const int JET = 38;
	const float every = JET_LIFE / JET;
	float ramp = clampf((t - t0) / 0.35f, 0, 1);
	for (int j = 0; j < JET; j += 2) {
		Puff pf;
		if (jet_puff(j, t - t0 - 0.08f - j * every, &pf)) draw_puff(&pf, cx, cy - 4, ramp, true);
	}

	// Como una válvula que se abre: arranca despacio y coge velocidad
	float spin = t < 0.8f ? 0.9f * t * t : 0.576f + 1.44f * (t - 0.8f);
	draw_sprite(SPR_WHEEL, cx, cy, 0.62f * pop, spin, pop);

	if (t > t0) {
		// Bocanada al abrirse: volutas hacia los lados que frenan y se deshacen
		for (int i = 0; i < 12; i++) {
			float age = t - t0 - i * 0.02f;
			const float life = 2.4f;
			if (age < 0 || age > life) continue;
			float p = age / life, ang = i * 0.5236f + hrand(i, 9) * 0.4f;
			float dist = 18 + 70 * (1 - expf(-age * 2.0f));
			Puff pf = {
				cosf(ang) * dist, sinf(ang) * dist * 0.7f - 14 * age * age,
				0.35f + 1.5f * (1 - expf(-age * 1.4f)), hrand(i, 5) * 6.28f + age * 0.3f,
				clampf(age / 0.12f, 0, 1) * powf(1 - p, 1.4f),
				(int)(p * (FLIP_FRAMES - 1)), clampf(p * 1.4f, 0, 1), 1, 0.75f + 0.25f * hrand(i, 6),
			};
			draw_puff(&pf, cx, cy, 0.9f, false);
		}
		// Capa de delante del chorro
		for (int j = 1; j < JET; j += 2) {
			Puff pf;
			if (jet_puff(j, t - t0 - 0.08f - j * every, &pf)) draw_puff(&pf, cx, cy - 4, ramp, false);
		}
	}
	float ta = clampf((t - 0.25f) / 0.4f, 0, 1);
	u8 a8 = (u8)(255 * ta);
	float w1 = ui_text_w("Steam", SZ_HUGE), w2 = ui_text_w("3DS", SZ_HUGE);
	float x = cx - (w1 + w2) / 2;
	ui_text(x, 158, "Steam", SZ_HUGE, C2D_Color32(0xff, 0xff, 0xff, a8), AL_LEFT, 0);
	ui_text(x + w1 - 1, 158, "3DS", SZ_HUGE, C2D_Color32(0x66, 0xc0, 0xf4, a8), AL_LEFT, 0);
	// Barra de carga: un tramo que va y viene hasta que la tienda responde
	float bw = 150, bx = cx - bw / 2, by = 196;
	ui_rect(bx, by, bw, 3, C2D_Color32(0x2a, 0x47, 0x5e, a8));
	if (splash_out >= 0) {
		ui_rect(bx, by, bw, 3, C2D_Color32(0x66, 0xc0, 0xf4, a8));
	} else {
		float seg = 46, p = (sinf(t * 3.0f) + 1) / 2;
		ui_rect(bx + p * (bw - seg), by, seg, 3, C2D_Color32(0x66, 0xc0, 0xf4, a8));
	}
}

static void draw_splash_bottom(void)
{
	draw_splash_bg(1, BOT_W, SCR_H);
	draw_ambient(splash_t + 3.1f, BOT_W, SCR_H, BOT_W / 2, -60, clampf(splash_t / 0.8f, 0, 1));
	float ta = clampf((splash_t - 0.5f) / 0.5f, 0, 1);
	u8 a8 = (u8)(255 * ta);
	ui_text(BOT_W / 2, 104, _("hecho por VENOMSHiNE"), SZ_BODY, C2D_Color32(0xc7, 0xd5, 0xe0, a8), AL_CENTER, 0);
	ui_text(BOT_W / 2, 124, "v" APP_VERSION, SZ_SMALL, C2D_Color32(0x8f, 0x98, 0xa0, a8), AL_CENTER, 0);
}

static void splash_update(float dt, u32 kd)
{
	splash_t += dt;
	if (splash_out < 0) {
		bool skip = splash_t > 0.6f && (kd & (KEY_A | KEY_B | KEY_TOUCH));
		if ((splash_t > 3.2f && splash_ready()) || splash_t > 6.5f || skip) splash_out = 0;
	} else {
		splash_out += dt;
		if (splash_out > 0.35f) {
			go_scene(SC_STORE, 0);
			// El resto de la app no usa estos sprites: se liberan en cuanto la GPU acabe con ellos
			sprites_free_in = 2;
		}
	}
}

// ============================================================================
// Entrada

static bool key_repeat(u32 down, u32 held, u32 mask)
{
	static u32 hold_mask;
	static u64 since, last;
	u64 now = osGetTime();
	if (down & mask) {
		hold_mask = mask;
		since = last = now;
		return true;
	}
	if ((held & mask) && hold_mask == mask && now - since > 350 && now - last > 70) {
		last = now;
		return true;
	}
	return false;
}

static const Box B_SEARCHBAR = {8, LIST_Y + 4, BOT_W - 16, SEARCH_BAR - 8};
static int press_update(u32 kd, u32 kh, u32 ku, touchPosition *tp, const int *ids, int n);

static void tab_switch(int nt)
{
	if (nt == S.tab) return;
	S.tab = nt;
	Tab *t = CUR;
	if (nt == TAB_SEARCH) {
		if (!t->term[0] && !t->list.req) search_show_recents();
	} else if (!t->started) {
		tab_more(nt);
	}
	t->sel_changed = osGetTime();
}

// A sobre la fila elegida (o el toque en la que ya estaba elegida)
static void store_activate(void)
{
	Tab *t = CUR;
	const StoreRow *r = cur_row();
	if (r && r->appid) open_game(r->appid, false);
	else if (r && S.tab == TAB_SEARCH) search_start(r->name);       // búsqueda reciente
	else if (S.tab == TAB_SEARCH && !t->term[0]) S.want_kb = true;
	else if (S.tab == TAB_SEARCH && t->term[0] && !t->list.req) search_start(t->term);
	else if (!t->list.req) {
		// Reintentar ya, sin esperar la cuenta atrás
		t->fail_at = 0;
		t->list.end = false;
		tab_more(S.tab);
	}
}

static void input_store(u32 kd, u32 kh, u32 ku, touchPosition *tp)
{
	if (kd & KEY_L) tab_switch((S.tab + TAB_COUNT - 1) % TAB_COUNT);
	if (kd & KEY_R) tab_switch((S.tab + 1) % TAB_COUNT);
	Tab *t = CUR;
	if (key_repeat(kd, kh, KEY_DOWN)) store_select(t->sel + 1, 1);
	if (key_repeat(kd, kh, KEY_UP)) store_select(t->sel - 1, -1);
	if (key_repeat(kd, kh, KEY_RIGHT)) store_select(t->sel + 6, 1);
	if (key_repeat(kd, kh, KEY_LEFT)) store_select(t->sel - 6, -1);
	if ((kd & KEY_Y) && S.tab == TAB_SEARCH) S.want_kb = true;
	if ((kd & KEY_B) && S.tab == TAB_SEARCH && t->term[0]) {
		// B borra la búsqueda y vuelve a las recientes
		if (t->list.req) net_release(t->list.req);
		t->list.req = NULL;
		t->list.err[0] = 0;
		search_show_recents();
	}
	if (kd & KEY_A) store_activate();

	if (S.tab == TAB_SEARCH) {
		static const int ids[] = {BT_SEARCH};
		if (press_update(kd, kh, ku, tp, ids, 1) == BT_SEARCH) S.want_kb = true;
	}

	float top = list_top(), lh = list_h();
	if (kd & KEY_TOUCH) {
		S.touch_x0 = tp->px;
		S.touch_y0 = tp->py;
		S.touch_moved = 0;
		S.drag_scroll0 = t->scroll_target;
		S.dragging = tp->py >= top && tp->py < top + lh;
	} else if ((kh & KEY_TOUCH) && S.dragging) {
		int dy = tp->py - S.touch_y0;
		if (abs(dy) > S.touch_moved) S.touch_moved = abs(dy);
		if (S.touch_moved >= 8) {
			float max = t->list.count * ROW_H - lh;
			if (max < 0) max = 0;
			t->scroll_target = clampf(S.drag_scroll0 - dy, 0, max);
			t->scroll = t->scroll_target;
			t->finger = true;
			if (tab_paged(S.tab) && t->scroll_target > max - 3 * ROW_H) tab_more(S.tab);
		}
	}
	if (ku & KEY_TOUCH) {
		if (S.touch_moved < 8) {
			int x = S.touch_x0, y = S.touch_y0;
			if (y < LIST_Y) {
				int nt = (int)(x / (BOT_W / 4.6f));
				tab_switch(nt >= TAB_COUNT ? TAB_SEARCH : nt);
			} else if (S.dragging) {
				int i = (int)((y - top + t->scroll) / ROW_H);
				if (i >= 0 && i < t->list.count && !t->list.rows[i].header) {
					if (i == t->sel) store_activate();
					else store_select(i, 0);
				}
			}
		}
		S.dragging = false;
	}
}

// Teclado del sistema para Buscar (se abre fuera del frame de dibujo)
static void open_keyboard(void)
{
	SwkbdState kb;
	char buf[64] = "";
	swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, 60);
	swkbdSetHintText(&kb, _("Nombre del juego"));
	swkbdSetInitialText(&kb, CUR->term);
	swkbdSetButton(&kb, SWKBD_BUTTON_LEFT, _("Cancelar"), false);
	swkbdSetButton(&kb, SWKBD_BUTTON_RIGHT, _("Buscar"), true);
	swkbdSetFeatures(&kb, SWKBD_DARKEN_TOP_SCREEN | SWKBD_ALLOW_HOME | SWKBD_ALLOW_RESET | SWKBD_ALLOW_POWER);
	swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	if (swkbdInputText(&kb, buf, sizeof buf) != SWKBD_BUTTON_RIGHT) return;
	char *s = buf;
	while (*s == ' ') s++;
	size_t n = strlen(s);
	while (n > 0 && s[n - 1] == ' ') s[--n] = 0;
	if (*s) search_start(s);
}

static RowList *game_list(void)
{
	return S.sec == SEC_EXTRAS ? &S.dlc : S.sec == SEC_SIMILAR ? &S.similar : NULL;
}

static void game_set_sec(int sec)
{
	sec = (sec + SEC_COUNT) % SEC_COUNT;
	if (sec == S.sec) return;
	S.sec_dir = sec > S.sec ? 1 : -1;
	S.sec_anim = 0;
	S.sec = sec;
	S.lsel = 0;
	S.lscroll = 0;
	S.lsel_y = 0;
	S.show_qr = false;
}

static const Box *button_box(int id)
{
	switch (id) {
	case BT_CART: return &B_CART;
	case BT_WISH: return &B_WISH;
	case BT_SHOTS: return &B_SHOTS;
	case BT_INST: return &B_INST;
	case BT_PREV: return &B_PREV;
	case BT_NEXT: return &B_NEXT;
	case BT_EXIT_NO: return &B_EXIT_NO;
	case BT_EXIT_YES: return &B_EXIT_YES;
	case BT_SEARCH: return &B_SEARCHBAR;
	}
	return NULL;
}

// Botones táctiles: se hunden al tocar y actúan al soltar dentro (como en Gemi3DS).
// Devuelve el botón soltado dentro, o BT_NONE.
static int touch_x, touch_y;
static int press_update(u32 kd, u32 kh, u32 ku, touchPosition *tp, const int *ids, int n)
{
	if (kh & KEY_TOUCH) {
		touch_x = tp->px;
		touch_y = tp->py;
	}
	if (kd & KEY_TOUCH) {
		S.pressed = BT_NONE;
		for (int i = 0; i < n; i++)
			if (hit(button_box(ids[i]), tp->px, tp->py)) S.pressed = ids[i];
	}
	if ((kh & KEY_TOUCH) && S.pressed && !hit(button_box(S.pressed), touch_x, touch_y)) S.pressed = BT_NONE;
	if (ku & KEY_TOUCH) {
		int r = S.pressed;
		S.pressed = BT_NONE;
		return r;
	}
	return BT_NONE;
}

static void input_game(u32 kd, u32 kh, u32 ku, touchPosition *tp)
{
	InfoEnt *e = info_get(S.app, false);
	const AppInfo *a = (e && e->done) ? &e->info : NULL;
	if (kd & KEY_B) { game_back(); return; }
	if ((kd & KEY_A) && e && e->failed) { info_retry(e); return; }
	if (kd & (KEY_L | KEY_LEFT)) game_set_sec(S.sec - 1);
	if (kd & (KEY_R | KEY_RIGHT)) game_set_sec(S.sec + 1);
	if ((kd & KEY_X) && a) S.show_qr = !S.show_qr;

	RowList *l = game_list();
	if (l) {
		if (key_repeat(kd, kh, KEY_DOWN) && S.lsel + 1 < l->count) S.lsel++;
		if (key_repeat(kd, kh, KEY_UP) && S.lsel > 0) S.lsel--;
		if ((kd & KEY_A) && S.lsel < l->count) { open_game(l->rows[S.lsel].appid, true); return; }
	}
	if (S.sec == SEC_REVIEWS) {
		if (key_repeat(kd, kh, KEY_DOWN) && S.rsel + 1 < S.nreviews) S.rsel++;
		if (key_repeat(kd, kh, KEY_UP) && S.rsel > 0) S.rsel--;
		if (S.rsel >= S.nreviews - 3) reviews_more();
	}

	if ((kd & KEY_TOUCH) && hit(&B_SEG, tp->px, tp->py))
		game_set_sec((int)((tp->px - B_SEG.x) / (B_SEG.w / SEC_COUNT)));
	static const int ids[] = {BT_CART, BT_WISH, BT_SHOTS, BT_INST};
	int b = a ? press_update(kd, kh, ku, tp, ids, 4) : BT_NONE;
	if (b == BT_SHOTS && a->nshots) {
		go_scene(SC_SHOTS, 1);
		S.shot = 0;
		S.strip = 0;
		S.shot_anim = 1;
	} else if (b == BT_CART || b == BT_WISH || b == BT_INST) {
		toast(_("Disponible al iniciar sesión"));
	}
}

static void set_shot(int n, int count)
{
	if (n < 0 || n >= count || n == S.shot) return;
	S.shot_dir = n > S.shot ? 1 : -1;
	S.shot = n;
	S.shot_anim = 0;
}

static void input_shots(u32 kd, u32 kh, u32 ku, touchPosition *tp)
{
	InfoEnt *e = info_get(S.app, false);
	int n = (e && e->done) ? e->info.nshots : 0;
	if (kd & (KEY_LEFT | KEY_L)) set_shot(S.shot - 1, n);
	if (kd & (KEY_RIGHT | KEY_R)) set_shot(S.shot + 1, n);
	if (kd & KEY_B) {
		go_scene(SC_GAME, -1);
		return;
	}
	static const int ids[] = {BT_PREV, BT_NEXT};
	int b = press_update(kd, kh, ku, tp, ids, 2);
	if (b == BT_PREV) set_shot(S.shot - 1, n);
	if (b == BT_NEXT) set_shot(S.shot + 1, n);
	// La tira: deslizar el dedo pasa de captura; tocar una miniatura la abre
	static int sx0;
	static bool on_strip;
	if (kd & KEY_TOUCH) {
		sx0 = tp->px;
		on_strip = tp->py >= STRIP_Y - 6 && tp->py < STRIP_Y + THUMB_H + 12;
	}
	if ((ku & KEY_TOUCH) && on_strip) {
		int dx = touch_x - sx0;
		if (abs(dx) > 30) set_shot(S.shot + (dx < 0 ? 1 : -1), n);
		else set_shot(S.shot + (int)floorf((sx0 - BOT_W / 2 + THUMB_STEP / 2.0f) / THUMB_STEP), n);
		on_strip = false;
	}
}

// ============================================================================

static void start_store(void)
{
	Lang l = !strcmp(g_cfg.lang, "es") ? LANG_ES : !strcmp(g_cfg.lang, "en") ? LANG_EN : !strcmp(g_cfg.lang, "ja") ? LANG_JA : i18n_system();
	i18n_set(l);
	const char *cc = g_cfg.country[0] ? g_cfg.country : "US";
	steam_set_context(i18n_steam_lang(cc), cc);
	S.ctx_ready = true;
	netlog("tienda: pais=%s idioma=%s", steam_country(), steam_lang());
	tab_more(S.tab);
}

static void poll_country(void)
{
	if (!S.country_req || S.country_req->state < REQ_DONE) return;
	Req *r = S.country_req;
	if (r->country[0] && strcmp(r->country, g_cfg.country)) {
		snprintf(g_cfg.country, sizeof g_cfg.country, "%s", r->country);
		config_save();
	}
	netlog("pais: %s (%s)", r->country[0] ? r->country : "?", r->state == REQ_DONE ? "ok" : r->err);
	net_release(r);
	S.country_req = NULL;
	start_store();
}

int main(int argc, char **argv)
{
	gfxInitDefault();
	romfsInit();
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS * 4);
	C2D_Prepare();
	C3D_RenderTarget *top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	C3D_RenderTarget *bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);

	config_load();
	{
		Lang l = !strcmp(g_cfg.lang, "es") ? LANG_ES : !strcmp(g_cfg.lang, "en") ? LANG_EN : !strcmp(g_cfg.lang, "ja") ? LANG_JA : i18n_system();
		i18n_set(l);
	}
	util_init();
	ui_init();
	review_buf = C2D_TextBufNew(MAX_REVIEWS * 3 * 90);
	img_init();
	net_init();
	auth_init();
	S.last_auth = auth_info()->state;

	S.scene = SC_SPLASH;
	S.tab = TAB_HOME;
	recents_load();
	S.tab_x = TAB_HOME;
	S.quit_fade = -1;
	S.scene_anim = S.sec_anim = S.shot_anim = 1;
	sprites = C2D_SpriteSheetLoad("romfs:/gfx/sprites.t3x");
	splash_bg = C2D_SpriteSheetLoad("romfs:/gfx/splash_bg.t3x");
	if (sprites) {
		for (int i = 0; i < 3; i++)
			C3D_TexSetFilter(C2D_SpriteSheetGetImage(sprites, i).tex, GPU_LINEAR, GPU_LINEAR);
	}
	// Primero el país (para los precios en tu moneda); si no responde, el último guardado
	S.country_req = steam_req_country();

	u64 last = osGetTime();
	while (aptMainLoop() && !S.quit) {
#ifdef DEV_RELAY
		u64 tick0 = svcGetSystemTick();
#endif
		u64 now = osGetTime();
		float dt = (now - last) / 1000.0f;
		last = now;
		if (dt > 0.1f) dt = 0.1f;   // tras HOME o una pausa larga, sin saltos raros
		g_time += dt;
		g_dt = dt;
		frame_no++;
		if (S.toast_t > 0) S.toast_t -= dt;

		hidScanInput();
		u32 kd = hidKeysDown(), kh = hidKeysHeld(), ku = hidKeysUp();
		touchPosition tp;
		hidTouchRead(&tp);

		poll_country();
		for (int ti = 0; ti < TAB_COUNT; ti++) {
			Tab *t = &tabs[ti];
			if (ti == TAB_HOME) home_poll();
			else list_poll(&t->list, tab_paged(ti));
			if (ti == TAB_BETAS) {
				// La etiqueta «Acceso anticipado» la ponen los usuarios y se queda en juegos que ya
				// salieron (PUBG): solo se dejan los que Steam marca como acceso anticipado de verdad
				int n = 0;
				for (int k = 0; k < t->list.count; k++)
					if (t->list.rows[k].early) t->list.rows[n++] = t->list.rows[k];
				t->list.count = n;
				if (t->sel >= n) t->sel = n ? n - 1 : 0;
			}
			if (t->list.err[0] && !t->list.req) {
				if (!t->fail_at) {
					t->fail_at = now;
					t->list.end = false;
				} else if (!t->list.count && t->started) {
					tab_more(ti);   // espera sola lo que toque
				} else if (ti == TAB_SEARCH && t->term[0] && tab_can_retry(t)) {
					char term[64];
					snprintf(term, sizeof term, "%s", t->term);
					search_start(term);
				}
			} else {
				t->fail_at = 0;
			}
		}
		info_poll();
		for (int i = 0; i < CACHE; i++) {
			InfoEnt *e = &cache[i];
			const StoreRow *cr = S.scene == SC_STORE ? cur_row() : NULL;
			bool shown = e->appid == S.app || (cr && e->appid == cr->appid);
			if (e->failed && shown && now - e->fail_at > (u64)retry_delay(e->err) * 1000) info_retry(e);
		}
		img_update();

		if (S.quit_fade >= 0) {
			S.quit_fade += dt;
			if (S.quit_fade > 0.3f) S.quit = true;
			kd = kh = ku = 0;
		} else if (S.ask_exit) {
			static const int ids[] = {BT_EXIT_NO, BT_EXIT_YES};
			int b = press_update(kd, kh, ku, &tp, ids, 2);
			if ((kd & KEY_A) || b == BT_EXIT_YES) S.quit_fade = 0;
			if ((kd & (KEY_B | KEY_START)) || b == BT_EXIT_NO) S.ask_exit = false;
			kd = kh = ku = 0;
		} else if (S.scene != SC_SPLASH) {
			if (kd & KEY_START) {
				S.start_hold = 0;
				S.start_armed = true;
			}
			if ((kh & KEY_START) && S.start_armed) {
				S.start_hold += dt;
				if (S.start_hold >= 2.0f) {
					S.start_armed = false;
					S.menu_open = false;
					S.ask_exit = true;
					S.exit_anim = 0;
					S.pressed = BT_NONE;
				}
			}
			if ((ku & KEY_START) && S.start_armed) {
				// Pulsación corta (al soltar): abre o cierra el menú
				S.start_armed = false;
				S.menu_open = !S.menu_open;
				S.menu_sel = 0;
			}
			if (!(kh & KEY_START)) S.start_hold = 0;
			kd &= ~KEY_START;
			ku &= ~KEY_START;
			if (S.menu_open) {
				input_menu(kd, kh, ku, &tp);
				kd = kh = ku = 0;
			}
		}

		if (S.want_kb) {
			S.want_kb = false;
			open_keyboard();
			kd = kh = ku = 0;
		}
		switch (S.scene) {
		case SC_STORE: input_store(kd, kh, ku, &tp); break;
		case SC_GAME: input_game(kd, kh, ku, &tp); break;
		case SC_SHOTS: input_shots(kd, kh, ku, &tp); break;
		case SC_SPLASH: splash_update(dt, kd); break;
		case SC_LOGIN: input_login(kd, dt); break;
		}
		auth_update();
		// Avisos de la sesión: entró desde la sesión guardada, o Steam ya no la acepta
		if (auth_info()->state != S.last_auth) {
			if (S.last_auth == AUTH_STARTING && auth_info()->state == AUTH_OUT && !strcmp(auth_info()->err, "expired"))
				toast(_("Tu sesión de Steam caducó: vuelve a iniciarla"));
			S.last_auth = auth_info()->state;
		}

		// Vista previa: la ficha del seleccionado, cuando el cursor se queda quieto un momento
		if (S.scene == SC_STORE && cur_row() && cur_row()->appid && now - CUR->sel_changed > 250)
			info_get(cur_row()->appid, true);

		InfoEnt *ge = S.scene != SC_STORE ? info_get(S.app, false) : NULL;
		game_poll(ge && ge->done ? &ge->info : NULL);

		// Desplazamientos suaves
		if (!CUR->finger) CUR->scroll = approach(CUR->scroll, CUR->scroll_target, 18);
		CUR->sel_y = approach(CUR->sel_y, CUR->sel, 24);
		S.tab_x = approach(S.tab_x, S.tab, 16);
		S.lsel_y = approach(S.lsel_y, S.lsel, 24);
		S.seg_x = approach(S.seg_x, S.sec, 16);
		S.strip = approach(S.strip, S.shot, 12);
		S.exit_anim = approach(S.exit_anim, S.ask_exit ? 1 : 0, S.ask_exit ? 7 : 16);
		S.menu_anim = approach(S.menu_anim, S.menu_open ? 1 : 0, S.menu_open ? 12 : 18);
		S.sec_anim = clampf(S.sec_anim + dt / 0.22f, 0, 1);
		S.scene_anim = clampf(S.scene_anim + dt / 0.28f, 0, 1);
		S.shot_anim = clampf(S.shot_anim + dt / 0.30f, 0, 1);
		{
			RowList *l = game_list();
			float rh = 22, vis = SCR_H - 4 - 42;
			if (l) {
				float y = S.lsel * rh, t = S.lscroll;
				if (y < t) t = y;
				if (y + rh > t + vis) t = y + rh - vis;
				S.lscroll = approach(S.lscroll, t, 18);
			}
			float card = 20 + 3 * 12 + 6 + 5, rvis = SCR_H - 20 - 52;
			float ry = S.rsel * card, rt = S.rscroll;
			if (ry < rt) rt = ry;
			if (ry + card > rt + rvis) rt = ry + card - rvis;
			S.rscroll = approach(S.rscroll, rt, 18);
		}

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		ui_begin_frame();
		C2D_TargetClear(top, COL_BG);
		C2D_SceneBegin(top);
		// Al cambiar de escena el contenido entra deslizándose y aparece desde el fondo
		float ce = ease_out_cubic(S.scene_anim);
		C2D_ViewReset();
		C2D_ViewTranslate(S.scene_dir * (1 - ce) * 26, 0);
		switch (S.scene) {
		case SC_STORE: draw_store_top(); break;
		case SC_GAME: draw_game_top(); break;
		case SC_SHOTS: draw_shots_top(); break;
		case SC_SPLASH: draw_splash_top(); break;
		case SC_LOGIN: draw_login_top(); break;
		}
		C2D_ViewReset();
		u32 fade_col = S.scene == SC_SHOTS ? C2D_Color32(0, 0, 0, 0xff) : COL_BG;
		if (ce < 1) ui_rect(0, 0, TOP_W, SCR_H, col_alpha(fade_col, (1 - ce) * 0.9f));
		if (S.scene == SC_STORE || S.scene == SC_GAME || S.scene == SC_LOGIN)
			draw_status(auth_logged() && auth_info()->persona[0] ? auth_info()->persona : NULL);
		if (S.scene == SC_SPLASH && splash_out > 0) ui_rect(0, 0, TOP_W, SCR_H, C2D_Color32(0, 0, 0, (u8)(255 * clampf(splash_out / 0.35f, 0, 1))));
		if (S.exit_anim > 0.01f) ui_rect(0, 0, TOP_W, SCR_H, C2D_Color32(0, 0, 0, (u8)(0x80 * ease_out_cubic(S.exit_anim))));
		if (S.quit_fade >= 0) ui_rect(0, 0, TOP_W, SCR_H, C2D_Color32(0, 0, 0, (u8)(255 * clampf(S.quit_fade / 0.3f, 0, 1))));

		C2D_TargetClear(bot, COL_BG);
		C2D_SceneBegin(bot);
		C2D_ViewTranslate(S.scene_dir * (1 - ce) * 26, 0);
		switch (S.scene) {
		case SC_STORE: draw_store_bottom(); break;
		case SC_GAME: draw_game_bottom(); break;
		case SC_SHOTS: draw_shots_bottom(); break;
		case SC_SPLASH: draw_splash_bottom(); break;
		case SC_LOGIN: draw_login_bottom(); break;
		}
		C2D_ViewReset();
		if (ce < 1) ui_rect(0, 0, BOT_W, SCR_H, col_alpha(COL_BG, (1 - ce) * 0.9f));
		if (S.scene == SC_SPLASH && splash_out > 0) ui_rect(0, 0, BOT_W, SCR_H, C2D_Color32(0, 0, 0, (u8)(255 * clampf(splash_out / 0.35f, 0, 1))));
		draw_menu();
		// START mantenido: barra que se llena; a los 2 s pregunta si sales
		if (S.start_hold > 0.25f && S.start_armed) {
			float f = clampf((S.start_hold - 0.25f) / 1.75f, 0, 1);
			ui_rect(0, SCR_H - 3, BOT_W, 3, C2D_Color32(0, 0, 0, 0x80));
			ui_rect(0, SCR_H - 3, BOT_W * f, 3, C2D_Color32(0xe0, 0x4b, 0x3a, 0xff));
		}
		draw_overlays_bottom();
		if (S.quit_fade >= 0) ui_rect(0, 0, BOT_W, SCR_H, C2D_Color32(0, 0, 0, (u8)(255 * clampf(S.quit_fade / 0.3f, 0, 1))));
#ifdef DEV_RELAY
		// Solo en pruebas: tiempo de CPU por frame (ticks de la consola), medio cada 2 s en net.log
		{
			static double acc, worst;
			static int n;
			static u64 last_log;
			double ms = (svcGetSystemTick() - tick0) / (double)SYSCLOCK_ARM11 * 1000.0;
			acc += ms;
			if (ms > worst) worst = ms;
			n++;
			if (now - last_log > 2000) {
				netlog("perf: escena=%d apartado=%d media=%.1fms peor=%.1fms", S.scene, S.sec, acc / n, worst);
				acc = worst = 0;
				n = 0;
				last_log = now;
			}
		}
#endif
		C3D_FrameEnd(0);
		if (sprites_free_in >= 0 && sprites_free_in-- == 0) {
			if (sprites) C2D_SpriteSheetFree(sprites);
			if (splash_bg) C2D_SpriteSheetFree(splash_bg);
			sprites = splash_bg = NULL;
		}
	}

	netlog("salir");
	if (S.country_req) net_release(S.country_req);
	for (int i = 0; i < TAB_COUNT; i++) {
		rowlist_free(&tabs[i].list);
		for (int k = 0; k < HOME_PARTS; k++) rowlist_free(&tabs[i].part[k]);
	}
	rowlist_free(&S.dlc);
	rowlist_free(&S.similar);
	if (S.rreq) net_release(S.rreq);
	for (int i = 0; i < S.nreviews; i++) free(S.reviews[i].text);
	for (int i = 0; i < CACHE; i++) {
		if (cache[i].req) net_release(cache[i].req);
		if (cache[i].sum) net_release(cache[i].sum);
		appinfo_free(&cache[i].info);
	}
	if (sprites) C2D_SpriteSheetFree(sprites);
	if (splash_bg) C2D_SpriteSheetFree(splash_bg);
	C2D_TextBufDelete(review_buf);
	img_exit();
	auth_exit();
	net_exit();
	ui_exit();
	util_exit();
	C2D_Fini();
	C3D_Fini();
	romfsExit();
	gfxExit();
	return 0;
}
