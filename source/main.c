#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "app.h"
#include "fx.h"
#include "cJSON.h"
#include "auth.h"
#include "i18n.h"
#include "img.h"
#include "net.h"
#include "pay.h"
#include "shop.h"
#include "steam.h"
#include "ui.h"
#include "util.h"

// ============================================================================
// Estado

typedef enum { SC_SPLASH, SC_STORE, SC_GAME, SC_SHOTS, SC_LOGIN, SC_PROFILE, SC_PAY, SC_ABOUT } Scene;
enum { TAB_HOME, TAB_OFFERS, TAB_DEMOS, TAB_BETAS, TAB_SEARCH, TAB_COUNT };
// Listas de tu cuenta: se ven como una pestaña más, pero sin barra de pestañas
// Pseudopestañas (cuenta) y las cuatro listas de Inicio (una por subcategoría)
enum { TAB_LIBRARY = TAB_COUNT, TAB_WISHLIST, TAB_CART, TAB_H0, TAB_ALL = TAB_H0 + 4 };
enum { SEC_SUMMARY, SEC_REQS, SEC_REVIEWS, SEC_EXTRAS, SEC_SIMILAR, SEC_COUNT };

#define ROW_H      29
#define LIST_Y     24
#define LIST_H     (SCR_H - LIST_Y - 18)
#define PAGE       30
#define MAX_REVIEWS 48
#define CACHE      6

// Zona táctil de un botón
typedef struct { float x, y, w, h; } Box;

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
	char toast[160];
	int toast_ic;                // icono del aviso (-1 sin icono)
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
	int prev_tab;                // pestaña de la tienda a la que vuelven Biblioteca y Deseados
	float login_done;            // segundos desde que entró (para el saludo)
	AuthState last_auth;

	// Compras
	bool pick_open;              // elegir edición (cuando hay varias formas de comprarlo)
	float pick_anim;
	int pick_sel;
	bool cart_qr;                // carrito: QR para pagarlo en el móvil
	u32 cart_seen;               // versión del carrito ya copiada a la lista

	// Pago
	bool pay_qr;                 // QR para pagarlo en el móvil
	bool want_cvv;               // abrir el teclado del CVV (fuera del frame)
	float pay_hold;              // A mantenido para confirmar (0..1)
	bool pay_armed;              // hay que soltar A antes de poder mantenerlo
	bool pay_ssa, pay_wd;        // casillas marcadas: Acuerdo de Suscriptor y renuncia al desistimiento
	int pay_focus;               // en el resumen: 0 casilla del acuerdo, 1 la del desistimiento, 2 pagar
	int paysel;                  // método elegido con la cruceta
	int home_sub;                // Inicio: subcategoría (lo más vendido, novedades, próximamente, gratis)
	float sub_x, sub_anim;       // píldora de la subcategoría y la barra que sale al estar en Inicio
	bool search_focus;           // Buscar: el foco está en la píldora de búsqueda (no en la lista)
	float search_x;              // la píldora se estira sobre las pestañas (0..1)
	int pfocus;                  // botón elegido con la cruceta en el perfil
	bool has_demo_btn;           // la ficha tiene el botón de la demo
	float oscroll, oscroll_t;    // Resumen: desplazamiento del texto (actual y destino)
	float ocontent;              // alto de todo el Resumen
} S;

enum { BT_NONE, BT_CART, BT_WISH, BT_SHOTS, BT_INST, BT_PREV, BT_NEXT, BT_EXIT_NO, BT_EXIT_YES, BT_SEARCH, BT_BACKBAR,
	BT_PLIB, BT_PWISH, BT_PLOGOUT, BT_PAYW, BT_PAYC, BT_PAYQ, BT_CVV, BT_PAYGO, BT_DEMO };

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
	u32 *wish;                   // Deseados: appids por prioridad, que se piden de 50 en 50
	int nwish, wish_next;
	bool wish_loaded;
	float anim_t;                // cuándo empezaron a entrar las filas (lista animada)
	int lib_stage;               // Biblioteca: 0 GetOwnedGames, 1 userdata de la tienda, 2 nombres por tandas
	int lib_base;                // juegos que ya dio GetOwnedGames (a los que se suman los de la tienda)
	int anim_count;
} Tab;
static Tab tabs[TAB_ALL];
// La pestaña de verdad: en Inicio, la lista de la subcategoría elegida
static int cur_ti(void);
#define CUR (&tabs[cur_ti()])

// Amigos y el juego abierto (con sesión)
static struct {
	u32 appid;
	Req *req, *names;
	bool done;
	int owns, in_game, wish;
	u64 ids[3];             // los primeros (jugando ahora, si no, los que lo tienen)
	int nids;
	char name[3][40];
	int nnames;
	bool playing;           // ids[0] lo está jugando ahora
} FR;

static InfoEnt cache[CACHE];
static u64 frame_no;
static C2D_SpriteSheet sprites, splash_bg, wheel_sheet, ui_sheet;   // ui_sheet: el logo (no se libera)
static int sprites_free_in = -1;   // fotogramas que faltan para liberar los sprites de la carga
static C2D_TextBuf review_buf;
static float splash_t, splash_out = -1;

static void toast(const char *msg)
{
	snprintf(S.toast, sizeof S.toast, "%s", msg);
	S.toast_t = 2.6f;
	S.toast_ic = -1;
}

static void toast_icon(const char *msg, Icon ic)
{
	toast(msg);
	S.toast_ic = ic;
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
		case 28: return _("Steam no responde (tiempo de espera agotado)");
		case 35: case 51: case 58: case 60: case 77: return _("No se pudo establecer una conexión segura con Steam");
		case 52: case 55: case 56: return _("Se perdió la conexión");
		}
		return _("Error de red");
	}
	if (sscanf(err, "HTTP %d", &code) == 1) {
		if (code == 429) return _("Demasiadas solicitudes. Inténtalo de nuevo en unos minutos.");
		if (code >= 500) return _("Los servidores de Steam no están disponibles en este momento");
		return _("Steam ha devuelto un error");
	}
	return _("Respuesta inesperada de Steam");
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
	free_e->sum = steam_req_items(&appid, 1, false);
	return free_e;
}

static void info_poll(void)
{
	for (int i = 0; i < CACHE; i++) {
		InfoEnt *e = &cache[i];
		if (e->req && e->req->state >= REQ_DONE) {
			if (e->req->state == REQ_DONE && steam_take_appdetails(e->req, &e->info)) e->done = true;
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
	if (l->req->state == REQ_DONE) {
		if (!steam_take_rows(l->req, l, append)) steam_parse_rows(l->req->data, l, append);
	}
	else snprintf(l->err, sizeof l->err, "%s", l->req->err);
	net_release(l->req);
	l->req = NULL;
}

static const StoreQuery home_q[HOME_PARTS] = {Q_TOP, Q_NEW, Q_SOON, Q_F2P};
static const char *home_title[HOME_PARTS] = {"Lo más vendido", "Novedades populares", "Próximamente", "Gratis para jugar"};

static bool tab_paged(int t)
{
	return t == TAB_OFFERS || t == TAB_DEMOS || t == TAB_BETAS || t == TAB_WISHLIST || t >= TAB_H0 ||
		(t == TAB_LIBRARY && tabs[TAB_LIBRARY].lib_stage == 2);
}

static int cur_ti(void)
{
	return S.tab == TAB_HOME ? TAB_H0 + S.home_sub : S.tab;
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
		tab_more(TAB_H0 + S.home_sub);
		return;
	} else if (ti >= TAB_H0) {
		if (t->list.end) return;
		t->list.req = steam_req_query(home_q[ti - TAB_H0], t->list.count, PAGE);
	} else if (ti == TAB_LIBRARY) {
		if (!auth_logged()) return;
		if (t->lib_stage == 2) {
			// Plan B: los juegos que dice la tienda, con sus nombres de 50 en 50
			if (t->wish_next >= t->nwish) return;
			int n = t->nwish - t->wish_next;
			if (n > 50) n = 50;
			t->list.req = steam_req_items(t->wish + t->wish_next, n, true);
			t->wish_next += n;
		} else {
			if (t->list.count) return;
			t->list.req = steam_req_owned(auth_info()->steamid, auth_token());
		}
	} else if (ti == TAB_WISHLIST) {
		if (!auth_logged()) return;
		if (!t->wish_loaded) {
			t->list.req = steam_req_wishlist(auth_info()->steamid, auth_token());
		} else if (t->wish_next < t->nwish) {
			int n = t->nwish - t->wish_next;
			if (n > 50) n = 50;
			t->list.req = steam_req_items(t->wish + t->wish_next, n, false);
			t->wish_next += n;
		}
	} else if (ti == TAB_CART) {
		if (!auth_logged()) return;
		shop_cart_load();
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

// Juegos «nuevos» de la biblioteca (un punto que brilla en su fila): los que tienes y no estaban
// la primera vez que se cargó la biblioteca en la consola, hasta que abres su ficha o los juegas.
// Se guarda por cuenta en la SD: así un juego comprado hace tiempo en el PC también sale como nuevo
static u32 *seen;
static int nseen, seen_cap;
static bool seen_exists;     // ya hay lista guardada (si no, la primera carga la crea sin puntos)
static u64 seen_owner;       // cuenta de la lista cargada
static u32 added_now[8];     // añadidos desde la app en esta sesión: nuevos aunque aún no hubiera lista
static int nadded_now;

static void note_added(u32 appid)
{
	if (nadded_now < 8) added_now[nadded_now++] = appid;
}

static void seen_path(char *p, size_t n)
{
	snprintf(p, n, APP_DIR "/vistos_%llu.bin", (unsigned long long)auth_info()->steamid);
}

// La lista se lee en el hilo de disco (abrir el archivo en la SD paraba la interfaz unos 40 ms
// justo al terminar de cargar la biblioteca); hasta que llega, nada sale como nuevo
static DiskLoad *seen_ld;
static bool seen_ready;      // la lista de seen_owner ya está leída
static bool seen_pending;    // la biblioteca terminó antes que la lectura: se repasa al llegar

static void seen_load(void)
{
	u64 id = auth_info()->steamid;
	if (seen_owner == id) return;
	seen_owner = id;
	nseen = 0;
	seen_exists = seen_ready = seen_pending = false;
	disk_load_free(seen_ld);
	char p[96];
	seen_path(p, sizeof p);
	seen_ld = disk_load(p);
}

static void seen_add(u32 appid);
static void seen_sync(Tab *t);

static void seen_poll(void)
{
	if (auth_logged()) seen_load();
	if (!seen_ld || !seen_ld->done) return;
	if (seen_ld->data) {
		seen_exists = true;
		for (size_t i = 0; i + 4 <= seen_ld->len; i += 4) {
			u32 a;
			memcpy(&a, seen_ld->data + i, 4);
			seen_add(a);
		}
	}
	disk_load_free(seen_ld);
	seen_ld = NULL;
	seen_ready = true;
	if (seen_pending) seen_sync(&tabs[TAB_LIBRARY]);
}

static bool seen_has(u32 appid)
{
	for (int i = 0; i < nseen; i++)
		if (seen[i] == appid) return true;
	return false;
}

static void seen_add(u32 appid)
{
	if (seen_has(appid)) return;
	if (nseen == seen_cap) {
		int cap = seen_cap ? seen_cap * 2 : 256;
		u32 *n = realloc(seen, cap * sizeof *n);
		if (!n) return;
		seen = n;
		seen_cap = cap;
	}
	seen[nseen++] = appid;
}

static void seen_save(void)
{
	char p[96];
	seen_path(p, sizeof p);
	disk_save(p, seen, nseen * sizeof *seen);
	seen_exists = true;
}

static bool lib_is_new(u32 appid)
{
	return auth_logged() && seen_owner == auth_info()->steamid && seen_exists && !seen_has(appid);
}

// La biblioteca ya está entera: la primera vez se apunta todo lo que hay (sin puntos); después,
// los que ya has jugado dejan de ser nuevos
static void seen_sync(Tab *t)
{
	seen_load();
	seen_pending = !seen_ready;
	if (!seen_ready) return;
	bool first = !seen_exists, changed = first;
	for (int i = 0; i < t->list.count; i++) {
		const StoreRow *r = &t->list.rows[i];
		bool added = false;
		for (int k = 0; k < nadded_now; k++) added |= added_now[k] == r->appid;
		if ((first && !added) || (r->minutes > 0 && !seen_has(r->appid))) {
			seen_add(r->appid);
			changed = true;
		}
	}
	if (changed) seen_save();
}

static bool owned_app(u32 appid);

// Al abrir la ficha de un juego tuyo deja de ser nuevo (la de uno que aún no tienes no cuenta)
static void seen_open(u32 appid)
{
	if (!seen_ready || !lib_is_new(appid) || !owned_app(appid)) return;
	seen_add(appid);
	seen_save();
}

// Ordena la biblioteca por nombre sin que la selección salte a otro juego (si aún no te has
// movido, se queda en el primero)
static void lib_sort(Tab *t)
{
	u32 sel = t->sel > 0 && t->sel < t->list.count ? t->list.rows[t->sel].appid : 0;
	steam_sort_by_name(&t->list);
	for (int i = 0; sel && i < t->list.count; i++)
		if (t->list.rows[i].appid == sel) {
			if (i != t->sel) t->sel_y = t->sel = i;
			break;
		}
}

static void owned_poll(Tab *t)
{
	Req *r = t->list.req;
	if (!r || r->state < REQ_DONE) return;
	if (t->lib_stage == 2) {
		// Los nombres llegan de 50 en 50: se piden todos seguidos para poder ordenar por nombre
		bool fail = r->state != REQ_DONE;
		list_poll(&t->list, true);
		t->list.total = t->lib_base + t->nwish;
		t->list.end = t->wish_next >= t->nwish;
		lib_sort(t);
		if (!t->list.end && !fail) tab_more(TAB_LIBRARY);
		else if (t->list.end) seen_sync(t);
		return;
	}
	if (t->lib_stage == 0) {
		bool ok = r->state == REQ_DONE && steam_parse_owned(r->data, &t->list);
		netlog("biblioteca: GetOwnedGames %s (HTTP %ld, eresult %d, %d juegos)", ok ? "ok" : "mal", r->http, r->eresult, t->list.count);
		if (!ok && r->state == REQ_DONE && r->data) netlog("biblioteca: respuesta %.120s", r->data);
		if (!ok) t->list.count = 0;
		// Además, lo que la tienda sabe que tienes (la misma lista con la que marca «En tu
		// biblioteca»): GetOwnedGames no trae las licencias gratis que aún no has jugado, y a
		// algunas cuentas no les devuelve nada
		t->lib_base = t->list.count;
		char ck[1200];
		net_release(r);
		t->list.req = NULL;
		if (auth_web_cookies("store.steampowered.com", ck, sizeof ck)) {
			t->list.req = net_request("https://store.steampowered.com/dynamicstore/userdata/", NULL, ck);
			t->lib_stage = 1;
			t->list.err[0] = 0;
		} else if (!ok) {
			snprintf(t->list.err, sizeof t->list.err, "%s", "json");
		}
		memset(ck, 0, sizeof ck);
		if (!t->list.req) {
			t->sel_changed = osGetTime();
			seen_sync(t);
		}
		return;
	} else {
		u32 *ids = NULL;
		int n = r->state == REQ_DONE ? steam_parse_userdata_owned(r->data, &ids) : -1;
		netlog("biblioteca: userdata de la tienda: %d juegos (HTTP %ld)", n, r->http);
		net_release(r);
		t->list.req = NULL;
		if (n < 0) {
			// Si GetOwnedGames ya dio juegos, se quedan esos
			if (!t->lib_base) snprintf(t->list.err, sizeof t->list.err, "%s", "json");
			else seen_sync(t);
			free(ids);
			return;
		}
		// Solo los que GetOwnedGames no trajo
		int m = 0;
		for (int i = 0; i < n; i++) {
			bool dup = false;
			for (int j = 0; j < t->lib_base && !dup; j++) dup = t->list.rows[j].appid == ids[i];
			if (!dup) ids[m++] = ids[i];
		}
		if (t->lib_base) netlog("biblioteca: %d de GetOwnedGames y %d más de la tienda", t->lib_base, m);
		n = m;
		free(t->wish);
		t->wish = ids;
		t->nwish = n;
		t->wish_loaded = true;
		t->wish_next = 0;
		t->list.total = t->lib_base + n;
		t->list.end = n == 0;
		t->lib_stage = 2;
		if (n) tab_more(TAB_LIBRARY);
		else seen_sync(t);
		return;
	}
	net_release(r);
	t->list.req = NULL;
	t->sel_changed = osGetTime();
	seen_sync(t);
}

static void wish_poll(Tab *t)
{
	Req *r = t->list.req;
	if (!r || r->state < REQ_DONE) return;
	if (!t->wish_loaded) {
		int n = r->state == REQ_DONE ? steam_parse_wishlist(r->data, &t->wish) : -1;
		if (n < 0) snprintf(t->list.err, sizeof t->list.err, "%s", r->state == REQ_DONE ? "json" : r->err);
		net_release(r);
		t->list.req = NULL;
		if (n < 0) return;
		t->wish_loaded = true;
		t->nwish = n;
		t->wish_next = 0;
		t->list.total = n;
		t->list.end = n == 0;
		tab_more(TAB_WISHLIST);
		return;
	}
	list_poll(&t->list, true);
	// Los nombres y precios llegan de 50 en 50: el total es el de la lista de deseos
	t->list.total = t->nwish;
	t->list.end = t->wish_next >= t->nwish;
}

static void account_lists_clear(void)
{
	for (int ti = TAB_LIBRARY; ti < TAB_H0; ti++) {
		Tab *t = &tabs[ti];
		rowlist_free(&t->list);
		free(t->wish);
		memset(t, 0, sizeof *t);
	}
}

static void open_pay(void);

// El carrito vive en shop.c; la pestaña lo copia como lista cuando cambia
static void cart_sync(void)
{
	if (S.cart_seen == shop_cart_version()) return;
	S.cart_seen = shop_cart_version();
	Tab *t = &tabs[TAB_CART];
	const CartInfo *c = shop_cart();
	t->list.count = 0;
	for (int i = 0; i < c->n; i++) {
		const CartLine *l = &c->line[i];
		StoreRow r = {0};
		r.appid = l->appid;
		snprintf(r.name, sizeof r.name, "%s", l->name[0] ? l->name : "…");
		snprintf(r.price, sizeof r.price, "%s", l->price);
		snprintf(r.orig, sizeof r.orig, "%s", l->orig);
		r.disc = l->disc;
		add_row(&t->list, &r);
	}
	t->list.total = c->n;
	t->list.end = true;
	t->started = true;
	if (t->sel >= t->list.count) t->sel = t->list.count ? t->list.count - 1 : 0;
	t->sel_changed = osGetTime();
}

// Deseados cambiados desde la consola: se actualizan los ids y la lista se vuelve a pedir
static void wish_changed(u32 appid, bool add)
{
	Tab *t = &tabs[TAB_WISHLIST];
	if (!t->wish_loaded) return;
	if (add) {
		u32 *n = realloc(t->wish, (t->nwish + 1) * sizeof(u32));
		if (!n) return;
		t->wish = n;
		t->wish[t->nwish++] = appid;
	} else {
		int k = 0;
		for (int i = 0; i < t->nwish; i++)
			if (t->wish[i] != appid) t->wish[k++] = t->wish[i];
		t->nwish = k;
	}
	if (t->list.req) net_release(t->list.req);
	t->list.req = NULL;
	t->list.count = 0;
	t->list.total = t->nwish;
	t->list.end = t->nwish == 0;
	t->wish_next = 0;
	t->started = false;
	if (t->sel >= t->nwish) t->sel = t->nwish ? t->nwish - 1 : 0;
}

static void library_changed(void)
{
	Tab *t = &tabs[TAB_LIBRARY];
	if (t->list.req) net_release(t->list.req);
	rowlist_free(&t->list);
	free(t->wish);
	t->wish = NULL;
	t->nwish = t->wish_next = 0;
	t->wish_loaded = false;
	t->lib_stage = t->lib_base = 0;
	t->started = false;
	t->sel = 0;
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
	char buf[8 * 64];
	size_t len = 0;
	for (int i = 0; i < n; i++) len += snprintf(buf + len, sizeof buf - len, "%s\n", S.recents[i]);
	disk_save(APP_DIR "/searches.txt", buf, len);
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
	fx_bar(0, 0, TOP_W, 18, true);
	float ty = floorf((18 - ui_line_h(SZ_SMALL)) / 2) + 1;
	// El logo (el volante) va delante del nombre y gira mientras se descarga algo
	static float ang, spin;
	bool busy = net_busy();
	spin = approach(spin, busy ? 1 : 0, busy ? 8 : 2.5f);
	ang += g_dt * 6.5f * spin;
	ui_wheel(14, 9, 5.5f, ang, col_lerp(C2D_Color32(0xc7, 0xd5, 0xe0, 0xff), COL_ACC, spin));
	float nw = ui_text(25, ty, APP_NAME, SZ_SMALL, COL_WHITE, AL_LEFT, 0) + 17;

	// Derecha: [usuario ·] fecha · hora   [Wi-Fi] [batería] 87 %
	char clock[48], right[128];
	util_clock(clock, sizeof clock);
	bool chg;
	int bat = util_battery(&chg);
	char pct[8];
	snprintf(pct, sizeof pct, "%d%%", bat);
	float r = TOP_W - 8;
	r -= ui_text(r, ty, pct, SZ_SMALL, COL_DIM, AL_RIGHT, 0) + 4;
	ui_battery(r - 22, 4, bat, chg);
	r -= 22 + 8;
	ui_wifi(r - 7, 14, net_wifi_ok() ? osGetWifiStrength() : -1);
	r -= 14 + 8;
	if (user) snprintf(right, sizeof right, "%s · %s", user, clock);
	else snprintf(right, sizeof right, "%s", clock);
	ui_text(r, ty, right, SZ_SMALL, COL_DIM, AL_RIGHT, r - (8 + nw + 24));
}

// Tecla de una pista: el glifo del sistema si la fuente lo tiene; si no, una tecla dibujada
static float hint_key(float x, float y, const char *key, bool draw)
{
	// Combinación «X+→»: la tecla, un «+» y la flecha
	const char *plus = strchr(key, '+');
	if (plus && plus != key && plus[1]) {
		char first[8];
		snprintf(first, sizeof first, "%.*s", (int)(plus - key), key);
		float w = hint_key(x, y, first, draw);
		float pw = ui_text_w("+", SZ_TINY);
		if (draw) ui_text(x + w + 1, y + 4, "+", SZ_TINY, COL_DIM, AL_LEFT, 0);
		// La flecha de la cruceta, dibujada (no todas las fuentes traen ← → ↑ ↓)
		float ax = x + w + 3 + pw, cx = ax + 4, cy = y + 10;
		if (draw) {
			const char *d = plus + 1;
			u32 c = COL_WHITE;
			if (!strcmp(d, "→")) C2D_DrawTriangle(cx + 4, cy, c, cx - 3, cy - 4, c, cx - 3, cy + 4, c, 0.5f);
			else if (!strcmp(d, "←")) C2D_DrawTriangle(cx - 4, cy, c, cx + 3, cy - 4, c, cx + 3, cy + 4, c, 0.5f);
			else if (!strcmp(d, "↓")) C2D_DrawTriangle(cx, cy + 4, c, cx - 4, cy - 3, c, cx + 4, cy - 3, c, 0.5f);
			else C2D_DrawTriangle(cx, cy - 4, c, cx - 4, cy + 3, c, cx + 4, cy + 3, c, 0.5f);
		}
		return w + 3 + pw + 9;
	}
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
		if (draw) ui_text(x, y + 2, g, SZ_SMALL, COL_WHITE, AL_LEFT, 0);
		return w;
	}
	float kw = ui_text_w(key, SZ_TINY) + 6;
	if (kw < 11) kw = 11;
	if (draw) {
		ui_rrect(x, y + 4.5f, kw, 11, 5.5f, C2D_Color32(0xff, 0xff, 0xff, 0x2a));
		ui_text(x + kw / 2, y + 4, key, SZ_TINY, COL_WHITE, AL_CENTER, 0);
	}
	return kw;
}

// Barra de botones: ocupa todo el borde de abajo y explica cada tecla que hace algo en la pantalla
// (las que ya van dibujadas en un botón táctil no hace falta repetirlas)
static void draw_hints(const char *const *pairs, int n)
{
	float y = SCR_H - 18;
	ui_rect(0, y, BOT_W, 18, C2D_Color32(0x0c, 0x12, 0x1b, 0xf2));
	ui_rect(0, y, BOT_W, 1, col_alpha(COL_WHITE, 0.10f));
	float total = 0, widths[8];
	for (int i = 0; i < n; i++) {
		widths[i] = hint_key(0, y, pairs[i * 2], false) + 3 + ui_text_w(_(pairs[i * 2 + 1]), SZ_TINY);
		total += widths[i];
	}
	// Repartidas a lo ancho, con el mismo hueco entre ellas
	float gap = n > 1 ? (BOT_W - 16 - total) / (n - 1) : 0;
	if (gap > 22) gap = 22;
	if (gap < 6) gap = 6;
	float x = (BOT_W - total - gap * (n - 1)) / 2;
	for (int i = 0; i < n; i++) {
		float kw = hint_key(x, y, pairs[i * 2], true);
		ui_text(x + kw + 3, y + 4, _(pairs[i * 2 + 1]), SZ_TINY, COL_TXT, AL_LEFT, 0);
		x += widths[i] + gap;
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
		ui_text(cx, cy + 16, _("Se reintentará automáticamente"), SZ_SMALL, COL_DIM, AL_CENTER, 0);
	} else {
		ui_spinner(cx, cy - 6, 9, COL_ACC);
		if (what) ui_text(cx, cy + 10, what, SZ_SMALL, COL_DIM, AL_CENTER, 0);
	}
}

static void draw_tag(float *x, float y, const char *t)
{
	float w = ui_text_w(t, SZ_TINY) + 14;
	ui_rrect(*x, y, w, 14, 7, col_alpha(COL_ACC, 0.16f));
	ui_text(*x + 7, y + 1.5f, t, SZ_TINY, COL_ACC, AL_LEFT, 0);
	*x += w + 4;
}

// Pantalla de un QR (arriba): el código a la izquierda y, a la derecha, el título subrayado, la
// explicación y, si se da, la portada del juego con su nombre o una línea más. Todo centrado en
// el alto que queda bajo la barra de arriba
static void draw_qr_panel(const char *url, const char *title, const char *body, const AppInfo *a, const char *foot, u32 foot_col)
{
	const float qs = 170, area = SCR_H - 18;
	ui_qr(url, 20, 18 + (area - qs) / 2, qs);
	const float x = 206, w = TOP_W - 12 - x, ih = floorf(w * 215 / 460);
	int nb = ui_text_lines(body, SZ_SMALL, w);
	if (nb > 7) nb = 7;
	int nf = foot ? ui_text_lines(foot, SZ_SMALL, w) : 0;
	if (nf > 2) nf = 2;
	float h = ui_line_h(SZ_LARGE) + 10 + (nb - 1) * 14 + ui_line_h(SZ_SMALL);
	if (a) h += 12 + ih + 6 + ui_line_h(SZ_SMALL);
	if (nf) h += 10 + (nf - 1) * 14 + ui_line_h(SZ_SMALL);
	float y = 18 + (area - h) / 2;
	float tw = ui_text(x, y, title, SZ_LARGE, COL_WHITE, AL_LEFT, w);
	ui_rrect(x, y + 19, tw < w ? tw : w, 2, 1, col_alpha(COL_ACC, 0.8f));   // subrayado, como al iniciar sesión
	y += ui_line_h(SZ_LARGE) + 10;
	ui_text_wrap(x, y, w, body, SZ_SMALL, COL_TXT, 7, 14);
	y += (nb - 1) * 14 + ui_line_h(SZ_SMALL);
	if (a) {
		y += 12;
		const Pic *p = img_get(a->header, 460);
		ui_rect(x - 1, y - 1, w + 2, ih + 2, C2D_Color32(0xff, 0xff, 0xff, 0x30));
		if (p) img_draw_cover(p, x, y, w, ih, 0.5f);
		else {
			ui_rect(x, y, w, ih, COL_PANEL);
			if (!img_failed(a->header)) ui_spinner(x + w / 2, y + ih / 2, 6, COL_DIM);
		}
		y += ih + 6;
		ui_text(x, y, a->name, SZ_SMALL, COL_WHITE, AL_LEFT, w);
		y += ui_line_h(SZ_SMALL);
	}
	if (nf) ui_text_wrap(x, y + 10, w, foot, SZ_SMALL, foot_col, 2, 14);
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
	size_t bl = strlen(big);
	float bs = s * (bl > 2 ? 0.018f : 0.026f);
	float bw = ui_text_w(big, bs);
	if (bw > s - 4) bs *= (s - 4) / bw;   // «E10+» y similares: más pequeño hasta que quepa
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
		// Chip de cristal muy sencillo
		ui_rrect(cx, cy, cw, 15, 7.5f, C2D_Color32(0xff, 0xff, 0xff, 0x14));
		ui_rect(cx + 6, cy, cw - 12, 1, C2D_Color32(0xff, 0xff, 0xff, 0x1c));
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

#define SUB_H 20        // alto de la barra de subcategorías de Inicio

static float list_top(void)
{
	return LIST_Y + ease_out_cubic(S.sub_anim) * (SUB_H + 2);
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
	case TAB_HOME: snprintf(out, n, "%s", _(home_title[S.home_sub])); break;
	case TAB_OFFERS: snprintf(out, n, "%s", _("Ofertas")); break;
	case TAB_DEMOS: snprintf(out, n, "%s", _("Demos")); break;
	case TAB_BETAS: snprintf(out, n, "%s", _("Acceso anticipado")); break;   // el total de Steam cuenta también los que ya salieron
	case TAB_LIBRARY: {
		const StoreRow *r = cur_row();
		if (r && r->minutes) snprintf(out, n, _("%d h jugadas"), (r->minutes + 30) / 60);
		else if (r) snprintf(out, n, "%s", _("Nunca jugado"));
		else out[0] = 0;
		break;
	}
	case TAB_WISHLIST: snprintf(out, n, "%s", _("Lista de deseos")); break;
	case TAB_CART:
		if (shop_cart()->subtotal[0]) snprintf(out, n, _("Total del carrito: %s"), shop_cart()->subtotal);
		else out[0] = 0;
		break;
	default:
		if (t->term[0]) snprintf(out, n, _("%d resultados para «%s»"), t->list.count, t->term);
		else out[0] = 0;
		break;
	}
}

static bool friends_text(char *out, size_t n);

// «Muy positivas · 87 %» (y Metacritic si cabe). Si la valoración no cabe, la primera palabra se
// abrevia («Extremadamente positivas» → «Extrem. positivas»)
static void draw_review_line(float x, float y, float w, const char *label, int pct, int metacritic)
{
	char lab[64], tail[48];
	snprintf(lab, sizeof lab, "%s", label);
	if (pct > 0) snprintf(tail, sizeof tail, " · %d %%", pct);
	else tail[0] = 0;   // «No hay reseñas»: sin porcentaje
	float tw = ui_text_w(tail, SZ_SMALL);
	if (ui_text_w(lab, SZ_SMALL) + tw > w) {
		char *sp = strchr(lab, ' ');
		size_t first = sp ? (size_t)(sp - lab) : strlen(lab);
		if (first > 7) {
			// Corta en 6 caracteres (sin partir una letra con tilde) y pone un punto
			size_t cut = 6;
			while (cut > 0 && ((unsigned char)lab[cut] & 0xC0) == 0x80) cut--;
			char rest[64];
			snprintf(rest, sizeof rest, "%s", lab + first);
			lab[cut] = '.';
			size_t rl = strlen(rest);
			if (rl > sizeof lab - cut - 2) rl = sizeof lab - cut - 2;
			memcpy(lab + cut + 1, rest, rl);
			lab[cut + 1 + rl] = 0;
		}
	}
	float lw = ui_text(x, y, lab, SZ_SMALL, review_col(pct), AL_LEFT, w - tw);
	ui_text(x + lw, y, tail, SZ_SMALL, COL_DIM, AL_LEFT, 0);
	if (metacritic) {
		char mc[32];
		snprintf(mc, sizeof mc, " · Metacritic %d", metacritic);
		if (lw + tw + ui_text_w(mc, SZ_SMALL) <= w) ui_text(x + lw + tw, y, mc, SZ_SMALL, COL_DIM, AL_LEFT, 0);
	}
}

static void draw_preview(const StoreRow *row)
{
	InfoEnt *e = info_get(row->appid, false);
	const AppInfo *a = (e && e->done) ? &e->info : NULL;

	// Imagen (o hueco mientras llega)
	const Pic *p = a ? img_get(a->header, 460) : NULL;
	ui_rect(12, 30, 200, 94, C2D_Color32(0, 0, 0, 0x30));
	if (p) img_draw_cover(p, 12, 28, 200, 94, 0.5f);
	else {
		ui_rect(12, 28, 200, 94, COL_PANEL);
		if (!(a && img_failed(a->header))) ui_spinner(112, 75, 7, COL_DIM);
	}
	fx_rim(12, 28, 200, 94, 0, 1, false);

	// Columna de la derecha, hasta la altura de la imagen: nombre, etiquetas (hasta dos filas),
	// reseñas y fecha · estudio
	float x = 224, w = TOP_W - 10 - x, bottom = 124;
	int nl = ui_text_wrap(x, 26, w, row->name, SZ_LARGE, COL_WHITE, 2, 17);
	float y = 26 + nl * 17 + 4;
	bool has_rev = row->review[0] && row->review_pct > 0;
	bool has_date = a && (a->release[0] || a->developer[0]);
	if (a && a->ngenres) {
		// Las filas de etiquetas que caben sin quitar sitio a reseñas y fecha
		float rest = bottom - y - (has_rev ? 13 : 0) - (has_date ? 13 : 0);
		int rows = rest >= 38 ? 2 : 1;
		float tx = x;
		for (int i = 0; i < a->ngenres; i++) {
			float tw = ui_text_w(a->genres[i], SZ_TINY) + 14;
			if (tx + tw > x + w && tx > x) {
				if (--rows <= 0) break;
				tx = x;
				y += 18;
			}
			draw_tag(&tx, y, a->genres[i]);
		}
		y += 19;
	}
	if (has_rev) {
		draw_review_line(x, y, w, row->review, row->review_pct, a ? a->metacritic : 0);
		y += 13;
	}
	if (has_date && y + 12 <= bottom + 4) {
		char t[112];
		snprintf(t, sizeof t, "%s%s%s", a->release, a->release[0] && a->developer[0] ? " · " : "", a->developer);
		ui_text(x, y, t, SZ_SMALL, COL_DIM, AL_LEFT, w);
	}

	if (a) {
		// Descripción en tres líneas, una raya fina y la clasificación con las características
		// Sin descripción (algunas demos no tienen): la clasificación y las características suben a su sitio
		float ry = 177;
		if (a->desc) {
			// En un solo párrafo: un salto de línea dejaría la línea a medias
			char d[480];
			size_t dn = 0;
			for (const char *q = a->desc; *q && dn < sizeof d - 1; q++) {
				char c = *q == '\n' ? ' ' : *q;
				if (c == ' ' && (dn == 0 || d[dn - 1] == ' ')) continue;
				d[dn++] = c;
			}
			d[dn] = 0;
			ui_text_wrap(12, 128, TOP_W - 24, d, SZ_SMALL, COL_TXT, 3, 13);
			ui_hline(12, 172, TOP_W - 24, C2D_Color32(0xff, 0xff, 0xff, 0x26));
		} else {
			ry = 132;
		}
		float fx = 12;
		if (a->rating[0]) {
			draw_rating(12, ry, 28, a);
			fx = 48;
		}
		draw_feats(fx, ry + 1, TOP_W - 12 - fx, a, a->desc ? 2 : 4);
	} else if (e && e->failed) {
		ui_text(12, 140, _("No se pudo cargar la página del juego"), SZ_SMALL, COL_DIM, AL_LEFT, 0);
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
		ui_text(TOP_W / 2, 136, _("Toca la barra de búsqueda o pulsa Y para escribir"), SZ_SMALL, COL_DIM, AL_CENTER, 380);
		if (S.nrecents) ui_text(TOP_W / 2, 154, _("Tus búsquedas recientes aparecen abajo"), SZ_SMALL, COL_DIM, AL_CENTER, 380);
		return;
	}
	if (S.tab == TAB_CART && S.cart_qr) {
		char tot[64];
		snprintf(tot, sizeof tot, _("Total: %s"), shop_cart()->subtotal);
		draw_qr_panel("https://store.steampowered.com/cart/", _("Pagar en el móvil"),
			_("Escanea el código para abrir tu carrito de Steam en el móvil y completar la compra."), NULL, tot, COL_LIME);
		return;
	}
	if (S.tab == TAB_CART && shop_cart_loaded() && shop_cart()->n == 0) {
		ui_icon(IC_CART, TOP_W / 2 - 14, 80, 28, COL_ACC);
		ui_text(TOP_W / 2, 120, _("Tu carrito está vacío"), SZ_LARGE, COL_TXT, AL_CENTER, 380);
		ui_text(TOP_W / 2, 144, _("Añade juegos desde su página con el botón del carrito"), SZ_SMALL, COL_DIM, AL_CENTER, 380);
		return;
	}
	if (!row || (row->appid == 0)) {
		if (t->list.err[0] && !t->list.req) {
			int left = retry_delay(t->list.err) - (int)((osGetTime() - t->fail_at) / 1000);
			char tt[64];
			snprintf(tt, sizeof tt, _("Reintentando en %d s · Pulsa A para reintentar ahora"), left < 0 ? 0 : left);
			ui_text(TOP_W / 2, 92, _("No se pudo cargar esta lista"), SZ_LARGE, COL_TXT, AL_CENTER, 0);
			ui_text(TOP_W / 2, 116, human_err(t->list.err), SZ_BODY, COL_TXT, AL_CENTER, 380);
			ui_text(TOP_W / 2, 136, tt, SZ_SMALL, COL_ACC, AL_CENTER, 0);
			ui_text(TOP_W / 2, 200, t->list.err, SZ_TINY, COL_DIM, AL_CENTER, 380);
		} else if (S.tab == TAB_WISHLIST && t->wish_loaded && !t->nwish) {
			ui_icon(IC_HEART, TOP_W / 2 - 14, 80, 28, COL_ACC);
			ui_text(TOP_W / 2, 120, _("Tu lista de deseos está vacía"), SZ_LARGE, COL_TXT, AL_CENTER, 380);
			ui_text(TOP_W / 2, 144, _("Añade juegos desde su página con el botón Deseados"), SZ_SMALL, COL_DIM, AL_CENTER, 380);
		} else if (S.tab == TAB_LIBRARY && !t->list.req && !t->list.count) {
			ui_text(TOP_W / 2, 120, _("No hay juegos en tu biblioteca"), SZ_LARGE, COL_TXT, AL_CENTER, 380);
		} else if (S.tab == TAB_SEARCH && !t->list.req) {
			char tt[112];
			snprintf(tt, sizeof tt, _("No hay resultados para «%s»"), t->term);
			ui_text(TOP_W / 2, 110, tt, SZ_LARGE, COL_TXT, AL_CENTER, 380);
			ui_text(TOP_W / 2, 134, _("Prueba con otros términos o con el nombre en inglés"), SZ_SMALL, COL_DIM, AL_CENTER, 380);
		} else {
			draw_net_wait(TOP_W / 2, 120, _("Cargando…"));
		}
		return;
	}
	draw_preview(row);

	float fe = clampf((osGetTime() - t->sel_changed) / 160.0f, 0, 1);
	if (fe < 1) ui_rect(0, 22, TOP_W, SCR_H - 40, col_alpha(COL_BG, (1 - ease_out_cubic(fe)) * 0.85f));

	char cnt[112], fr[160];
	footer_text(cnt, sizeof cnt);
	float cw = ui_text(TOP_W - 12, SCR_H - 18, cnt, SZ_SMALL, COL_DIM, AL_RIGHT, TOP_W - 24);
	// Con sesión: qué amigos lo tienen o lo están jugando
	if (FR.appid == row->appid && friends_text(fr, sizeof fr)) {
		ui_icon(IC_USERS, 12, SCR_H - 17, 12, FR.playing ? COL_LIME : COL_ACC);
		ui_text(28, SCR_H - 18, fr, SZ_SMALL, FR.playing ? COL_LIME : COL_TXT, AL_LEFT, TOP_W - 52 - cw);
	}
}

static const char *home_sub_name[HOME_PARTS] = {"Más vendidos", "Novedades", "Próximamente", "Gratis"};

// Subcategorías de Inicio: una barra que cuelga de la de pestañas (recta arriba, redonda abajo)
static void draw_subbar(void)
{
	float e = ease_out_cubic(S.sub_anim);
	if (e <= 0.01f) return;
	float x = 10, w = BOT_W - 20, y = LIST_Y - 3 - (1 - e) * SUB_H, h = SUB_H + 1;
	ui_scissor(GFX_BOTTOM, 0, LIST_Y - 3, BOT_W, SUB_H + 4);
	ui_rrect_bottom(x, y, w, h, 9, col_alpha(C2D_Color32(0x1c, 0x28, 0x3a, 0xff), 0.96f));
	ui_rrect_bottom(x, y, w, h, 9, col_alpha(COL_WHITE, 0.05f));
	ui_rect(x + 9, y + h - 1, w - 18, 1, col_alpha(COL_WHITE, 0.10f));
	float sw = w / HOME_PARTS;
	float lh = ui_line_h(SZ_TINY), ty = y + 3 + (h - 3 - lh) / 2;
	// La elegida: un resplandor suave que respira, sin burbuja
	float hx = x + S.sub_x * sw;
	float br = 0.07f + 0.04f * sinf(g_time * 2.4f);
	ui_rrect(hx + 6, y + 4, sw - 12, h - 7, (h - 7) / 2, col_alpha(COL_WHITE, br * e));
	for (int i = 0; i < HOME_PARTS; i++) {
		float on = 1 - clampf(fabsf(S.sub_x - i), 0, 1);
		ui_text(x + sw * i + sw / 2, ty, _(home_sub_name[i]), SZ_TINY, col_alpha(col_lerp(COL_DIM, COL_WHITE, on), e), AL_CENTER, sw - 8);
		if (i) ui_rect(x + sw * i, y + 6, 1, h - 11, col_alpha(COL_WHITE, 0.16f * e));   // la «|» entre ellas
	}
	ui_scissor_off();
}
// Huecos de las pestañas dentro de la cápsula: cuatro iguales para el texto y uno más estrecho
// para la lupa. Los textos se centran en su hueco y la píldora va de hueco en hueco
#define TAB_SLOT_W 64.0f
static void tab_slot(float t, float *x, float *w)
{
	float x0 = 5, last = TAB_COUNT - 1;
	float sw_last = BOT_W - 10 - last * TAB_SLOT_W;
	if (t <= last - 1) {
		*x = x0 + t * TAB_SLOT_W;
		*w = TAB_SLOT_W;
	} else {
		float f = t - (last - 1);   // entre Betas y la lupa
		*x = x0 + t * TAB_SLOT_W;
		*w = TAB_SLOT_W + (sw_last - TAB_SLOT_W) * f;
	}
}

static int tab_at(float x)
{
	int t = (int)((x - 5) / TAB_SLOT_W);
	return t < 0 ? 0 : t >= TAB_COUNT ? TAB_COUNT - 1 : t;
}

static void draw_tabs(void)
{
	// Control segmentado: una cápsula de cristal y, dentro, la píldora de la pestaña que se desliza.
	// En Buscar, con el foco arriba, la píldora (ya pegada a la lupa) se estira hacia la izquierda
	const float by = 2, bh = LIST_Y - 4;
	fx_glass(3, by, BOT_W - 6, bh, bh / 2, 1, 0);
	float px, pw;
	tab_slot(S.tab_x, &px, &pw);
	float se = ease_out_cubic(S.search_x);
	float right = px + pw;
	px += (5 - px) * se;
	pw = right - px;
	fx_select(px + 1, by + 2, pw - 2, bh - 4, (bh - 4) / 2.0f, 1);
	float lh = ui_line_h(SZ_SMALL), ty = by + (bh - lh) / 2 + 1;
	for (int t = 0; t < TAB_COUNT; t++) {
		float x, tw;
		tab_slot(t, &x, &tw);
		float on = 1 - clampf(fabsf(S.tab_x - t), 0, 1);
		u32 c = col_alpha(col_lerp(COL_DIM, COL_WHITE, on), 1 - se);
		if (t == TAB_SEARCH) ui_icon(IC_SEARCH, x + tw / 2 - 6, by + (bh - 12) / 2, 12, c);
		else if (se < 0.99f) ui_text(x + tw / 2, ty, tab_name(t), SZ_SMALL, c, AL_CENTER, tw - 6);
	}
	if (se > 0.05f) {
		// Dentro de la píldora estirada: la lupa a la izquierda, lo buscado y la tecla Y
		Tab *t = &tabs[TAB_SEARCH];
		ui_icon(IC_SEARCH, px + 10, by + (bh - 12) / 2, 12, col_alpha(COL_ACC, se));
		if (t->term[0]) ui_text(px + 28, ty, t->term, SZ_SMALL, col_alpha(COL_WHITE, se), AL_LEFT, pw - 60);
		else ui_text(px + 28, ty, _("Buscar en la tienda…"), SZ_SMALL, col_alpha(COL_DIM, se), AL_LEFT, pw - 60);
		if (se > 0.9f) hint_key(px + pw - 26, by + (bh - 18) / 2, "Y", true);
	}
}

static const Box B_BACKBAR = {0, 0, BOT_W, LIST_Y};

static void draw_account_bar(void)
{
	fx_bar(0, 0, BOT_W, LIST_Y, true);
	if (S.pressed == BT_BACKBAR) ui_rect(0, 0, BOT_W, LIST_Y, col_alpha(COL_WHITE, 0.08f));
	ui_text(10, 4, "‹", SZ_LARGE, COL_ACC, AL_LEFT, 0);
	Tab *t = CUR;
	char title[64];
	if (S.tab == TAB_LIBRARY) snprintf(title, sizeof title, _("Biblioteca · %d juegos"), t->list.count);
	else if (S.tab == TAB_CART) snprintf(title, sizeof title, _("Carrito · %d"), t->list.count);
	else snprintf(title, sizeof title, _("Lista de deseos · %d"), t->list.total);
	ui_icon(S.tab == TAB_LIBRARY ? IC_PAD : S.tab == TAB_CART ? IC_CART : IC_HEART, 28, 6, 12, COL_ACC);
	if (S.tab == TAB_CART && shop_cart()->subtotal[0])
		ui_text(BOT_W - 10, 5, shop_cart()->subtotal, SZ_SMALL, COL_LIME, AL_RIGHT, 0);
	ui_text(46, 5, title, SZ_SMALL, COL_WHITE, AL_LEFT, BOT_W - 56);
}


// Fila elegida: un resaltado suave que respira (sin burbuja)
static void draw_row_sel(float hy)
{
	float br = 0.075f + 0.035f * sinf(g_time * 2.4f);
	ui_rrect(3, hy + 1, BOT_W - 8, ROW_H - 2, 6, col_alpha(COL_WHITE, br));
	ui_rrect(3, hy + 4, 2, ROW_H - 8, 1, col_alpha(COL_ACC, 0.8f));
}

static void draw_store_bottom(void)
{
	Tab *t = CUR;
	if (S.tab >= TAB_COUNT) draw_account_bar();
	else {
		draw_subbar();
		draw_tabs();
	}
	float top = list_top(), lh = list_h();
	ui_scissor(GFX_BOTTOM, 0, top, BOT_W, lh);
	int first = (int)(t->scroll / ROW_H);
	if (t->list.count && cur_row() && !(S.tab == TAB_SEARCH && S.search_focus)) {
		float hy = top + t->sel_y * ROW_H - t->scroll;
		draw_row_sel(hy);
	}
	// Las filas entran una tras otra desde la derecha al llegar la lista o al cambiar de pestaña
	if (t->list.count && !t->anim_count) t->anim_t = g_time;
	t->anim_count = t->list.count;
	C3D_Mtx row_view;
	C2D_ViewSave(&row_view);
	for (int i = first; i < t->list.count && i < first + lh / ROW_H + 2; i++) {
		const StoreRow *r = &t->list.rows[i];
		float y = top + i * ROW_H - t->scroll;
		float re = ease_out_cubic(clampf((g_time - t->anim_t - (i - first) * 0.035f) / 0.28f, 0, 1));
		C2D_ViewRestore(&row_view);
		if (re < 1) C2D_ViewTranslate((1 - re) * 40, 0);
		if (r->header) {
			// Título de apartado (Inicio)
			ui_hline(8, y + ROW_H - 1, BOT_W - 16, C2D_Color32(0xff, 0xff, 0xff, 0x14));
			ui_text(8, y + 7, r->name, SZ_SMALL, COL_ACC, AL_LEFT, BOT_W - 16);
			continue;
		}
		bool on = i == t->sel;
		ui_hline(8, y + ROW_H - 1, BOT_W - 16, C2D_Color32(0xff, 0xff, 0xff, 0x0c));
		if (r->appid == 0) {
			// Búsqueda reciente
			ui_icon(IC_SEARCH, 10, y + 9, 11, COL_DIM);
			ui_text(28, y + 7, r->name, SZ_BODY - 0.02f, on ? COL_WHITE : COL_TXT, AL_LEFT, BOT_W - 40);
			continue;
		}
		float pw = S.tab == TAB_LIBRARY ? 58 : r->disc > 0 ? 36 + 50 : 56;
		float nx = 8;
		if (S.tab == TAB_LIBRARY) {
			// El icono del juego, como en la biblioteca de Steam
			const float is = 20, iy = y + (ROW_H - 1 - is) / 2;
			char iu[160];
			steam_icon_url(r, iu, sizeof iu);
			const Pic *ip = iu[0] ? img_get(iu, 32) : NULL;
			if (ip) img_draw_cover(ip, nx + 2, iy, is, is, 0.5f);
			else ui_rrect(nx + 2, iy, is, is, 4, col_alpha(COL_WHITE, 0.07f));
			nx += is + 10;
		} else if (r->type == 1 || r->type == 4) {
			// Etiqueta pequeña para demos y DLC (en Buscar salen mezclados)
			const char *tag = r->type == 1 ? _("DEMO") : "DLC";
			float tw = ui_text_w(tag, SZ_TINY) + 8;
			ui_rect(nx, y + 8, tw, 13, COL_TAG);
			ui_text(nx + 4, y + 8.5f, tag, SZ_TINY, COL_ACC, AL_LEFT, 0);
			nx += tw + 5;
		}
		ui_text(nx, y + 7, r->name, SZ_BODY - 0.02f, on ? COL_WHITE : COL_TXT, AL_LEFT, BOT_W - 10 - nx - pw);
		if (S.tab == TAB_LIBRARY) {
			char hrs[24];
			if (r->minutes >= 60) snprintf(hrs, sizeof hrs, "%d h", (r->minutes + 30) / 60);
			else if (r->minutes) snprintf(hrs, sizeof hrs, "%d min", r->minutes);
			else snprintf(hrs, sizeof hrs, "–");
			ui_text(BOT_W - 22, y + 8, hrs, SZ_SMALL, COL_DIM, AL_RIGHT, 0);
			if (lib_is_new(r->appid)) {
				// Nuevo: un punto que brilla al otro lado de la barrita, hasta que abres su ficha
				float cx = BOT_W - 12, cy = y + (ROW_H - 1) / 2.0f, pu = 0.5f + 0.5f * sinf(g_time * 3.2f + i);
				ui_circle(cx, cy, 5.5f, col_alpha(COL_ACC, 0.10f + 0.16f * pu));
				ui_circle(cx, cy, 3.2f, col_alpha(COL_ACC, 0.85f + 0.15f * pu));
				ui_circle(cx - 0.8f, cy - 0.8f, 1.1f, col_alpha(COL_WHITE, 0.7f));
			}
		} else if (r->coming_soon && !r->price[0]) {
			ui_text(BOT_W - 10, y + 8, _("Próximamente"), SZ_TINY, COL_DIM, AL_RIGHT, 0);
		} else {
			draw_price_right(BOT_W - 6, y + 2, ROW_H - 5, r, 36, 50);
		}
	}
	C2D_ViewRestore(&row_view);
	float end_y = top + t->list.count * ROW_H - t->scroll;
	if (t->list.req && end_y < SCR_H) ui_spinner(BOT_W / 2, end_y + 14, 6, COL_DIM);
	if (S.tab == TAB_SEARCH && !t->term[0] && !t->list.count)
		ui_text(BOT_W / 2, top + 30, _("No hay búsquedas recientes"), SZ_SMALL, COL_DIM, AL_CENTER, 0);
	if (S.tab == TAB_WISHLIST && t->wish_loaded && !t->nwish)
		ui_text(BOT_W / 2, top + 30, _("Tu lista de deseos está vacía"), SZ_SMALL, COL_DIM, AL_CENTER, 0);
	if (S.tab == TAB_CART) {
		if (shop_cart_loaded() && !shop_cart()->n)
			ui_text(BOT_W / 2, top + 30, _("Tu carrito está vacío"), SZ_SMALL, COL_DIM, AL_CENTER, 0);
		else if (!shop_cart_loaded() && shop_cart_loading())
			ui_spinner(BOT_W / 2, top + 36, 6, COL_DIM);
	}
	ui_scissor_off();
	// Barra de scroll
	float total = t->list.count * ROW_H;
	if (total > lh) {
		float hh = lh * lh / total;
		if (hh < 12) hh = 12;
		float y = top + (lh - hh) * (t->scroll / (total - lh));
		ui_rrect(BOT_W - 4, y + 2, 3, hh - 4, 1.5f, C2D_Color32(0xff, 0xff, 0xff, 0x55));
	}
	if (S.tab == TAB_CART) {
		static const char *hc[] = {"A", "Abrir", "↑↓", "Elegir", "Y", "Quitar", "X", "Pagar", "B", "Volver"};
		static const char *hb[] = {"B", "Volver", "START", "Menú"};
		if (t->list.count) draw_hints(hc, 5);
		else draw_hints(hb, 2);
	} else if (S.tab >= TAB_COUNT) {
		static const char *ha[] = {"A", "Abrir", "↑↓", "Elegir", "B", "Volver", "START", "Menú"};
		draw_hints(ha, 4);
	} else if (S.tab == TAB_SEARCH) {
		if (S.search_focus) {
			static const char *hs[] = {"A", "Escribir", "↓", "Resultados", "L R", "Pestaña", "START", "Menú"};
			static const char *h1[] = {"A", "Escribir", "L R", "Pestaña", "START", "Menú"};
			if (t->list.count) draw_hints(hs, 4);
			else draw_hints(h1, 3);
		} else if (t->term[0]) {
			static const char *hs[] = {"A", "Abrir", "↑↓", "Elegir", "Y", "Buscar", "B", "Borrar", "L R", "Pestaña"};
			draw_hints(hs, 5);
		} else {
			static const char *hs[] = {"A", "Buscar esto", "↑↓", "Elegir", "Y", "Escribir", "L R", "Pestaña"};
			draw_hints(hs, 4);
		}
	} else if (t->list.err[0] && !t->list.req && !t->list.count) {
		static const char *hh3[] = {"A", "Reintentar", "L R", "Pestaña", "START", "Menú"};
		draw_hints(hh3, 3);
	} else if (S.tab == TAB_HOME) {
		static const char *hh4[] = {"A", "Abrir", "↑↓", "Elegir", "← →", "Categoría", "L R", "Pestaña", "START", "Menú"};
		draw_hints(hh4, 5);
	} else {
		static const char *hh2[] = {"A", "Abrir", "↑↓", "Elegir", "← →", "Saltar", "L R", "Pestaña", "START", "Menú"};
		draw_hints(hh2, 5);
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
	if (tab_paged(cur_ti()) && t->sel >= t->list.count - 8) tab_more(cur_ti());
}

// ============================================================================
// Ficha


// ---------------------------------------------------------------------------- Tu cuenta y la ficha

static const StoreRow *owned_row(u32 appid)
{
	Tab *t = &tabs[TAB_LIBRARY];
	for (int i = 0; i < t->list.count; i++)
		if (t->list.rows[i].appid == appid) return &t->list.rows[i];
	return NULL;
}

// ¿Es tuyo? Por la biblioteca, o por la lista de la tienda cuando se usa el plan B
static bool owned_app(u32 appid)
{
	if (owned_row(appid)) return true;
	Tab *t = &tabs[TAB_LIBRARY];
	if (t->lib_stage == 2)
		for (int i = 0; i < t->nwish; i++)
			if (t->wish[i] == appid) return true;
	return false;
}

static bool in_wishlist(u32 appid)
{
	Tab *t = &tabs[TAB_WISHLIST];
	for (int i = 0; i < t->nwish; i++)
		if (t->wish[i] == appid) return true;
	return false;
}

static void friends_reset(void)
{
	if (FR.req) net_release(FR.req);
	if (FR.names) net_release(FR.names);
	memset(&FR, 0, sizeof FR);
}

static void friends_request(u32 appid)
{
	if (FR.appid == appid && (FR.req || FR.done)) return;   // ya pedido (la vista previa y luego la ficha)
	friends_reset();
	if (!auth_logged()) return;
	FR.appid = appid;
	char url[1300];
	snprintf(url, sizeof url, "https://api.steampowered.com/IPlayerService/GetFriendsGameplayInfo/v1/?access_token=%s&appid=%lu",
		auth_token(), (unsigned long)appid);
	FR.req = net_get(url, LANE_API);
}

static void friends_poll(void)
{
	if (FR.req && FR.req->state >= REQ_DONE) {
		cJSON *root = FR.req->state == REQ_DONE ? cJSON_Parse(FR.req->data) : NULL;
		const cJSON *r = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
		const cJSON *ig = r ? cJSON_GetObjectItemCaseSensitive(r, "in_game") : NULL;
		const cJSON *ow = r ? cJSON_GetObjectItemCaseSensitive(r, "owns") : NULL;
		const cJSON *wl = r ? cJSON_GetObjectItemCaseSensitive(r, "in_wishlist") : NULL;
		FR.in_game = cJSON_GetArraySize(ig);
		FR.owns = cJSON_GetArraySize(ow);
		FR.wish = cJSON_GetArraySize(wl);
		// Primero quien lo está jugando ahora; luego quien lo tiene; si nadie, quien lo desea
		const cJSON *lists[] = {ig, ow, wl}, *f;
		for (int k = 0; k < 3; k++) {
			cJSON_ArrayForEach(f, lists[k]) {
				const cJSON *sid = cJSON_GetObjectItemCaseSensitive(f, "steamid");
				u64 id = cJSON_IsString(sid) ? strtoull(sid->valuestring, NULL, 10) : 0;
				bool dup = false;
				for (int i = 0; i < FR.nids; i++) dup |= FR.ids[i] == id;
				if (id && !dup && FR.nids < 3) FR.ids[FR.nids++] = id;
			}
			if (k == 0 && FR.nids) FR.playing = true;
			if (k == 1 && FR.nids) break;
		}
		if (!r) netlog("amigos: sin datos (%s)", FR.req->err[0] ? FR.req->err : "json");
		cJSON_Delete(root);
		net_release(FR.req);
		FR.req = NULL;
		FR.done = true;
		if (FR.nids && auth_logged()) {
			char ids[96] = "", url[1500];
			for (int i = 0; i < FR.nids; i++) {
				char one[32];
				snprintf(one, sizeof one, "%s%%22%llu%%22", i ? "%2C" : "", (unsigned long long)FR.ids[i]);
				strcat(ids, one);
			}
			snprintf(url, sizeof url, "https://api.steampowered.com/IPlayerService/GetPlayerLinkDetails/v1/?access_token=%s&input_json=%%7B%%22steamids%%22%%3A%%5B%s%%5D%%7D",
				auth_token(), ids);
			FR.names = net_get(url, LANE_API);
		}
	}
	if (FR.names && FR.names->state >= REQ_DONE) {
		cJSON *root = FR.names->state == REQ_DONE ? cJSON_Parse(FR.names->data) : NULL;
		const cJSON *r = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
		const cJSON *acc, *accounts = r ? cJSON_GetObjectItemCaseSensitive(r, "accounts") : NULL;
		// Los nombres, en el mismo orden que los pedimos
		for (int i = 0; i < FR.nids; i++) {
			cJSON_ArrayForEach(acc, accounts) {
				const cJSON *pd = cJSON_GetObjectItemCaseSensitive(acc, "public_data");
				const cJSON *sid = pd ? cJSON_GetObjectItemCaseSensitive(pd, "steamid") : NULL;
				const cJSON *nm = pd ? cJSON_GetObjectItemCaseSensitive(pd, "persona_name") : NULL;
				if (cJSON_IsString(sid) && cJSON_IsString(nm) && strtoull(sid->valuestring, NULL, 10) == FR.ids[i])
					snprintf(FR.name[FR.nnames++], 40, "%s", nm->valuestring);
			}
		}
		cJSON_Delete(root);
		net_release(FR.names);
		FR.names = NULL;
	}
}

// «3 amigos lo tienen: Kenji, Lucía y 1 más» / «Kenji lo está jugando ahora»
static bool friends_text(char *out, size_t n)
{
	if (!FR.done || FR.appid != S.app || (!FR.owns && !FR.in_game && !FR.wish)) return false;
	char names[128] = "";
	int shown = FR.nnames < 2 ? FR.nnames : 2;
	for (int i = 0; i < shown; i++) {
		if (i) strcat(names, ", ");
		strncat(names, FR.name[i], sizeof names - strlen(names) - 1);
	}
	if (FR.in_game && FR.nnames && FR.playing) {
		if (FR.in_game == 1) snprintf(out, n, _("%s lo está jugando ahora"), FR.name[0]);
		else snprintf(out, n, _("%s y %d amigos más lo están jugando"), FR.name[0], FR.in_game - 1);
	} else if (FR.owns) {
		int rest = FR.owns - shown;
		if (FR.owns == 1 && shown) snprintf(out, n, _("%s lo tiene"), names);
		else if (rest > 0 && shown) snprintf(out, n, _("%d amigos lo tienen: %s y %d más"), FR.owns, names, rest);
		else if (shown) snprintf(out, n, _("%d amigos lo tienen: %s"), FR.owns, names);
		else snprintf(out, n, _("%d amigos lo tienen"), FR.owns);
	} else {
		if (FR.wish == 1 && shown) snprintf(out, n, _("%s lo tiene en su lista de deseos"), names);
		else snprintf(out, n, _("%d amigos lo tienen en su lista de deseos"), FR.wish);
	}
	return true;
}

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
	seen_open(appid);
	S.sec = SEC_SUMMARY;
	S.show_qr = false;
	S.pick_open = false;
	S.oscroll = S.oscroll_t = 0;
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
	friends_request(appid);
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
	friends_poll();
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
		if (a->fullgame) S.dlc.req = steam_req_items(&a->fullgame, 1, false);
		else if (a->ndlc || a->demo) {
			// La demo (si la hay) va primero, y luego el contenido descargable
			u32 ids[51];
			int n = 0;
			if (a->demo) ids[n++] = a->demo;
			for (int i = 0; i < a->ndlc && n < 51; i++) ids[n++] = a->dlc[i];
			S.dlc.req = steam_req_items(ids, n > 50 ? 50 : n, false);
		}
	}
	if (S.sec == SEC_SIMILAR && S.similar_for != a->appid) {
		rowlist_free(&S.similar);
		S.similar_for = a->appid;
		S.similar.req = steam_req_morelike(a->appid);
	}
	list_poll(&S.dlc, false);
	list_poll(&S.similar, false);
}

// «Acerca de este juego» partido en líneas una sola vez (luego solo se dibujan las que se ven)
typedef struct { const char *p; u16 len; u8 kind; } ALine;   // kind: 0 texto, 1 título, 2 hueco
#define MAX_ALINES 700
static ALine alines[MAX_ALINES];
static int nalines;
static u32 alines_for;

static void about_add(const char *p, size_t len, int kind)
{
	if (nalines < MAX_ALINES) alines[nalines++] = (ALine){p, (u16)len, (u8)kind};
}

static void about_wrap(const char *p, size_t n, float sz, int kind, float w)
{
	char buf[1024];
	while (n > 0) {
		// ui_next_line necesita el texto terminado en '\0': se mide sobre una copia del párrafo
		size_t take = n < sizeof buf - 1 ? n : sizeof buf - 1;
		memcpy(buf, p, take);
		buf[take] = 0;
		size_t len, used = ui_next_line(buf, sz, w, &len);
		if (!used) break;
		about_add(p, len, kind);
		p += used;
		n -= used;
	}
}

static void about_layout(const AppInfo *a)
{
	if (alines_for == a->appid) return;
	alines_for = a->appid;
	nalines = 0;
	float w = TOP_W - 24;
	if (a->desc) {
		about_wrap(a->desc, strlen(a->desc), SZ_SMALL, 0, w);
		about_add(NULL, 0, 2);
	}
	if (!a->about) return;
	about_add(NULL, 0, 2);
	const char *p = a->about;
	while (*p) {
		const char *e = strchr(p, '\n');
		size_t len = e ? (size_t)(e - p) : strlen(p);
		if (!len) {
			if (nalines && alines[nalines - 1].kind != 2) about_add(NULL, 0, 2);
		} else if (p[0] == '\x01') {
			if (nalines && alines[nalines - 1].kind != 2) about_add(NULL, 0, 2);
			about_wrap(p + 1, len - 1, SZ_BODY, 1, w);
		} else {
			about_wrap(p, len, SZ_SMALL, 0, w);
		}
		p += len + (e ? 1 : 0);
	}
}

static void draw_summary_top(const AppInfo *a)
{
	about_layout(a);
	// Todo el Resumen se desplaza: imagen, nombre, etiquetas, clasificación, descripción y «acerca de»
	ui_scissor(GFX_TOP, 0, 18, TOP_W, SCR_H - 18);
	float oy = -floorf(S.oscroll);
	const Pic *p = img_get(a->header, 460);
	if (p) img_draw_cover(p, 0, 18 + oy, TOP_W, 110, 0.3f);
	else ui_rect(0, 18 + oy, TOP_W, 110, COL_PANEL);
	// La imagen se funde con el fondo (sin línea de corte)
	ui_vgrad(0, 60 + oy, TOP_W, 69, C2D_Color32(0x0c, 0x13, 0x1f, 0x00), C2D_Color32(0x0c, 0x13, 0x1f, 0xe0));
	ui_text(12, 104 + oy, a->name, SZ_TITLE, COL_WHITE, AL_LEFT, TOP_W - 24);

	// Debajo de la imagen: géneros y reseñas en la misma línea
	float x = 12, y = 134 + oy;
	for (int i = 0; i < a->ngenres; i++) {
		float tw = ui_text_w(a->genres[i], SZ_TINY) + 14;
		if (x + tw > TOP_W / 2) break;
		draw_tag(&x, y, a->genres[i]);
	}
	if (a->review_label[0]) draw_review_line(x + 4, y + 0.5f, TOP_W - 12 - x - 4, a->review_label, a->review_pct, a->metacritic);

	y = 156 + oy;
	float fx = 12;
	float block = y;
	if (a->rating[0]) {
		draw_rating(12, y, 32, a);
		fx = 52;
		block = y + 32 * 1.2f;
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
	y += draw_feats(fx, y, TOP_W - 12 - fx, a, 3);
	if (y < block) y = block;
	y += 6;

	// Qué amigos lo tienen (a la izquierda) y fecha · estudio, siempre a la derecha
	char t[112], fr[160];
	snprintf(t, sizeof t, "%s%s%s", a->release, a->release[0] && a->developer[0] ? " · " : "", a->developer);
	if (friends_text(fr, sizeof fr)) {
		ui_icon(IC_USERS, 12, y, 12, FR.playing ? COL_LIME : COL_ACC);
		float fw = ui_text(28, y, fr, SZ_SMALL, FR.playing ? COL_LIME : COL_TXT, AL_LEFT, 230);
		ui_text(TOP_W - 12, y, t, SZ_SMALL, COL_DIM, AL_RIGHT, TOP_W - 52 - fw);
	} else {
		ui_text(TOP_W - 12, y, t, SZ_SMALL, COL_DIM, AL_RIGHT, TOP_W - 24);
	}
	y += 18;
	ui_hline(12, y, TOP_W - 24, C2D_Color32(0xff, 0xff, 0xff, 0x26));
	y += 8;

	// Descripción y «acerca de este juego» (solo las líneas que se ven)
	for (int i = 0; i < nalines; i++) {
		const ALine *l = &alines[i];
		float lh = l->kind == 1 ? 17 : l->kind == 2 ? 7 : 13;
		if (y + lh > 18 && y < SCR_H && l->kind != 2) {
			char buf[512];
			size_t n = l->len < sizeof buf - 1 ? l->len : sizeof buf - 1;
			memcpy(buf, l->p, n);
			buf[n] = 0;
			if (l->kind == 1) ui_text(12, y, buf, SZ_BODY, COL_ACC, AL_LEFT, 0);
			else ui_text(12, y, buf, SZ_SMALL, COL_TXT, AL_LEFT, 0);
		}
		y += lh;
	}
	ui_scissor_off();
	S.ocontent = y - oy - 18 + 10;

	// Barra de desplazamiento fina
	float vis = SCR_H - 18;
	if (S.ocontent > vis) {
		float hh = vis * vis / S.ocontent;
		if (hh < 14) hh = 14;
		float sy = 18 + (vis - hh) * (S.oscroll / (S.ocontent - vis));
		ui_rrect(TOP_W - 4, sy + 2, 3, hh - 4, 1.5f, C2D_Color32(0xff, 0xff, 0xff, 0x55));
	}
}
static float draw_req_col(float x, float y, float w, const char *title, const ReqLine *l, int n)
{
	ui_text(x, y, title, SZ_SMALL, COL_ACC, AL_LEFT, w);
	y += 15;
	for (int i = 0; i < n; i++) {
		// La clave en gris y el valor en claro, con sangría colgante si ocupa más de una línea
		char k[32];
		snprintf(k, sizeof k, "%s:", l[i].key);
		float kw = ui_text(x, y, k, SZ_TINY, COL_DIM, AL_LEFT, w * 0.45f) + 3;
		int lines = ui_text_wrap(x + kw, y, w - kw, l[i].val, SZ_TINY, COL_TXT, 3, 12);
		y += lines * 12 + 1;
	}
	return y;
}

static void draw_reqs_top(const AppInfo *a)
{
	// «Nombre requisitos del sistema (Windows)»: una sola frase, con el nombre más grande
	bool mac = g_cfg.reqs_mac && a->mac;
	float x = 12 + ui_text(12, 24, a->name, SZ_LARGE, COL_WHITE, AL_LEFT, 220);
	ui_text(x + 6, 27, mac ? _("requisitos del sistema (macOS)") : _("requisitos del sistema (Windows)"), SZ_SMALL, COL_DIM, AL_LEFT, TOP_W - 18 - x);
	const ReqLine *mn = mac ? a->mmin : a->rmin, *rc = mac ? a->mrec : a->rrec;
	int nmn = mac ? a->nmmin : a->nmin, nrc = mac ? a->nmrec : a->nrec;
	if (!nmn && !nrc) {
		ui_text(TOP_W / 2, 110, _("No hay requisitos del sistema disponibles para este juego"), SZ_BODY, COL_DIM, AL_CENTER, 0);
	} else {
		if (nmn) draw_req_col(12, 46, nrc ? 182 : 376, _("MÍNIMO"), mn, nmn);
		if (nrc) draw_req_col(nmn ? 206 : 12, 46, nmn ? 182 : 376, _("RECOMENDADO"), rc, nrc);
	}
}
// Alto de la tarjeta de una opinión: según las líneas de texto que tenga (hasta tres)
static float review_h(int i)
{
	int n = S.rtext[i].n < 1 ? 1 : S.rtext[i].n;
	return 26 + 12 * n;
}

static void draw_reviews_top(const AppInfo *a)
{
	if (a->review_label[0]) {
		ui_text(12, 24, a->review_label, SZ_BODY, COL_WHITE, AL_LEFT, 0);
		char t[64];
		snprintf(t, sizeof t, _("%d %% de %d reseñas"), a->review_pct, a->review_count);
		ui_text(TOP_W - 12, 25, t, SZ_SMALL, COL_DIM, AL_RIGHT, 0);
		float bw = (TOP_W - 24) * a->review_pct / 100.0f;
		ui_rrect(12, 42, TOP_W - 24, 4, 2, col_alpha(COL_NEG, 0.8f));
		ui_rrect(12, 42, bw, 4, 2, COL_ACC);
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
		float h = review_h(i);
		if (y + h > top && y < bottom) {
			bool on = i == S.rsel;
			ui_rrect(12, y, TOP_W - 24, h, 10, col_alpha(COL_WHITE, on ? 0.10f + 0.035f * sinf(g_time * 2.4f) : 0.045f));
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
	ui_rrect(10, hy, TOP_W - 20, rh - 1, 6, col_alpha(COL_WHITE, 0.075f + 0.035f * sinf(g_time * 2.4f)));
	ui_rrect(10, hy + 3, 2, rh - 7, 1, col_alpha(COL_ACC, 0.8f));
	for (int i = 0; i < l->count; i++) {
		float y = top + i * rh - S.lscroll;
		if (y + rh < top || y > bottom) continue;
		const StoreRow *r = &l->rows[i];
		bool on = i == S.lsel;
		float pw = r->disc > 0 ? 36 + 52 : 56;
		float nx = 20;
		if (r->type == 1 || r->type == 4) {
			const char *tag = r->type == 1 ? _("DEMO") : "DLC";
			float tw = ui_text_w(tag, SZ_TINY) + 8;
			ui_rrect(nx, y + 5, tw, 12, 6, col_alpha(COL_ACC, 0.18f));
			ui_text(nx + 4, y + 5, tag, SZ_TINY, COL_ACC, AL_LEFT, 0);
			nx += tw + 5;
		}
		float nw = ui_text(nx, y + 4, r->name, SZ_SMALL, on ? COL_WHITE : COL_TXT, AL_LEFT, TOP_W - 20 - nx - pw) + nx - 20;
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
			snprintf(t, sizeof t, _("Reintentando en %d s · Pulsa A para reintentar ahora"), left < 0 ? 0 : left);
			ui_text(TOP_W / 2, 92, _("No se pudo cargar la página del juego"), SZ_LARGE, COL_TXT, AL_CENTER, 0);
			ui_text(TOP_W / 2, 116, human_err(e->err), SZ_BODY, COL_TXT, AL_CENTER, 380);
			ui_text(TOP_W / 2, 136, t, SZ_SMALL, COL_ACC, AL_CENTER, 0);
			ui_text(TOP_W / 2, 200, e->err, SZ_TINY, COL_DIM, AL_CENTER, 380);
		} else {
			draw_net_wait(TOP_W / 2, 120, _("Cargando…"));
		}
		return;
	}
	const AppInfo *a = &e->info;
	if (S.show_qr) {
		char url[96];
		snprintf(url, sizeof url, "https://store.steampowered.com/app/%lu/", (unsigned long)a->appid);
		draw_qr_panel(url, _("Ver en Steam"),
			_("Escanea el código con la cámara del móvil para abrir la página de este juego en la tienda de Steam."), a, NULL, 0);
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
				ui_text_wrap(12, 72, TOP_W - 24, a->is_demo ? _("Esta es una demo del juego completo.")
					: _("Para usar este contenido descargable necesitas tener el juego base en tu biblioteca."),
					SZ_SMALL, COL_DIM, 3, 14);
			break;
		}
		char t[64];
		if (a->demo) snprintf(t, sizeof t, _("DEMO Y CONTENIDO DESCARGABLE (%d)"), a->ndlc + 1);
		else snprintf(t, sizeof t, _("CONTENIDO DESCARGABLE (%d)"), a->ndlc);
		draw_list_top(t, &S.dlc, _("Este juego no tiene contenido descargable"));
		break;
	}
	case SEC_SIMILAR: {
		char t[160];
		snprintf(t, sizeof t, _("MÁS COMO %s"), a->name);
		draw_list_top(t, &S.similar, _("No hay juegos similares disponibles"));
		break;
	}
	}
	C2D_ViewRestore(&view);
	if (se < 1) ui_rect(0, 18, TOP_W, SCR_H - 18, col_alpha(COL_BG, (1 - se) * 0.9f));
}

// Botones de la ficha (también se usan para el toque)
static const Box B_SEG = {12, 6, 296, 22};
// Botones de la ficha: se colocan según haya o no botón de demo (game_layout)
static Box B_CART = {12, 84, 296, 26};
static Box B_DEMO = {12, 112, 296, 22};
static Box B_WISH = {12, 116, 145, 26};
static Box B_SHOTS = {163, 116, 145, 26};
static Box B_INST = {12, 148, 296, 26};

static void game_layout(const AppInfo *a)
{
	S.has_demo_btn = auth_logged() && a->demo && !a->is_demo;
	if (S.has_demo_btn) {
		B_CART = (Box){12, 82, 296, 24};
		B_DEMO = (Box){12, 110, 296, 22};
		B_WISH = (Box){12, 138, 145, 24};
		B_SHOTS = (Box){163, 138, 145, 24};
		B_INST = (Box){12, 166, 296, 24};
	} else {
		B_CART = (Box){12, 84, 296, 26};
		B_WISH = (Box){12, 116, 145, 26};
		B_SHOTS = (Box){163, 116, 145, 26};
		B_INST = (Box){12, 148, 296, 26};
	}
}

static bool hit(const Box *b, int x, int y)
{
	return x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h;
}

// Botón de cristal con su icono y, si se puede pulsar también con un botón de la consola, la
// tecla delante (key); focus: elegido con la cruceta (reflejo de color en el borde)
static void draw_button_k(const Box *b, const char *key, bool focus, Icon ic, const char *label, u32 top, u32 bot, u32 fg, float alpha, bool pressed)
{
	// Los botones de color (principales) son cristal teñido; los demás, cristal neutro
	bool flat = top == bot;
	FxButton fb = {flat ? 0 : col_lerp(top, bot, 0.5f), alpha, pressed, focus};
	fx_button(b->x, b->y, b->w, b->h, b->h / 2, &fb);
	float kw = key ? hint_key(0, 0, key, false) + 5 : 0;
	float tw = kw + ui_text_w(label, SZ_SMALL) + 18;
	if (tw > b->w - 16) tw = b->w - 16;
	float x = b->x + (b->w - tw) / 2;
	u32 f = col_alpha(fg, alpha < 1 ? 0.5f : 1);
	float dy = pressed ? 1 : 0;
	float ty = b->y + (b->h - ui_line_h(SZ_SMALL)) / 2 + 1 + dy;
	if (key && alpha >= 1) hint_key(x, b->y + (b->h - 18) / 2 + dy, key, true);
	x += kw;
	ui_icon(ic, x, b->y + (b->h - 12) / 2 + dy, 12, f);
	ui_text(x + 18, ty, label, SZ_SMALL, f, AL_LEFT, b->x + b->w - 8 - (x + 18));
}


// Gratis: el juego lo es, es una demo, o la única forma de tenerlo es una licencia gratuita
static bool game_is_free(const AppInfo *a, const BuyOpt *opts, int nopt)
{
	if (a->is_free || a->is_demo) return true;
	return nopt > 0 && opts[0].free && nopt == 1;
}

// Elegir edición: un panel que sube desde abajo, como el menú de START
#define PICK_ROW 32
static float pick_y(int n)
{
	return SCR_H - (34 + n * PICK_ROW + 10);
}

static void draw_pick(const AppInfo *a)
{
	float an = ease_out_cubic(S.pick_anim);
	if (an <= 0.01f) return;
	int n = 0;
	const BuyOpt *o = shop_buyopts(a->appid, &n, NULL);
	if (!o || !n) return;
	ui_rect(0, 0, BOT_W, SCR_H, C2D_Color32(0, 0, 0, (u8)(0x90 * an)));
	float y0 = pick_y(n) + (1 - an) * 60;
	fx_panel(6, y0, BOT_W - 12, SCR_H - y0 + 16, 16, an);
	ui_rrect(BOT_W / 2 - 16, y0 + 6, 32, 3, 1.5f, col_alpha(COL_DIM, an * 0.6f));
	ui_text(18, y0 + 12, _("Elige la edición"), SZ_BODY, col_alpha(COL_WHITE, an), AL_LEFT, BOT_W - 36);
	for (int i = 0; i < n; i++) {
		float y = y0 + 34 + i * PICK_ROW;
		bool on = i == S.pick_sel;
		if (on) fx_select(12, y, BOT_W - 24, PICK_ROW - 3, 10, an);
		StoreRow pr = {0};
		pr.disc = o[i].disc;
		snprintf(pr.price, sizeof pr.price, "%s", o[i].price);
		snprintf(pr.orig, sizeof pr.orig, "%s", o[i].orig);
		pr.free = o[i].free;
		float pw = pr.disc > 0 ? 36 + 50 : ui_text_w(pr.price, SZ_BODY) + 10;
		ui_text(20, y + 8, o[i].name, SZ_BODY - 0.02f, col_alpha(on ? COL_WHITE : COL_TXT, an), AL_LEFT, BOT_W - 44 - pw);
		if (an > 0.95f) draw_price_right(BOT_W - 18, y + 2, PICK_ROW - 7, &pr, 36, 50);
	}
}

static void draw_game_bottom(void)
{
	InfoEnt *e = info_get(S.app, false);
	const AppInfo *a = (e && e->done) ? &e->info : NULL;

	// Selector de apartados
	fx_glass(B_SEG.x, B_SEG.y, B_SEG.w, B_SEG.h, B_SEG.h / 2, 1, 0);
	static const char *secs[] = {"Resumen", "Requisitos", "Opiniones", "Extras", "Similares"};
	float sw = (B_SEG.w - 4) / SEC_COUNT;
	// La píldora del apartado se desliza hasta su sitio
	{
		float px = B_SEG.x + 2 + S.seg_x * sw;
		fx_select(px, B_SEG.y + 2, sw, B_SEG.h - 4, (B_SEG.h - 4) / 2, 1);
	}
	for (int i = 0; i < SEC_COUNT; i++) {
		float x = B_SEG.x + 2 + i * sw;
		float on = 1 - clampf(fabsf(S.seg_x - i), 0, 1);
		ui_text(x + sw / 2, B_SEG.y + (B_SEG.h - ui_line_h(SZ_TINY)) / 2 + 1, _(secs[i]), SZ_TINY, col_lerp(COL_DIM, COL_WHITE, on), AL_CENTER, sw - 4);
	}

	if (a) {
		game_layout(a);
		bool in = auth_logged();
		const StoreRow *own_row = in ? owned_row(a->appid) : NULL;
		bool own = in && owned_app(a->appid);
		bool wished = in && in_wishlist(a->appid);
		// Si ya es tuyo, lo dice la línea de arriba (con tus horas); el precio se ve siempre igual,
		// porque puedes querer comprarlo para regalarlo
		char top[64];
		if (own && own_row && own_row->minutes >= 60)
			snprintf(top, sizeof top, _("En tu biblioteca · %d h jugadas"), (own_row->minutes + 30) / 60);
		else snprintf(top, sizeof top, "%s", own ? _("En tu biblioteca") : a->is_free ? _("Jugar") : _("Comprar"));
		ui_text(12, 36, top, SZ_SMALL, own ? COL_LIME : COL_DIM, AL_LEFT, 0);
		ui_text(12, 50, a->name, SZ_BODY, COL_WHITE, AL_LEFT, a->disc ? 160 : 190);
		int nopt = 0;
		const BuyOpt *opts = shop_buyopts(a->appid, &nopt, NULL);
		if (a->disc > 0) {
			ui_badge_disc(308 - 64 - 52, 36, 52, 32, a->disc, SZ_LARGE);
			ui_price_box(308 - 64, 36, 64, 32, a->orig, a->price, SZ_SMALL, SZ_BODY + 0.05f);
			// Cuenta atrás hasta que acaba la oferta (con la hora de los servidores de Steam)
			u32 end = opts && nopt ? opts[0].disc_end : 0;
			if (end && g_server_known) {
				long long left = (long long)end - ((long long)time(NULL) + g_server_delta);
				if (left > 0) {
					char cd[64], t[80];
					int d = (int)(left / 86400), h = (int)(left % 86400 / 3600), m = (int)(left % 3600 / 60), sec = (int)(left % 60);
					if (d) snprintf(cd, sizeof cd, "%d d %02d:%02d:%02d", d, h, m, sec);
					else snprintf(cd, sizeof cd, "%02d:%02d:%02d", h, m, sec);
					snprintf(t, sizeof t, _("La oferta acaba en %s"), cd);
					ui_icon(IC_COIN, 308 - ui_text_w(t, SZ_TINY) - 16, 70.5f, 10, COL_LIME);
					ui_text(308, 70, t, SZ_TINY, COL_LIME, AL_RIGHT, 0);
				}
			}
		} else {
			const char *p = a->is_free ? _("Gratis") : a->has_price ? a->price : a->coming_soon ? _("Próximamente") : _("No disponible");
			float w = ui_text_w(p, SZ_BODY + 0.05f) + 16;
			ui_rect(308 - w, 36, w, 32, COL_SHADE);
			ui_text(308 - 8, 44, p, SZ_BODY + 0.05f, a->is_free ? COL_LIME : COL_TXT, AL_RIGHT, 0);
		}
		bool free = game_is_free(a, opts, nopt);
		bool carted = in && !own && shop_in_cart(a->appid, opts, nopt);
		bool busy_cart = shop_busy_with(free ? SH_FREE : SH_CART_ADD, a->appid);
		// Sin forma de comprarlo (aún no ha salido o no se vende aquí): el botón lo dice y se apaga
		bool soon = in && !own && !free && !carted && opts && nopt == 0;
		const char *cl = own ? _("Ya está en tu biblioteca") : free ? _("Añadir a la biblioteca") : carted ? _("En el carrito · Ver carrito")
			: soon ? (a->coming_soon ? _("Próximamente") : _("No está a la venta")) : _("Añadir al carrito");
		if (busy_cart) cl = free ? _("Añadiendo a tu biblioteca…") : _("Añadiendo al carrito…");
		if (!in) draw_button_k(&B_CART, "Y", false, IC_PHONE, _("Ver en Steam"), C2D_Color32(0x75, 0xb0, 0x22, 0xff), C2D_Color32(0x58, 0x8a, 0x1b, 0xff),
			COL_WHITE, 1, S.pressed == BT_CART);
		else if (carted) draw_button_k(&B_CART, "Y", false, IC_CART, cl, C2D_Color32(0x3a, 0x6f, 0x96, 0xff), C2D_Color32(0x2a, 0x55, 0x78, 0xff), COL_WHITE, 1, S.pressed == BT_CART);
		else draw_button_k(&B_CART, in && !own && !soon ? "Y" : NULL, false, IC_CART, cl, C2D_Color32(0x75, 0xb0, 0x22, 0xff), C2D_Color32(0x58, 0x8a, 0x1b, 0xff), C2D_Color32(0xd2, 0xef, 0xa9, 0xff),
			in && !own && !soon ? 1 : 0.45f, S.pressed == BT_CART);
		if (S.has_demo_btn) {
			// La demo gratis del juego, en el azul claro que usa Steam para «Añadir a la biblioteca»
			bool demo_own = owned_app(a->demo);
			bool busy = shop_busy_with(SH_FREE, a->demo);
			const char *dl = demo_own ? _("La demo ya está en tu biblioteca") : busy ? _("Añadiendo a tu biblioteca…") : _("Añadir la demo a tu biblioteca");
			draw_button_k(&B_DEMO, demo_own ? NULL : "SELECT", false, IC_PAD, dl, C2D_Color32(0x67, 0xc1, 0xf5, 0xff), C2D_Color32(0x41, 0x7a, 0xa6, 0xff),
				COL_WHITE, demo_own ? 0.45f : 1, S.pressed == BT_DEMO);
		}
		bool busy_wish = shop_busy_with(wished ? SH_WISH_REMOVE : SH_WISH_ADD, a->appid);
		if (wished) draw_button_k(&B_WISH, own ? NULL : "X+←", false, IC_HEART, busy_wish ? _("Quitando…") : _("En deseados"), C2D_Color32(0x3a, 0x6f, 0x96, 0xff), C2D_Color32(0x2a, 0x55, 0x78, 0xff), COL_WHITE, 1, S.pressed == BT_WISH);
		else draw_button_k(&B_WISH, in && !own ? "X+←" : NULL, false, IC_HEART, busy_wish ? _("Añadiendo…") : _("Deseados"), COL_BTN2, COL_BTN2, COL_ACC, in && !own ? 1 : 0.45f, S.pressed == BT_WISH);
		char shots[40];
		snprintf(shots, sizeof shots, _("Capturas (%d)"), a->nshots);
		draw_button_k(&B_SHOTS, a->nshots ? "X+→" : NULL, false, IC_IMAGE, shots, COL_BTN2, COL_BTN2, COL_ACC, a->nshots ? 1 : 0.45f, S.pressed == BT_SHOTS);
		draw_button_k(&B_INST, own ? "X+↓" : NULL, false, IC_PC, _("Instalar en tu PC de forma remota"), C2D_Color32(0x47, 0xbf, 0xff, 0xff), C2D_Color32(0x1a, 0x44, 0xc2, 0xff), COL_WHITE, own ? 1 : 0.4f, S.pressed == BT_INST);
		float ny = B_INST.y + B_INST.h + 5;
		if (!in) ui_text(BOT_W / 2, ny, _("Inicia sesión (START) para comprar y usar tu lista de deseos"), SZ_TINY, COL_DIM, AL_CENTER, 300);
		else if (!own) ui_text(BOT_W / 2, ny, _("La instalación remota requiere tener el juego en tu biblioteca"), SZ_TINY, COL_DIM, AL_CENTER, 300);
	}

	if (S.show_qr) {
		static const char *h[] = {"B", "Volver"};
		draw_hints(h, 1);
	} else if (S.sec >= SEC_EXTRAS) {
		static const char *h[] = {"↑↓", "Elegir", "A", "Abrir", "L R", "Apartado", "B", "Volver", "START", "Menú"};
		draw_hints(h, 5);
	} else if (S.sec == SEC_REVIEWS) {
		static const char *h[] = {"↑↓", "Leer", "L R", "Apartado", "B", "Volver", "START", "Menú"};
		draw_hints(h, 4);
	} else if (S.sec == SEC_SUMMARY) {
		static const char *h[] = {"↑↓", "Desplazar", "L R", "Apartado", "B", "Volver", "START", "Menú"};
		draw_hints(h, 4);
	} else {
		static const char *h[] = {"L R", "Apartado", "B", "Volver", "START", "Menú"};
		draw_hints(h, 3);
	}
	if (a) draw_pick(a);
}

// ============================================================================
// Capturas

static void draw_shots_top(void)
{
	InfoEnt *e = info_get(S.app, false);
	ui_rect(0, 0, TOP_W, SCR_H, C2D_Color32(0, 0, 0, 0xff));
	if (!e || !e->done || e->info.nshots == 0) return;
	const char *url = e->info.shots[S.shot];
	const Pic *p = img_get(url, 400);   // la pantalla de arriba mide 400
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
	if (S.shot + 1 < e->info.nshots) img_get(e->info.shots[S.shot + 1], 400);
	if (S.shot > 0) img_get(e->info.shots[S.shot - 1], 400);
}

#define THUMB_W 92
#define THUMB_H 52
#define THUMB_STEP 100
#define STRIP_Y 76
static const Box B_PREV = {12, 156, 140, 34};
static const Box B_NEXT = {168, 156, 140, 34};

static void draw_round_button(const Box *b, const char *key, const char *label, bool enabled, bool pressed)
{
	FxButton fb = {0, enabled ? 1 : 0.45f, pressed, false};
	fx_button(b->x, b->y, b->w, b->h, b->h / 2, &fb);
	float kw = hint_key(0, 0, key, false) + 5;
	float tw = kw + ui_text_w(label, SZ_BODY);
	float x = b->x + (b->w - tw) / 2, dy = pressed ? 1 : 0;
	if (enabled) hint_key(x, b->y + (b->h - 18) / 2 + dy, key, true);
	ui_text(x + kw, b->y + (b->h - ui_line_h(SZ_BODY)) / 2 + 1 + dy, label, SZ_BODY, col_alpha(COL_WHITE, enabled ? 1 : 0.4f), AL_LEFT, 0);
}

static void draw_shots_bottom(void)
{
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
			if (i == S.shot) fx_select(x0 - 4, y0 - 4, w + 8, hh + 8, 6, on);
			ui_rect(x0, y0, w, hh, C2D_Color32(0x0b, 0x10, 0x16, 0xff));
			const Pic *p = img_get(a->shots[i], 150);
			if (p) img_draw_fit_a(p, x0, y0, w, hh, 0.45f + 0.55f * on);
			else ui_spinner(cx, y0 + hh / 2, 5, COL_DIM);
		}
		draw_round_button(&B_PREV, "L", _("Anterior"), S.shot > 0, S.pressed == BT_PREV);
		draw_round_button(&B_NEXT, "R", _("Siguiente"), S.shot + 1 < a->nshots, S.pressed == BT_NEXT);
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
	fx_panel(x0, y0, 264, 156, 20, a);
	ui_circle(cx, y0 + 30, 17, col_alpha(C2D_Color32(0xff, 0xff, 0xff, 0x14), a));
	ui_icon(IC_POWER, cx - 9, y0 + 21, 18, col_alpha(C2D_Color32(0xff, 0x7a, 0x6a, 0xff), a));
	ui_text(cx, y0 + 54, _("¿Salir de Steam3DS?"), SZ_LARGE, col_alpha(COL_WHITE, a), AL_CENTER, 240);
	ui_text(cx, y0 + 78, _("Volverás al menú HOME."), SZ_SMALL, col_alpha(COL_DIM, a), AL_CENTER, 240);

	const char *gb = ui_button_glyph("B"), *ga = ui_button_glyph("A");
	char no[48], yes[48];
	snprintf(no, sizeof no, "%s%s%s", gb ? gb : "", gb ? " " : "", _("Cancelar"));
	snprintf(yes, sizeof yes, "%s%s%s", ga ? ga : "", ga ? " " : "", _("Salir"));
	bool pn = S.pressed == BT_EXIT_NO, py = S.pressed == BT_EXIT_YES;
	FxButton bn = {0, a, pn, false};
	fx_button(B_EXIT_NO.x, B_EXIT_NO.y, B_EXIT_NO.w, B_EXIT_NO.h, 17, &bn);
	ui_text(B_EXIT_NO.x + B_EXIT_NO.w / 2, B_EXIT_NO.y + 9 + (pn ? 1 : 0), no, SZ_BODY, col_alpha(COL_WHITE, a), AL_CENTER, 104);
	FxButton by = {C2D_Color32(0xd8, 0x44, 0x36, 0xff), a, py, false};
	fx_button(B_EXIT_YES.x, B_EXIT_YES.y, B_EXIT_YES.w, B_EXIT_YES.h, 17, &by);
	ui_text(B_EXIT_YES.x + B_EXIT_YES.w / 2, B_EXIT_YES.y + 9 + (py ? 1 : 0), yes, SZ_BODY, col_alpha(COL_WHITE, a), AL_CENTER, 104);
	C2D_ViewRestore(&view);
}

static int menu_items(int *out);
static float menu_y(int n);

static void draw_overlays_bottom(void)
{
	if (S.toast_t > 0) {
		// Sube desde abajo y se apaga al final
		float in = ease_out_cubic((2.6f - S.toast_t) / 0.25f);
		float a = clampf(S.toast_t / 0.3f, 0, 1) * in;
		float pad = S.toast_ic >= 0 ? 44 : 28;
		float w = ui_text_w(S.toast, SZ_SMALL) + pad;
		// Los avisos largos van en dos líneas en vez de cortarse
		bool two = w > 300;
		if (two) w = 300;
		float th = two ? 40 : 26;
		float base = two ? 176 : 190;
		// Con el menú de START o el de ediciones abiertos, el aviso sube por encima del panel
		if (S.menu_anim > 0.01f) {
			int items[8];
			base = menu_y(menu_items(items)) - th - 6;
			if (base < 4) base = 4;
		}
		float x = (BOT_W - w) / 2, y = base + (1 - in) * 16;
		fx_panel(x, y, w, th, 13, a);
		if (S.toast_ic >= 0) ui_icon((Icon)S.toast_ic, x + 12, y + (th - 12) / 2, 12, col_alpha(COL_ACC, a));
		if (two) ui_text_wrap(x + pad - 14, y + 6, w - pad, S.toast, SZ_SMALL, col_alpha(COL_WHITE, a), 2, 14);
		else ui_text(x + pad - 14, y + 5, S.toast, SZ_SMALL, col_alpha(COL_WHITE, a), AL_LEFT, w - pad);
	}
	if (S.exit_anim > 0.01f) draw_exit_dialog();
}



// ============================================================================
// Menú de START e inicio de sesión

static bool key_repeat(u32 down, u32 held, u32 mask);

enum { MI_LOGIN, MI_PROFILE, MI_LIBRARY, MI_WISHLIST, MI_CART, MI_LOGOUT, MI_REQS, MI_ABOUT, MI_EXIT };
#define MENU_ROW 24
#define MENU_HEAD 38

static int menu_items(int *out)
{
	int n = 0;
	if (auth_info()->state == AUTH_STARTING) {
		// Hay sesión guardada pero aún no conecta (sin red): no se ofrece iniciar otra
		out[n++] = MI_LOGOUT;
	} else if (!auth_logged()) {
		out[n++] = MI_LOGIN;
	} else {
		out[n++] = MI_PROFILE;
		out[n++] = MI_LIBRARY;
		out[n++] = MI_WISHLIST;
		out[n++] = MI_CART;
		out[n++] = MI_LOGOUT;
	}
	out[n++] = MI_REQS;
	out[n++] = MI_ABOUT;
	out[n++] = MI_EXIT;
	return n;
}

static float menu_y(int n)
{
	// El panel llega hasta abajo (tapa la barra de botones): con sesión son siete opciones
	return SCR_H - (MENU_HEAD + n * MENU_ROW + 8);
}

static void open_login(void)
{
	S.login_back = S.scene == SC_LOGIN ? SC_STORE : S.scene;
	S.login_done = 0;
	go_scene(SC_LOGIN, 1);
	if (auth_info()->state != AUTH_QR) auth_begin_qr();
}


static void open_account_list(int ti)
{
	if (S.tab < TAB_COUNT) S.prev_tab = S.tab;
	S.tab = ti;
	go_scene(SC_STORE, 1);
	Tab *t = CUR;
	if (!t->started || ti == TAB_CART) tab_more(ti);   // el carrito puede haber cambiado en el móvil
	t->sel_changed = osGetTime();
	S.cart_qr = false;
}

// ---------------------------------------------------------------------------- Perfil

static const Box B_PLIB = {12, 104, 296, 30};
static const Box B_PWISH = {12, 140, 296, 30};
static const Box B_PLOGOUT = {12, 176, 296, 30};

static void draw_profile_top(void)
{
	const AuthInfo *ai = auth_info();
	ui_vgrad(0, 18, TOP_W, 96, C2D_Color32(0x2a, 0x47, 0x5e, 0xc0), C2D_Color32(0x2a, 0x47, 0x5e, 0x00));
	// Avatar con marco de neón y esquinas de HUD, y el nivel en un círculo, como en Steam
	const Pic *p = ai->avatar[0] ? img_get(ai->avatar, 64) : NULL;
	ui_rect(16, 36, 76, 76, C2D_Color32(0, 0, 0, 0x40));
	ui_rect(16, 34, 76, 76, C2D_Color32(0xff, 0xff, 0xff, 0x30));
	if (p) img_draw_cover(p, 18, 36, 72, 72, 0.5f);
	else ui_rect(18, 36, 72, 72, COL_PANEL);
	ui_text(108, 38, ai->persona[0] ? ai->persona : ai->account, SZ_TITLE, COL_WHITE, AL_LEFT, TOP_W - 120);
	if (ai->level) {
		ui_circle(122, 82, 12, C2D_Color32(0x9b, 0x9b, 0x9b, 0xff));
		ui_circle(122, 82, 10, COL_BG);
		char lv[8];
		snprintf(lv, sizeof lv, "%d", ai->level);
		ui_text(122, 76, lv, SZ_SMALL, COL_WHITE, AL_CENTER, 0);
		ui_text(140, 76, _("Nivel de Steam"), SZ_SMALL, COL_DIM, AL_LEFT, 0);
	}
	ui_text(108, 96, ai->account, SZ_TINY, COL_DIM, AL_LEFT, TOP_W - 120);

	Tab *lib = &tabs[TAB_LIBRARY];
	if (!lib->list.count) {
		if (lib->list.req) draw_net_wait(TOP_W / 2, 170, _("Cargando tu biblioteca…"));
		else if (lib->list.err[0]) ui_text(TOP_W / 2, 160, human_err(lib->list.err), SZ_SMALL, COL_DIM, AL_CENTER, 380);
		return;
	}
	// Juegos, horas y lo más jugado
	long total = 0;
	int top3[3] = {-1, -1, -1};
	for (int i = 0; i < lib->list.count; i++) {
		int m = lib->list.rows[i].minutes;
		total += m;
		for (int k = 0; k < 3; k++) {
			if (top3[k] < 0 || m > lib->list.rows[top3[k]].minutes) {
				for (int j = 2; j > k; j--) top3[j] = top3[j - 1];
				top3[k] = i;
				break;
			}
		}
	}
	char st[64];
	snprintf(st, sizeof st, _("%d juegos · %ld h jugadas"), lib->list.count, (total + 30) / 60);
	fx_glass(8, 120, TOP_W - 16, 114, 14, 1, 0);
	ui_text(18, 128, st, SZ_BODY, COL_TXT, AL_LEFT, 380);
	ui_text(18, 150, _("LO MÁS JUGADO"), SZ_TINY, COL_ACC, AL_LEFT, 0);
	int maxm = top3[0] >= 0 ? lib->list.rows[top3[0]].minutes : 0;
	if (!maxm) ui_text(TOP_W / 2, 162 + (72 - ui_line_h(SZ_SMALL)) / 2 + 1, _("Aún no hay horas de juego en tu cuenta"), SZ_SMALL, COL_DIM, AL_CENTER, 360);
	for (int k = 0; k < 3; k++) {
		if (top3[k] < 0 || !lib->list.rows[top3[k]].minutes) break;
		const StoreRow *r = &lib->list.rows[top3[k]];
		float y = 166 + k * 22;
		char hrs[16];
		snprintf(hrs, sizeof hrs, "%d h", (r->minutes + 30) / 60);
		ui_text(18, y, r->name, SZ_SMALL, COL_WHITE, AL_LEFT, 250);
		ui_text(TOP_W - 18, y, hrs, SZ_SMALL, COL_DIM, AL_RIGHT, 0);
		float bw = (TOP_W - 36) * r->minutes / (float)maxm;
		ui_rrect(18, y + 14, TOP_W - 36, 4, 2, C2D_Color32(0xff, 0xff, 0xff, 0x14));
		ui_rrect(18, y + 14, bw, 4, 2, COL_ACC);
	}
}

static void draw_profile_bottom(void)
{
	ui_text(BOT_W / 2, 30, _("Tu cuenta de Steam"), SZ_LARGE, COL_WHITE, AL_CENTER, 300);
	ui_text(BOT_W / 2, 54, _("Consulta tu biblioteca, tu lista de deseos y tu cuenta"), SZ_SMALL, COL_DIM, AL_CENTER, 300);
	char lib[48], wish[48];
	Tab *l = &tabs[TAB_LIBRARY], *w = &tabs[TAB_WISHLIST];
	if (l->list.count) snprintf(lib, sizeof lib, _("Biblioteca (%d)"), l->list.count);
	else snprintf(lib, sizeof lib, "%s", _("Biblioteca"));
	if (w->wish_loaded) snprintf(wish, sizeof wish, _("Lista de deseos (%d)"), w->nwish);
	else snprintf(wish, sizeof wish, "%s", _("Lista de deseos"));
	draw_button_k(&B_PLIB, NULL, S.pfocus == 0, IC_PAD, lib, COL_BTN2, COL_BTN2, COL_ACC, 1, S.pressed == BT_PLIB);
	draw_button_k(&B_PWISH, NULL, S.pfocus == 1, IC_HEART, wish, COL_BTN2, COL_BTN2, COL_ACC, 1, S.pressed == BT_PWISH);
	u32 red = C2D_Color32(0x6a, 0x26, 0x22, 0xff);
	draw_button_k(&B_PLOGOUT, NULL, S.pfocus == 2, IC_LOCK, _("Cerrar sesión"), red, red, C2D_Color32(0xff, 0x9a, 0x8c, 0xff), 1, S.pressed == BT_PLOGOUT);
	static const char *hh[] = {"↑↓", "Elegir", "A", "Abrir", "B", "Volver", "START", "Menú"};
	draw_hints(hh, 4);
}

static int press_update(u32 kd, u32 kh, u32 ku, touchPosition *tp, const int *ids, int n);

static void do_logout(void)
{
	auth_logout();
	account_lists_clear();
	friends_reset();
	shop_reset();
	S.cart_seen = 0;
	S.pick_open = false;
	toast_icon(_("Sesión cerrada"), IC_LOCK);
}

static void input_profile(u32 kd, u32 kh, u32 ku, touchPosition *tp)
{
	if (kd & KEY_B) {
		go_scene(SC_STORE, -1);
		return;
	}
	static const int ids[] = {BT_PLIB, BT_PWISH, BT_PLOGOUT};
	int b = press_update(kd, kh, ku, tp, ids, 3);
	// Con la cruceta se elige uno de los tres botones y A lo pulsa
	if (key_repeat(kd, kh, KEY_DOWN)) S.pfocus = (S.pfocus + 1) % 3;
	if (key_repeat(kd, kh, KEY_UP)) S.pfocus = (S.pfocus + 2) % 3;
	if (kd & KEY_A) b = ids[S.pfocus];
	if (b == BT_PLIB) open_account_list(TAB_LIBRARY);
	if (b == BT_PWISH) open_account_list(TAB_WISHLIST);
	if (b == BT_PLOGOUT) {
		do_logout();
		go_scene(SC_STORE, -1);
	}
}

// ---------------------------------------------------------------------------- Acerca de

#define URL_GITHUB  "https://github.com/venomshine"
#define URL_DISCORD "https://discord.com/invite/yVKp8JwV4e"
#define URL_KOFI    "https://ko-fi.com/venomshine"

enum { AB_APP, AB_GITHUB, AB_DISCORD, AB_KOFI, AB_CREDITS, AB_LEGAL, AB_COUNT };
static Scene about_back;
static int about_sel;

static const char *const about_desc = "Explora la tienda de Steam, consulta ofertas y reseñas, gestiona tu lista de deseos y tu carrito, y compra con los métodos de pago de tu cuenta, todo desde la consola.";
static const char *const about_legal = "Steam3DS es un proyecto independiente y no está afiliado, patrocinado ni respaldado por Valve Corporation ni por Nintendo. Steam y el logotipo de Steam son marcas comerciales o marcas registradas de Valve Corporation. Nintendo 3DS es una marca registrada de Nintendo.";

// Créditos: lo que usa la app y sus licencias
static const char *const about_credits[][2] = {
	{"libctru · citro2d · citro3d", "zlib"},
	{"libcurl", "curl"},
	{"Mbed TLS", "Apache-2.0"},
	{"zlib", "zlib"},
	{"libjpeg-turbo", "IJG · BSD-3"},
	{"cJSON", "MIT"},
	{"QR Code generator (Nayuki)", "MIT"},
	{"Certificados CA de Mozilla", "MPL-2.0"},
	{"devkitPro · devkitARM", ""},
};
#define NCREDITS (int)(sizeof about_credits / sizeof *about_credits)

static void open_about(void)
{
	about_back = S.scene == SC_ABOUT ? SC_STORE : S.scene;
	about_sel = 0;
	go_scene(SC_ABOUT, 1);
}

static void draw_logo(float cx, float cy, float size, float alpha)
{
	if (!ui_sheet) return;
	C2D_Image img = C2D_SpriteSheetGetImage(ui_sheet, 0);
	C2D_ImageTint tint;
	C2D_AlphaImageTint(&tint, alpha);
	float s = size / 96.0f;
	C2D_DrawImageAt(img, cx - size / 2, cy - size / 2, 0.5f, &tint, s, s);
}

static const char *about_link_url(int i)
{
	return i == AB_GITHUB ? URL_GITHUB : i == AB_DISCORD ? URL_DISCORD : URL_KOFI;
}

static const char *about_link_name(int i)
{
	return i == AB_GITHUB ? "GitHub" : i == AB_DISCORD ? "Discord" : "Ko-fi";
}

static const char *about_link_desc(int i)
{
	return i == AB_GITHUB ? _("Código, novedades y avisos de errores. Si algo falla, abre un issue y lo reviso.")
		: i == AB_DISCORD ? _("Únete a la comunidad: dudas, ayuda, ideas y anuncios de nuevas versiones.")
		: _("Si te gusta Steam3DS, puedes invitarme a un café. Ayuda mucho a seguir con el proyecto.");
}

// La ficha de la app: logo, nombre, versión, autor y para qué sirve
static void about_app_card(float x, float y, float w)
{
	draw_logo(x + 36, y + 36, 72, 1);
	float tx = x + 84;
	ui_text(tx, y + 6, "Steam3DS", SZ_TITLE, COL_WHITE, AL_LEFT, w - 84);
	ui_text(tx, y + 32, _("La tienda de Steam en tu Nintendo 3DS"), SZ_SMALL, COL_ACC, AL_LEFT, w - 84);
	char v[64];
	snprintf(v, sizeof v, _("Versión %s · Hecho por VENOMSHiNE"), APP_VERSION);
	ui_text(tx, y + 50, v, SZ_SMALL, COL_DIM, AL_LEFT, w - 84);
}

// Arriba: la ficha de la app, el QR del enlace elegido, los créditos o el aviso legal
static void draw_about_top(void)
{
	const float X = 16, W = TOP_W - 32;
	if (about_sel >= AB_GITHUB && about_sel <= AB_KOFI) {
		draw_qr_panel(about_link_url(about_sel), about_link_name(about_sel), about_link_desc(about_sel), NULL, about_link_url(about_sel) + 8, COL_ACC);
	} else if (about_sel == AB_CREDITS) {
		ui_text(X, 28, _("Créditos y licencias"), SZ_LARGE, COL_WHITE, AL_LEFT, W);
		ui_rrect(X, 47, ui_text_w(_("Créditos y licencias"), SZ_LARGE), 2, 1, col_alpha(COL_ACC, 0.8f));
		for (int i = 0; i < NCREDITS; i++) {
			float y = 60 + i * 17;
			ui_text(X, y, about_credits[i][0], SZ_SMALL, COL_TXT, AL_LEFT, W * 0.7f);
			ui_text(X + W, y, about_credits[i][1], SZ_SMALL, COL_DIM, AL_RIGHT, 0);
		}
		ui_text(X, 218, _("Datos de la tienda: Steam (Valve Corporation)"), SZ_SMALL, COL_DIM, AL_LEFT, W);
	} else if (about_sel == AB_LEGAL) {
		ui_text(X, 28, _("Aviso legal"), SZ_LARGE, COL_WHITE, AL_LEFT, W);
		ui_rrect(X, 47, ui_text_w(_("Aviso legal"), SZ_LARGE), 2, 1, col_alpha(COL_ACC, 0.8f));
		ui_text_wrap(X, 62, W, _(about_legal), SZ_SMALL, COL_TXT, 8, 15);
	} else {
		fx_glass(10, 26, TOP_W - 20, 84, 14, 1, 0);
		about_app_card(16, 32, TOP_W - 32);
		ui_text_wrap(X, 122, W, _(about_desc), SZ_SMALL, COL_TXT, 4, 15);
		ui_text_wrap(X, 186, W, _("Si encuentras algún problema o tienes una sugerencia, escríbeme por GitHub o Discord."), SZ_SMALL, COL_ACC, 2, 15);
	}
}

// Abajo: la lista (la app, GitHub, Discord, Ko-fi, créditos y aviso legal)
static void draw_about_bottom(void)
{
	fx_bar(0, 0, BOT_W, LIST_Y, true);
	ui_text(10, 4, "‹", SZ_LARGE, COL_ACC, AL_LEFT, 0);
	ui_icon(IC_INFO, 28, 6, 12, COL_ACC);
	ui_text(46, 5, _("Acerca de Steam3DS"), SZ_SMALL, COL_WHITE, AL_LEFT, BOT_W - 56);
	static const Icon ic[AB_COUNT] = {IC_PAD, IC_CODE, IC_CHAT, IC_CUP, IC_DOC, IC_DOC};
	const u32 col[AB_COUNT] = {COL_ACC, C2D_Color32(0xc9, 0xd1, 0xd9, 0xff), C2D_Color32(0x7f, 0x8c, 0xff, 0xff),
		C2D_Color32(0xff, 0x6e, 0x6b, 0xff), COL_DIM, COL_DIM};
	char ver[48];
	snprintf(ver, sizeof ver, "Steam3DS %s", APP_VERSION);
	const char *title[AB_COUNT] = {ver, "GitHub", "Discord", "Ko-fi", _("Créditos y licencias"), _("Aviso legal")};
	const char *sub[AB_COUNT] = {_("Gracias por usar Steam3DS"), _("Código y avisos de errores"), _("Comunidad y ayuda"),
		_("Invítame a un café"), _("Librerías que usa la app"), _("Steam3DS no es una app oficial de Steam")};
	const float RH = 32;
	for (int i = 0; i < AB_COUNT; i++) {
		float y = LIST_Y + 2 + i * RH;
		if (i == about_sel) {
			float br = 0.075f + 0.035f * sinf(g_time * 2.4f);
			ui_rrect(3, y + 1, BOT_W - 8, RH - 2, 6, col_alpha(COL_WHITE, br));
			ui_rrect(3, y + 5, 2, RH - 10, 1, col_alpha(COL_ACC, 0.8f));
		}
		ui_icon(ic[i], 14, y + 10, 12, col[i]);
		ui_text(36, y + 3, title[i], SZ_BODY - 0.02f, i == about_sel ? COL_WHITE : COL_TXT, AL_LEFT, BOT_W - 60);
		ui_text(36, y + 17, sub[i], SZ_TINY, COL_DIM, AL_LEFT, BOT_W - 60);
		if (i >= AB_GITHUB && i <= AB_KOFI && i == about_sel) ui_icon(IC_PHONE, BOT_W - 24, y + 10, 12, COL_ACC);
		if (i < AB_COUNT - 1) ui_hline(36, y + RH - 1, BOT_W - 44, C2D_Color32(0xff, 0xff, 0xff, 0x0c));
	}
	static const char *h[] = {"↑↓", "Elegir", "B", "Volver"};
	draw_hints(h, 2);
}

static void input_about(u32 kd, u32 kh)
{
	if (kd & KEY_B) {
		go_scene(about_back, -1);
		return;
	}
	if (key_repeat(kd, kh, KEY_DOWN)) about_sel = (about_sel + 1) % AB_COUNT;
	if (key_repeat(kd, kh, KEY_UP)) about_sel = (about_sel + AB_COUNT - 1) % AB_COUNT;
}

static void menu_choose(int item)
{
	S.menu_open = false;
	switch (item) {
	case MI_LOGIN: open_login(); break;
	case MI_LOGOUT:
		do_logout();
		if (S.tab >= TAB_COUNT || S.scene == SC_PROFILE) {
			S.tab = S.tab >= TAB_COUNT ? S.prev_tab : S.tab;
			S.tab_x = S.tab;
			go_scene(SC_STORE, -1);
		}
		break;
	case MI_PROFILE:
		S.pfocus = 0;
		go_scene(SC_PROFILE, 1);
		if (!tabs[TAB_LIBRARY].started) tab_more(TAB_LIBRARY);
		if (!tabs[TAB_WISHLIST].started) tab_more(TAB_WISHLIST);
		break;
	case MI_LIBRARY: open_account_list(TAB_LIBRARY); break;
	case MI_WISHLIST: open_account_list(TAB_WISHLIST); break;
	case MI_CART: open_account_list(TAB_CART); break;
	case MI_REQS:
		// Ajuste: requisitos de Windows o de macOS en las fichas
		g_cfg.reqs_mac = !g_cfg.reqs_mac;
		config_save();
		S.menu_open = true;   // el menú se queda abierto para ver el cambio
		toast(g_cfg.reqs_mac ? _("Se mostrarán los requisitos de macOS") : _("Se mostrarán los requisitos de Windows"));
		break;
	case MI_ABOUT: open_about(); break;
	case MI_EXIT:
		S.ask_exit = true;
		S.exit_anim = 0;
		break;
	default:
		toast(_("Disponible próximamente"));
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
	fx_panel(6, y0, BOT_W - 12, hgt + 16, 16, a);
	ui_rrect(BOT_W / 2 - 16, y0 + 6, 32, 3, 1.5f, col_alpha(COL_DIM, a * 0.6f));
	// Cabecera: tu cuenta (o «sin sesión»)
	const AuthInfo *ai = auth_info();
	float hx = 18, hy = y0 + 11;
	if (auth_logged()) {
		const Pic *p = ai->avatar[0] ? img_get(ai->avatar, 64) : NULL;
		ui_rect(hx - 1, hy - 1, 28, 28, col_alpha(COL_WHITE, 0.25f * a));
		ui_rect(hx, hy, 26, 26, col_alpha(COL_BG, a));
		if (p) img_draw_cover(p, hx, hy, 26, 26, 0.5f);
		else ui_icon(IC_USER, hx + 5, hy + 5, 16, col_alpha(COL_DIM, a));
		ui_text(hx + 34, hy, ai->persona, SZ_BODY, col_alpha(COL_WHITE, a), AL_LEFT, 200);
		char lv[72];
		if (ai->level) snprintf(lv, sizeof lv, _("Nivel %d"), ai->level);
		else snprintf(lv, sizeof lv, "%s", ai->account);
		ui_text(hx + 34, hy + 14, lv, SZ_TINY, col_alpha(COL_DIM, a), AL_LEFT, 200);
	} else if (ai->state == AUTH_STARTING) {
		ui_icon(IC_USER, hx + 3, hy + 4, 18, col_alpha(COL_DIM, a));
		ui_text(hx + 34, hy, ai->account[0] ? ai->account : APP_NAME, SZ_BODY, col_alpha(COL_WHITE, a), AL_LEFT, 200);
		ui_text(hx + 34, hy + 14, _("Conectando tu sesión de Steam…"), SZ_TINY, col_alpha(COL_DIM, a), AL_LEFT, 200);
	} else {
		ui_icon(IC_USER, hx + 3, hy + 4, 18, col_alpha(COL_DIM, a));
		ui_text(hx + 34, hy, APP_NAME, SZ_BODY, col_alpha(COL_WHITE, a), AL_LEFT, 200);
		ui_text(hx + 34, hy + 14, _("No has iniciado sesión"), SZ_TINY, col_alpha(COL_DIM, a), AL_LEFT, 200);
	}
	static const struct { Icon ic; const char *label; } info[] = {
		[MI_LOGIN] = {IC_PHONE, "Iniciar sesión con el móvil"},
		[MI_PROFILE] = {IC_USER, "Mi perfil"},
		[MI_LIBRARY] = {IC_PAD, "Biblioteca"},
		[MI_WISHLIST] = {IC_HEART, "Lista de deseos"},
		[MI_CART] = {IC_CART, "Carrito"},
		[MI_REQS] = {IC_WRENCH, "Requisitos"},
		[MI_LOGOUT] = {IC_LOCK, "Cerrar sesión"},
		[MI_ABOUT] = {IC_INFO, "Acerca de Steam3DS"},
		[MI_EXIT] = {IC_POWER, "Salir de Steam3DS"},
	};
	for (int i = 0; i < n; i++) {
		float y = y0 + MENU_HEAD + i * MENU_ROW;
		bool on = i == S.menu_sel;
		if (on) fx_select(12, y, BOT_W - 24, MENU_ROW - 2, 10, a);
		u32 c = items[i] == MI_EXIT || items[i] == MI_LOGOUT ? C2D_Color32(0xff, 0x8a, 0x7a, 0xff) : COL_ACC;
		ui_icon(info[items[i]].ic, 22, y + 5, 12, col_alpha(c, a));
		ui_text(42, y + 3, _(info[items[i]].label), SZ_BODY - 0.02f, col_alpha(on ? COL_WHITE : COL_TXT, a), AL_LEFT, BOT_W - 60);
		if (items[i] == MI_REQS)
			ui_text(BOT_W - 24, y + 4, g_cfg.reqs_mac ? "macOS" : "Windows", SZ_SMALL, col_alpha(COL_ACC, a), AL_RIGHT, 0);
		if (items[i] == MI_CART && shop_cart()->n) {
			// Cuántos hay, en una píldora verde
			char nc[8];
			snprintf(nc, sizeof nc, "%d", shop_cart()->n);
			float w = ui_text_w(nc, SZ_SMALL) + 12;
			ui_rrect(BOT_W - 24 - w, y + 3, w, 16, 8, col_alpha(C2D_Color32(0x5c, 0x7e, 0x10, 0xff), a));
			ui_text(BOT_W - 24 - w / 2, y + 4, nc, SZ_SMALL, col_alpha(COL_WHITE, a), AL_CENTER, 0);
		}
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
		ui_rrect(TOP_W / 2 - s / 2 - 3, 60 - 3 + (64 - s) / 2, s + 6, s + 6, 6, C2D_Color32(0xff, 0xff, 0xff, 0x40));
		if (p) img_draw_cover(p, TOP_W / 2 - s / 2, 60 + (64 - s) / 2, s, s, 0.5f);
		else ui_rect(TOP_W / 2 - s / 2, 60 + (64 - s) / 2, s, s, COL_PANEL);
		char t[96];
		snprintf(t, sizeof t, _("Te damos la bienvenida, %s"), ai->persona[0] ? ai->persona : ai->account);
		ui_text(TOP_W / 2, 140, t, SZ_TITLE, col_alpha(COL_WHITE, e), AL_CENTER, 380);
		ui_text(TOP_W / 2, 166, _("Has iniciado sesión correctamente"), SZ_SMALL, col_alpha(COL_DIM, e), AL_CENTER, 380);
		return;
	}
	if (!ai->qr_url[0]) {
		if (ai->err[0]) {
			ui_text(TOP_W / 2, 100, _("No se pudo obtener el código de Steam"), SZ_LARGE, COL_TXT, AL_CENTER, 380);
			ui_text(TOP_W / 2, 124, human_err(ai->err), SZ_BODY, COL_DIM, AL_CENTER, 380);
			ui_text(TOP_W / 2, 144, _("Se reintentará automáticamente"), SZ_SMALL, COL_ACC, AL_CENTER, 0);
		} else {
			draw_net_wait(TOP_W / 2, 120, _("Obteniendo el código de Steam…"));
		}
		return;
	}
	// El QR, centrado en el alto que queda bajo la barra de arriba
	const float qs = 176, qy = 18 + (SCR_H - 18 - qs) / 2;
	ui_qr(ai->qr_url, 14, qy, qs);
	float x = 204, w = TOP_W - 12 - x;
	float tw = ui_text(x, qy, _("Inicia sesión con Steam"), SZ_LARGE, COL_WHITE, AL_LEFT, w);
	ui_rrect(x, qy + 19, tw < w ? tw : w, 2, 1, col_alpha(COL_ACC, 0.8f));   // subrayado
	static const char *steps[] = {
		"Abre la app de Steam en el móvil",
		"Toca el escudo de Steam Guard",
		"Selecciona «Escanear un código QR» y apunta a esta pantalla",
	};
	float y = qy + 30;
	float lt = ui_line_h(SZ_TINY), ls = ui_line_h(SZ_SMALL);
	for (int i = 0; i < 3; i++) {
		char num[4];
		snprintf(num, sizeof num, "%d", i + 1);
		// El círculo, centrado en la primera línea del paso, y el número centrado en el círculo
		float cy = y + ls * 0.55f;
		ui_circle(x + 7, cy, 7.5f, C2D_Color32(0xff, 0xff, 0xff, 0x1c));
		ui_text(x + 7, cy - lt * 0.55f, num, SZ_TINY, COL_ACC, AL_CENTER, 0);
		int nl = ui_text_wrap(x + 20, y, w - 20, _(steps[i]), SZ_SMALL, COL_TXT, 3, 13);
		y += nl * 13 + 8;
	}
	// Estado: esperando, o ya escaneado y falta aprobar (alineado con el pie del QR)
	float sy = qy + qs - 26;
	if (ai->qr_scanned) {
		int nl = ui_text_lines(_("Código escaneado. Confirma el inicio de sesión en el móvil"), SZ_SMALL, w - 20);
		float bh = 12 + 13 * nl;
		FxButton ok = {C2D_Color32(0x2b, 0x55, 0x1a, 0xff), 1, false, false};
		fx_button(x, sy + 26 - bh, w, bh, 10, &ok);
		ui_text_wrap(x + 10, sy + 26 - bh + 6, w - 20, _("Código escaneado. Confirma el inicio de sesión en el móvil"), SZ_SMALL, COL_LIME, 2, 13);
	} else {
		float cy = sy + 13;
		ui_spinner(x + 8, cy, 6, COL_DIM);
		ui_text(x + 22, cy - ls * 0.55f, _("Esperando confirmación…"), SZ_SMALL, COL_DIM, AL_LEFT, w - 30);
	}
}

static void draw_login_bottom(void)
{
	// Todo centrado dentro del panel: el candado, el título y el texto (cada línea centrada)
	const char *msg = _("El inicio de sesión se confirma desde la app oficial de Steam. La sesión se guarda en la tarjeta SD y puedes cerrarla en cualquier momento desde el menú (START) o desde Steam, en Dispositivos autorizados.");
	const float px = 14, py = 12, pw = BOT_W - 28, ph = SCR_H - 18 - 12 - py;
	fx_glass(px, py, pw, ph, 18, 1, 0);
	int nl = ui_text_lines(msg, SZ_SMALL, pw - 36);
	float content = 36 + 10 + 16 + 10 + nl * 14;
	float y = py + (ph - content) / 2;
	ui_circle(BOT_W / 2, y + 18, 18, C2D_Color32(0xff, 0xff, 0xff, 0x12));
	ui_icon(IC_LOCK, BOT_W / 2 - 10, y + 18 - 10 - 2, 20, COL_ACC);
	y += 36 + 10;
	ui_text(BOT_W / 2, y, _("Tu contraseña nunca se introduce en la consola"), SZ_BODY, COL_WHITE, AL_CENTER, pw - 24);
	y += 16 + 10;
	ui_text_wrap_center(BOT_W / 2, y, pw - 36, msg, SZ_SMALL, COL_DIM, 8, 14);
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
// La válvula en 3D (tools/gen_wheel3d.py): su sombra y 24 fotogramas de giro (3° cada uno) con la
// luz del bombillo en cada extremo del balanceo
// (en su propia hoja, wheel3d.t3x)
enum { W3D_SHADOW, W3D_DIRT, W3D_CRACK, W3D_A = W3D_CRACK + 3, W3D_B = W3D_A + 24 };
#define W3D_SIZE    112.0f
#define WHEEL_PX    100.0f      // tamaño de la válvula en pantalla
#define WHEEL_R     46.0f       // radio del aro en pantalla

// El bombillo: flojo, cálido, fuera de la pantalla arriba a la derecha. Se balancea despacio (la
// luz va de 22° a 68° sobre la horizontal) y de vez en cuando flojea. Lo usan la válvula, su
// sombra, el charco de luz en la pared, el polvo y el vapor
static struct { float swing, lx, ly, power; } bulb;

// La entrada: la válvula llega desde un poco más cerca (sin crecer ni rebotar) y se clava en la
// pared de golpe; la pantalla tiembla, arranca a girar cuando se asienta y entonces sale el vapor
#define SPLASH_HIT   0.40f
#define SPLASH_SPIN  0.85f
#define SPLASH_STEAM 1.15f

// Temblor del golpe: fuerte y corto, cada eje con su ritmo
static void splash_shake(float t, float amp, float *dx, float *dy)
{
	float u = t - SPLASH_HIT;
	*dx = *dy = 0;
	if (u < 0 || u > 0.6f) return;
	float e = amp * expf(-u * 9.0f);
	*dx = e * sinf(u * 2 * (float)M_PI * 19);
	*dy = e * 0.8f * sinf(u * 2 * (float)M_PI * 23 + 1.3f);
}

// Los parpadeos del bombillo en el tramo n de BULB_PERIOD s: cuándo empiezan, cuánto bajan y
// cuánto duran (lo usan la luz y el chisporroteo, para que vayan a la vez)
#define BULB_PERIOD 2.7f
static int bulb_dips(u32 n, float *at, float *depth, float *len)
{
	float h = ((n * 2654435761u) >> 8 & 0xffff) / 65535.0f;
	if (h <= 0.35f) return 0;
	at[0] = n * BULB_PERIOD + 0.4f + h * 1.6f;
	depth[0] = 0.25f + 0.25f * h;
	len[0] = 0.07f;
	if (h <= 0.7f) return 1;
	at[1] = at[0] + 0.12f;
	depth[1] = depth[0] * 0.6f;
	len[1] = 0.05f;
	return 2;
}

static void bulb_update(float t)
{
	bulb.swing = 0.5f + 0.5f * sinf(t * 2 * (float)M_PI / 4.0f);
	float th = (22 + 46 * bulb.swing) * (float)M_PI / 180;
	// El golpe sacude el bombillo: se balancea de más un momento
	float u = t - SPLASH_HIT;
	if (u > 0) th += 0.16f * expf(-u * 2.2f) * sinf(u * 7.0f);
	bulb.lx = cosf(th);
	bulb.ly = -sinf(th);
	// Tiembla un poco siempre y, a ratos, pierde fuerza un instante (a veces dos seguidos)
	float f = 0.96f + 0.04f * sinf(t * 23);
	float at[2], depth[2], len[2];
	int nd = bulb_dips((u32)(t / BULB_PERIOD), at, depth, len);
	for (int i = 0; i < nd; i++)
		if (t >= at[i] && t < at[i] + len[i]) f -= depth[i];
	// y parpadea con el golpe
	if (u > 0 && u < 0.3f) f -= 0.4f * expf(-u * 14) * (0.6f + 0.4f * sinf(u * 90));
	bulb.power = f;
}



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
	// En penumbra: el lado que mira al bombillo recoge su luz cálida
	float d = hypotf(pf->x, pf->y);
	float side = d > 1 ? clampf((pf->x * bulb.lx + pf->y * bulb.ly) / d, 0, 1) : 0;
	c = col_lerp(c, C2D_Color32(0x1a, 0x22, 0x2e, 0xff), 0.42f - 0.12f * bulb.power);
	c = col_lerp(c, C2D_Color32(0xe8, 0xc0, 0x90, 0xff), (0.10f + 0.22f * side) * bulb.power);
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
	return CUR->list.count > 0 || CUR->list.err[0] || net_status_text()[0];
}

// Una voluta del chorro, repartida entre detrás y delante de la válvula: mientras está dentro
// del aro va por detrás (se ve por los huecos entre los radios) y al salir pasa por delante, así
// el vapor atraviesa la válvula
static float puff_front(const Puff *pf, float oy)
{
	float r = hypotf(pf->x, pf->y + oy);
	return clampf((r - WHEEL_R * 0.8f) / (WHEEL_R * 0.45f), 0, 1);
}

// La válvula: el fotograma del giro con la luz de cada extremo del balanceo, mezclados según
// dónde esté el bombillo, y girada lo que falte entre fotograma y fotograma. Los fotogramas se
// repiten cada 72° (cinco radios); la mugre no, así que va en su capa y gira entera (si no,
// saltaba 72° atrás cada vez que volvía al primer fotograma). lift: lo separada que está aún de
// la pared (0 = clavada): la sombra se aleja, crece y se apaga
static void draw_wheel3d(float cx, float cy, float scale, float spin, float alpha, float lift)
{
	if (!wheel_sheet || alpha <= 0) return;
	float deg = spin * 180 / (float)M_PI;
	float step = roundf(deg / 3);
	int k = ((int)step % 24 + 24) % 24;
	float rest = (deg - step * 3) * (float)M_PI / 180;
	float s = scale * WHEEL_PX / W3D_SIZE;
	C2D_ImageTint tint;
	// La sombra en la pared, al lado contrario de la luz (más larga cuanto más de lado viene)
	float off = (11 + 4 * (1 + bulb.ly)) * scale * (1 + 2.6f * lift);
	float ss = s * (1 + 0.18f * lift);
	C2D_AlphaImageTint(&tint, clampf(0.85f * alpha * (0.7f + 0.3f * bulb.power) * (1 - 0.55f * lift), 0, 1));
	C2D_DrawImageAtRotated(C2D_SpriteSheetGetImage(wheel_sheet, W3D_SHADOW), cx - bulb.lx * off, cy - bulb.ly * off, 0.5f, spin, &tint, ss, ss);
	// Los dos fotogramas y la mugre, oscurecidos cuando el bombillo flojea
	float dark = 1 - bulb.power;
	C2D_PlainImageTint(&tint, C2D_Color32(0, 0, 0, (u8)(255 * clampf(alpha, 0, 1))), dark);
	C2D_DrawImageAtRotated(C2D_SpriteSheetGetImage(wheel_sheet, W3D_A + k), cx, cy, 0.5f, rest, &tint, s, s);
	C2D_PlainImageTint(&tint, C2D_Color32(0, 0, 0, (u8)(255 * clampf(alpha * bulb.swing, 0, 1))), dark);
	C2D_DrawImageAtRotated(C2D_SpriteSheetGetImage(wheel_sheet, W3D_B + k), cx, cy, 0.5f, rest, &tint, s, s);
	C2D_PlainImageTint(&tint, C2D_Color32(0, 0, 0, (u8)(255 * clampf(alpha, 0, 1))), dark);
	C2D_DrawImageAtRotated(C2D_SpriteSheetGetImage(wheel_sheet, W3D_DIRT), cx, cy, 0.5f, spin, &tint, s, s);
}

// El golpe en la pared: el yeso se raja desde el aro (las grietas corren en tres etapas), sale una
// nube de polvo de yeso que se abre y sube despacio, y caen cascotes. Las grietas y el polvo van
// detrás de la válvula; los cascotes, delante
static void draw_impact_back(float t, float cx, float cy)
{
	float u = t - SPLASH_HIT;
	if (u < 0 || !wheel_sheet) return;
	int st = u < 0.035f ? 0 : u < 0.08f ? 1 : 2;
	C2D_ImageTint tint;
	C2D_PlainImageTint(&tint, C2D_Color32(0, 0, 0, 0xff), 1 - bulb.power);
	C2D_DrawImageAt(C2D_SpriteSheetGetImage(wheel_sheet, W3D_CRACK + st), cx - 100, cy - 100, 0.5f, &tint, 1, 1);
	for (int i = 0; i < 10; i++) {
		const float life = 2.2f;
		float age = u - i * 0.012f;
		if (age < 0 || age > life) continue;
		float p = age / life;
		float a = i * 0.628f + hrand(i, 31) * 0.5f;
		float r = 48 + 34 * (1 - expf(-age * 2.4f));
		float x = cx + cosf(a) * r, y = cy + sinf(a) * r * 0.9f - 16 * age - 6 * age * age;
		float al = clampf(age / 0.06f, 0, 1) * powf(1 - p, 1.6f) * 0.42f;
		// Yeso gris en penumbra, con el lado del bombillo algo más cálido
		u32 c = col_lerp(C2D_Color32(0x3a, 0x3c, 0x40, 0xff), C2D_Color32(0xb8, 0xa8, 0x90, 0xff),
			(0.25f + 0.35f * clampf(cosf(a) * bulb.lx + sinf(a) * bulb.ly, 0, 1)) * bulb.power);
		draw_sprite_ex(SPR_STEAM, x, y, 0.35f + 1.2f * (1 - expf(-age * 1.6f)), 0.3f + 0.9f * (1 - expf(-age * 1.6f)),
			hrand(i, 32) * 6.28f + age * 0.4f, col_alpha(c, al), 1);
	}
}

static void draw_impact_front(float t, float cx, float cy)
{
	float u = t - SPLASH_HIT;
	if (u < 0 || u > 1.6f) return;
	for (int i = 0; i < 18; i++) {
		float a = hrand(i, 41) * 6.28f;
		float v = 40 + 90 * hrand(i, 42), up = 30 + 80 * hrand(i, 43);
		float age = u - hrand(i, 44) * 0.05f;
		if (age < 0) continue;
		float x = cx + cosf(a) * (47 + v * age), y = cy + sinf(a) * 47 - up * age + 0.5f * 520 * age * age;
		if (y > SCR_H + 4) continue;
		float sz = 1.2f + 2.2f * hrand(i, 45);
		float al = 1 - clampf((age - 0.9f) / 0.5f, 0, 1);
		u32 c = col_lerp(C2D_Color32(0x2c, 0x2b, 0x2a, 0xff), C2D_Color32(0x9c, 0x92, 0x84, 0xff), 0.5f * hrand(i, 46) * bulb.power);
		ui_rect(x - sz / 2, y - sz / 2, sz, sz, col_alpha(c, al));
	}
}

static void draw_splash_top(void)
{
	float t = splash_t;
	bulb_update(t);
	float dx, dy;
	splash_shake(t, 6, &dx, &dy);
	// El fondo, un poco ampliado para que el temblor no deje ver el borde
	if (splash_bg) C2D_DrawImageAt(C2D_SpriteSheetGetImage(splash_bg, 0), -8 + dx, -6 + dy, 0.5f, NULL, (TOP_W + 16.0f) / TOP_W, (SCR_H + 12.0f) / SCR_H);
	else draw_splash_bg(0, TOP_W, SCR_H);
	C3D_Mtx view;
	C2D_ViewSave(&view);
	C2D_ViewTranslate(dx, dy);
	// Sala en penumbra y el charco de luz cálida del bombillo, que se balancea
	ui_rect(-10, -10, TOP_W + 20, SCR_H + 20, C2D_Color32(0, 0, 0, 0x58));
	float cx = TOP_W / 2, cy = 100;
	float px = cx + bulb.lx * 210, py = cy + bulb.ly * 210;
	draw_sprite_ex(SPR_GLOW, px, py, 4.7f, 4.7f, 0, C2D_Color32(0xff, 0xc4, 0x82, (u8)(0x70 * bulb.power)), 1);
	draw_ambient(t, TOP_W, SCR_H, px, py, clampf(t / 0.8f, 0, 1));
	// Hasta el golpe se acerca a la pared cada vez más deprisa; después ya no cambia de tamaño
	float k = clampf(t / SPLASH_HIT, 0, 1);
	float lift = 1 - k * k;
	float scale = 1 + 0.28f * lift;
	float pop = clampf(t / 0.2f, 0, 1);
	const float t0 = SPLASH_STEAM;      // la válvula se abre (ya girando)
	float flash = t > t0 ? expf(-(t - t0) * 3.0f) : 0;
	draw_sprite(SPR_GLOW, cx, cy, (1.9f + 0.08f * sinf(t * 2.4f) + 0.5f * flash) * scale, 0, (0.20f + 0.25f * flash) * pop * bulb.power);

	// Neblina muy tenue alrededor: ambiente
	if (t > t0) {
		float hz = clampf((t - t0) / 1.2f, 0, 1);
		for (int i = 0; i < 3; i++) {
			float a = hz * (0.08f + 0.03f * sinf(t * 0.7f + i * 2)) * bulb.power;
			draw_sprite_ex(SPR_STEAM, cx + (i - 1) * 70 + sinf(t * 0.3f + i) * 12, cy + 30 - i * 18, 2.6f, 1.6f, t * 0.05f * (i - 1),
				C2D_Color32(0xa8, 0xb4, 0xc0, (u8)(255 * a)), 0.6f);
		}
	}
	// El chorro sale de detrás de la válvula (del vástago): las volutas pares van siempre detrás,
	// para dar fondo; las impares la atraviesan (detrás dentro del aro, delante al salir)
	const int JET = 38;
	const float every = JET_LIFE / JET;
	float ramp = clampf((t - t0) / 0.35f, 0, 1);
	for (int j = 0; j < JET; j++) {
		Puff pf;
		if (!jet_puff(j, t - t0 - 0.08f - j * every, &pf)) continue;
		if (j % 2 == 0) draw_puff(&pf, cx, cy - 4, ramp, true);
		else draw_puff(&pf, cx, cy - 4, ramp * (1 - puff_front(&pf, -4)), false);
	}
	// Bocanada al abrirse: volutas hacia los lados que frenan y se deshacen
	Puff burst[12];
	int nb = 0;
	if (t > t0) {
		for (int i = 0; i < 12; i++) {
			float age = t - t0 - i * 0.02f;
			const float life = 2.4f;
			if (age < 0 || age > life) continue;
			float p = age / life, ang = i * 0.5236f + hrand(i, 9) * 0.4f;
			float dist = 18 + 70 * (1 - expf(-age * 2.0f));
			burst[nb++] = (Puff){
				cosf(ang) * dist, sinf(ang) * dist * 0.7f - 14 * age * age,
				0.35f + 1.5f * (1 - expf(-age * 1.4f)), hrand(i, 5) * 6.28f + age * 0.3f,
				clampf(age / 0.12f, 0, 1) * powf(1 - p, 1.4f),
				(int)(p * (FLIP_FRAMES - 1)), clampf(p * 1.4f, 0, 1), 1, 0.75f + 0.25f * hrand(i, 6),
			};
		}
	}
	for (int i = 0; i < nb; i++) draw_puff(&burst[i], cx, cy, 0.9f * (1 - puff_front(&burst[i], 0)), false);

	// Como una válvula que se abre: quieta hasta asentarse en la pared, luego arranca despacio y
	// coge velocidad
	float u = t - SPLASH_SPIN;
	float spin = u <= 0 ? 0 : u < 0.8f ? 0.9f * u * u : 0.576f + 1.44f * (u - 0.8f);
	draw_impact_back(t, cx, cy);
	draw_wheel3d(cx, cy, scale, spin, pop, lift);
	draw_impact_front(t, cx, cy);

	// Lo que ya ha salido del aro, por delante
	for (int i = 0; i < nb; i++) draw_puff(&burst[i], cx, cy, 0.9f * puff_front(&burst[i], 0), false);
	for (int j = 1; j < JET; j += 2) {
		Puff pf;
		if (jet_puff(j, t - t0 - 0.08f - j * every, &pf)) draw_puff(&pf, cx, cy - 4, ramp * puff_front(&pf, -4), false);
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
	C2D_ViewRestore(&view);
}

static void draw_splash_bottom(void)
{
	// Abajo el golpe llega más flojo
	float dx, dy;
	splash_shake(splash_t, 3, &dx, &dy);
	if (splash_bg) C2D_DrawImageAt(C2D_SpriteSheetGetImage(splash_bg, 1), -4 + dx, -4 + dy, 0.5f, NULL, (BOT_W + 8.0f) / BOT_W, (SCR_H + 8.0f) / SCR_H);
	else draw_splash_bg(1, BOT_W, SCR_H);
	C3D_Mtx view;
	C2D_ViewSave(&view);
	C2D_ViewTranslate(dx, dy);
	ui_rect(-6, -6, BOT_W + 12, SCR_H + 12, C2D_Color32(0, 0, 0, 0x70));   // abajo llega aún menos luz
	draw_ambient(splash_t + 3.1f, BOT_W, SCR_H, BOT_W + 40, -120, clampf(splash_t / 0.8f, 0, 1));
	float ta = clampf((splash_t - 0.5f) / 0.5f, 0, 1);
	u8 a8 = (u8)(255 * ta);
	ui_text(BOT_W / 2, 104, _("hecho por VENOMSHiNE"), SZ_BODY, C2D_Color32(0xc7, 0xd5, 0xe0, a8), AL_CENTER, 0);
	ui_text(BOT_W / 2, 124, "v" APP_VERSION, SZ_SMALL, C2D_Color32(0x8f, 0x98, 0xa0, a8), AL_CENTER, 0);
	C2D_ViewRestore(&view);
}

static void splash_update(float dt, u32 kd)
{
	// Los primeros fotogramas tardan (se está cargando todo): sin este tope la entrada se saltaba
	splash_t += dt < 0.034f ? dt : 0.034f;
	if (splash_out < 0) {
		bool skip = splash_t > 0.6f && (kd & (KEY_A | KEY_B | KEY_TOUCH));
		if ((splash_t > 3.5f && splash_ready()) || splash_t > 6.5f || skip) splash_out = 0;
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

static const Box B_SEARCHBAR = {5, 2, BOT_W - 10, LIST_Y - 4};   // la píldora estirada de Buscar
static int press_update(u32 kd, u32 kh, u32 ku, touchPosition *tp, const int *ids, int n);

static void tab_switch(int nt)
{
	if (nt == S.tab) return;
	S.tab = nt;
	Tab *t = CUR;
	t->anim_t = g_time;
	if (nt == TAB_SEARCH) {
		S.search_focus = true;
		if (!t->term[0] && !t->list.req) search_show_recents();
	} else if (!t->started) {
		tab_more(nt);
	}
	t->sel_changed = osGetTime();
}

static void home_switch(int sub)
{
	if (sub < 0 || sub >= HOME_PARTS || sub == S.home_sub) return;
	S.home_sub = sub;
	Tab *t = CUR;
	if (!t->started) tab_more(cur_ti());
	t->anim_t = g_time;
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
		tab_more(cur_ti());
	}
}

static void close_account_list(void)
{
	S.tab = S.prev_tab;
	S.tab_x = S.prev_tab;
	go_scene(SC_STORE, -1);
}

static void input_store(u32 kd, u32 kh, u32 ku, touchPosition *tp)
{
	if (S.tab == TAB_CART) {
		if (S.cart_qr) {
			if (kd & (KEY_B | KEY_X)) S.cart_qr = false;
			return;
		}
		const CartInfo *c = shop_cart();
		int sel = CUR->sel;
		if ((kd & KEY_Y) && sel >= 0 && sel < c->n) {
			if (shop_busy()) toast(_("Espera a que termine la acción anterior"));
			else shop_cart_remove(sel);
		}
		if ((kd & KEY_X) && c->n) open_pay();
	}
	if (S.tab >= TAB_COUNT) {
		static const int ids[] = {BT_BACKBAR};
		if ((kd & KEY_B) || press_update(kd, kh, ku, tp, ids, 1) == BT_BACKBAR) {
			close_account_list();
			return;
		}
	} else {
		if (kd & KEY_L) tab_switch((S.tab + TAB_COUNT - 1) % TAB_COUNT);
		if (kd & KEY_R) tab_switch((S.tab + 1) % TAB_COUNT);
	}
	Tab *t = CUR;
	if (S.tab == TAB_SEARCH && S.search_focus) {
		// Foco en la píldora de búsqueda: A o Y escriben; abajo baja a la lista
		if (key_repeat(kd, kh, KEY_DOWN) && t->list.count) {
			S.search_focus = false;
			store_select(0, 1);
			t->sel_changed = osGetTime();
		}
		if (kd & (KEY_A | KEY_Y)) S.want_kb = true;
		kd &= ~KEY_A;
	} else {
		if (key_repeat(kd, kh, KEY_DOWN)) store_select(t->sel + 1, 1);
		if (key_repeat(kd, kh, KEY_UP)) {
			if (S.tab == TAB_SEARCH && t->sel == 0) S.search_focus = true;   // arriba del todo: vuelve a la búsqueda
			else store_select(t->sel - 1, -1);
		}
		if (S.tab == TAB_HOME) {
			// En Inicio, izquierda y derecha cambian de subcategoría
			if (kd & KEY_LEFT) home_switch(S.home_sub - 1);
			if (kd & KEY_RIGHT) home_switch(S.home_sub + 1);
		} else {
			if (key_repeat(kd, kh, KEY_RIGHT)) store_select(t->sel + 6, 1);
			if (key_repeat(kd, kh, KEY_LEFT)) store_select(t->sel - 6, -1);
		}
		if ((kd & KEY_Y) && S.tab == TAB_SEARCH) S.want_kb = true;
	}
	if ((kd & KEY_B) && S.tab == TAB_SEARCH && t->term[0]) {
		// B borra la búsqueda y vuelve a las recientes
		if (t->list.req) net_release(t->list.req);
		t->list.req = NULL;
		t->list.err[0] = 0;
		search_show_recents();
	}
	if (kd & KEY_A) store_activate();

	if (S.tab == TAB_SEARCH && S.search_focus) {
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
			if (tab_paged(cur_ti()) && t->scroll_target > max - 3 * ROW_H) tab_more(cur_ti());
		}
	}
	if (ku & KEY_TOUCH) {
		if (S.touch_moved < 8) {
			int x = S.touch_x0, y = S.touch_y0;
			if (y < LIST_Y && S.tab < TAB_COUNT) {
				if (!(S.tab == TAB_SEARCH && S.search_focus)) {
					int nt = tab_at(x);
					if (nt == TAB_SEARCH && S.tab == TAB_SEARCH) S.search_focus = true;   // tocar la lupa sube el foco
					else tab_switch(nt);
				}
			} else if (S.tab == TAB_HOME && y < LIST_Y + SUB_H) {
				int i = (int)((x - 10) / ((BOT_W - 20) / (float)HOME_PARTS));
				home_switch(i < 0 ? 0 : i);
			} else if (S.dragging) {
				int i = (int)((y - top + t->scroll) / ROW_H);
				if (i >= 0 && i < t->list.count && !t->list.rows[i].header) {
					if (S.tab == TAB_SEARCH && S.search_focus) {
						S.search_focus = false;
						store_select(i, 0);
					} else if (i == t->sel) store_activate();
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

// ============================================================================
// Pago desde la consola: solo con lo que ya tienes en Steam (cartera o tarjeta guardada)

static const Box B_PAYW = {12, 44, 296, 34};
static const Box B_PAYC = {12, 84, 296, 34};
static const Box B_PAYQ = {12, 124, 296, 28};
static const Box B_CVV = {12, 72, 296, 26};
static const Box B_PAYGO = {12, 184, 296, 32};
#define PAY_HOLD_S 1.4f

static void open_pay(void)
{
	S.pay_qr = false;
	S.pay_hold = 0;
	S.pay_armed = false;
	S.cart_qr = false;
	pay_open();
	go_scene(SC_PAY, 1);
}

static void close_pay(void)
{
	if (pay_busy()) return;
	pay_close();
	S.tab = TAB_CART;
	go_scene(SC_STORE, -1);
	shop_cart_load();
}

static const char *card_name(const char *type)
{
	static const struct { const char *k, *n; } names[] = {
		{"visa", "Visa"}, {"mastercard", "Mastercard"}, {"amex", "American Express"}, {"discover", "Discover"},
		{"jcb", "JCB"}, {"cartebleue", "Carte Bleue"}, {"dankort", "Dankort"}, {"mir", "MIR"},
	};
	for (size_t i = 0; i < sizeof names / sizeof *names; i++)
		if (!strcmp(type, names[i].k)) return names[i].n;
	return _("Tarjeta");
}

// Las casillas que Steam obliga a marcar antes de pagar, como en su web
static const char *pay_ssa_text(const PayInfo *p)
{
	return p->ssa_text[0] ? p->ssa_text : _("Acepto las condiciones del Acuerdo de Suscriptor a Steam.");
}

static const char *pay_wd_text(const PayInfo *p)
{
	return p->withdrawal_text[0] ? p->withdrawal_text
		: _("Acepto que el contenido digital esté disponible de inmediato y renuncio a mi derecho de desistimiento.");
}

// Caja de cada casilla (k: 0 acuerdo, 1 desistimiento); false si esa no se pide
static bool pay_check_box(const PayInfo *p, int k, Box *out)
{
	if (k == 1 && !p->withdrawal) return false;
	float y = p->need_cvv ? 102 : 78;
	for (int i = 0; i <= k; i++) {
		int nl = ui_text_lines(i == 0 ? pay_ssa_text(p) : pay_wd_text(p), SZ_TINY, BOT_W - 52);
		if (nl > 4) nl = 4;
		float h = 8 + nl * 11;
		if (i == k) {
			*out = (Box){8, y, BOT_W - 16, h};
			return true;
		}
		y += h + 3;
	}
	return false;
}

static bool pay_accepted(const PayInfo *p)
{
	return S.pay_ssa && (!p->withdrawal || S.pay_wd);
}

// Por qué no se pudo pagar, en palabras normales (EPurchaseResultDetail)
static const char *pay_err_text(const PayInfo *p)
{
	if (p->maybe_paid) return _("No se pudo confirmar el cobro. Revisa tu correo o el historial de compras de Steam antes de volver a intentarlo.");
	if (p->err_desc[0]) return p->err_desc;   // el motivo que da Steam
	// Con tarjeta, como la web de Steam: 0 (y 86 si al final falla) es que la compañía de la tarjeta lo rechazó
	if (p->method == PM_CARD && !p->err[0]) {
		switch (p->detail) {
		case 0: case 86: return _("La compañía de tu tarjeta ha rechazado el pago. A veces el banco retiene el importe un tiempo, pero no se realiza ningún cargo.");
		case 1: return _("La compañía de tu tarjeta ha rechazado el pago porque la dirección de facturación no coincide. Revísala en la app oficial de Steam.");
		case 2: return _("La compañía de tu tarjeta ha rechazado el pago por saldo insuficiente.");
		}
	}
	switch (p->detail) {
	case 2: return _("Saldo insuficiente.");
	case 4: case 34: return _("La compra ha caducado y Steam la ha cancelado. No se ha realizado ningún cargo.");
	case 6: case 16: case 49: return _("Este método de pago no está disponible para esta compra. Elige otro o paga desde el móvil.");
	case 8: case 45: case 52: return _("Tienes otra compra pendiente en Steam. Complétala o cancélala desde el móvil e inténtalo de nuevo.");
	case 9: return _("Ya tienes uno de los artículos del carrito. Elimínalo e inténtalo de nuevo.");
	case 11: return _("El banco o Steam han rechazado el pago por motivos de seguridad. Inténtalo desde el móvil o contacta con tu banco.");
	case 13: case 19: case 23: return _("Algún artículo del carrito no está disponible en tu país.");
	case 24: return _("Algún artículo del carrito requiere el juego base.");
	case 33: return _("La tarjeta guardada ha caducado. Actualízala en la app oficial de Steam.");
	case 47: return _("El carrito contiene artículos duplicados (un lote incluye otro). Elimina uno.");
	case 53: return _("Demasiados intentos. Espera unos minutos.");
	case 82: return _("Este método de pago no está disponible en este momento. Inténtalo más tarde.");
	case 44: return _("Las compras de tu cuenta están bloqueadas. Contacta con el soporte de Steam.");
	case 46: return _("Por seguridad, Steam ha rechazado la compra y limitará las compras durante un tiempo. Contacta con el soporte de Steam.");
	}
	if (!strcmp(p->err, "sin cookies del checkout")) return _("La sesión no está lista. Cierra sesión y vuelve a iniciarla.");
	if (!strcmp(p->err, "pagina de pago no reconocida")) return _("No se pudo abrir el pago. Vuelve a iniciar sesión o paga desde el móvil.");
	if (!strcmp(p->err, "verificacion rechazada")) return _("El banco no verificó la compra. No se ha realizado ningún cargo.");
	if (p->err[0] && strncmp(p->err, "HTTP", 4) && strncmp(p->err, "curl", 4)) return _("Steam ha rechazado el pago.");
	if (p->err[0]) return _("No se puede conectar con Steam. No se ha realizado ningún cargo.");
	return _("Steam ha rechazado el pago.");
}

static void pay_events(void)
{
	static PayState last;
	const PayInfo *p = pay_info();
	if (p->state == last) return;
	if (p->state == PAY_DONE) {
		// Lo comprado ya es tuyo y el carrito se vació
		library_changed();
		shop_cart_load();
	}
	if (p->state == PAY_REVIEW) {
		S.pay_hold = 0;
		S.pay_armed = false;
		S.pay_ssa = S.pay_wd = false;   // se marcan en cada compra, como en la web
		S.pay_focus = 0;
	}
	if (p->state == PAY_CHOOSE) S.paysel = p->wallet ? 0 : p->card ? 1 : 2;
	last = p->state;
}

static void open_cvv_keyboard(void)
{
	SwkbdState kb;
	char buf[8] = "";
	swkbdInit(&kb, SWKBD_TYPE_NUMPAD, 2, 4);
	swkbdSetHintText(&kb, _("CVV (3 o 4 dígitos, en el reverso de la tarjeta)"));
	swkbdSetPasswordMode(&kb, SWKBD_PASSWORD_HIDE_DELAY);
	swkbdSetButton(&kb, SWKBD_BUTTON_LEFT, _("Cancelar"), false);
	swkbdSetButton(&kb, SWKBD_BUTTON_RIGHT, _("Aceptar"), true);
	swkbdSetFeatures(&kb, SWKBD_DARKEN_TOP_SCREEN | SWKBD_ALLOW_HOME | SWKBD_ALLOW_RESET | SWKBD_ALLOW_POWER);
	swkbdSetValidation(&kb, SWKBD_FIXEDLEN, 0, 0);
	swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	if (swkbdInputText(&kb, buf, sizeof buf) == SWKBD_BUTTON_RIGHT) {
		pay_set_cvv(buf);
		if (!pay_info()->cvv_set) toast(_("El CVV debe tener 3 o 4 dígitos"));
	}
	memset(buf, 0, sizeof buf);
	memset(&kb, 0, sizeof kb);
}

static void draw_pay_top(void)
{
	const PayInfo *p = pay_info();
	if (S.pay_qr) {
		draw_qr_panel("https://store.steampowered.com/cart/", _("Pagar en el móvil"),
			_("Escanea el código para abrir tu carrito de Steam en el móvil y completar la compra."), NULL, NULL, 0);
		return;
	}
	if (p->state == PAY_BANK) {
		draw_qr_panel(p->bank_url, _("Verificación del banco"),
			_("Escanea el código con el móvil y confirma la compra según te indique tu banco. Esta pantalla se actualizará automáticamente."),
			NULL, _("Si no la verificas, no se realizará ningún cargo."), COL_DIM);
		return;
	}
	if (p->state == PAY_DONE) {
		float a = ease_out_back(clampf((osGetTime() - p->since) / 450.0f, 0, 1));
		float cx = TOP_W / 2, cy = 82;
		ui_circle(cx, cy, 30 * a, C2D_Color32(0x5c, 0x7e, 0x10, 0xff));
		// La marca se traza sola: primero el trazo corto y luego el largo
		float k = clampf((osGetTime() - p->since - 200) / 350.0f, 0, 1);
		float k1 = clampf(k / 0.35f, 0, 1), k2 = clampf((k - 0.35f) / 0.65f, 0, 1);
		float ax = cx - 13, ay = cy + 1, bx = cx - 4, by = cy + 10, ex = cx + 14, ey = cy - 9;
		if (k1 > 0) C2D_DrawLine(ax, ay, COL_WHITE, ax + (bx - ax) * k1, ay + (by - ay) * k1, COL_WHITE, 5, 0.5f);
		if (k2 > 0) C2D_DrawLine(bx, by, COL_WHITE, bx + (ex - bx) * k2, by + (ey - by) * k2, COL_WHITE, 5, 0.5f);
		const char *t = p->approval ? _("Compra pendiente de aprobación") : p->pending ? _("Compra en proceso") : _("Compra completada");
		ui_text(cx, 124, t, SZ_TITLE, COL_WHITE, AL_CENTER, 380);
		char l[96];
		snprintf(l, sizeof l, _("Total: %s"), p->receipt_total);
		ui_text(cx, 152, l, SZ_BODY, COL_LIME, AL_CENTER, 380);
		if (p->receipt_id[0]) {
			snprintf(l, sizeof l, _("Código de compra: %s"), p->receipt_id);
			ui_text(cx, 172, l, SZ_SMALL, COL_DIM, AL_CENTER, 380);
		}
		ui_text(cx, 192, p->approval ? _("El administrador de tu familia de Steam debe aprobarla.")
			: p->pending ? _("Steam está procesando la compra. Recibirás el recibo por correo electrónico.")
			: _("Ya está en tu biblioteca. Recibirás el recibo por correo electrónico."), SZ_SMALL, COL_TXT, AL_CENTER, 380);
		return;
	}
	// Resumen del carrito
	const CartInfo *c = shop_cart();
	int shown_n = c->n < 7 ? c->n : 7;
	float ph = 30 + shown_n * 19 + (c->n > 7 ? 19 : 0) + 30;
	fx_glass(8, 22, TOP_W - 16, ph, 16, 1, 0);
	ui_icon(IC_CART, 16, 29, 14, COL_ACC);
	ui_text(38, 28, _("Pagar el carrito"), SZ_LARGE, COL_WHITE, AL_LEFT, 300);
	ui_hline(16, 47, TOP_W - 32, C2D_Color32(0xff, 0xff, 0xff, 0x14));
	float y = 54;
	for (int i = 0; i < c->n && i < 7; i++, y += 19) {
		ui_text(16, y, c->line[i].name, SZ_SMALL, COL_TXT, AL_LEFT, 290);
		ui_text(TOP_W - 16, y, c->line[i].price, SZ_SMALL, COL_TXT, AL_RIGHT, 0);
		ui_hline(16, y + 16, TOP_W - 32, COL_LINE);
	}
	if (c->n > 7) {
		char more[48];
		snprintf(more, sizeof more, _("y %d más"), c->n - 7);
		ui_text(16, y, more, SZ_SMALL, COL_DIM, AL_LEFT, 0);
		y += 19;
	}
	const char *tot = p->total_fmt[0] ? p->total_fmt : c->subtotal;
	float tw = ui_text_w(tot, SZ_LARGE) + 20;
	ui_rrect(TOP_W - 16 - tw, y + 2, tw, 22, 11, C2D_Color32(0xbe, 0xee, 0x11, 0x22));
	ui_text(16, y + 6, _("Total"), SZ_BODY, COL_WHITE, AL_LEFT, 0);
	ui_text(TOP_W - 26, y + 4, tot, SZ_LARGE, COL_LIME, AL_RIGHT, 0);
	if (p->state == PAY_REVIEW)
		ui_text_wrap(16, SCR_H - 40, TOP_W - 32, _("Los reembolsos de Steam siguen disponibles: 14 días desde la compra y menos de 2 horas de juego."), SZ_TINY, COL_DIM, 2, 11);
	else
		ui_text(TOP_W / 2, SCR_H - 18, _("Los métodos de pago solo se pueden cambiar en la app oficial de Steam"), SZ_SMALL, COL_DIM, AL_CENTER, 380);
}

static void draw_pay_bottom(void)
{
	const PayInfo *p = pay_info();
	ui_rect(0, 0, BOT_W, LIST_Y, COL_BAR);
	ui_text(12, 5, _("Pagar"), SZ_SMALL, COL_WHITE, AL_LEFT, 200);
	ui_rect(0, LIST_Y - 1, BOT_W, 1, COL_LINE);
	char t[128];
	switch (p->state) {
	case PAY_LOADING:
	case PAY_PREPARING:
		ui_spinner(BOT_W / 2, 100, 9, COL_ACC);
		ui_text(BOT_W / 2, 124, p->state == PAY_LOADING ? _("Abriendo el pago…") : _("Calculando el total…"), SZ_BODY, COL_TXT, AL_CENTER, 300);
		break;
	case PAY_CHOOSE: {
		ui_text(BOT_W / 2, 28, _("¿Cómo quieres pagar?"), SZ_SMALL, COL_DIM, AL_CENTER, 0);
		bool pw = S.pressed == BT_PAYW, pc = S.pressed == BT_PAYC;
		draw_button_k(&B_PAYW, NULL, S.paysel == 0, IC_COIN, _("Cartera de Steam"), C2D_Color32(0x75, 0xb0, 0x22, 0xff), C2D_Color32(0x58, 0x8a, 0x1b, 0xff), COL_WHITE, p->wallet ? 1 : 0.4f, pw);
		snprintf(t, sizeof t, _("Saldo: %s"), p->wallet_fmt[0] ? p->wallet_fmt : "–");
		ui_text(B_PAYW.x + B_PAYW.w - 10, B_PAYW.y + 10, t, SZ_SMALL, col_alpha(COL_WHITE, p->wallet ? 1 : 0.5f), AL_RIGHT, 0);
		if (p->card && p->card_last4[0]) snprintf(t, sizeof t, _("%s terminada en %s"), card_name(p->card_type), p->card_last4);
		else if (p->card) snprintf(t, sizeof t, "%s", card_name(p->card_type));
		else snprintf(t, sizeof t, "%s", _("Sin tarjeta guardada"));
		draw_button_k(&B_PAYC, NULL, S.paysel == 1, IC_CARD, t, C2D_Color32(0x47, 0xbf, 0xff, 0xff), C2D_Color32(0x1a, 0x44, 0xc2, 0xff), COL_WHITE, p->card ? 1 : 0.4f, pc);
		draw_button_k(&B_PAYQ, "X", S.paysel == 2, IC_PHONE, _("Pagar en el móvil (QR)"), COL_BTN2, COL_BTN2, COL_ACC, 1, S.pressed == BT_PAYQ);
		ui_text_wrap(16, 160, BOT_W - 32, _("Solo puedes usar los métodos de pago guardados en tu cuenta. Para añadir o cambiar una tarjeta, usa la app oficial de Steam."), SZ_TINY, COL_DIM, 3, 11);
		break;
	}
	case PAY_REVIEW: {
		if (p->method == PM_WALLET) snprintf(t, sizeof t, _("Cartera de Steam · Saldo: %s"), p->wallet_fmt);
		else snprintf(t, sizeof t, _("%s terminada en %s"), card_name(p->card_type), p->card_last4);
		ui_text(BOT_W / 2, 30, t, SZ_SMALL, COL_DIM, AL_CENTER, 300);
		ui_text(BOT_W / 2, 46, p->total_fmt, SZ_TITLE, COL_WHITE, AL_CENTER, 300);
		if (p->short_wallet) {
			ui_text_wrap(20, 92, BOT_W - 40, _("Tu saldo no cubre el total. Pulsa B para elegir otro método de pago o paga desde el móvil."), SZ_SMALL, C2D_Color32(0xff, 0x8a, 0x7a, 0xff), 3, 14);
			break;
		}
		if (p->need_cvv) {
			snprintf(t, sizeof t, "%s", p->cvv_set ? _("CVV introducido · Cambiar") : _("Introduce el CVV"));
			draw_button_k(&B_CVV, "Y", false, IC_LOCK, t, COL_BTN2, COL_BTN2, p->cvv_set ? COL_LIME : COL_ACC, 1, S.pressed == BT_CVV);
		}
		// Las casillas: hay que marcarlas, como en la web de Steam
		for (int k = 0; k < 2; k++) {
			Box cb;
			if (!pay_check_box(p, k, &cb)) continue;
			bool on = k == 0 ? S.pay_ssa : S.pay_wd;
			if (S.pay_focus == k) ui_rrect(cb.x, cb.y, cb.w, cb.h, 6, col_alpha(COL_WHITE, 0.075f + 0.035f * sinf(g_time * 2.4f)));
			ui_rrect(cb.x + 6, cb.y + 4, 14, 14, 3, on ? COL_ACC : col_alpha(COL_WHITE, 0.14f));
			if (!on) ui_rrect(cb.x + 7, cb.y + 5, 12, 12, 2, C2D_Color32(0x16, 0x20, 0x2e, 0xff));
			else ui_icon(IC_CHECK, cb.x + 8, cb.y + 5, 10, COL_WHITE);
			ui_text_wrap(cb.x + 28, cb.y + 4, cb.w - 34, k == 0 ? pay_ssa_text(p) : pay_wd_text(p), SZ_TINY, on ? COL_WHITE : COL_TXT, 4, 11);
		}
		bool ready = (!p->need_cvv || p->cvv_set) && pay_accepted(p);
		// El botón se llena mientras mantienes A (o el dedo): así no se paga por un toque sin querer
		float f = clampf(S.pay_hold, 0, 1);
		const Box *b = &B_PAYGO;
		FxButton gb = {C2D_Color32(0x2b, 0x45, 0x12, 0xff), ready ? 1 : 0.4f, false, S.pay_focus == 2 && ready};
		fx_button(b->x, b->y, b->w, b->h, b->h / 2, &gb);
		if (f > 0) {
			// Se llena de verde vivo mientras mantienes A
			float fw = (b->w - 2) * f;
			if (fw < b->h - 2) fw = b->h - 2;
			ui_rrect(b->x + 1, b->y + 1, fw, b->h - 2, (b->h - 2) / 2, C2D_Color32(0x6f, 0xa8, 0x1e, 0xff));
			ui_vgrad(b->x + 8, b->y + 2, fw - 16 > 0 ? fw - 16 : 0, b->h * 0.4f, col_alpha(COL_WHITE, 0.18f), col_alpha(COL_WHITE, 0));
		}
		snprintf(t, sizeof t, _("Mantén A para pagar %s"), p->total_fmt);
		ui_text(b->x + b->w / 2, b->y + 8, t, SZ_BODY, col_alpha(COL_WHITE, ready ? 1 : 0.5f), AL_CENTER, b->w - 16);
		break;
	}
	case PAY_PAYING:
		ui_spinner(BOT_W / 2, 96, 9, COL_LIME);
		ui_text(BOT_W / 2, 120, _("Pagando…"), SZ_LARGE, COL_WHITE, AL_CENTER, 0);
		ui_text(BOT_W / 2, 144, _("No apagues la consola ni cierres la app"), SZ_SMALL, COL_DIM, AL_CENTER, 300);
		break;
	case PAY_BANK: {
		int secs = (int)((osGetTime() - p->since) / 1000);
		ui_spinner(BOT_W / 2, 80, 9, COL_ACC);
		ui_text(BOT_W / 2, 104, _("Esperando a tu banco…"), SZ_LARGE, COL_WHITE, AL_CENTER, 0);
		snprintf(t, sizeof t, "%d:%02d", secs / 60, secs % 60);
		ui_text(BOT_W / 2, 128, t, SZ_BODY, COL_DIM, AL_CENTER, 0);
		ui_text_wrap(20, 152, BOT_W - 40, _("Escanea el código QR de la pantalla superior con el móvil. Esta pantalla se actualizará automáticamente."), SZ_SMALL, COL_TXT, 3, 14);
		break;
	}
	case PAY_DONE: {
		fx_glass(30, 60, BOT_W - 60, 90, 18, 1, 0);
		ui_icon(IC_PAD, BOT_W / 2 - 12, 74, 24, COL_LIME);
		ui_text(BOT_W / 2, 106, _("Gracias por tu compra"), SZ_LARGE, COL_WHITE, AL_CENTER, 0);
		ui_text(BOT_W / 2, 128, _("Pulsa A para ir a tu biblioteca"), SZ_SMALL, COL_DIM, AL_CENTER, 240);
		break;
	}
	case PAY_FAILED:
		fx_glass(14, 30, BOT_W - 28, 160, 18, 1, 0);
		ui_icon(IC_CART, BOT_W / 2 - 12, 40, 24, C2D_Color32(0xff, 0x8a, 0x7a, 0xff));
		ui_text(BOT_W / 2, 74, _("No se pudo pagar"), SZ_LARGE, COL_WHITE, AL_CENTER, 0);
		ui_text_wrap(20, 98, BOT_W - 40, pay_err_text(p), SZ_SMALL, COL_TXT, 5, 14);
		if (p->detail || p->err[0]) {
			snprintf(t, sizeof t, _("Código %d %s"), p->detail, p->err);
			ui_text(BOT_W / 2, 176, t, SZ_TINY, COL_DIM, AL_CENTER, 300);
		}
		break;
	case PAY_NONE:
		break;
	}
	if (S.pay_qr) {
		static const char *h[] = {"B", "Volver"};
		draw_hints(h, 1);
	} else if (p->state == PAY_DONE) {
		static const char *h[] = {"A", "Biblioteca", "B", "Tienda"};
		draw_hints(h, 2);
	} else if (p->state == PAY_FAILED && !p->maybe_paid) {
		static const char *h[] = {"A", "Reintentar", "X", "Pagar en el móvil", "B", "Volver"};
		draw_hints(h, 3);
	} else if (p->state == PAY_CHOOSE) {
		static const char *h[] = {"↑↓", "Elegir", "A", "Aceptar", "B", "Volver"};
		draw_hints(h, 3);
	} else if (p->state == PAY_REVIEW && !p->short_wallet) {
		static const char *hc[] = {"↑↓", "Elegir", "A", "Marcar", "B", "Volver"};
		static const char *hp[] = {"↑↓", "Elegir", "A", "Mantener para pagar", "B", "Volver"};
		draw_hints(S.pay_focus == 2 ? hp : hc, 3);
	} else if (!pay_busy() && p->state != PAY_LOADING && p->state != PAY_PREPARING) {
		static const char *h[] = {"B", "Volver"};
		draw_hints(h, 1);
	}
}

static void input_pay(u32 kd, u32 kh, u32 ku, touchPosition *tp, float dt)
{
	const PayInfo *p = pay_info();
	if (S.pay_qr) {
		if (kd & (KEY_B | KEY_X)) S.pay_qr = false;
		return;
	}
	if (pay_busy()) return;   // ya se mandó: nada de salir a medias
	if (p->state == PAY_DONE) {
		if (kd & KEY_A) {
			pay_close();
			S.prev_tab = TAB_HOME;
			open_account_list(TAB_LIBRARY);
		} else if (kd & KEY_B) {
			pay_close();
			S.tab = S.prev_tab < TAB_COUNT ? S.prev_tab : TAB_HOME;
			go_scene(SC_STORE, -1);
		}
		return;
	}
	if (kd & KEY_B) {
		// En el resumen, B vuelve a elegir método (y Steam olvida la compra preparada)
		if (p->state == PAY_REVIEW) { pay_close(); pay_open(); }
		else close_pay();
		return;
	}
	if (p->state == PAY_FAILED) {
		if ((kd & KEY_A) && !p->maybe_paid) pay_open();
		if (kd & KEY_X) S.pay_qr = true;
		return;
	}
	if (p->state == PAY_CHOOSE) {
		static const int ids[] = {BT_PAYW, BT_PAYC, BT_PAYQ};
		int b = press_update(kd, kh, ku, tp, ids, 3);
		if (key_repeat(kd, kh, KEY_DOWN)) S.paysel = (S.paysel + 1) % 3;
		if (key_repeat(kd, kh, KEY_UP)) S.paysel = (S.paysel + 2) % 3;
		if (kd & KEY_A) b = ids[S.paysel];
		if (b == BT_PAYW) { if (p->wallet) pay_choose(PM_WALLET); else toast(_("Tu cartera no tiene saldo")); }
		if (b == BT_PAYC) { if (p->card) pay_choose(PM_CARD); else toast(_("Guarda una tarjeta desde la app oficial de Steam")); }
		if (b == BT_PAYQ || (kd & KEY_X)) S.pay_qr = true;
		return;
	}
	if (p->state == PAY_REVIEW && !p->short_wallet) {
		if (p->need_cvv) {
			static const int ids[] = {BT_CVV};
			if (press_update(kd, kh, ku, tp, ids, 1) == BT_CVV || (kd & KEY_Y)) S.want_cvv = true;
		}
		// Cruceta: casillas y botón de pagar; A marca la casilla elegida. También se marcan tocándolas
		int last = p->withdrawal ? 1 : 0;
		if (key_repeat(kd, kh, KEY_DOWN)) S.pay_focus = S.pay_focus >= last ? 2 : S.pay_focus + 1;
		if (key_repeat(kd, kh, KEY_UP)) S.pay_focus = S.pay_focus == 2 ? last : S.pay_focus > 0 ? S.pay_focus - 1 : 0;
		for (int k = 0; k < 2; k++) {
			Box cb;
			if (!pay_check_box(p, k, &cb)) continue;
			bool tap = (kd & KEY_TOUCH) && hit(&cb, tp->px, tp->py);
			if (tap || (S.pay_focus == k && (kd & KEY_A))) {
				if (k == 0) S.pay_ssa = !S.pay_ssa;
				else S.pay_wd = !S.pay_wd;
				S.pay_focus = k;
				S.pay_armed = false;
			}
		}
		bool ready = (!p->need_cvv || p->cvv_set) && pay_accepted(p);
		// Hay que soltar A una vez dentro del resumen (el A que te trajo aquí no cuenta)
		if (!(kh & KEY_A) && !(kh & KEY_TOUCH)) S.pay_armed = true;
		bool holding = S.pay_armed && ((S.pay_focus == 2 && (kh & KEY_A)) || ((kh & KEY_TOUCH) && hit(&B_PAYGO, tp->px, tp->py)));
		if (holding && !ready) {
			if (kd & (KEY_A | KEY_TOUCH))
				toast(!pay_accepted(p) ? _("Acepta las condiciones para continuar") : _("Introduce primero el CVV"));
			S.pay_hold = 0;
		} else if (holding) {
			S.pay_hold += dt / PAY_HOLD_S;
			if (S.pay_hold >= 1) {
				S.pay_hold = 0;
				S.pay_armed = false;
				pay_confirm();
			}
		} else {
			S.pay_hold = approach(S.pay_hold, 0, 3);
		}
	}
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
	case BT_BACKBAR: return &B_BACKBAR;
	case BT_PLIB: return &B_PLIB;
	case BT_PWISH: return &B_PWISH;
	case BT_PLOGOUT: return &B_PLOGOUT;
	case BT_PAYW: return &B_PAYW;
	case BT_PAYC: return &B_PAYC;
	case BT_PAYQ: return &B_PAYQ;
	case BT_CVV: return &B_CVV;
	case BT_PAYGO: return &B_PAYGO;
	case BT_DEMO: return &B_DEMO;
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

static void pick_choose(const AppInfo *a, int i)
{
	int n = 0;
	const BuyOpt *o = shop_buyopts(a->appid, &n, NULL);
	S.pick_open = false;
	if (!o || i < 0 || i >= n) return;
	if (o[i].free) shop_free(a->appid, a->name);
	else shop_cart_add(a->appid, &o[i]);
}

static void input_pick(const AppInfo *a, u32 kd, u32 kh, touchPosition *tp)
{
	int n = 0;
	shop_buyopts(a->appid, &n, NULL);
	if (!n) { S.pick_open = false; return; }
	if (key_repeat(kd, kh, KEY_DOWN)) S.pick_sel = (S.pick_sel + 1) % n;
	if (key_repeat(kd, kh, KEY_UP)) S.pick_sel = (S.pick_sel + n - 1) % n;
	if (kd & KEY_B) S.pick_open = false;
	if (kd & KEY_A) pick_choose(a, S.pick_sel);
	if (kd & KEY_TOUCH) {
		float y0 = pick_y(n);
		int i = (int)((tp->py - y0 - 34) / PICK_ROW);
		if (tp->py < y0) S.pick_open = false;
		else if (tp->py >= y0 + 34 && i >= 0 && i < n) {
			S.pick_sel = i;
			pick_choose(a, i);
		}
	}
}

// El botón verde de la ficha
static void game_cart_button(const AppInfo *a)
{
	if (owned_app(a->appid)) {
		toast(_("Ya está en tu biblioteca"));
		return;
	}
	if (shop_busy()) {
		toast(_("Espera a que termine la acción anterior"));
		return;
	}
	int n = 0;
	bool failed = false;
	const BuyOpt *o = shop_buyopts(a->appid, &n, &failed);
	if (a->is_free || a->is_demo) {
		shop_free(a->appid, a->name);
	} else if (shop_in_cart(a->appid, o, n)) {
		open_account_list(TAB_CART);
	} else if (!o) {
		if (failed) shop_buyopts_load(a->appid);
		toast(failed ? _("No se pudieron cargar las opciones de compra. Reintentando…") : _("Cargando las ediciones disponibles…"));
	} else if (n == 0) {
		toast(a->coming_soon ? _("Este juego aún no está disponible para comprar") : _("No está a la venta en tu país en este momento"));
	} else if (n == 1) {
		if (o[0].free) shop_free(a->appid, a->name);
		else shop_cart_add(a->appid, &o[0]);
	} else {
		S.pick_open = true;
		S.pick_sel = 0;
		S.pick_anim = 0;
	}
}

static void input_game(u32 kd, u32 kh, u32 ku, touchPosition *tp)
{
	InfoEnt *e = info_get(S.app, false);
	const AppInfo *a = (e && e->done) ? &e->info : NULL;
	if (S.pick_open && a) {
		input_pick(a, kd, kh, tp);
		return;
	}
	if (S.show_qr) {
		// El QR para comprar en el móvil tapa la ficha: solo se cierra
		if (kd & (KEY_B | KEY_Y)) S.show_qr = false;
		return;
	}
	if (kd & KEY_B) { game_back(); return; }
	if ((kd & KEY_A) && e && e->failed) { info_retry(e); return; }
	// Botones de la consola para lo que hay en la pantalla táctil (la tecla va dibujada en cada botón):
	// Y carrito, SELECT demo, y X con la cruceta: → capturas, ← deseados, ↓ instalar en el PC
	int key_b = BT_NONE;
	bool combo = kh & KEY_X;
	if (a) {
		if (kd & KEY_Y) key_b = auth_logged() ? BT_CART : -1;
		if (combo && (kd & KEY_RIGHT)) key_b = BT_SHOTS;
		if (combo && (kd & KEY_LEFT)) key_b = BT_WISH;
		if (combo && (kd & KEY_DOWN)) key_b = BT_INST;
		if ((kd & KEY_SELECT) && S.has_demo_btn) key_b = BT_DEMO;
	}
	if (combo) {
		kd &= ~(KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN);   // la cruceta es de la combinación
		kh &= ~(KEY_UP | KEY_DOWN);
	}
	if (kd & (KEY_L | KEY_LEFT)) game_set_sec(S.sec - 1);
	if (kd & (KEY_R | KEY_RIGHT)) game_set_sec(S.sec + 1);
	if (S.sec == SEC_SUMMARY) {
		// El Resumen se desplaza con la cruceta (manteniendo, sigue bajando)
		float vis = SCR_H - 18;
		float max = S.ocontent - vis;
		if (max < 0) max = 0;
		if (kh & KEY_DOWN) S.oscroll_t += 220 * g_dt;
		if (kh & KEY_UP) S.oscroll_t -= 220 * g_dt;
		S.oscroll_t = clampf(S.oscroll_t, 0, max);
	}
	if (key_b == -1) {
		S.show_qr = true;   // sin sesión, Y lleva a comprar en el móvil
		return;
	}

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
	static const int ids[] = {BT_CART, BT_WISH, BT_SHOTS, BT_INST, BT_DEMO};
	int b = a ? press_update(kd, kh, ku, tp, ids, S.has_demo_btn ? 5 : 4) : BT_NONE;
	if (key_b) b = key_b;
	if (b == BT_SHOTS && a->nshots) {
		go_scene(SC_SHOTS, 1);
		S.shot = 0;
		S.strip = 0;
		S.shot_anim = 1;
	} else if (b == BT_CART && !auth_logged()) {
		S.show_qr = true;   // sin sesión, el botón verde lleva a comprar en el móvil
	} else if ((b == BT_WISH || b == BT_INST) && !auth_logged()) {
		toast(_("Disponible al iniciar sesión"));
	} else if (b == BT_CART) {
		game_cart_button(a);
	} else if (b == BT_WISH) {
		if (owned_app(a->appid)) toast(_("Ya está en tu biblioteca"));
		else if (shop_busy()) toast(_("Espera a que termine la acción anterior"));
		else shop_wish(a->appid, a->name, !in_wishlist(a->appid));
	} else if (b == BT_INST) {
		toast(_("La instalación remota estará disponible en una próxima actualización"));
	} else if (b == BT_DEMO) {
		if (owned_app(a->demo)) toast(_("Ya está en tu biblioteca"));
		else if (shop_busy()) toast(_("Espera a que termine la acción anterior"));
		else {
			char dn[140];
			snprintf(dn, sizeof dn, _("Demo de %s"), a->name);
			shop_free(a->demo, dn);
		}
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

// Por qué Steam no hizo lo que se le pidió, en palabras normales
static const char *shop_err(int code)
{
	switch (code) {
	case 2: return _("Se produjo un error en Steam. Inténtalo de nuevo.");
	case 15: return _("Esta acción no está permitida en esta cuenta");
	case 25: case 41: return _("Se ha alcanzado el límite de artículos");
	case 29: return _("No se requieren cambios");
	case 84: return _("Demasiadas solicitudes. Inténtalo de nuevo en unos minutos.");
	case 1009: return _("Ya está en tu biblioteca");
	case 1013: case 1019: return _("No está disponible en tu país");
	case 1024: return _("Necesitas el juego base");
	case 1004: case 1008: return _("Steam está ocupado. Inténtalo de nuevo más tarde.");
	}
	if (code < 0) return _("No se puede conectar con Steam");
	return _("Steam ha rechazado la solicitud");
}

static void shop_events(void)
{
	ShopEvent ev;
	while (shop_event(&ev)) {
		char t[160];
		// «Ya estaba» en deseados cuenta como hecho: el resultado es el que querías
		if (!ev.ok && ev.code == 29 && (ev.kind == SH_WISH_ADD || ev.kind == SH_WISH_REMOVE)) ev.ok = true;
		switch (ev.kind) {
		case SH_CART_ADD:
			if (ev.ok) snprintf(t, sizeof t, _("Añadido al carrito: %s"), ev.what);
			else snprintf(t, sizeof t, _("No se pudo añadir al carrito: %s"), shop_err(ev.code));
			break;
		case SH_CART_REMOVE:
			if (ev.ok) snprintf(t, sizeof t, "%s", _("Quitado del carrito"));
			else snprintf(t, sizeof t, _("No se pudo quitar: %s"), shop_err(ev.code));
			break;
		case SH_WISH_ADD:
		case SH_WISH_REMOVE:
			if (ev.ok) {
				wish_changed(ev.appid, ev.kind == SH_WISH_ADD);
				snprintf(t, sizeof t, "%s", ev.kind == SH_WISH_ADD ? _("Añadido a tu lista de deseos") : _("Quitado de tu lista de deseos"));
			} else {
				snprintf(t, sizeof t, _("No se pudo cambiar la lista de deseos: %s"), shop_err(ev.code));
			}
			break;
		case SH_FREE:
			if (ev.ok) {
				note_added(ev.appid);
				library_changed();
				snprintf(t, sizeof t, _("Añadido a tu biblioteca: %s"), ev.what);
			} else if (ev.code == 1009) {
				snprintf(t, sizeof t, "%s", _("Ya está en tu biblioteca"));
			} else {
				snprintf(t, sizeof t, _("No se pudo añadir a tu biblioteca: %s"), shop_err(ev.code));
			}
			break;
		}
		static const Icon ic[] = {[SH_CART_ADD] = IC_CART, [SH_CART_REMOVE] = IC_CART, [SH_WISH_ADD] = IC_HEART,
			[SH_WISH_REMOVE] = IC_HEART, [SH_FREE] = IC_PAD};
		toast_icon(t, ic[ev.kind]);
	}
}

static void start_store(void)
{
	Lang l = !strcmp(g_cfg.lang, "es") ? LANG_ES : !strcmp(g_cfg.lang, "en") ? LANG_EN : !strcmp(g_cfg.lang, "ja") ? LANG_JA : i18n_system();
	i18n_set(l);
	const char *cc = g_cfg.country[0] ? g_cfg.country : "US";
	steam_set_context(i18n_steam_lang(cc), cc);
	S.ctx_ready = true;
	netlog("tienda: pais=%s idioma=%s", steam_country(), steam_lang());
	tab_more(cur_ti());
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
	// En la New 3DS: CPU a 804 MHz y la caché L2 (sin esto va a 268 MHz, como la vieja)
	osSetSpeedupEnable(true);
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
	fx_init();
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
	wheel_sheet = C2D_SpriteSheetLoad("romfs:/gfx/wheel3d.t3x");
	ui_sheet = C2D_SpriteSheetLoad("romfs:/gfx/ui.t3x");
	if (ui_sheet) C3D_TexSetFilter(C2D_SpriteSheetGetImage(ui_sheet, 0).tex, GPU_LINEAR, GPU_LINEAR);
	if (sprites) {
		for (int i = 0; i < 3; i++)
			C3D_TexSetFilter(C2D_SpriteSheetGetImage(sprites, i).tex, GPU_LINEAR, GPU_LINEAR);
	}
	if (wheel_sheet) C3D_TexSetFilter(C2D_SpriteSheetGetImage(wheel_sheet, 0).tex, GPU_LINEAR, GPU_LINEAR);
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
		for (int ti = 0; ti < TAB_ALL; ti++) {
			Tab *t = &tabs[ti];
			if (ti == TAB_HOME) continue;   // Inicio no tiene lista propia: son las de TAB_H0…
			else if (ti == TAB_LIBRARY) owned_poll(t);
			else if (ti == TAB_WISHLIST) wish_poll(t);
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
					char term[sizeof t->term];
					memcpy(term, t->term, sizeof term);
					search_start(term);
				}
			} else {
				t->fail_at = 0;
			}
		}
		seen_poll();
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
		} else if (S.scene != SC_SPLASH && !pay_busy()) {
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
		if (S.want_cvv) {
			S.want_cvv = false;
			open_cvv_keyboard();
			kd = kh = ku = 0;
		}
		switch (S.scene) {
		case SC_STORE: input_store(kd, kh, ku, &tp); break;
		case SC_GAME: input_game(kd, kh, ku, &tp); break;
		case SC_SHOTS: input_shots(kd, kh, ku, &tp); break;
		case SC_SPLASH: splash_update(dt, kd); break;
		case SC_LOGIN: input_login(kd, dt); break;
		case SC_PROFILE: input_profile(kd, kh, ku, &tp); break;
		case SC_ABOUT: input_about(kd, kh); break;
		case SC_PAY: input_pay(kd, kh, ku, &tp, dt); break;
		}
		auth_update();
		// Con sesión, la biblioteca y los deseados se cargan en segundo plano (para «ya es tuyo» y
		// «en deseados» en las fichas); se espera a que la tienda sepa el país y el idioma
		if (auth_logged() && S.ctx_ready) {
			if (!tabs[TAB_LIBRARY].started) tab_more(TAB_LIBRARY);
			if (!tabs[TAB_WISHLIST].started) tab_more(TAB_WISHLIST);
			if (!tabs[TAB_CART].started) {
				tabs[TAB_CART].started = true;
				shop_cart_load();
			}
		}
		// Las formas de comprar (y cuándo acaba la oferta) se piden también sin sesión
		if (S.ctx_ready && (S.scene == SC_GAME || S.scene == SC_SHOTS)) shop_buyopts_load(S.app);
		shop_update();
		cart_sync();
		shop_events();
		pay_update();
		pay_events();
		// Avisos de la sesión: entró desde la sesión guardada, o Steam ya no la acepta
		if (auth_info()->state != S.last_auth) {
			if (auth_info()->state == AUTH_IN && S.scene == SC_GAME) friends_request(S.app);
			if (S.last_auth == AUTH_STARTING && auth_info()->state == AUTH_OUT && !strcmp(auth_info()->err, "expired"))
			{
				toast(_("Tu sesión ha caducado. Vuelve a iniciar sesión."));
				account_lists_clear();
			}
			S.last_auth = auth_info()->state;
		}

		// Vista previa: la ficha del seleccionado, cuando el cursor se queda quieto un momento
		if (S.scene == SC_STORE && cur_row() && cur_row()->appid && now - CUR->sel_changed > 250) {
			info_get(cur_row()->appid, true);
			if (auth_logged() && now - CUR->sel_changed > 600) friends_request(cur_row()->appid);
		}

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
		S.pick_anim = approach(S.pick_anim, S.pick_open ? 1 : 0, S.pick_open ? 12 : 18);
		S.sub_anim = approach(S.sub_anim, S.scene == SC_STORE && S.tab == TAB_HOME ? 1 : 0, 14);
		S.sub_x = approach(S.sub_x, S.home_sub, 16);
		S.oscroll = approach(S.oscroll, S.oscroll_t, 18);
		// La píldora primero llega a la lupa y solo entonces se estira
		bool at_lupa = fabsf(S.tab_x - TAB_SEARCH) < 0.03f;
		S.search_x = approach(S.search_x, S.tab == TAB_SEARCH && S.search_focus && at_lupa ? 1 : 0, 14);
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
			float rvis = SCR_H - 20 - 52, ry = 0, rt = S.rscroll;
			for (int i = 0; i < S.rsel && i < S.nreviews; i++) ry += review_h(i) + 5;
			float card = S.rsel < S.nreviews ? review_h(S.rsel) + 5 : 0;
			if (ry < rt) rt = ry;
			if (ry + card > rt + rvis) rt = ry + card - rvis;
			S.rscroll = approach(S.rscroll, rt, 18);
		}

#ifdef DEV_RELAY
		u64 tick_upd = svcGetSystemTick();
#endif
		C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
		ui_begin_frame();
		C2D_TargetClear(top, COL_BG);
		C2D_SceneBegin(top);
		if (S.scene != SC_SPLASH && S.scene != SC_SHOTS) fx_backdrop(GFX_TOP, TOP_W, SCR_H);
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
		case SC_PROFILE: draw_profile_top(); break;
		case SC_ABOUT: draw_about_top(); break;
		case SC_PAY: draw_pay_top(); break;
		}
		C2D_ViewReset();
		u32 fade_col = S.scene == SC_SHOTS ? C2D_Color32(0, 0, 0, 0xff) : COL_BG;
		if (ce < 1) ui_rect(0, 0, TOP_W, SCR_H, col_alpha(fade_col, (1 - ce) * 0.9f));
		if (S.scene == SC_STORE || S.scene == SC_GAME || S.scene == SC_LOGIN || S.scene == SC_PROFILE || S.scene == SC_PAY || S.scene == SC_ABOUT)
			draw_status(auth_logged() && auth_info()->persona[0] ? auth_info()->persona : NULL);
		if (S.scene == SC_SPLASH && splash_out > 0) ui_rect(0, 0, TOP_W, SCR_H, C2D_Color32(0, 0, 0, (u8)(255 * clampf(splash_out / 0.35f, 0, 1))));
		if (S.exit_anim > 0.01f) ui_rect(0, 0, TOP_W, SCR_H, C2D_Color32(0, 0, 0, (u8)(0x80 * ease_out_cubic(S.exit_anim))));
		if (S.quit_fade >= 0) ui_rect(0, 0, TOP_W, SCR_H, C2D_Color32(0, 0, 0, (u8)(255 * clampf(S.quit_fade / 0.3f, 0, 1))));

#ifdef DEV_RELAY
		u64 tick_top = svcGetSystemTick();
#endif
		C2D_TargetClear(bot, COL_BG);
		C2D_SceneBegin(bot);
		if (S.scene != SC_SPLASH) fx_backdrop(GFX_BOTTOM, BOT_W, SCR_H);
		C2D_ViewTranslate(S.scene_dir * (1 - ce) * 26, 0);
		switch (S.scene) {
		case SC_STORE: draw_store_bottom(); break;
		case SC_GAME: draw_game_bottom(); break;
		case SC_SHOTS: draw_shots_bottom(); break;
		case SC_SPLASH: draw_splash_bottom(); break;
		case SC_LOGIN: draw_login_bottom(); break;
		case SC_PROFILE: draw_profile_bottom(); break;
		case SC_ABOUT: draw_about_bottom(); break;
		case SC_PAY: draw_pay_bottom(); break;
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
			u64 tick_end = svcGetSystemTick();
			double ms = (tick_end - tick0) / (double)SYSCLOCK_ARM11 * 1000.0;
			// Un frame lento: en qué parte se fue el tiempo
			if (ms > 40) {
				double k = 1000.0 / SYSCLOCK_ARM11;
				netlog("lento: %.1fms escena=%d pago=%d actualizar=%.1f arriba=%.1f abajo=%.1f", ms, S.scene, pay_info()->state,
					(tick_upd - tick0) * k, (tick_top - tick_upd) * k, (tick_end - tick_top) * k);
			}
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
			if (wheel_sheet) C2D_SpriteSheetFree(wheel_sheet);
			sprites = splash_bg = wheel_sheet = NULL;
		}
	}

	netlog("salir");
	if (S.country_req) net_release(S.country_req);
	friends_reset();
	account_lists_clear();
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
	if (wheel_sheet) C2D_SpriteSheetFree(wheel_sheet);
	if (ui_sheet) C2D_SpriteSheetFree(ui_sheet);
	C2D_TextBufDelete(review_buf);
	img_exit();
	auth_exit();
	net_exit();
	fx_exit();
	ui_exit();
	util_exit();
	C2D_Fini();
	C3D_Fini();
	romfsExit();
	gfxExit();
	return 0;
}
