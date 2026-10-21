#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "i18n.h"
#include "img.h"
#include "net.h"
#include "steam.h"
#include "ui.h"
#include "util.h"

// ============================================================================
// Estado

typedef enum { SC_SPLASH, SC_STORE, SC_GAME, SC_SHOTS } Scene;
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
	RowList offers;
	u64 offers_fail_at;
	int sel;
	float scroll, scroll_target;
	bool dragging;
	int touch_y0, touch_x0, touch_moved;
	float drag_scroll0;
	bool finger_scrolled;
	u64 sel_changed;

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
	int shot;
} S;

static InfoEnt cache[CACHE];
static u64 frame_no;
static C2D_SpriteSheet sprites;
static C2D_TextBuf review_buf;
static float splash_t, splash_out = -1;

static void toast(const char *msg)
{
	snprintf(S.toast, sizeof S.toast, "%s", msg);
	S.toast_t = 2.5f;
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

static float approach(float cur, float target, float speed)
{
	float d = target - cur;
	if (fabsf(d) < 0.3f) return target;
	return cur + d * speed;
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

static void offers_more(void)
{
	if (S.offers.req || S.offers.end || !S.ctx_ready) return;
	if (S.offers.err[0]) {
		if (osGetTime() - S.offers_fail_at < (u64)retry_delay(S.offers.err) * 1000) return;
		S.offers.err[0] = 0;
	}
	S.offers.req = steam_req_offers(S.offers.count, PAGE);
}

// ============================================================================
// Dibujo común

static void draw_status(const char *user)
{
	ui_rect(0, 0, TOP_W, 18, COL_BAR);
	ui_text(8, 2, APP_NAME, SZ_SMALL, COL_ACC, AL_LEFT, 0);

	char clock[48], right[96];
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

static void draw_hints(const char *const *pairs, int n)
{
	float y = SCR_H - 18;
	ui_rect(0, y, BOT_W, 18, COL_BAR);
	float total = 0, widths[8];
	for (int i = 0; i < n; i++) {
		float kw = ui_text_w(pairs[i * 2], SZ_TINY) + 6;
		if (kw < 11) kw = 11;
		widths[i] = kw + 3 + ui_text_w(_(pairs[i * 2 + 1]), SZ_TINY);
		total += widths[i] + (i ? 12 : 0);
	}
	float x = (BOT_W - total) / 2;
	for (int i = 0; i < n; i++) {
		float kw = ui_text_w(pairs[i * 2], SZ_TINY) + 6;
		if (kw < 11) kw = 11;
		ui_rrect(x, y + 3.5f, kw, 11, 5.5f, C2D_Color32(0x3d, 0x44, 0x50, 0xff));
		ui_text(x + kw / 2, y + 3.5f, pairs[i * 2], SZ_TINY, COL_WHITE, AL_CENTER, 0);
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

static void draw_store_top(void)
{
	draw_status(NULL);
	if (S.tab != TAB_OFFERS) {
		ui_text(TOP_W / 2, 100, tab_name(S.tab)[0] ? tab_name(S.tab) : _("Buscar"), SZ_TITLE, COL_WHITE, AL_CENTER, 0);
		ui_text(TOP_W / 2, 126, _("Esta pestaña llega en la próxima fase"), SZ_BODY, COL_DIM, AL_CENTER, 0);
		return;
	}
	if (S.offers.count == 0) {
		if (S.offers.err[0] && !S.offers.req) {
			int left = retry_delay(S.offers.err) - (int)((osGetTime() - S.offers_fail_at) / 1000);
			char t[64];
			snprintf(t, sizeof t, _("Reintentando en %d s · A para ya"), left < 0 ? 0 : left);
			ui_text(TOP_W / 2, 92, _("No se pudieron cargar las ofertas"), SZ_LARGE, COL_TXT, AL_CENTER, 0);
			ui_text(TOP_W / 2, 116, human_err(S.offers.err), SZ_BODY, COL_TXT, AL_CENTER, 380);
			ui_text(TOP_W / 2, 136, t, SZ_SMALL, COL_ACC, AL_CENTER, 0);
			ui_text(TOP_W / 2, 200, S.offers.err, SZ_TINY, COL_DIM, AL_CENTER, 380);
		} else {
			draw_net_wait(TOP_W / 2, 120, _("Cargando ofertas…"));
		}
		return;
	}
	const StoreRow *row = &S.offers.rows[S.sel];
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
	if (row->review[0]) {
		char t[96];
		float rx = x + ui_text(x, y, _("Reseñas: "), SZ_SMALL, COL_DIM, AL_LEFT, 0);
		snprintf(t, sizeof t, "%s", row->review);
		rx += ui_text(rx, y, t, SZ_SMALL, review_col(row->review_pct), AL_LEFT, x + w - rx);
		y += 13;
		if (row->review_pct > 0) {
			if (a && a->metacritic) snprintf(t, sizeof t, "%d %% · Metacritic %d", row->review_pct, a->metacritic);
			else snprintf(t, sizeof t, "%d %%", row->review_pct);
			ui_text(x, y, t, SZ_SMALL, COL_DIM, AL_LEFT, w);
			y += 13;
		}
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

	char cnt[64];
	snprintf(cnt, sizeof cnt, _("Oferta %d de %d"), S.sel + 1, S.offers.total);
	ui_text(TOP_W - 12, SCR_H - 18, cnt, SZ_SMALL, COL_DIM, AL_RIGHT, 0);
}

static void draw_tabs(void)
{
	ui_rect(0, 0, BOT_W, LIST_Y, COL_BAR);
	float w = BOT_W / 4.6f;
	for (int t = 0; t < TAB_COUNT; t++) {
		float x = t * w, tw = t == TAB_SEARCH ? BOT_W - x : w;
		bool on = t == S.tab;
		if (on) {
			ui_rect(x, 0, tw, LIST_Y, COL_PANEL);
			ui_rect(x, LIST_Y - 2, tw, 2, COL_ACC);
		}
		if (t == TAB_SEARCH) ui_icon(IC_SEARCH, x + tw / 2 - 6, 6, 12, on ? COL_WHITE : COL_DIM);
		else ui_text(x + tw / 2, 5, tab_name(t), SZ_SMALL, on ? COL_WHITE : COL_DIM, AL_CENTER, tw - 4);
	}
}

static void draw_store_bottom(void)
{
	ui_rect(0, 0, BOT_W, SCR_H, COL_BG);
	draw_tabs();
	if (S.tab == TAB_OFFERS) {
		ui_scissor(GFX_BOTTOM, 0, LIST_Y, BOT_W, LIST_H);
		int first = (int)(S.scroll / ROW_H);
		for (int i = first; i < S.offers.count && i < first + LIST_H / ROW_H + 2; i++) {
			const StoreRow *r = &S.offers.rows[i];
			float y = LIST_Y + i * ROW_H - S.scroll;
			bool on = i == S.sel;
			if (on) {
				ui_rect(0, y, BOT_W, ROW_H, COL_SEL);
				ui_rect(0, y, 3, ROW_H, COL_ACC);
			}
			ui_hline(0, y + ROW_H - 1, BOT_W, COL_LINE);
			float pw = r->disc > 0 ? 36 + 50 : 50;
			ui_text(8, y + 7, r->name, SZ_BODY - 0.02f, on ? COL_WHITE : COL_TXT, AL_LEFT, BOT_W - 18 - pw);
			draw_price_right(BOT_W - 6, y + 2, ROW_H - 5, r, 36, 50);
		}
		float end_y = LIST_Y + S.offers.count * ROW_H - S.scroll;
		if (S.offers.req && end_y < SCR_H) ui_spinner(BOT_W / 2, end_y + 14, 6, COL_DIM);
		ui_scissor_off();
		// Barra de scroll
		float total = S.offers.count * ROW_H;
		if (total > LIST_H) {
			float h = LIST_H * LIST_H / total;
			if (h < 12) h = 12;
			float y = LIST_Y + (LIST_H - h) * (S.scroll / (total - LIST_H));
			ui_rect(BOT_W - 2, y, 2, h, C2D_Color32(0x3d, 0x5a, 0x73, 0xff));
		}
	} else {
		ui_text(BOT_W / 2, 110, _("Próximamente"), SZ_LARGE, COL_DIM, AL_CENTER, 0);
	}
	static const char *h[] = {"A", "Ficha", "L R", "Pestaña", "START", "Salir"};
	draw_hints(h, 3);
}

static void store_select(int sel)
{
	if (S.offers.count == 0) return;
	if (sel < 0) sel = 0;
	if (sel >= S.offers.count) sel = S.offers.count - 1;
	if (sel != S.sel) {
		S.sel = sel;
		S.sel_changed = osGetTime();
	}
	S.finger_scrolled = false;
	float y = S.sel * ROW_H;
	if (y < S.scroll_target) S.scroll_target = y;
	if (y + ROW_H > S.scroll_target + LIST_H) S.scroll_target = y + ROW_H - LIST_H;
	if (S.sel >= S.offers.count - 8) offers_more();
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
	S.scene = SC_GAME;
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
		S.lsel = lsel;
		return;
	}
	S.scene = SC_STORE;
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
	for (int i = 0; i < l->count; i++) {
		float y = top + i * rh - S.lscroll;
		if (y + rh < top || y > bottom) continue;
		const StoreRow *r = &l->rows[i];
		bool on = i == S.lsel;
		if (on) {
			ui_rect(12, y, TOP_W - 24, rh - 1, COL_SEL);
			ui_rect(12, y, 3, rh - 1, COL_ACC);
		}
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
	draw_status(NULL);
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
	switch (S.sec) {
	case SEC_SUMMARY: draw_summary_top(a); break;
	case SEC_REQS: draw_reqs_top(a); break;
	case SEC_REVIEWS: draw_reviews_top(a); break;
	case SEC_EXTRAS: {
		if (a->fullgame) {
			draw_list_top(_("CONTENIDO NECESARIO"), &S.dlc, "");
			if (S.dlc.count)
				ui_text_wrap(12, 72, TOP_W - 24, _("Para usar este contenido descargable necesitas tener el juego base en tu biblioteca."),
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

static void draw_button(const Box *b, Icon ic, const char *label, u32 top, u32 bot, u32 fg, float alpha)
{
	u32 a8 = (u32)(alpha * 255) << 24;
	ui_vgrad(b->x, b->y, b->w, b->h, (top & 0x00FFFFFF) | a8, (bot & 0x00FFFFFF) | a8);
	float tw = ui_text_w(label, SZ_SMALL) + 18;
	float x = b->x + (b->w - tw) / 2;
	u32 f = (fg & 0x00FFFFFF) | ((u32)(alpha < 1 ? 0x80 : 0xff) << 24);
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
	for (int i = 0; i < SEC_COUNT; i++) {
		float x = B_SEG.x + 2 + i * sw;
		if (i == S.sec) ui_rrect(x, B_SEG.y + 2, sw, B_SEG.h - 4, 2, COL_SEL);
		ui_text(x + sw / 2, B_SEG.y + 5, _(secs[i]), SZ_TINY, i == S.sec ? COL_WHITE : COL_DIM, AL_CENTER, sw - 4);
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
			C2D_Color32(0x75, 0xb0, 0x22, 0xff), C2D_Color32(0x58, 0x8a, 0x1b, 0xff), C2D_Color32(0xd2, 0xef, 0xa9, 0xff), 0.45f);
		draw_button(&B_WISH, IC_HEART, _("Deseados"), COL_BTN2, COL_BTN2, COL_ACC, 0.45f);
		char shots[40];
		snprintf(shots, sizeof shots, _("Capturas (%d)"), a->nshots);
		draw_button(&B_SHOTS, IC_IMAGE, shots, COL_BTN2, COL_BTN2, COL_ACC, a->nshots ? 1 : 0.45f);
		draw_button(&B_INST, IC_PC, _("Instalar en mi PC"), C2D_Color32(0x47, 0xbf, 0xff, 0xff), C2D_Color32(0x1a, 0x44, 0xc2, 0xff), COL_WHITE, 0.4f);
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
	if (p) img_draw_fit(p, 0, 0, TOP_W, SCR_H);
	else if (img_failed(url)) ui_text(TOP_W / 2, 112, _("No se pudo cargar la captura"), SZ_BODY, COL_DIM, AL_CENTER, 0);
	else ui_spinner(TOP_W / 2, SCR_H / 2, 9, COL_DIM);
	// La siguiente, ya pedida para que el cambio sea inmediato
	if (S.shot + 1 < e->info.nshots) img_get(e->info.shots[S.shot + 1], 600);
}

static void draw_shots_bottom(void)
{
	ui_rect(0, 0, BOT_W, SCR_H, COL_BG);
	InfoEnt *e = info_get(S.app, false);
	if (e && e->done) {
		ui_text(BOT_W / 2, 70, e->info.name, SZ_LARGE, COL_WHITE, AL_CENTER, 296);
		char t[48];
		snprintf(t, sizeof t, _("Captura %d de %d"), S.shot + 1, e->info.nshots);
		ui_text(BOT_W / 2, 98, t, SZ_BODY, COL_DIM, AL_CENTER, 0);
		// Flechas táctiles
		ui_rrect(12, 130, 140, 40, 4, COL_BTN2);
		ui_rrect(168, 130, 140, 40, 4, COL_BTN2);
		ui_text(82, 141, _("‹ Anterior"), SZ_BODY, S.shot > 0 ? COL_ACC : COL_DIM, AL_CENTER, 0);
		ui_text(238, 141, _("Siguiente ›"), SZ_BODY, S.shot + 1 < e->info.nshots ? COL_ACC : COL_DIM, AL_CENTER, 0);
	}
	static const char *h[] = {"← →", "Cambiar", "B", "Volver"};
	draw_hints(h, 2);
}

// ============================================================================
// Diálogo de salida y avisos

static void draw_overlays_bottom(void)
{
	if (S.toast_t > 0) {
		float a = S.toast_t > 0.3f ? 1 : S.toast_t / 0.3f;
		float w = ui_text_w(S.toast, SZ_SMALL) + 24;
		if (w > 300) w = 300;
		ui_rrect((BOT_W - w) / 2, 186, w, 24, 12, C2D_Color32(0x0e, 0x14, 0x1b, (u8)(0xee * a)));
		ui_text(BOT_W / 2, 191, S.toast, SZ_SMALL, (COL_WHITE & 0x00FFFFFF) | ((u32)(255 * a) << 24), AL_CENTER, 280);
	}
	if (S.ask_exit) {
		ui_rect(0, 0, BOT_W, SCR_H, C2D_Color32(0, 0, 0, 0xa0));
		ui_rrect(30, 60, 260, 120, 6, COL_PANEL);
		ui_text(BOT_W / 2, 78, _("¿Salir de Steam3DS?"), SZ_LARGE, COL_WHITE, AL_CENTER, 240);
		ui_rrect(46, 124, 108, 36, 4, COL_BTN2);
		ui_rrect(166, 124, 108, 36, 4, C2D_Color32(0xc0, 0x39, 0x2b, 0xff));
		ui_text(100, 133, _("Cancelar"), SZ_BODY, COL_ACC, AL_CENTER, 100);
		ui_text(220, 133, _("Salir"), SZ_BODY, COL_WHITE, AL_CENTER, 100);
	}
}


// ============================================================================
// Pantalla de carga: la rueda de válvula gira, sale vapor y una barra de carga

enum { SPR_WHEEL, SPR_PUFF, SPR_GLOW };

static float ease_out_back(float t)
{
	if (t >= 1) return 1;
	const float c1 = 1.70158f, c3 = c1 + 1;
	return 1 + c3 * powf(t - 1, 3) + c1 * powf(t - 1, 2);
}

static float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

static void draw_sprite(int idx, float cx, float cy, float scale, float angle, float alpha)
{
	if (!sprites || alpha <= 0) return;
	C2D_Image img = C2D_SpriteSheetGetImage(sprites, idx);
	C2D_ImageTint tint;
	C2D_AlphaImageTint(&tint, clampf(alpha, 0, 1));
	C2D_DrawImageAtRotated(img, cx, cy, 0.5f, angle, &tint, scale, scale);
}

static bool splash_ready(void)
{
	return S.offers.count > 0 || S.offers.err[0] || net_status_text()[0];
}

static void draw_splash_top(void)
{
	float t = splash_t;
	ui_vgrad(0, 0, TOP_W, SCR_H, C2D_Color32(0x24, 0x3b, 0x50, 0xff), C2D_Color32(0x17, 0x1a, 0x21, 0xff));
	float cx = TOP_W / 2, cy = 100;
	float pop = ease_out_back(clampf(t / 0.55f, 0, 1));
	draw_sprite(SPR_GLOW, cx, cy, (1.7f + 0.08f * sinf(t * 2.4f)) * pop, 0, 0.55f * pop);
	// Como una válvula que se abre: arranca despacio y coge velocidad
	float spin = t < 0.8f ? 0.9f * t * t : 0.576f + 1.44f * (t - 0.8f);
	draw_sprite(SPR_WHEEL, cx, cy, 0.62f * pop, spin, pop);
	// Vapor: nubes que suben a la derecha, crecen y se desvanecen
	for (int i = 0; i < 5; i++) {
		float life = fmodf(t * 0.75f + i * 0.2f, 1.0f);
		if (t < 0.35f + i * 0.12f) continue;
		float a = (life < 0.15f ? life / 0.15f : 1 - (life - 0.15f) / 0.85f) * 0.75f;
		float x = cx + 30 + life * 34 + sinf(i * 2.1f + t) * 4;
		float y = cy - 34 - life * 58;
		draw_sprite(SPR_PUFF, x, y, 0.35f + life * 0.65f, 0, a);
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
	ui_rect(0, 0, BOT_W, SCR_H, C2D_Color32(0x12, 0x15, 0x1b, 0xff));
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
		if ((splash_t > 2.0f && splash_ready()) || splash_t > 6.0f || skip) splash_out = 0;
	} else {
		splash_out += dt;
		if (splash_out > 0.35f) S.scene = SC_STORE;
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

static void input_store(u32 kd, u32 kh, u32 ku, touchPosition *tp)
{
	if (kd & KEY_L) S.tab = (S.tab + TAB_COUNT - 1) % TAB_COUNT;
	if (kd & KEY_R) S.tab = (S.tab + 1) % TAB_COUNT;
	if (S.tab != TAB_OFFERS) goto touch;
	if (key_repeat(kd, kh, KEY_DOWN)) store_select(S.sel + 1);
	if (key_repeat(kd, kh, KEY_UP)) store_select(S.sel - 1);
	if (key_repeat(kd, kh, KEY_RIGHT)) store_select(S.sel + 6);
	if (key_repeat(kd, kh, KEY_LEFT)) store_select(S.sel - 6);
	if (kd & KEY_A) {
		if (S.offers.count) open_game(S.offers.rows[S.sel].appid, false);
		else if (!S.offers.req) { S.offers_fail_at = 0; S.offers.end = false; offers_more(); }
	}
touch:
	if (kd & KEY_TOUCH) {
		S.touch_x0 = tp->px;
		S.touch_y0 = tp->py;
		S.touch_moved = 0;
		S.drag_scroll0 = S.scroll_target;
		S.dragging = tp->py >= LIST_Y && tp->py < LIST_Y + LIST_H;
	} else if ((kh & KEY_TOUCH) && S.dragging) {
		int dy = tp->py - S.touch_y0;
		if (abs(dy) > S.touch_moved) S.touch_moved = abs(dy);
		if (S.touch_moved >= 8) {
			float max = S.offers.count * ROW_H - LIST_H;
			if (max < 0) max = 0;
			S.scroll_target = S.drag_scroll0 - dy;
			if (S.scroll_target < 0) S.scroll_target = 0;
			if (S.scroll_target > max) S.scroll_target = max;
			S.scroll = S.scroll_target;
			S.finger_scrolled = true;
			if (S.scroll_target > max - 3 * ROW_H) offers_more();
		}
	}
	if (ku & KEY_TOUCH) {
		if (S.touch_moved < 8) {
			int x = S.touch_x0, y = S.touch_y0;
			if (y < LIST_Y) {
				int t = (int)(x / (BOT_W / 4.6f));
				S.tab = t >= TAB_COUNT ? TAB_SEARCH : t;
			} else if (S.dragging && S.tab == TAB_OFFERS) {
				int i = (int)((y - LIST_Y + S.scroll) / ROW_H);
				if (i >= 0 && i < S.offers.count) {
					if (i == S.sel) open_game(S.offers.rows[i].appid, false);
					else store_select(i);
				}
			}
		}
		S.dragging = false;
	}
}

static RowList *game_list(void)
{
	return S.sec == SEC_EXTRAS ? &S.dlc : S.sec == SEC_SIMILAR ? &S.similar : NULL;
}

static void game_set_sec(int sec)
{
	S.sec = (sec + SEC_COUNT) % SEC_COUNT;
	S.lsel = 0;
	S.lscroll = 0;
	S.show_qr = false;
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

	if (kd & KEY_TOUCH) {
		int x = tp->px, y = tp->py;
		if (hit(&B_SEG, x, y)) game_set_sec((int)((x - B_SEG.x) / (B_SEG.w / SEC_COUNT)));
		else if (a && hit(&B_SHOTS, x, y) && a->nshots) { S.scene = SC_SHOTS; S.shot = 0; }
		else if (a && (hit(&B_CART, x, y) || hit(&B_WISH, x, y) || hit(&B_INST, x, y)))
			toast(_("Disponible al iniciar sesión"));
	}
}

static void input_shots(u32 kd)
{
	InfoEnt *e = info_get(S.app, false);
	int n = (e && e->done) ? e->info.nshots : 0;
	touchPosition tp;
	hidTouchRead(&tp);
	bool prev = (kd & (KEY_LEFT | KEY_L)) || ((kd & KEY_TOUCH) && tp.px < 160 && tp.py > 120 && tp.py < 180);
	bool next = (kd & (KEY_RIGHT | KEY_R)) || ((kd & KEY_TOUCH) && tp.px >= 160 && tp.py > 120 && tp.py < 180);
	if (prev && S.shot > 0) S.shot--;
	if (next && S.shot + 1 < n) S.shot++;
	if (kd & KEY_B) S.scene = SC_GAME;
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
	offers_more();
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

	S.scene = SC_SPLASH;
	S.tab = TAB_OFFERS;
	sprites = C2D_SpriteSheetLoad("romfs:/gfx/sprites.t3x");
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
		g_time += dt;
		frame_no++;
		if (S.toast_t > 0) S.toast_t -= dt;

		hidScanInput();
		u32 kd = hidKeysDown(), kh = hidKeysHeld(), ku = hidKeysUp();
		touchPosition tp;
		hidTouchRead(&tp);

		poll_country();
		list_poll(&S.offers, true);
		if (S.offers.err[0] && !S.offers.req) {
			if (!S.offers_fail_at) {
				S.offers_fail_at = now;
				S.offers.end = false;
			} else if (S.offers.count == 0) {
				offers_more();   // espera sola lo que toque
			}
		} else {
			S.offers_fail_at = 0;
		}
		info_poll();
		for (int i = 0; i < CACHE; i++) {
			InfoEnt *e = &cache[i];
			bool shown = e->appid == S.app || (S.offers.count && e->appid == S.offers.rows[S.sel].appid);
			if (e->failed && shown && now - e->fail_at > (u64)retry_delay(e->err) * 1000) info_retry(e);
		}
		img_update();

		if (S.ask_exit) {
			if (kd & KEY_A) S.quit = true;
			if (kd & (KEY_B | KEY_START)) S.ask_exit = false;
			if (kd & KEY_TOUCH) {
				if (tp.py >= 124 && tp.py < 160 && tp.px >= 166 && tp.px < 274) S.quit = true;
				else if (tp.py >= 124 && tp.py < 160 && tp.px >= 46 && tp.px < 154) S.ask_exit = false;
			}
			kd = kh = ku = 0;
		} else if (kd & KEY_START) {
			S.ask_exit = true;
			kd = kh = ku = 0;
		}

		switch (S.scene) {
		case SC_STORE: input_store(kd, kh, ku, &tp); break;
		case SC_GAME: input_game(kd, kh, ku, &tp); break;
		case SC_SHOTS: input_shots(kd); break;
		case SC_SPLASH: splash_update(dt, kd); break;
		}

		// Vista previa: la ficha del seleccionado, cuando el cursor se queda quieto un momento
		if (S.scene == SC_STORE && S.tab == TAB_OFFERS && S.offers.count && now - S.sel_changed > 250)
			info_get(S.offers.rows[S.sel].appid, true);

		InfoEnt *ge = S.scene != SC_STORE ? info_get(S.app, false) : NULL;
		game_poll(ge && ge->done ? &ge->info : NULL);

		// Desplazamientos suaves
		if (!S.finger_scrolled) S.scroll = approach(S.scroll, S.scroll_target, 0.3f);
		{
			RowList *l = game_list();
			float rh = 22, vis = SCR_H - 4 - 42;
			if (l) {
				float y = S.lsel * rh, t = S.lscroll;
				if (y < t) t = y;
				if (y + rh > t + vis) t = y + rh - vis;
				S.lscroll = approach(S.lscroll, t, 0.3f);
			}
			float card = 20 + 3 * 12 + 6 + 5, rvis = SCR_H - 20 - 52;
			float ry = S.rsel * card, rt = S.rscroll;
			if (ry < rt) rt = ry;
			if (ry + card > rt + rvis) rt = ry + card - rvis;
			S.rscroll = approach(S.rscroll, rt, 0.3f);
		}

		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		ui_begin_frame();
		C2D_TargetClear(top, COL_BG);
		C2D_SceneBegin(top);
		switch (S.scene) {
		case SC_STORE: draw_store_top(); break;
		case SC_GAME: draw_game_top(); break;
		case SC_SHOTS: draw_shots_top(); break;
		case SC_SPLASH: draw_splash_top(); break;
		}
		if (S.scene == SC_SPLASH && splash_out > 0) ui_rect(0, 0, TOP_W, SCR_H, C2D_Color32(0, 0, 0, (u8)(255 * clampf(splash_out / 0.35f, 0, 1))));
		if (S.ask_exit) ui_rect(0, 0, TOP_W, SCR_H, C2D_Color32(0, 0, 0, 0x80));

		C2D_TargetClear(bot, COL_BG);
		C2D_SceneBegin(bot);
		switch (S.scene) {
		case SC_STORE: draw_store_bottom(); break;
		case SC_GAME: draw_game_bottom(); break;
		case SC_SHOTS: draw_shots_bottom(); break;
		case SC_SPLASH: draw_splash_bottom(); break;
		}
		if (S.scene == SC_SPLASH && splash_out > 0) ui_rect(0, 0, BOT_W, SCR_H, C2D_Color32(0, 0, 0, (u8)(255 * clampf(splash_out / 0.35f, 0, 1))));
		draw_overlays_bottom();
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
	}

	netlog("salir");
	if (S.country_req) net_release(S.country_req);
	rowlist_free(&S.offers);
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
	C2D_TextBufDelete(review_buf);
	img_exit();
	net_exit();
	ui_exit();
	util_exit();
	C2D_Fini();
	C3D_Fini();
	romfsExit();
	gfxExit();
	return 0;
}
