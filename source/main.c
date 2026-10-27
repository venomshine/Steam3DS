#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
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
#include "sfx.h"

// ============================================================================
// Estado

typedef enum { SC_SPLASH, SC_STORE, SC_GAME, SC_SHOTS, SC_LOGIN, SC_PROFILE, SC_PAY, SC_ABOUT, SC_NOTIF, SC_FRIENDS, SC_ACCOUNTS, SC_SETTINGS, SC_USER, SC_ACCSET, SC_PCDL, SC_POINTS, SC_INV, SC_HIST } Scene;
enum { TAB_HOME, TAB_OFFERS, TAB_DEMOS, TAB_BETAS, TAB_SEARCH, TAB_COUNT };
// Listas de tu cuenta: se ven como una pestaña más, pero sin barra de pestañas
// Pseudopestañas (cuenta) y las cuatro listas de Inicio (una por subcategoría)
enum { TAB_LIBRARY = TAB_COUNT, TAB_WISHLIST, TAB_CART, TAB_H0, TAB_ALL = TAB_H0 + 5 };
enum { SEC_SUMMARY, SEC_REQS, SEC_REVIEWS, SEC_ACH, SEC_EXTRAS, SEC_SIMILAR, SEC_COUNT };

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
	bool want_key;               // el teclado para activar un producto
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
	// Logros: los del juego y, si es tuyo, cuáles tienes
	Achievement *ach;
	int nach, ach_mine;          // ach_mine: los que tienes (-1 si no se sabe)
	u32 ach_for;
	Req *ach_req, *ach_mine_req;
	char ach_err[128];
	// Comparar con un amigo que lo tiene (cmp_i: cuál de FR.ids, -1 sin comparar)
	int cmp_i, cmp_n;
	bool *cmp_done, cmp_loaded;
	Req *cmp_req;
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
	Scene game_back;             // a dónde vuelve la ficha con B
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
	bool price_focus;            // Buscar: el foco está en la barra de precio
	bool price_drag;             // la barra de precio se está moviendo con el dedo
	int price;                   // Buscar: paso de la barra de precio (ver price_units)
	float price_x;               // el tirador, animado hacia price
	u64 price_changed;           // cuándo se movió la barra (se aplica al soltarla un momento)
	StoreRow top[3];             // Buscar: «Búsquedas populares», lo más visitado ahora en la tienda
	int ntop;
	Req *tags_req;               // la petición de esos tres
	float search_x;              // la píldora se estira sobre las pestañas (0..1)
	int pfocus;                  // botón elegido con la cruceta en el perfil
	bool has_demo_btn;           // la ficha tiene el botón de la demo (o el del playtest)
	bool demo_is_pt;             // ese botón es el de pedir acceso al playtest
	float oscroll, oscroll_t;    // Resumen: desplazamiento del texto (actual y destino)
	float ocontent;              // alto de todo el Resumen
} S;

enum { BT_NONE, BT_IGNORE, BT_CART, BT_WISH, BT_SHOTS, BT_INST, BT_PREV, BT_NEXT, BT_EXIT_NO, BT_EXIT_YES, BT_SEARCH, BT_BACKBAR,
	BT_PLIB, BT_PWISH, BT_PLOGOUT, BT_PFRIENDS, BT_PACT, BT_PSWITCH, BT_PAYW, BT_PAYC, BT_PAYQ, BT_CVV, BT_PAYGO, BT_DEMO, BT_UGAME, BT_UQR, BT_PVIEW, BT_RUP, BT_RDOWN, BT_RTEXT, BT_RVIS, BT_RCANCEL, BT_RSEND, BT_PDL, BT_UADD, BT_UADDW, BT_UIGN, BT_FOLLOW, BT_PPOINTS, BT_PINV };

// Pestañas de la tienda
#define HOME_PARTS 5
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
	bool browse;                 // Buscar sin escribir nada: todo el catálogo, por páginas
	u32 tag;                     // Buscar: el catálogo de una etiqueta (búsqueda popular), si no es 0
	char tag_name[40];
	int raw_next, raw_total;     // Buscar: por dónde va en el catálogo de Steam (sin el filtro de precio)
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

static const StoreQuery home_q[HOME_PARTS] = {Q_TOP, Q_NEW, Q_SOON, Q_F2P, Q_TOP};   // el último («Para ti») es tu cola
static const char *home_title[HOME_PARTS] = {"Lo más vendido", "Novedades populares", "Próximamente", "Gratis para jugar", "Recomendado para ti"};
#define TAB_FORYOU (TAB_H0 + 4)

// «Para ti»: tu cola de descubrimiento; cada página es una cola nueva (como «Siguiente cola» en la tienda)
static struct {
	Req *req;
	int pages;
} DQ;

static bool tab_paged(int t)
{
	return t == TAB_OFFERS || t == TAB_DEMOS || t == TAB_BETAS || t == TAB_WISHLIST || t >= TAB_H0 ||
		(t == TAB_SEARCH && tabs[TAB_SEARCH].browse) ||
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

// Barra de precio de Buscar, como la de la web de Steam: de «Gratis» a «Cualquier precio».
// Steam no deja pedir un precio máximo, así que se filtra aquí con el precio de cada juego
#define PRICE_STEPS 10
#define PRICE_ANY (PRICE_STEPS - 1)
static const int price_units[PRICE_STEPS] = {0, 5, 10, 15, 20, 30, 40, 50, 60, -1};   // 0 gratis, -1 cualquiera

// El formato de tu moneda sale de un precio que ya haya dado Steam («17,99€», «$17.99»…)
static bool money_template(char *pre, char *suf, char *sep)
{
	for (int ti = 0; ti < TAB_ALL; ti++)
		for (int i = 0; i < tabs[ti].list.count; i++) {
			const char *p = tabs[ti].list.rows[i].price;
			const char *a = strpbrk(p, "0123456789");
			if (!a) continue;
			const char *b = a;
			for (const char *q = a; *q; q++)
				if (*q >= '0' && *q <= '9') b = q;
			snprintf(pre, 12, "%.*s", (int)(a - p), p);
			snprintf(suf, 12, "%s", b + 1);
			*sep = (b - a >= 3 && (b[-2] < '0' || b[-2] > '9')) ? b[-2] : 0;
			return true;
		}
	return false;
}

// Cuántas unidades de tu moneda son «5 €» en la barra (los pasos van en la moneda de cada país)
static int money_factor(const char *pre, const char *suf)
{
	static const struct { const char *sym; int f; } t[] = {
		{"¥", 100}, {"￥", 100}, {"₩", 1000}, {"₹", 50}, {"R$", 5}, {"Mex$", 20}, {"CLP", 800}, {"COL$", 3000},
		{"S/", 3}, {"zł", 4}, {"kr", 10}, {"TL", 30}, {"₺", 30}, {"₴", 40}, {"₸", 400}, {"Rp", 10000},
		{"₱", 50}, {"฿", 30}, {"RM", 4}, {"NT$", 30}, {"HK$", 8}, {"₪", 3}, {"₫", 20000}, {"pуб", 80},
	};
	for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
		if (strstr(pre, t[i].sym) || strstr(suf, t[i].sym)) return t[i].f;
	return 1;
}

static int price_limit_cents(int step)
{
	char pre[12], suf[12], sep;
	int f = money_template(pre, suf, &sep) ? money_factor(pre, suf) : 1;
	return price_units[step] * f * 100;
}

static void price_label(int step, char *out, size_t n)
{
	int u = price_units[step];
	if (u < 0) {
		snprintf(out, n, "%s", _("Cualquier precio"));
		return;
	}
	if (u == 0) {
		snprintf(out, n, "%s", _("Gratis"));
		return;
	}
	char pre[12], suf[12], sep, amount[40];
	if (money_template(pre, suf, &sep)) {
		int v = u * money_factor(pre, suf);
		if (sep) snprintf(amount, sizeof amount, "%s%d%c00%s", pre, v, sep, suf);
		else snprintf(amount, sizeof amount, "%s%d%s", pre, v, suf);
	} else {
		snprintf(amount, sizeof amount, "%d €", u);
	}
	snprintf(out, n, _("Menos de %s"), amount);
}

static bool price_ok(const StoreRow *r)
{
	int u = price_units[S.price];
	if (u < 0 || r->header || !r->appid) return true;
	int c = r->free ? 0 : r->cents;
	if (u == 0) return c == 0;
	return c >= 0 && c < price_limit_cents(S.price);
}

static void tab_more(int ti)
{
	Tab *t = &tabs[ti];
	if (!S.ctx_ready || t->list.req || !tab_can_retry(t)) return;
	if (ti == TAB_HOME) {
		tab_more(TAB_H0 + S.home_sub);
		return;
	} else if (ti == TAB_FORYOU) {
		if (t->list.end || DQ.req) return;
		if (!auth_logged()) {
			snprintf(t->list.err, sizeof t->list.err, "%s", _("Inicia sesión (START) para ver los juegos que Steam te recomienda"));
			t->list.end = true;
			return;
		}
		DQ.req = steam_req_discovery(DQ.pages > 0, auth_token());
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
	} else if (ti == TAB_SEARCH) {
		// Sin escribir nada: el catálogo entero por relevancia (lo gratuito lo filtra ya Steam)
		if (!t->browse || t->list.end) return;
		t->list.req = steam_req_browse(t->raw_next, PAGE, price_units[S.price] == 0, t->tag);
		t->raw_next += PAGE;
	} else if (tab_paged(ti)) {
		if (t->list.end) return;
		static const StoreQuery q[TAB_COUNT] = {[TAB_OFFERS] = Q_OFFERS, [TAB_DEMOS] = Q_DEMOS, [TAB_BETAS] = Q_EARLY};
		// Betas quita lo que ya no está en acceso anticipado: la página siguiente va por lo que se
		// pidió a Steam, no por lo que queda (si no, se repetían juegos)
		int start = ti == TAB_BETAS ? t->raw_next : t->list.count;
		t->list.req = steam_req_query(q[ti], start, PAGE);
		if (ti == TAB_BETAS) t->raw_next += PAGE;
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

// Familia de Steam: cuando la biblioteca ya está, se pregunta si estás en una familia y qué juegos
// comparten los demás; se suman a la biblioteca con la etiqueta «Familia»
static struct { int stage; Req *req; u64 gid; } FAM;   // stage: 0 nada, 1 grupo, 2 juegos, 3 hecho

static void lib_sort(Tab *t);

static void family_reset(void)
{
	if (FAM.req) net_release(FAM.req);
	memset(&FAM, 0, sizeof FAM);
}

static void family_poll(void)
{
	Tab *t = &tabs[TAB_LIBRARY];
	if (!auth_logged()) return;
	if (FAM.stage == 0) {
		// Cuando la biblioteca ha terminado de cargar
		if (!t->started || t->list.req || !t->list.end || t->list.err[0]) return;
		FAM.req = steam_req_family_group(auth_info()->steamid, auth_token());
		FAM.stage = 1;
		return;
	}
	Req *r = FAM.req;
	if (!r || r->state < REQ_DONE) return;
	if (FAM.stage == 1) {
		int st = r->state == REQ_DONE ? steam_parse_family_group(r->data, &FAM.gid) : -1;
		netlog("familia: %s (HTTP %ld)", st == 1 ? "estás en una familia" : st == 0 ? "no estás en ninguna" : "respuesta no válida", r->http);
		net_release(r);
		FAM.req = NULL;
		FAM.stage = 3;
		if (st == 1) {
			FAM.req = steam_req_family_apps(FAM.gid, auth_info()->steamid, auth_token());
			FAM.stage = 2;
		}
		return;
	}
	RowList fam = {0};
	int n = r->state == REQ_DONE ? steam_parse_family_apps(r->data, auth_info()->steamid, &fam) : -1;
	net_release(r);
	FAM.req = NULL;
	FAM.stage = 3;
	int added = 0;
	for (int i = 0; i < fam.count; i++) {
		bool dup = false;
		for (int j = 0; j < t->list.count && !dup; j++) dup = t->list.rows[j].appid == fam.rows[i].appid;
		if (!dup) {
			add_row(&t->list, &fam.rows[i]);
			added++;
		}
	}
	rowlist_free(&fam);
	netlog("familia: %d juegos compartidos, %d nuevos en la biblioteca", n, added);
	if (added) {
		t->list.total = t->list.count;
		lib_sort(t);
	}
}

// Lo que la tienda sabe que tienes también trae DLC, bandas sonoras, vídeos o hardware: en la
// biblioteca, como en la de Steam, solo van los juegos, demos, mods, programas y betas
// (tipos de la tienda: 0 juego, 1 demo, 2 mod, 4 DLC, 6 programa, 11 música, 12 beta…)
static bool lib_type_ok(int type)
{
	return type == 0 || type == 1 || type == 2 || type == 6 || type == 12;
}

static void owned_poll(Tab *t)
{
	Req *r = t->list.req;
	if (!r || r->state < REQ_DONE) return;
	if (t->lib_stage == 2) {
		// Los nombres llegan de 50 en 50: se piden todos seguidos para poder ordenar por nombre
		bool fail = r->state != REQ_DONE;
		int before = t->list.count;
		list_poll(&t->list, true);
		int n = before;
		for (int i = before; i < t->list.count; i++)
			if (lib_type_ok(t->list.rows[i].type)) t->list.rows[n++] = t->list.rows[i];
		t->list.count = n;
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
		// En la misma respuesta vienen los juegos que has ignorado
		u32 *ign = NULL;
		int ni = r->state == REQ_DONE ? steam_parse_userdata_ignored(r->data, &ign) : -1;
		if (ni >= 0) shop_set_ignored(ign, ni);
		free(ign);
		u32 *fo = NULL;
		int nf = r->state == REQ_DONE ? steam_parse_userdata_followed(r->data, &fo) : -1;
		if (nf >= 0) shop_set_followed(fo, nf);
		free(fo);
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


// ---------------------------------------------------------------------------- Orden de la biblioteca y la lista de deseos

// Y las cambia: biblioteca por nombre, más jugados o jugados recientemente; deseados por prioridad,
// mayor descuento, precio más bajo o solo ofertas (lo que no está rebajado se aparta en wish_stash)
static int lib_mode, wish_mode;
static RowList wish_stash;
static const char *const lib_mode_name[] = {"Por nombre", "Más jugados", "Jugados recientemente"};
static const char *const wish_mode_name[] = {"Por prioridad", "Mayor descuento", "Precio más bajo", "Solo ofertas"};
static const Tab *sort_tab;

static int cmp_lib(const void *x, const void *y)
{
	const StoreRow *a = x, *b = y;
	if (lib_mode == 1 && a->minutes != b->minutes) return b->minutes - a->minutes;
	if (lib_mode == 2 && a->last_played != b->last_played) return a->last_played < b->last_played ? 1 : -1;
	return strcasecmp(a->name, b->name);
}

static int wish_prio(u32 appid)
{
	for (int i = 0; i < sort_tab->nwish; i++)
		if (sort_tab->wish[i] == appid) return i;
	return 1 << 20;
}

static int cmp_wish(const void *x, const void *y)
{
	const StoreRow *a = x, *b = y;
	if ((wish_mode == 1 || wish_mode == 3) && a->disc != b->disc) return b->disc - a->disc;
	if (wish_mode == 2) {
		// Gratis primero; sin precio (aún no a la venta), al final
		int ca = a->free ? 0 : a->cents < 0 ? 1 << 30 : a->cents, cb = b->free ? 0 : b->cents < 0 ? 1 << 30 : b->cents;
		if (ca != cb) return ca < cb ? -1 : 1;
	}
	return wish_prio(a->appid) - wish_prio(b->appid);
}

static void lib_apply(Tab *t)
{
	if (t->list.count > 1) qsort(t->list.rows, t->list.count, sizeof *t->list.rows, cmp_lib);
}

static void wish_apply(Tab *t)
{
	RowList *l = &t->list;
	if (wish_mode == 3) {
		int n = 0;
		for (int i = 0; i < l->count; i++) {
			if (l->rows[i].disc > 0) l->rows[n++] = l->rows[i];
			else add_row(&wish_stash, &l->rows[i]);
		}
		l->count = n;
	} else {
		for (int i = 0; i < wish_stash.count; i++) add_row(l, &wish_stash.rows[i]);
		wish_stash.count = 0;
	}
	sort_tab = t;
	if (l->count > 1) qsort(l->rows, l->count, sizeof *l->rows, cmp_wish);
}

static void list_mode_next(void)
{
	Tab *t = CUR;
	if (S.tab == TAB_LIBRARY) {
		lib_mode = (lib_mode + 1) % 3;
		lib_apply(t);
		toast_icon(_(lib_mode_name[lib_mode]), IC_PAD);
	} else if (S.tab == TAB_WISHLIST) {
		wish_mode = (wish_mode + 1) % 4;
		wish_apply(t);
		toast_icon(_(wish_mode_name[wish_mode]), IC_HEART);
	} else {
		return;
	}
	t->sel = 0;
	t->sel_y = 0;
	t->scroll = t->scroll_target = 0;
	t->sel_changed = osGetTime();
}

// Avisos de ofertas: al abrir la app, los juegos de tu lista de deseos que han entrado en oferta desde
// la última vez (se recuerdan en la SD para no avisar dos veces de la misma rebaja)
#define DEALS_FILE APP_DIR "/ofertas.txt"
static bool deals_checked;

static void deals_check(Tab *t)
{
	if (deals_checked || !t->wish_loaded || t->list.req || t->wish_next < t->nwish) return;
	deals_checked = true;
	// Lo ya avisado: «appid descuento» por línea
	u32 seen[256];
	int sd[256], ns = 0;
	FILE *f = fopen(DEALS_FILE, "r");
	if (f) {
		unsigned long a;
		int d;
		while (ns < 256 && fscanf(f, "%lu %d", &a, &d) == 2) seen[ns] = (u32)a, sd[ns++] = d;
		fclose(f);
	}
	const StoreRow *all[2] = {t->list.rows, wish_stash.rows};
	int counts[2] = {t->list.count, wish_stash.count};
	int nnew = 0;
	const StoreRow *first = NULL;
	f = fopen(DEALS_FILE ".tmp", "w");
	for (int k = 0; k < 2; k++)
		for (int i = 0; i < counts[k]; i++) {
			const StoreRow *r = &all[k][i];
			if (r->disc <= 0) continue;
			bool old = false;
			for (int j = 0; j < ns; j++) old |= seen[j] == r->appid && sd[j] >= r->disc;
			if (!old) {
				nnew++;
				if (!first || r->disc > first->disc) first = r;
			}
			if (f) fprintf(f, "%lu %d\n", (unsigned long)r->appid, r->disc);
		}
	if (f) {
		fclose(f);
		remove(DEALS_FILE);
		rename(DEALS_FILE ".tmp", DEALS_FILE);
	}
	netlog("ofertas: %d nuevas en tu lista de deseos", nnew);
	if (!nnew) return;
	char msg[160];
	if (nnew == 1) snprintf(msg, sizeof msg, _("En oferta en tu lista de deseos: %s (-%d %%)"), first->name, first->disc);
	else snprintf(msg, sizeof msg, _("%d juegos de tu lista de deseos están en oferta. El que más: %s (-%d %%)"), nnew, first->name, first->disc);
	toast_icon(msg, IC_HEART);
}

static void notif_reset(void);
static void flist_reset(void);

// Las listas de la tienda se vuelven a pedir (al cambiar de idioma)
static void store_tabs_reload(void)
{
	for (int ti = 0; ti < TAB_ALL; ti++) {
		if (ti >= TAB_COUNT && ti < TAB_H0) continue;   // biblioteca, deseados y carrito no cambian
		Tab *t = &tabs[ti];
		rowlist_free(&t->list);   // suelta también su petición
		for (int k = 0; k < HOME_PARTS; k++) rowlist_free(&t->part[k]);
		memset(t, 0, sizeof *t);
	}
	if (S.tags_req) net_release(S.tags_req);
	S.tags_req = NULL;
	S.ntop = 0;   // los populares, en el idioma nuevo
	tab_more(cur_ti());
}

static void account_lists_clear(void)
{
	family_reset();
	notif_reset();
	flist_reset();
	wish_stash.count = 0;
	deals_checked = false;
	for (int ti = TAB_LIBRARY; ti < TAB_H0; ti++) {
		Tab *t = &tabs[ti];
		rowlist_free(&t->list);
		free(t->wish);
		memset(t, 0, sizeof *t);
	}
}

static void open_pay(void);
static void gift_open(int line);
static bool gift_is_open(void);
static void user_person_live(const SteamPerson *p);
static void draw_gift_bottom(void);
static void input_gift(u32 kd, u32 kh, touchPosition *tp);

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
	family_reset();   // lo de la familia se vuelve a sumar cuando esté la biblioteca nueva
	rowlist_free(&t->list);   // suelta también su petición (soltarla antes era soltarla dos veces)
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

static void add_header(RowList *l, const char *name)
{
	StoreRow r = {0};
	r.header = true;
	snprintf(r.name, sizeof r.name, "%s", name);
	add_row(l, &r);
}

static void add_popular(RowList *l)
{
	if (!S.ntop) return;
	add_header(l, _("Búsquedas populares"));
	for (int i = 0; i < S.ntop; i++) add_row(l, &S.top[i]);
}

// Sin búsqueda: arriba los tres juegos más buscados del momento y, justo debajo, todo el catálogo
// por relevancia, por páginas y con el filtro de precio
static void search_show_browse(void)
{
	Tab *t = &tabs[TAB_SEARCH];
	if (t->list.req) net_release(t->list.req);
	t->list.req = NULL;
	t->list.count = 0;
	t->list.err[0] = 0;
	t->term[0] = 0;
	t->tag = 0;
	add_popular(&t->list);
	if (!S.ntop && !S.tags_req && S.ctx_ready) S.tags_req = steam_req_most_visited(10);
	add_header(&t->list, _("Explorar la tienda"));
	t->browse = true;
	t->raw_next = t->raw_total = 0;
	t->list.total = t->list.count;
	t->list.end = false;
	t->sel = 0;
	t->sel_y = 0;
	t->scroll = t->scroll_target = 0;
	t->sel_changed = osGetTime();
	tab_more(TAB_SEARCH);
}

// Una búsqueda popular: el catálogo con esa etiqueta (B vuelve)
static void search_tag(u32 id, const char *name)
{
	Tab *t = &tabs[TAB_SEARCH];
	char nm[40];
	snprintf(nm, sizeof nm, "%s", name);
	if (t->list.req) net_release(t->list.req);
	t->list.req = NULL;
	t->list.count = 0;
	t->list.err[0] = 0;
	t->term[0] = 0;
	t->tag = id;
	snprintf(t->tag_name, sizeof t->tag_name, "%s", nm);
	char h[96];
	snprintf(h, sizeof h, _("Juegos con la etiqueta «%s»"), nm);
	add_header(&t->list, h);
	t->browse = true;
	t->raw_next = t->raw_total = 0;
	t->list.total = t->list.count;
	t->list.end = false;
	t->sel = 0;
	t->sel_y = 0;
	t->scroll = t->scroll_target = 0;
	t->sel_changed = osGetTime();
	S.search_focus = S.price_focus = false;
	tab_more(TAB_SEARCH);
}

// Llegan las búsquedas populares: se quedan las tres primeras y, si se está viendo el catálogo, se meten
// encima
static void tags_poll(void)
{
	Req *r = S.tags_req;
	if (!r || r->state < REQ_DONE) return;
	RowList got = {0};
	bool ok = r->state == REQ_DONE && (steam_take_rows(r, &got, false) || steam_parse_rows(r->data, &got, false));
	net_release(r);
	S.tags_req = NULL;
	S.ntop = 0;
	for (int i = 0; ok && i < got.count && S.ntop < 3; i++)
		if (got.rows[i].appid) S.top[S.ntop++] = got.rows[i];
	rowlist_free(&got);
	if (!S.ntop) return;
	Tab *t = &tabs[TAB_SEARCH];
	if (!t->browse || t->tag || t->term[0]) return;
	const char *ex = _("Explorar la tienda");
	int h = -1;
	for (int i = 0; i < t->list.count && h < 0; i++)
		if (t->list.rows[i].header && !strcmp(t->list.rows[i].name, ex)) h = i;
	if (h < 0) return;
	RowList pop = {0};
	add_popular(&pop);
	int k = pop.count, before = t->list.count;
	for (int i = 0; i < k; i++) add_row(&t->list, &pop.rows[i]);   // hace sitio al final
	if (t->list.count != before + k) {
		t->list.count = before;   // sin memoria: se quedan sin las populares
		rowlist_free(&pop);
		return;
	}
	memmove(t->list.rows + h + k, t->list.rows + h, (before - h) * sizeof(StoreRow));
	memcpy(t->list.rows + h, pop.rows, k * sizeof(StoreRow));
	rowlist_free(&pop);
	t->list.total = t->list.count;
	if (t->sel >= h) t->sel += k;
	t->sel_y = t->sel;
	if (t->scroll_target > 0) t->scroll = t->scroll_target = t->scroll_target + k * ROW_H;
}

static void search_run(const char *term)
{
	Tab *t = &tabs[TAB_SEARCH];
	if (!term[0] || !S.ctx_ready) return;
	if (t->list.req) net_release(t->list.req);
	t->list.req = steam_req_search(term);
	t->browse = false;
	t->tag = 0;
	t->list.count = 0;
	t->list.err[0] = 0;
	t->list.end = true;
	if (term != t->term) snprintf(t->term, sizeof t->term, "%s", term);
	t->sel = 0;
	t->sel_y = 0;
	t->scroll = t->scroll_target = 0;
	t->sel_changed = osGetTime();
}

static void search_start(const char *term)
{
	if (!term[0] || !S.ctx_ready) return;
	recents_add(term);
	search_run(term);
}

// Lo que llega de Buscar: se quita lo que no entra en el precio elegido y, en el catálogo, se
// siguen pidiendo páginas hasta llenar la pantalla (con un precio bajo pasan pocos de cada página)
static void search_poll(Tab *t)
{
	Req *r = t->list.req;
	if (!r) {
		// El catálogo se va pidiendo solo mientras no llene la pantalla (también al empezar sin red)
		if (t->browse && !t->list.end && !t->list.err[0] && t->list.count < t->sel + 14) tab_more(TAB_SEARCH);
		return;
	}
	if (r->state < REQ_DONE) return;
	bool ok = r->state == REQ_DONE;
	int from = t->browse ? t->list.count : 0;
	list_poll(&t->list, t->browse);
	if (!ok || t->list.err[0]) {
		if (t->browse) t->raw_next -= PAGE;   // esa página se vuelve a pedir
		return;
	}
	if (t->browse) t->raw_total = t->list.total;
	int n = from;
	for (int i = from; i < t->list.count; i++)
		if (price_ok(&t->list.rows[i])) t->list.rows[n++] = t->list.rows[i];
	t->list.count = n;
	t->list.total = n;
	if (!t->browse) {
		t->list.end = true;
		return;
	}
	t->list.end = t->raw_next >= t->raw_total;
}

static void price_set(int step)
{
	if (step < 0) step = 0;
	if (step > PRICE_ANY) step = PRICE_ANY;
	if (step == S.price) return;
	S.price = step;
	S.price_changed = osGetTime();
}

// Con la barra quieta un momento, se vuelve a pedir lo que se estaba viendo con el precio nuevo
static void price_apply(void)
{
	if (!S.price_changed || osGetTime() - S.price_changed < 350 || S.price_drag) return;
	S.price_changed = 0;
	Tab *t = &tabs[TAB_SEARCH];
	if (t->term[0]) search_run(t->term);
	else if (t->tag) search_tag(t->tag, t->tag_name);
	else search_show_browse();
}

// ============================================================================
// Dibujo común

static int notif_unread(void);

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
	// La campana, con las notificaciones que no has leído
	int nu = notif_unread();
	if (nu) {
		char c[8];
		snprintf(c, sizeof c, "%d", nu);
		r -= ui_text(r, ty, c, SZ_SMALL, COL_ACC, AL_RIGHT, 0) + 2;
		ui_icon(IC_BELL, r - 11, 4, 11, COL_ACC);
		r -= 11 + 8;
	}
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
		if (draw) ui_text(x + w + 1, y + 5, "+", SZ_TINY, COL_DIM, AL_LEFT, 0);
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
		if (draw) ui_text(x, y + 5, g, SZ_SMALL, COL_WHITE, AL_LEFT, 0);   // a la altura de las teclas dibujadas (START)
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
	// Un título largo pasa a dos líneas en vez de cortarse
	int nt = ui_text_lines(title, SZ_LARGE, w);
	if (nt > 2) nt = 2;
	float h = ui_line_h(SZ_LARGE) + (nt - 1) * 20 + 10 + (nb - 1) * 14 + ui_line_h(SZ_SMALL);
	if (a) h += 12 + ih + 6 + ui_line_h(SZ_SMALL);
	if (nf) h += 10 + (nf - 1) * 14 + ui_line_h(SZ_SMALL);
	float y = 18 + (area - h) / 2;
	if (nt == 1) {
		float tw = ui_text(x, y, title, SZ_LARGE, COL_WHITE, AL_LEFT, w);
		ui_rrect(x, y + 19, tw < w ? tw : w, 2, 1, col_alpha(COL_ACC, 0.8f));   // subrayado, como al iniciar sesión
	} else {
		ui_text_wrap(x, y, w, title, SZ_LARGE, COL_WHITE, 2, 20);
		ui_rrect(x, y + 20 + 19, w * 0.6f, 2, 1, col_alpha(COL_ACC, 0.8f));
	}
	y += ui_line_h(SZ_LARGE) + (nt - 1) * 20 + 10;
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

static int deck_for(u32 appid);

static int feat_list(const AppInfo *a, Icon *ic, char (*lab)[32], int max)
{
	int n = 0;
#define ADD(i, t) do { if (n < max) { ic[n] = i; snprintf(lab[n], 32, "%s", t); n++; } } while (0)
	// Compatibilidad con Steam Deck, como la enseña la tienda (si ya se sabe)
	int deck = deck_for(a->appid);
	if (deck == 3) ADD(IC_CHECK, _("Steam Deck: verificado"));
	else if (deck == 2) ADD(IC_INFO, _("Steam Deck: jugable"));
	else if (deck == 1) ADD(IC_HIDE, _("Steam Deck: no compatible"));
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
	if (S.tab == TAB_SEARCH && !t->term[0] && (S.search_focus || S.price_focus || !row || !row->appid)) {
		// Buscar, sin búsqueda (o sin un juego elegido): qué se puede hacer
		ui_icon(IC_SEARCH, TOP_W / 2 - 18, 62, 36, COL_ACC);
		ui_text(TOP_W / 2, 112, _("Busca juegos, DLC y demos"), SZ_LARGE, COL_WHITE, AL_CENTER, 380);
		ui_text(TOP_W / 2, 136, _("Toca la barra de búsqueda o pulsa Y para escribir"), SZ_SMALL, COL_DIM, AL_CENTER, 380);
		ui_text(TOP_W / 2, 154, _("O explora la tienda abajo y filtra por precio"), SZ_SMALL, COL_DIM, AL_CENTER, 380);
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
			if (S.price != PRICE_ANY)
				ui_text(TOP_W / 2, 134, _("Prueba con otro precio en la barra de abajo"), SZ_SMALL, COL_DIM, AL_CENTER, 380);
			else
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

static const char *home_sub_name[HOME_PARTS] = {"Más vendidos", "Novedades", "Próximamente", "Gratis", "Para ti"};

// Subcategorías de Inicio: una barra que cuelga de la de pestañas (recta arriba, redonda abajo)
// La barra de precio de Buscar (en el sitio de la de subcategorías de Inicio): el precio elegido a
// la izquierda y el carril con un punto por paso y el tirador
#define PRICE_TX0 132.0f
#define PRICE_TX1 (BOT_W - 26.0f)

static int price_step_at(int px)
{
	float f = (px - PRICE_TX0) / (PRICE_TX1 - PRICE_TX0);
	int st = (int)roundf(f * PRICE_ANY);
	return st < 0 ? 0 : st > PRICE_ANY ? PRICE_ANY : st;
}

static void draw_pricebar(float x, float y, float w, float h, float e)
{
	bool on = S.price_focus;
	char lab[48];
	price_label(S.price, lab, sizeof lab);
	float lh = ui_line_h(SZ_TINY), ty = y + 3 + (h - 3 - lh) / 2;
	ui_icon(IC_TAG, x + 9, y + 3 + (h - 3 - 10) / 2, 10, col_alpha(on ? COL_ACC : COL_DIM, e));
	ui_text(x + 24, ty, lab, SZ_TINY, col_alpha(on ? COL_WHITE : COL_TXT, e), AL_LEFT, PRICE_TX0 - x - 34);
	float cy = y + 3 + (h - 3) / 2, x0 = PRICE_TX0, x1 = PRICE_TX1;
	float kx = x0 + (x1 - x0) * S.price_x / PRICE_ANY;
	ui_rrect(x0, cy - 1.5f, x1 - x0, 3, 1.5f, col_alpha(COL_WHITE, 0.14f * e));
	ui_rrect(x0, cy - 1.5f, kx - x0, 3, 1.5f, col_alpha(COL_ACC, (on ? 0.9f : 0.55f) * e));
	for (int i = 0; i <= PRICE_ANY; i++) {
		float px = x0 + (x1 - x0) * i / PRICE_ANY;
		ui_rect(px - 1, cy - 1, 2, 2, col_alpha(i <= S.price ? COL_WHITE : COL_DIM, 0.7f * e));
	}
	if (on) {
		float br = 0.25f + 0.12f * sinf(g_time * 2.4f);
		ui_rrect(kx - 8, cy - 8, 16, 16, 8, col_alpha(COL_ACC, br * e));
	}
	ui_rrect(kx - 5, cy - 5, 10, 10, 5, col_alpha(on ? COL_WHITE : C2D_Color32(0xc7, 0xd5, 0xe0, 0xff), e));
}

static void draw_subbar(void)
{
	float e = ease_out_cubic(S.sub_anim);
	if (e <= 0.01f) return;
	float x = 10, w = BOT_W - 20, y = LIST_Y - 3 - (1 - e) * SUB_H, h = SUB_H + 1;
	ui_scissor(GFX_BOTTOM, 0, LIST_Y - 3, BOT_W, SUB_H + 4);
	ui_rrect_bottom(x, y, w, h, 9, col_alpha(C2D_Color32(0x1c, 0x28, 0x3a, 0xff), 0.96f));
	ui_rrect_bottom(x, y, w, h, 9, col_alpha(COL_WHITE, 0.05f));
	ui_rect(x + 9, y + h - 1, w - 18, 1, col_alpha(COL_WHITE, 0.10f));
	if (S.tab == TAB_SEARCH) {
		draw_pricebar(x, y, w, h, e);
		ui_scissor_off();
		return;
	}
	float sw = w / HOME_PARTS;
	float lh = ui_line_h(SZ_TINY), ty = y + 3 + (h - 3 - lh) / 2;
	// La elegida: un resplandor suave que respira, sin burbuja
	float hx = x + S.sub_x * sw;
	float br = 0.07f + 0.04f * sinf(g_time * 2.4f);
	ui_rrect(hx + 6, y + 4, sw - 12, h - 7, (h - 7) / 2, col_alpha(COL_WHITE, br * e));
	for (int i = 0; i < HOME_PARTS; i++) {
		float on = 1 - clampf(fabsf(S.sub_x - i), 0, 1);
		// Con cinco no caben todos: el nombre se encoge un poco en vez de cortarse
		float tw = ui_text_w(_(home_sub_name[i]), SZ_TINY), tsz = tw > sw - 8 ? SZ_TINY * (sw - 8) / tw : SZ_TINY;
		ui_text(x + sw * i + sw / 2, ty + (ui_line_h(SZ_TINY) - ui_line_h(tsz)) / 2, _(home_sub_name[i]), tsz, col_alpha(col_lerp(COL_DIM, COL_WHITE, on), e), AL_CENTER, 0);
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
		else if (t->tag) ui_text(px + 28, ty, t->tag_name, SZ_SMALL, col_alpha(COL_WHITE, se), AL_LEFT, pw - 60);
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
	else snprintf(title, sizeof title, _("Lista de deseos · %d"), wish_mode == 3 ? t->list.count : t->list.total);
	if (S.tab == TAB_LIBRARY || S.tab == TAB_WISHLIST)
		ui_text(BOT_W - 10, 6, _(S.tab == TAB_LIBRARY ? lib_mode_name[lib_mode] : wish_mode_name[wish_mode]), SZ_TINY, COL_ACC, AL_RIGHT, 120);
	ui_icon(S.tab == TAB_LIBRARY ? IC_PAD : S.tab == TAB_CART ? IC_CART : IC_HEART, 28, 6, 12, COL_ACC);
	if (S.tab == TAB_CART && shop_cart()->subtotal[0])
		ui_text(BOT_W - 10, 5, shop_cart()->subtotal, SZ_SMALL, COL_LIME, AL_RIGHT, 0);
	ui_text(46, 5, title, SZ_SMALL, COL_WHITE, AL_LEFT, S.tab == TAB_CART ? BOT_W - 56 : BOT_W - 180);
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
	if (S.tab == TAB_CART && gift_is_open()) {
		draw_gift_bottom();
		return;
	}
	if (S.tab >= TAB_COUNT) draw_account_bar();
	else {
		draw_subbar();
		draw_tabs();
	}
	float top = list_top(), lh = list_h();
	ui_scissor(GFX_BOTTOM, 0, top, BOT_W, lh);
	int first = (int)(t->scroll / ROW_H);
	if (t->list.count && cur_row() && !(S.tab == TAB_SEARCH && (S.search_focus || S.price_focus))) {
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
			// Búsqueda reciente o popular (una etiqueta)
			ui_icon(r->tagid ? IC_TAG : IC_SEARCH, 10, y + 9, 11, COL_DIM);
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
		bool ign = S.tab != TAB_LIBRARY && r->appid && shop_ignored(r->appid);
		if (ign) {
			// Lo ignoraste: se ve, pero apagado y con su etiqueta (como en la tienda)
			const char *tag = _("IGNORADO");
			float tw = ui_text_w(tag, SZ_TINY) + 8;
			ui_rect(nx, y + 8, tw, 13, col_alpha(COL_WHITE, 0.08f));
			ui_text(nx + 4, y + 8.5f, tag, SZ_TINY, COL_DIM, AL_LEFT, 0);
			nx += tw + 5;
		}
		if (S.tab == TAB_CART && i < shop_cart()->n && shop_cart()->line[i].giftee) {
			// Lo regalas: para quién es, delante del nombre
			const CartLine *cl = &shop_cart()->line[i];
			char gt[64];
			snprintf(gt, sizeof gt, _("REGALO · %s"), cl->giftee_name[0] ? cl->giftee_name : "…");
			float tw = ui_text_w(gt, SZ_TINY) + 8;
			if (tw > 110) tw = 110;
			ui_rect(nx, y + 8, tw, 13, COL_TAG);
			ui_text(nx + 4, y + 8.5f, gt, SZ_TINY, COL_ACC, AL_LEFT, tw - 8);
			nx += tw + 5;
		}
		ui_text(nx, y + 7, r->name, SZ_BODY - 0.02f, ign ? COL_DIM : on ? COL_WHITE : COL_TXT, AL_LEFT, BOT_W - 10 - nx - pw);
		if (S.tab == TAB_LIBRARY) {
			char hrs[24];
			if (r->minutes >= 60) snprintf(hrs, sizeof hrs, "%d h", (r->minutes + 30) / 60);
			else if (r->minutes) snprintf(hrs, sizeof hrs, "%d min", r->minutes);
			else snprintf(hrs, sizeof hrs, "–");
			if (r->family) {
				// Te lo presta tu familia: en vez de tus horas, la etiqueta
				const char *ft = _("Familia");
				float fw = ui_text_w(ft, SZ_TINY) + 10;
				ui_rrect(BOT_W - 22 - fw, y + 7, fw, 14, 7, col_alpha(COL_ACC, 0.18f));
				ui_text(BOT_W - 22 - fw / 2, y + 7.5f, ft, SZ_TINY, COL_ACC, AL_CENTER, 0);
			} else {
				ui_text(BOT_W - 22, y + 8, hrs, SZ_SMALL, COL_DIM, AL_RIGHT, 0);
			}
			if (!r->family && lib_is_new(r->appid)) {
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
	else if (S.tab == TAB_WISHLIST && wish_mode == 3 && !t->list.count && !t->list.req)
		ui_text(BOT_W / 2, top + 30, _("Ningún juego de tu lista de deseos está en oferta"), SZ_SMALL, COL_DIM, AL_CENTER, 300);
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
		static const char *hc[] = {"A", "Abrir", "Y", "Quitar", "SELECT", "Regalar", "X", "Pagar", "B", "Volver"};
		static const char *hb[] = {"B", "Volver", "START", "Menú"};
		if (t->list.count) draw_hints(hc, 5);
		else draw_hints(hb, 2);
	} else if (S.tab == TAB_LIBRARY || S.tab == TAB_WISHLIST) {
		static const char *ho[] = {"A", "Abrir", "↑↓", "Elegir", "Y", "Ordenar", "B", "Volver"};
		draw_hints(ho, 4);
	} else if (S.tab >= TAB_COUNT) {
		static const char *ha[] = {"A", "Abrir", "↑↓", "Elegir", "B", "Volver", "START", "Menú"};
		draw_hints(ha, 4);
	} else if (S.tab == TAB_SEARCH) {
		if (S.search_focus) {
			static const char *hs[] = {"A", "Escribir", "X", "Ver todo", "↓", "Precio", "L R", "Pestaña", "START", "Menú"};
			draw_hints(hs, 4);
		} else if (S.price_focus) {
			static const char *hp[] = {"← →", "Precio", "↓", "Resultados", "Y", "Escribir", "L R", "Pestaña"};
			draw_hints(hp, 4);
		} else if (t->term[0]) {
			static const char *hs[] = {"A", "Abrir", "↑↓", "Elegir", "Y", "Buscar", "B", "Borrar", "L R", "Pestaña"};
			draw_hints(hs, 5);
		} else if (t->tag) {
			static const char *hs[] = {"A", "Abrir", "↑↓", "Elegir", "Y", "Escribir", "B", "Volver", "L R", "Pestaña"};
			draw_hints(hs, 5);
		} else if (cur_row() && cur_row()->appid) {
			static const char *hs[] = {"A", "Abrir", "↑↓", "Elegir", "Y", "Escribir", "L R", "Pestaña"};
			draw_hints(hs, 4);
		} else {
			static const char *hs[] = {"A", "Buscar esto", "↑↓", "Elegir", "Y", "Escribir", "L R", "Pestaña"};
			draw_hints(hs, 4);
		}
	} else if (t->list.err[0] && !t->list.req && !t->list.count) {
		static const char *hh3[] = {"A", "Reintentar", "L R", "Pestaña", "START", "Menú"};
		draw_hints(hh3, 3);
	} else if (S.tab == TAB_HOME) {
		static const char *hh4[] = {"A", "Abrir", "↑↓", "Elegir", "L R", "Pestaña", "START", "Menú"};
		draw_hints(hh4, 4);
	} else {
		static const char *hh2[] = {"A", "Abrir", "↑↓", "Elegir", "L R", "Pestaña", "START", "Menú"};
		draw_hints(hh2, 4);
	}
}

// Elige una fila de la pestaña actual, saltándose los títulos de apartado
// La primera fila que se puede elegir (no un título de apartado), o -1 si no hay ninguna
static int first_row(const Tab *t)
{
	for (int i = 0; i < t->list.count; i++)
		if (!t->list.rows[i].header) return i;
	return -1;
}

static bool cur_row_any(const Tab *t)
{
	return first_row(t) >= 0;
}

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

// Lo de la biblioteca que es tuyo (no lo que te presta tu familia)
static const StoreRow *owned_row(u32 appid)
{
	Tab *t = &tabs[TAB_LIBRARY];
	for (int i = 0; i < t->list.count; i++)
		if (t->list.rows[i].appid == appid && !t->list.rows[i].family) return &t->list.rows[i];
	return NULL;
}

// Te lo presta alguien de tu Familia de Steam (y no es tuyo)
static bool family_app(u32 appid)
{
	Tab *t = &tabs[TAB_LIBRARY];
	for (int i = 0; i < t->list.count; i++)
		if (t->list.rows[i].appid == appid && t->list.rows[i].family) return true;
	return false;
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

static void fmt_date(u32 t, char *out, size_t n);
static void draw_review_preview(const AppInfo *a);
static void review_editor_close(void);
static void remote_close(void);
static void award_close(void);
static void remote_poll(const AppInfo *a);
static int press_update(u32 kd, u32 kh, u32 ku, touchPosition *tp, const int *ids, int n);
static bool key_repeat(u32 down, u32 held, u32 mask);

static void open_game(u32 appid, bool push)
{
	if (push && S.scene == SC_GAME && S.depth < 8) {
		S.stack_app[S.depth] = S.app;
		S.stack_sec[S.depth] = S.sec;
		S.stack_lsel[S.depth] = S.lsel;
		S.depth++;
	}
	if (!push) S.depth = 0;
	// Desde fuera de la tienda (amigos, un perfil, notificaciones), B vuelve allí
	if (S.scene != SC_GAME) {
		S.depth = 0;
		S.game_back = S.scene == SC_SHOTS || S.scene == SC_SPLASH ? SC_STORE : S.scene;
	}
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
	S.rsel = auth_logged() && owned_app(appid) ? -1 : 0;   // tu reseña, la primera
	S.rscroll = 0;
	review_editor_close();
	remote_close();
	award_close();
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
	go_scene(S.game_back, -1);
}

// Filtro de Opiniones: 0 destacadas (tu idioma), 1 positivas, 2 negativas, 3 recientes, 4 todos los idiomas
static int rv_filter;
static const char *const rv_filter_name[] = {"Destacadas", "Positivas", "Negativas", "Recientes", "Todos los idiomas"};

static void reviews_set_filter(int f)
{
	rv_filter = f % 5;
	steam_reviews_filter(rv_filter == 1 ? 1 : rv_filter == 2 ? 2 : 0, rv_filter == 3, rv_filter == 4);
	for (int i = 0; i < S.nreviews; i++) free(S.reviews[i].text);
	S.nreviews = 0;
	S.rcursor[0] = 0;
	C2D_TextBufClear(review_buf);
	if (S.rreq) net_release(S.rreq);
	S.rreq = NULL;
	S.rend = false;
	if (S.rsel > 0) S.rsel = 0;
	S.rscroll = 0;
}

static void reviews_more(void)
{
	if (S.rreq || S.rend || S.nreviews >= MAX_REVIEWS) return;
	S.rreq = steam_req_reviews(S.app, S.rcursor);
}

static void notif_when(u32 t, char *out, size_t sz);

// Tus capturas del juego (las de tu perfil de Steam), para el visor de capturas
#define MAX_MYSHOTS 50
static struct {
	u32 appid;
	MyShot s[MAX_MYSHOTS];
	int n, total;
	Req *req;
} MS;
static bool shots_mine;   // el visor enseña las tuyas en vez de las de la tienda

static void myshots_poll(const AppInfo *a)
{
	if (MS.appid != a->appid && auth_logged() && owned_app(a->appid)) {
		if (MS.req) net_release(MS.req);
		memset(&MS, 0, sizeof MS);
		MS.appid = a->appid;
		MS.req = steam_req_my_shots(auth_info()->steamid, a->appid, auth_token());
	}
	if (MS.req && MS.req->state >= REQ_DONE) {
		int n = MS.req->state == REQ_DONE ? steam_parse_my_shots(MS.req->data, MS.appid, MS.s, MAX_MYSHOTS, &MS.total) : -1;
		netlog("tus capturas de %lu: %d de %d (HTTP %ld)", (unsigned long)MS.appid, n, MS.total, MS.req->http);
		MS.n = n > 0 ? n : 0;
		net_release(MS.req);
		MS.req = NULL;
	}
}

// Tu reseña del juego (Opiniones) y el editor para escribirla o cambiarla
static struct {
	u32 appid;
	MyReview r;
	Req *req;
	bool loaded;
} MR;
#define REV_MAX 6000
static struct {
	bool open, up, pub, want_kb;
	int focus;                   // 0 recomendar, 1 texto, 2 visibilidad, 3 cancelar/publicar
	bool on_publish;             // en la última fila: publicar (si no, cancelar)
	char text[REV_MAX];
} RE;

static bool my_review_shown(const AppInfo *a)
{
	return auth_logged() && owned_app(a->appid);
}

static void myreview_poll(const AppInfo *a)
{
	if (MR.appid != a->appid && my_review_shown(a)) {
		if (MR.req) net_release(MR.req);
		free(MR.r.text);
		memset(&MR, 0, sizeof MR);
		MR.appid = a->appid;
		MR.req = steam_req_my_review(auth_info()->steamid, a->appid, auth_token());
	}
	if (MR.req && MR.req->state >= REQ_DONE) {
		bool ok = MR.req->state == REQ_DONE && steam_parse_my_review(MR.req->data, MR.appid, &MR.r);
		netlog("tu reseña de %lu: %s (HTTP %ld)", (unsigned long)MR.appid, !ok ? "falló" : MR.r.has ? (MR.r.up ? "recomendado" : "no recomendado") : "no hay", MR.req->http);
		MR.loaded = ok;
		net_release(MR.req);
		MR.req = NULL;
	}
}

// El playtest del juego abierto y los que ya has pedido en esta sesión
static struct {
	u32 appid, playtest;
	int deck;                    // compatibilidad con Steam Deck (-1 no se sabe)
	Req *req;
	u32 asked[16];
	int nasked;
} PT;

static int deck_for(u32 appid)
{
	return PT.appid == appid && !PT.req ? PT.deck : -1;
}

// Noticias oficiales del juego abierto (al final del Resumen)
static struct {
	u32 appid;
	SteamNews n[4];
	int nn;
	Req *req;
	bool done;
} NW;

static bool pt_asked(u32 appid)
{
	for (int i = 0; i < PT.nasked; i++)
		if (PT.asked[i] == appid) return true;
	return false;
}

static void playtest_poll(const AppInfo *a)
{
	if (PT.appid != a->appid) {
		if (PT.req) net_release(PT.req);
		PT.req = steam_req_playtest(a->appid);
		PT.appid = a->appid;
		PT.playtest = 0;
		PT.deck = -1;
	}
	if (PT.req && PT.req->state >= REQ_DONE) {
		if (PT.req->state == REQ_DONE) {
			PT.playtest = steam_parse_playtest(PT.req->data, PT.appid);
			PT.deck = steam_parse_deck(PT.req->data, PT.appid);
		}
		if (PT.playtest) netlog("playtest de %lu: %lu", (unsigned long)PT.appid, (unsigned long)PT.playtest);
		net_release(PT.req);
		PT.req = NULL;
	}
}

static u32 playtest_for(u32 appid)
{
	return PT.appid == appid ? PT.playtest : 0;
}

static int my_shots_for(u32 appid)
{
	return MS.appid == appid ? MS.n : 0;
}

static int shots_count(const AppInfo *a)
{
	return shots_mine ? my_shots_for(a->appid) : a->nshots;
}

static const char *shot_url(const AppInfo *a, int i)
{
	return shots_mine ? MS.s[i].url : a->shots[i];
}

static void ach_cmp_stop(void)
{
	if (S.cmp_req) net_release(S.cmp_req);
	S.cmp_req = NULL;
	free(S.cmp_done);
	S.cmp_done = NULL;
	S.cmp_i = -1;
	S.cmp_n = 0;
	S.cmp_loaded = false;
}

// Los amigos con los que se puede comparar: los que lo tienen (FR los pone primero)
static int ach_cmp_count(void)
{
	int own = FR.in_game + FR.owns;
	if (!FR.done || own <= 0 || FR.appid != S.app) return 0;
	return FR.nids < own ? FR.nids : own;
}

static void ach_cmp_next(const AppInfo *a)
{
	int n = ach_cmp_count(), next = S.cmp_i + 1;
	ach_cmp_stop();
	if (next >= n || !S.nach) return;   // tras el último, se deja de comparar
	S.cmp_i = next;
	S.cmp_req = steam_req_my_achievements(a->appid, FR.ids[next], auth_token());
}

static void ach_reset(void)
{
	free(S.ach);
	S.ach = NULL;
	S.nach = 0;
	S.ach_mine = -1;
	S.ach_for = 0;
	S.ach_err[0] = 0;
	if (S.ach_req) net_release(S.ach_req);
	if (S.ach_mine_req) net_release(S.ach_mine_req);
	S.ach_req = S.ach_mine_req = NULL;
	ach_cmp_stop();
}

// Como en Steam: primero los que tienes y luego los demás, de más comunes a más raros
static int ach_cmp(const void *x, const void *y)
{
	const Achievement *a = x, *b = y;
	if (a->done != b->done) return a->done ? -1 : 1;
	return a->pct < b->pct ? 1 : a->pct > b->pct ? -1 : 0;
}

static void ach_poll(const AppInfo *a)
{
	if (S.ach_req && S.ach_req->state >= REQ_DONE) {
		if (S.ach_req->state == REQ_DONE) {
			S.nach = steam_parse_achievements(S.ach_req->data, &S.ach);
			if (S.nach < 0) {
				S.nach = 0;
				snprintf(S.ach_err, sizeof S.ach_err, "%s", _("No se pudieron cargar los logros"));
			}
			netlog("logros %lu: %d", (unsigned long)a->appid, S.nach);
			// Los tuyos solo se piden si el juego está en tu biblioteca
			if (S.nach > 0 && auth_logged() && owned_app(a->appid))
				S.ach_mine_req = steam_req_my_achievements(a->appid, auth_info()->steamid, auth_token());
			qsort(S.ach, S.nach, sizeof *S.ach, ach_cmp);
		} else {
			snprintf(S.ach_err, sizeof S.ach_err, "%s", S.ach_req->err);
		}
		net_release(S.ach_req);
		S.ach_req = NULL;
	}
	if (S.ach_mine_req && S.ach_mine_req->state >= REQ_DONE) {
		if (S.ach_mine_req->state == REQ_DONE) {
			int n = steam_mark_unlocked(S.ach_mine_req->data, S.ach, S.nach);
			netlog("logros %lu desbloqueados: %d", (unsigned long)a->appid, n);
			if (n >= 0) {
				S.ach_mine = n;
				qsort(S.ach, S.nach, sizeof *S.ach, ach_cmp);
				S.lsel = 0;
			}
		} else {
			netlog("logros %lu desbloqueados: %s", (unsigned long)a->appid, S.ach_mine_req->err);
		}
		net_release(S.ach_mine_req);
		S.ach_mine_req = NULL;
	}
	if (S.cmp_req && S.cmp_req->state >= REQ_DONE) {
		// Sus logros se casan igual que los tuyos, sobre una copia (los tuyos no se tocan)
		Achievement *tmp = S.nach ? malloc(sizeof *tmp * S.nach) : NULL;
		int n = -1;
		if (tmp && S.cmp_req->state == REQ_DONE) {
			memcpy(tmp, S.ach, sizeof *tmp * S.nach);
			for (int i = 0; i < S.nach; i++) tmp[i].done = false;
			n = steam_mark_unlocked(S.cmp_req->data, tmp, S.nach);
		}
		if (n >= 0) {
			S.cmp_done = malloc(sizeof *S.cmp_done * S.nach);
			for (int i = 0; S.cmp_done && i < S.nach; i++) S.cmp_done[i] = tmp[i].done;
			S.cmp_n = n;
			S.cmp_loaded = S.cmp_done != NULL;
		}
		netlog("logros %lu: comparar con %llu -> %d", (unsigned long)a->appid, (unsigned long long)(S.cmp_i >= 0 ? FR.ids[S.cmp_i] : 0), n);
		if (n < 0) toast(_("No se pudieron ver sus logros (puede que su perfil sea privado)"));
		free(tmp);
		net_release(S.cmp_req);
		S.cmp_req = NULL;
	}
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
	if (S.sec == SEC_ACH && S.ach_for != a->appid) {
		ach_reset();
		S.ach_for = a->appid;
		S.ach_req = steam_req_achievements(a->appid);
	}
	ach_poll(a);
	myshots_poll(a);
	myreview_poll(a);
	playtest_poll(a);
	if (S.sec == SEC_SUMMARY && NW.appid != a->appid) {
		if (NW.req) net_release(NW.req);
		memset(&NW, 0, sizeof NW);
		NW.appid = a->appid;
		NW.req = steam_req_news(a->appid);
	}
	if (NW.req && NW.req->state >= REQ_DONE) {
		int n = NW.req->state == REQ_DONE ? steam_parse_news(NW.req->data, NW.n, 4) : -1;
		NW.nn = n < 0 ? 0 : n;
		NW.done = true;
		net_release(NW.req);
		NW.req = NULL;
	}
	remote_poll(a);
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
	// Noticias: los últimos anuncios oficiales
	if (NW.appid == a->appid && NW.nn) {
		y += 10;
		ui_hline(12, y, TOP_W - 24, C2D_Color32(0xff, 0xff, 0xff, 0x26));
		y += 8;
		if (y + 17 > 18 && y < SCR_H) ui_text(12, y, _("NOTICIAS"), SZ_BODY, COL_ACC, AL_LEFT, 0);
		y += 20;
		for (int i = 0; i < NW.nn; i++) {
			const SteamNews *nw = &NW.n[i];
			int nl = ui_text_lines(nw->text, SZ_TINY, TOP_W - 24);
			if (nl > 3) nl = 3;
			float h = 30 + nl * 12 + 8;
			if (y + h > 18 && y < SCR_H) {
				char d[40];
				notif_when(nw->date, d, sizeof d);
				ui_text(12, y, nw->title, SZ_SMALL, COL_WHITE, AL_LEFT, TOP_W - 24);
				ui_text(12, y + 15, d, SZ_TINY, COL_DIM, AL_LEFT, 0);
				ui_text_wrap(12, y + 29, TOP_W - 24, nw->text, SZ_TINY, COL_TXT, 3, 12);
			}
			y += h;
		}
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
	if (a->mac) {
		// La tecla dibujada (no todas las fuentes traen las flechas) y lo que hace
		const char *l = g_cfg.reqs_mac ? _("Ver los de Windows") : _("Ver los de macOS");
		float kw = hint_key(0, 0, "↑↓", false), tw = ui_text_w(l, SZ_TINY), x0 = TOP_W - 10 - tw - kw - 4;
		hint_key(x0, 218, "↑↓", true);
		ui_text(x0 + kw + 4, 222, l, SZ_TINY, COL_ACC, AL_LEFT, 0);
	}
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

// La tarjeta «Tu reseña» va la primera de la lista (rsel == -1)
static float my_review_h(const AppInfo *a)
{
	if (!my_review_shown(a)) return 0;
	if (!MR.r.has || !MR.r.text) return 40;
	int n = ui_text_lines(MR.r.text, SZ_TINY, TOP_W - 36);
	return 26 + 12 * (n < 1 ? 1 : n > 4 ? 4 : n);
}

static void draw_my_review(const AppInfo *a, float y)
{
	float h = my_review_h(a);
	bool on = S.rsel == -1;
	ui_rrect(12, y, TOP_W - 24, h, 10, col_alpha(COL_ACC, on ? 0.20f + 0.05f * sinf(g_time * 2.4f) : 0.10f));
	ui_rrect(12, y, 3, h, 1, col_alpha(COL_ACC, 0.9f));
	if (!MR.loaded) {
		ui_text(22, y + 4, _("TU RESEÑA"), SZ_TINY, COL_ACC, AL_LEFT, 0);
		ui_spinner(TOP_W / 2, y + 26, 5, COL_DIM);
		return;
	}
	if (!MR.r.has) {
		ui_text(22, y + 4, _("TU RESEÑA"), SZ_TINY, COL_ACC, AL_LEFT, 0);
		ui_text(22, y + 19, _("Aún no has escrito una reseña de este juego · Pulsa A para escribirla"), SZ_SMALL, COL_TXT, AL_LEFT, TOP_W - 44);
		return;
	}
	ui_icon(MR.r.up ? IC_THUMB_UP : IC_THUMB_DOWN, 20, y + 5, 11, MR.r.up ? COL_ACC : COL_NEG);
	char t[96];
	snprintf(t, sizeof t, "%s · %s", _("Tu reseña"), MR.r.up ? _("Recomendado") : _("No recomendado"));
	ui_text(36, y + 4, t, SZ_SMALL, COL_WHITE, AL_LEFT, 0);
	char d[40];
	fmt_date(MR.r.updated ? MR.r.updated : MR.r.created, d, sizeof d);
	snprintf(t, sizeof t, MR.r.votes_up == 1 ? _("%s · %d persona la ha valorado útil") : _("%s · %d personas la han valorado útil"), d, MR.r.votes_up);
	ui_text(TOP_W - 18, y + 5, MR.r.updated || MR.r.created ? t : "", SZ_TINY, COL_DIM, AL_RIGHT, 0);
	ui_text_wrap(18, y + 20, TOP_W - 36, MR.r.text, SZ_TINY, COL_TXT, 4, 12);
}

static void draw_reviews_top(const AppInfo *a)
{
	if (a->review_label[0]) {
		float lw = ui_text(12, 24, a->review_label, SZ_BODY, COL_WHITE, AL_LEFT, 0);
		char ft[48];
		snprintf(ft, sizeof ft, "· %s", _(rv_filter_name[rv_filter]));
		ui_text(12 + lw + 8, 25, ft, SZ_SMALL, COL_ACC, AL_LEFT, 150);
		char t[64];
		snprintf(t, sizeof t, _("%d %% de %d reseñas"), a->review_pct, a->review_count);
		ui_text(TOP_W - 12, 25, t, SZ_SMALL, COL_DIM, AL_RIGHT, 0);
		float bw = (TOP_W - 24) * a->review_pct / 100.0f;
		ui_rrect(12, 42, TOP_W - 24, 4, 2, col_alpha(COL_NEG, 0.8f));
		ui_rrect(12, 42, bw, 4, 2, COL_ACC);
	}
	float top = 52, bottom = SCR_H - 20, clip = top;
	float mine_h = my_review_h(a);
	if (mine_h > 0) {
		ui_scissor(GFX_TOP, 0, clip, TOP_W, bottom - clip);
		if (top - S.rscroll + mine_h > clip) draw_my_review(a, top - S.rscroll);
		ui_scissor_off();
		top += mine_h + 5;   // las demás, debajo (se desplaza todo junto)
	}
	if (S.nreviews == 0) {
		if (S.rend) ui_text(TOP_W / 2, 120, rv_filter == 0 ? _("Todavía no hay reseñas en tu idioma") : _("No hay reseñas con este filtro"), SZ_BODY, COL_DIM, AL_CENTER, 0);
		else draw_net_wait(TOP_W / 2, 125, NULL);
		return;
	}
	ui_scissor(GFX_TOP, 0, clip, TOP_W, bottom - clip);
	float y = top - S.rscroll;
	for (int i = 0; i < S.nreviews; i++) {
		const Review *r = &S.reviews[i];
		float h = review_h(i);
		if (y + h > clip && y < bottom) {
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

#define ACH_RH 23
static float ach_top = 120;   // donde empieza la lista (más arriba si no hay progreso ni actividad que enseñar)

static void ach_icon_url(const AppInfo *a, const Achievement *x, char *out, size_t n)
{
	snprintf(out, n, "https://cdn.akamai.steamstatic.com/steamcommunity/public/images/apps/%lu/%s",
		(unsigned long)a->appid, x->done || S.ach_mine < 0 ? x->icon : x->icon_gray);
}

static void draw_ach_icon(const AppInfo *a, const Achievement *x, float px, float py, float sz)
{
	char url[200];
	ach_icon_url(a, x, url, sizeof url);
	const Pic *p = img_get(url, 64);
	if (p) img_draw_fit(p, px, py, sz, sz);
	else {
		ui_rrect(px, py, sz, sz, 3, col_alpha(COL_WHITE, 0.06f));
		ui_icon(IC_TROPHY, px + sz * 0.25f, py + sz * 0.25f, sz * 0.5f, COL_DIM);
	}
}

static bool ach_secret(const Achievement *x)
{
	return x->hidden && !x->done;
}

// Logros: arriba tu progreso y tu actividad, el logro elegido en grande y debajo la lista
static void draw_ach_top(const AppInfo *a)
{
	char t[96];
	ui_text(12, 24, _("LOGROS"), SZ_SMALL, COL_ACC, AL_LEFT, 0);
	if (S.ach_for != a->appid || S.ach_req) {
		draw_net_wait(TOP_W / 2, 125, NULL);
		return;
	}
	if (S.nach == 0) {
		ui_text(TOP_W / 2, 120, S.ach_err[0] ? S.ach_err : _("Este juego no tiene logros de Steam"), SZ_BODY, COL_DIM, AL_CENTER, 380);
		return;
	}
	if (S.ach_mine >= 0) {
		snprintf(t, sizeof t, _("%d de %d desbloqueados (%d %%)"), S.ach_mine, S.nach, S.ach_mine * 100 / S.nach);
		ui_text(TOP_W - 12, 24, t, SZ_SMALL, COL_TXT, AL_RIGHT, 0);
		ui_rrect(12, 40, TOP_W - 24, 4, 2, col_alpha(COL_WHITE, 0.10f));
		ui_rrect(12, 40, (TOP_W - 24) * S.ach_mine / (float)S.nach, 4, 2, COL_ACC);
	} else {
		snprintf(t, sizeof t, S.nach == 1 ? _("%d logro") : _("%d logros"), S.nach);
		ui_text(TOP_W - 12, 24, t, SZ_SMALL, COL_DIM, AL_RIGHT, 0);
		if (S.ach_mine_req) ui_spinner(TOP_W - 24 - ui_text_w(t, SZ_SMALL), 31, 5, COL_DIM);
	}
	// Comparando con un amigo: su progreso en la misma línea que tu actividad
	if (S.cmp_i >= 0) {
		const char *fn = S.cmp_i < FR.nnames ? FR.name[S.cmp_i] : _("tu amigo");
		if (S.cmp_loaded) snprintf(t, sizeof t, _("%s: %d de %d"), fn, S.cmp_n, S.nach);
		else snprintf(t, sizeof t, _("Cargando los logros de %s…"), fn);
		ui_icon(IC_USER, TOP_W - 18 - ui_text_w(t, SZ_TINY) - 14, 49, 11, C2D_Color32(0x67, 0xc1, 0xf5, 0xff));
		ui_text(TOP_W - 18, 48, t, SZ_TINY, C2D_Color32(0x67, 0xc1, 0xf5, 0xff), AL_RIGHT, 180);
	}
	// Tu actividad: horas y última vez (de la biblioteca)
	const StoreRow *own = auth_logged() ? owned_row(a->appid) : NULL;
	bool act = (own && own->minutes) || S.cmp_i >= 0;
	if (act) {
		char hrs[32], when[40];
		if (own->minutes >= 60) snprintf(hrs, sizeof hrs, _("%d h jugadas"), (own->minutes + 30) / 60);
		else snprintf(hrs, sizeof hrs, _("%d min jugados"), own->minutes);
		if (own->last_played) {
			notif_when(own->last_played, when, sizeof when);
			snprintf(t, sizeof t, _("%s · Jugado por última vez %s"), hrs, when);
		} else {
			snprintf(t, sizeof t, "%s", hrs);
		}
		ui_icon(IC_PAD, 12, 49, 11, COL_DIM);
		ui_text(27, 48, t, SZ_TINY, COL_DIM, AL_LEFT, S.cmp_i >= 0 ? 180 : TOP_W - 40);
	}

	// El elegido, en grande
	int sel = S.lsel < S.nach ? S.lsel : 0;
	const Achievement *x = &S.ach[sel];
	float cy = act || S.ach_mine >= 0 ? 64 : 42;
	ach_top = cy + 56;
	ui_rrect(10, cy, TOP_W - 20, 50, 8, col_alpha(COL_WHITE, 0.06f));
	draw_ach_icon(a, x, 16, cy + 5, 40);
	float rx = TOP_W - 18;
	if (x->done) {
		ui_icon(IC_CHECK, rx - ui_text_w(_("Desbloqueado"), SZ_TINY) - 15, cy + 6, 11, COL_LIME);
		ui_text(rx, cy + 5, _("Desbloqueado"), SZ_TINY, COL_LIME, AL_RIGHT, 0);
	}
	float nw = x->done ? TOP_W - 66 - 26 - ui_text_w(_("Desbloqueado"), SZ_TINY) - 14 : TOP_W - 84;
	ui_text(64, cy + 4, ach_secret(x) ? _("Logro oculto") : x->name, SZ_SMALL, COL_WHITE, AL_LEFT, nw);
	ui_text_wrap(64, cy + 20, TOP_W - 84, ach_secret(x) ? _("Los detalles de este logro se revelarán cuando lo desbloquees.") : x->desc,
		SZ_TINY, COL_TXT, 1, 12);
	snprintf(t, sizeof t, _("Lo tiene el %.1f %% de los jugadores"), x->pct);
	ui_text(64, cy + 33, t, SZ_TINY, x->pct < 10 ? C2D_Color32(0xe8, 0xb8, 0x4a, 0xff) : COL_DIM, AL_LEFT, 0);
	if (S.cmp_loaded && S.cmp_done && S.cmp_i >= 0) {
		char ft[80];
		const char *fn = S.cmp_i < FR.nnames ? FR.name[S.cmp_i] : _("tu amigo");
		snprintf(ft, sizeof ft, S.cmp_done[sel] ? _("%s lo ha desbloqueado") : _("%s no lo ha desbloqueado"), fn);
		ui_text(TOP_W - 18, cy + 33, ft, SZ_TINY, S.cmp_done[sel] ? C2D_Color32(0x67, 0xc1, 0xf5, 0xff) : COL_DIM, AL_RIGHT, 140);
	}

	// La lista
	float top = ach_top, bottom = SCR_H - 4, rh = ACH_RH;
	ui_scissor(GFX_TOP, 0, top, TOP_W, bottom - top);
	float hy = top + S.lsel_y * rh - S.lscroll;
	ui_rrect(10, hy, TOP_W - 20, rh - 1, 6, col_alpha(COL_WHITE, 0.075f + 0.035f * sinf(g_time * 2.4f)));
	ui_rrect(10, hy + 3, 2, rh - 7, 1, col_alpha(COL_ACC, 0.8f));
	for (int i = 0; i < S.nach; i++) {
		float y = top + i * rh - S.lscroll;
		if (y + rh < top || y > bottom) continue;
		const Achievement *r = &S.ach[i];
		bool on = i == S.lsel;
		draw_ach_icon(a, r, 18, y + 2, rh - 5);
		u32 nc = r->done || S.ach_mine < 0 ? (on ? COL_WHITE : COL_TXT) : COL_DIM;
		ui_text(44, y + 4, ach_secret(r) ? _("Logro oculto") : r->name, SZ_SMALL, nc, AL_LEFT, TOP_W - 44 - (S.cmp_loaded ? 100 : 80));
		if (S.cmp_loaded && S.cmp_done) {
			// Su marca, en azul, a la izquierda de la tuya
			if (S.cmp_done[i]) ui_icon(IC_USER, TOP_W - 64, y + 6, 11, C2D_Color32(0x67, 0xc1, 0xf5, 0xff));
			else ui_text(TOP_W - 59, y + 4, "·", SZ_SMALL, COL_DIM, AL_CENTER, 0);
		}
		if (r->done) {
			ui_icon(IC_CHECK, TOP_W - 30, y + 6, 11, COL_LIME);
		} else {
			snprintf(t, sizeof t, "%.1f %%", r->pct);
			ui_text(TOP_W - 18, y + 5, t, SZ_TINY, COL_DIM, AL_RIGHT, 0);
		}
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
	if (RE.open) {
		draw_review_preview(a);
		C2D_ViewRestore(&view);
		return;
	}
	switch (S.sec) {
	case SEC_SUMMARY: draw_summary_top(a); break;
	case SEC_REQS: draw_reqs_top(a); break;
	case SEC_REVIEWS: draw_reviews_top(a); break;
	case SEC_ACH: draw_ach_top(a); break;
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
static Box B_IGNORE = {178, 148, 130, 26};
static Box B_FOLLOW = {178, 148, 130, 26};
static bool has_ignore_btn, has_follow_btn;

static void game_layout(const AppInfo *a)
{
	bool demo = a->demo && !a->is_demo;
	S.has_demo_btn = auth_logged() && (demo || playtest_for(a->appid));
	S.demo_is_pt = !demo && playtest_for(a->appid);
	// Ignorar, como en la tienda: con sesión y si no es tuyo, al lado de instalar en el PC
	has_ignore_btn = auth_logged() && !owned_app(a->appid);
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
	// Con sesión: si es tuyo, Instalar en el PC y Seguir; si no, Seguir e Ignorar (como en la tienda)
	has_follow_btn = auth_logged();
	if (has_ignore_btn) {
		B_FOLLOW = (Box){12, B_INST.y, 145, B_INST.h};
		B_IGNORE = (Box){163, B_INST.y, 145, B_INST.h};
	} else if (has_follow_btn) {
		B_INST.w = 160;
		B_FOLLOW = (Box){178, B_INST.y, 130, B_INST.h};
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


// ---------------------------------------------------------------------------- Instalar en tu PC de forma remota

#define RP_MAX 6
static struct {
	bool open;
	float anim;
	u32 appid;
	Req *req, *apps[RP_MAX], *inst;
	RemotePC pc[RP_MAX];
	int n, sel, inst_i;
	bool loaded, failed;
} RP;

static void remote_close(void)
{
	RP.open = false;
}

static void remote_open(const AppInfo *a)
{
	if (RP.req) net_release(RP.req);
	for (int i = 0; i < RP_MAX; i++)
		if (RP.apps[i]) net_release(RP.apps[i]), RP.apps[i] = NULL;
	RP.req = steam_req_remote_pcs(auth_token());
	RP.appid = a->appid;
	RP.n = RP.sel = 0;
	RP.loaded = RP.failed = false;
	RP.open = true;
}

static void remote_poll(const AppInfo *a)
{
	if (RP.req && RP.req->state >= REQ_DONE) {
		int n = RP.req->state == REQ_DONE ? steam_parse_remote_pcs(RP.req->data, RP.pc, RP_MAX) : -1;
		netlog("instalar en el PC: %d equipos (HTTP %ld)", n, RP.req->http);
		RP.failed = n < 0;
		RP.n = n < 0 ? 0 : n;
		RP.loaded = true;
		net_release(RP.req);
		RP.req = NULL;
		// En cada uno, si ya lo tienes instalado
		for (int i = 0; i < RP.n; i++) RP.apps[i] = steam_req_remote_apps(RP.pc[i].id, RP.appid, auth_token());
	}
	for (int i = 0; i < RP.n; i++) {
		Req *r = RP.apps[i];
		if (!r || r->state < REQ_DONE) continue;
		RP.pc[i].installed = r->state == REQ_DONE ? steam_parse_remote_app(r->data, RP.appid) : -1;
		netlog("instalar en el PC: %s -> %d", RP.pc[i].name, RP.pc[i].installed);
		net_release(r);
		RP.apps[i] = NULL;
	}
	if (RP.inst && RP.inst->state >= REQ_DONE) {
		Req *r = RP.inst;
		// {"response":{}} con x-eresult 1, o un resultado distinto de 1 si el PC no puede
		cJSON *root = r->state == REQ_DONE ? cJSON_Parse(r->data) : NULL;
		const cJSON *res = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "response"), "result");
		bool ok = r->state == REQ_DONE && r->http == 200 && (r->eresult == 1 || r->eresult == 0) && root &&
			(!cJSON_IsNumber(res) || res->valueint == 1);
		netlog("instalar en el PC: orden -> %s (HTTP %ld, eresult %d, result %d)", ok ? "ok" : "fallo", r->http, r->eresult,
			cJSON_IsNumber(res) ? res->valueint : -1);
		cJSON_Delete(root);
		char t[160];
		const char *pc = RP.inst_i < RP.n ? RP.pc[RP.inst_i].name : "";
		if (ok && RP.inst_i < RP.n) RP.pc[RP.inst_i].installed = 2;
		if (ok) snprintf(t, sizeof t, _("Se ha empezado a instalar %s en %s"), a ? a->name : "", pc);
		else snprintf(t, sizeof t, _("%s no ha podido empezar la instalación. Comprueba que Steam sigue abierto en ese PC"), pc);
		toast_icon(t, IC_PC);
		net_release(r);
		RP.inst = NULL;
	}
}

static float remote_y(void)
{
	int rows = RP.n ? RP.n : 2;
	return SCR_H - (34 + rows * PICK_ROW + 12);
}

static void draw_remote(const AppInfo *a)
{
	float an = ease_out_cubic(RP.anim);
	if (an <= 0.01f) return;
	ui_rect(0, 0, BOT_W, SCR_H, C2D_Color32(0, 0, 0, (u8)(0x90 * an)));
	float y0 = remote_y() + (1 - an) * 60;
	fx_panel(6, y0, BOT_W - 12, SCR_H - y0 + 16, 16, an);
	ui_rrect(BOT_W / 2 - 16, y0 + 6, 32, 3, 1.5f, col_alpha(COL_DIM, an * 0.6f));
	ui_text(18, y0 + 12, _("Instalar en tu PC de forma remota"), SZ_BODY, col_alpha(COL_WHITE, an), AL_LEFT, BOT_W - 36);
	float y = y0 + 34;
	if (!RP.loaded) {
		ui_spinner(BOT_W / 2, y + PICK_ROW, 6, COL_DIM);
		return;
	}
	if (!RP.n) {
		ui_text_wrap(20, y + 2, BOT_W - 40, RP.failed ? _("No se pudo consultar tus equipos. Inténtalo de nuevo.")
			: _("No hay ningún PC con Steam abierto con tu cuenta. Abre Steam en tu PC y vuelve a intentarlo."), SZ_SMALL, COL_TXT, 4, 15);
		return;
	}
	for (int i = 0; i < RP.n; i++) {
		const RemotePC *pc = &RP.pc[i];
		float ry = y + i * PICK_ROW;
		bool on = i == RP.sel;
		if (on) fx_select(12, ry, BOT_W - 24, PICK_ROW - 3, 10, an);
		ui_icon(IC_PC, 20, ry + 8, 13, col_alpha(COL_ACC, an));
		ui_text(40, ry + 2, pc->name[0] ? pc->name : _("PC"), SZ_SMALL, col_alpha(on ? COL_WHITE : COL_TXT, an), AL_LEFT, BOT_W - 140);
		ui_text(40, ry + 15, pc->os, SZ_TINY, col_alpha(COL_DIM, an), AL_LEFT, BOT_W - 140);
		const char *st = RP.inst && RP.inst_i == i ? _("Enviando…") : pc->installed == 1 ? _("Ya está instalado") : pc->installed == 2 ? _("Instalación iniciada") : RP.apps[i] ? "" : _("Pulsa A para instalar");
		ui_text(BOT_W - 20, ry + 9, st, SZ_TINY, col_alpha(pc->installed >= 1 ? COL_LIME : COL_ACC, an), AL_RIGHT, 120);
		if (RP.apps[i]) ui_spinner(BOT_W - 30, ry + 14, 4, COL_DIM);
	}
	(void)a;
}

static void input_remote(const AppInfo *a, u32 kd, u32 kh, touchPosition *tp)
{
	if (kd & KEY_B) {
		RP.open = false;
		return;
	}
	if (key_repeat(kd, kh, KEY_DOWN) && RP.sel + 1 < RP.n) RP.sel++;
	if (key_repeat(kd, kh, KEY_UP) && RP.sel > 0) RP.sel--;
	int pick = -1;
	if ((kd & KEY_A) && RP.sel < RP.n) pick = RP.sel;
	if (kd & KEY_TOUCH) {
		float y = remote_y() + 34;
		if (tp->py < remote_y()) {
			RP.open = false;   // fuera del panel: se cierra
			return;
		}
		int i = (int)((tp->py - y) / PICK_ROW);
		if (tp->py >= y && i >= 0 && i < RP.n) {
			if (i == RP.sel) pick = i;
			else RP.sel = i;
		}
	}
	if (pick < 0 || RP.inst) return;
	if (RP.pc[pick].installed >= 1) {
		toast(RP.pc[pick].installed == 1 ? _("Ya está instalado en ese PC") : _("La instalación ya se ha iniciado en ese PC"));
		return;
	}
	RP.inst_i = pick;
	RP.inst = steam_req_remote_install(RP.pc[pick].id, a->appid, auth_token());
}

// ---------------------------------------------------------------------------- Descargas de tu PC

static struct {
	Req *pcs, *apps, *act;
	RemotePC pc[RP_MAX];
	int npc, cur, sel;
	PcApp *a;
	int na;
	long long freeb;
	bool pcs_done, apps_done, failed;
	u64 refresh_at, confirm_at;
	u32 confirm_app;
	int act_kind;              // 1 pausar/reanudar, 2 desinstalar
	float scroll;
} DL;

static void pcdl_load_apps(void)
{
	if (DL.apps || DL.cur >= DL.npc) return;
	DL.apps = steam_req_pc_apps(DL.pc[DL.cur].id, auth_token());
}

static void open_pcdl(void)
{
	if (DL.pcs) net_release(DL.pcs);
	if (DL.apps) net_release(DL.apps);
	free(DL.a);
	Req *act = DL.act;
	memset(&DL, 0, sizeof DL);
	DL.act = act;
	DL.pcs = steam_req_remote_pcs(auth_token());
	go_scene(SC_PCDL, 1);
}

static bool pcdl_any_paused(void)
{
	for (int i = 0; i < DL.na; i++)
		if (DL.a[i].changing && DL.a[i].paused) return true;
	return false;
}

static bool pcdl_any_changing(void)
{
	for (int i = 0; i < DL.na; i++)
		if (DL.a[i].changing) return true;
	return false;
}

static void pcdl_poll(void)
{
	if (DL.pcs && DL.pcs->state >= REQ_DONE) {
		int n = DL.pcs->state == REQ_DONE ? steam_parse_remote_pcs(DL.pcs->data, DL.pc, RP_MAX) : -1;
		netlog("descargas del PC: %d equipos", n);
		DL.npc = n < 0 ? 0 : n;
		DL.failed = n < 0;
		DL.pcs_done = true;
		net_release(DL.pcs);
		DL.pcs = NULL;
		pcdl_load_apps();
	}
	if (DL.apps && DL.apps->state >= REQ_DONE) {
		PcApp *a = NULL;
		long long fb = 0;
		int n = DL.apps->state == REQ_DONE ? steam_parse_pc_apps(DL.apps->data, &a, &fb) : -1;
		if (n >= 0) {
			u32 keep = DL.sel < DL.na ? DL.a[DL.sel].appid : 0;
			free(DL.a);
			DL.a = a;
			DL.na = n;
			DL.freeb = fb;
			// La misma fila elegida aunque cambie el orden
			DL.sel = 0;
			for (int i = 0; i < n; i++)
				if (a[i].appid == keep) DL.sel = i;
		} else {
			free(a);
		}
		if (!DL.apps_done) netlog("descargas del PC: %d juegos (HTTP %ld)", n, DL.apps->http);
		DL.apps_done = true;
		net_release(DL.apps);
		DL.apps = NULL;
		DL.refresh_at = osGetTime() + (pcdl_any_changing() ? 4000 : 15000);
	}
	// Se refresca solo mientras miras la pantalla (más a menudo si algo se está descargando)
	if (S.scene == SC_PCDL && DL.apps_done && !DL.apps && osGetTime() > DL.refresh_at) pcdl_load_apps();
	if (DL.act && DL.act->state >= REQ_DONE) {
		bool ok = DL.act->state == REQ_DONE && DL.act->http == 200 && (DL.act->eresult == 1 || DL.act->eresult == 0);
		netlog("descargas del PC: acción %d -> %s (HTTP %ld, eresult %d)", DL.act_kind, ok ? "ok" : "fallo", DL.act->http, DL.act->eresult);
		const char *t = DL.act_kind == 2 ? (ok ? _("Se ha empezado a desinstalar en tu PC") : _("No se pudo desinstalar"))
			: ok ? _("Hecho. Tu PC lo aplicará en unos segundos") : _("Tu PC no respondió. Comprueba que Steam sigue abierto");
		toast_icon(t, IC_PC);
		net_release(DL.act);
		DL.act = NULL;
		DL.refresh_at = osGetTime() + 1500;
	}
}

static void fmt_bytes(long long b, char *out, size_t n)
{
	if (b >= (1LL << 30)) snprintf(out, n, "%.1f GB", b / 1073741824.0);
	else if (b >= (1 << 20)) snprintf(out, n, "%.0f MB", b / 1048576.0);
	else snprintf(out, n, "%lld KB", b >> 10);
}

static void pcdl_status(const PcApp *a, char *out, size_t n, u32 *col)
{
	*col = COL_DIM;
	if (a->changing && a->total > 0) {
		char d[24], t[24];
		fmt_bytes(a->done, d, sizeof d);
		fmt_bytes(a->total, t, sizeof t);
		int pct = (int)(a->done * 100 / a->total);
		if (a->paused) snprintf(out, n, _("En pausa · %d %% · %s de %s"), pct, d, t);
		else if (a->queue > 0) snprintf(out, n, _("En cola (%d) · %d %%"), a->queue, pct);
		else snprintf(out, n, _("Descargando · %d %% · %s de %s"), pct, d, t);
		*col = a->paused ? COL_DIM : COL_ACC;
	} else if (a->changing) {
		snprintf(out, n, "%s", a->paused ? _("En pausa") : _("Actualización pendiente"));
		*col = COL_ACC;
	} else if (a->running) {
		snprintf(out, n, "%s", _("Jugando ahora"));
		*col = COL_LIME;
	} else {
		snprintf(out, n, "%s", _("Instalado"));
	}
}

static void draw_pcdl_top(void)
{
	const float X = 16, W = TOP_W - 32;
	if (!DL.pcs_done || (DL.npc && !DL.apps_done)) {
		draw_net_wait(TOP_W / 2, 120, _("Consultando tu PC…"));
		return;
	}
	if (!DL.npc) {
		ui_icon(IC_PC, TOP_W / 2 - 14, 70, 28, COL_DIM);
		ui_text(TOP_W / 2, 110, DL.failed ? _("No se pudo consultar tus equipos") : _("No hay ningún PC con Steam abierto"), SZ_LARGE, COL_TXT, AL_CENTER, 380);
		ui_text_wrap(X, 136, W, _("Abre Steam en tu PC con esta cuenta y vuelve a entrar. Desde aquí podrás ver lo que se descarga, pausarlo o desinstalar juegos."), SZ_SMALL, COL_DIM, 4, 15);
		return;
	}
	const RemotePC *pc = &DL.pc[DL.cur];
	char t[96], fb[24];
	ui_icon(IC_PC, X, 27, 14, COL_ACC);
	ui_text(X + 20, 24, pc->name, SZ_LARGE, COL_WHITE, AL_LEFT, W - 140);
	fmt_bytes(DL.freeb, fb, sizeof fb);
	snprintf(t, sizeof t, _("%s · %s libres"), pc->os, fb);
	ui_text(TOP_W - X, 28, DL.freeb ? t : pc->os, SZ_SMALL, COL_DIM, AL_RIGHT, 200);
	if (!DL.na) {
		ui_text(TOP_W / 2, 120, _("No hay juegos instalados en este PC"), SZ_BODY, COL_DIM, AL_CENTER, 380);
		return;
	}
	const PcApp *a = &DL.a[DL.sel < DL.na ? DL.sel : 0];
	char url[160];
	snprintf(url, sizeof url, "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/%lu/header.jpg", (unsigned long)a->appid);
	const float iw = 230, ih = floorf(iw * 215 / 460), iy = 54;
	const Pic *p = img_get(url, 460);
	ui_rect(X - 1, iy - 1, iw + 2, ih + 2, C2D_Color32(0xff, 0xff, 0xff, 0x30));
	if (p) img_draw_cover(p, X, iy, iw, ih, 0.5f);
	else ui_rect(X, iy, iw, ih, COL_PANEL);
	ui_text_wrap(X + iw + 12, iy, W - iw - 12, a->name, SZ_BODY, COL_WHITE, 3, 16);
	u32 col;
	pcdl_status(a, t, sizeof t, &col);
	ui_text_wrap(X + iw + 12, iy + 52, W - iw - 12, t, SZ_SMALL, col, 3, 14);
	if (a->changing && a->total > 0) {
		float by = iy + ih + 14;
		ui_rrect(X, by, W, 8, 4, col_alpha(COL_WHITE, 0.10f));
		ui_rrect(X, by, W * (float)a->done / (float)a->total, 8, 4, a->paused ? COL_DIM : COL_ACC);
	}
	const char *hint = DL.confirm_app == a->appid && osGetTime() < DL.confirm_at ? _("Pulsa X otra vez para desinstalarlo de tu PC") : NULL;
	if (hint) ui_text(TOP_W / 2, 208, hint, SZ_SMALL, COL_NEG, AL_CENTER, 380);
}

static void draw_pcdl_bottom(void)
{
	fx_bar(0, 0, BOT_W, LIST_Y, true);
	ui_text(10, 4, "‹", SZ_LARGE, COL_ACC, AL_LEFT, 0);
	ui_icon(IC_PC, 28, 6, 12, COL_ACC);
	char t[96];
	if (DL.npc > 1) snprintf(t, sizeof t, "%s (%d/%d)", DL.pc[DL.cur].name, DL.cur + 1, DL.npc);
	else snprintf(t, sizeof t, "%s", _("Descargas del PC"));
	ui_text(46, 5, t, SZ_SMALL, COL_WHITE, AL_LEFT, BOT_W - 60);
	if (DL.apps) ui_spinner(BOT_W - 14, 12, 4, COL_DIM);
	const float RH = 26, top = LIST_Y + 2;
	const int vis = (int)((SCR_H - 18 - top) / RH);
	int first = DL.sel - (vis - 2);
	if (first < 0) first = 0;
	bool header_dl = false, header_in = false;
	(void)header_dl;
	(void)header_in;
	for (int i = first; i < DL.na && i < first + vis; i++) {
		const PcApp *a = &DL.a[i];
		float y = top + (i - first) * RH;
		bool on = i == DL.sel;
		if (on) {
			float br = 0.075f + 0.035f * sinf(g_time * 2.4f);
			ui_rrect(3, y + 1, BOT_W - 8, RH - 2, 6, col_alpha(COL_WHITE, br));
			ui_rrect(3, y + 4, 2, RH - 8, 1, col_alpha(COL_ACC, 0.8f));
		}
		u32 col;
		pcdl_status(a, t, sizeof t, &col);
		ui_text(12, y + 2, a->name, SZ_SMALL, on ? COL_WHITE : COL_TXT, AL_LEFT, BOT_W - 24);
		ui_text(12, y + 14, t, SZ_TINY, col, AL_LEFT, BOT_W - 24);
		if (a->changing && a->total > 0) {
			ui_rrect(BOT_W - 70, y + 17, 56, 3, 1.5f, col_alpha(COL_WHITE, 0.10f));
			ui_rrect(BOT_W - 70, y + 17, 56 * (float)a->done / (float)a->total, 3, 1.5f, a->paused ? COL_DIM : COL_ACC);
		}
	}
	bool ch = pcdl_any_changing(), pa = pcdl_any_paused();
	if (DL.npc > 1) {
		static const char *h1[] = {"A", "Ficha", "Y", "Pausar", "X", "Desinstalar", "L R", "PC"};
		static const char *h2[] = {"A", "Ficha", "Y", "Reanudar", "X", "Desinstalar", "L R", "PC"};
		static const char *h3[] = {"A", "Ficha", "X", "Desinstalar", "L R", "PC", "B", "Volver"};
		draw_hints(ch ? (pa ? h2 : h1) : h3, 4);
	} else {
		static const char *h1[] = {"A", "Ficha", "Y", "Pausar", "X", "Desinstalar", "B", "Volver"};
		static const char *h2[] = {"A", "Ficha", "Y", "Reanudar", "X", "Desinstalar", "B", "Volver"};
		static const char *h3[] = {"A", "Ficha", "X", "Desinstalar", "B", "Volver"};
		if (ch) draw_hints(pa ? h2 : h1, 4);
		else draw_hints(h3, DL.na ? 3 : 0);
	}
}

static void input_pcdl(u32 kd, u32 kh, touchPosition *tp)
{
	if ((kd & KEY_B) || ((kd & KEY_TOUCH) && tp->py < LIST_Y && tp->px < 120)) {
		go_scene(SC_PROFILE, -1);
		return;
	}
	if (DL.npc > 1 && (kd & (KEY_L | KEY_R))) {
		DL.cur = (DL.cur + ((kd & KEY_R) ? 1 : DL.npc - 1)) % DL.npc;
		free(DL.a);
		DL.a = NULL;
		DL.na = DL.sel = 0;
		DL.apps_done = false;
		if (DL.apps) net_release(DL.apps);
		DL.apps = NULL;
		pcdl_load_apps();
		return;
	}
	if (!DL.na) return;
	if (key_repeat(kd, kh, KEY_DOWN) && DL.sel + 1 < DL.na) DL.sel++;
	if (key_repeat(kd, kh, KEY_UP) && DL.sel > 0) DL.sel--;
	if ((kd & KEY_TOUCH) && tp->py >= LIST_Y + 2) {
		const float RH = 26;
		const int vis = (int)((SCR_H - 18 - LIST_Y - 2) / RH);
		int first = DL.sel - (vis - 2);
		if (first < 0) first = 0;
		int i = first + (int)((tp->py - LIST_Y - 2) / RH);
		if (i >= 0 && i < DL.na) {
			if (i == DL.sel) kd |= KEY_A;
			DL.sel = i;
		}
	}
	const PcApp *a = &DL.a[DL.sel];
	if (kd & KEY_A) {
		open_game(a->appid, true);
		return;
	}
	if (DL.act) return;
	if ((kd & KEY_Y) && pcdl_any_changing()) {
		DL.act_kind = 1;
		DL.act = steam_req_pc_downloads(DL.pc[DL.cur].id, pcdl_any_paused(), auth_token());
	}
	if (kd & KEY_X) {
		// Dos veces seguidas para desinstalar (no tiene vuelta atrás)
		if (DL.confirm_app == a->appid && osGetTime() < DL.confirm_at) {
			DL.confirm_app = 0;
			DL.act_kind = 2;
			DL.act = steam_req_pc_uninstall(DL.pc[DL.cur].id, a->appid, auth_token());
		} else {
			DL.confirm_app = a->appid;
			DL.confirm_at = osGetTime() + 3000;
		}
	}
}

// ---------------------------------------------------------------------------- Tienda de puntos

static const int pt_cls[] = {3, 14, 15, 4, 11, 13};
static const char *const pt_cls_name[] = {"Fondos de perfil", "Marcos de avatar", "Avatares animados", "Emoticonos", "Pegatinas", "Fondos de miniperfil"};
#define PT_CLS 6
#define PT_MAX 180
static struct {
	long long points;
	Req *sum, *items, *redeem;
	PointItem it[PT_MAX];
	int n, total, cls, sel;
	char cursor[64];
	bool loaded, end, failed;
	u32 confirm;
	u64 confirm_at;
} PS;

static void points_load_more(void)
{
	if (PS.items || PS.end || PS.n >= PT_MAX) return;
	PS.items = steam_req_point_items(pt_cls[PS.cls], PS.cursor);
}

static void points_set_cls(int c)
{
	if (PS.items) net_release(PS.items);
	PS.items = NULL;
	PS.cls = (c + PT_CLS) % PT_CLS;
	PS.n = PS.sel = 0;
	PS.cursor[0] = 0;
	PS.loaded = PS.end = PS.failed = false;
	PS.confirm = 0;
	points_load_more();
}

static void open_points(void)
{
	if (PS.sum) net_release(PS.sum);
	PS.sum = steam_req_points(auth_info()->steamid, auth_token());
	PS.points = -1;
	points_set_cls(PS.cls);
	go_scene(SC_POINTS, 1);
}

static void points_poll(void)
{
	if (PS.sum && PS.sum->state >= REQ_DONE) {
		PS.points = PS.sum->state == REQ_DONE ? steam_parse_points(PS.sum->data) : -1;
		netlog("puntos de Steam: %lld", PS.points);
		net_release(PS.sum);
		PS.sum = NULL;
	}
	if (PS.items && PS.items->state >= REQ_DONE) {
		int n = PS.items->state == REQ_DONE ? steam_parse_point_items(PS.items->data, PS.it + PS.n, PT_MAX - PS.n, PS.cursor, sizeof PS.cursor, &PS.total) : -1;
		if (n < 0) PS.failed = !PS.n;
		else PS.n += n;
		PS.end = n <= 0 || !PS.cursor[0] || PS.n >= PT_MAX;
		PS.loaded = true;
		net_release(PS.items);
		PS.items = NULL;
	}
	if (PS.redeem && PS.redeem->state >= REQ_DONE) {
		Req *r = PS.redeem;
		bool ok = r->state == REQ_DONE && r->http == 200 && (r->eresult == 1 || r->eresult == 0) && r->data && strstr(r->data, "\"response\"");
		netlog("puntos de Steam: canjear -> %s (HTTP %ld, eresult %d)", ok ? "ok" : "fallo", r->http, r->eresult);
		char t[160];
		if (ok) snprintf(t, sizeof t, "%s", _("Canjeado. Ya lo tienes en tu cuenta para usarlo en tu perfil"));
		else if (r->eresult == 107 || r->eresult == 6) snprintf(t, sizeof t, "%s", _("No tienes puntos suficientes"));
		else if (r->eresult == 29 || r->eresult == 11) snprintf(t, sizeof t, "%s", _("Ya tienes este artículo"));
		else snprintf(t, sizeof t, _("No se pudo canjear (código %d)"), r->eresult);
		toast_icon(t, IC_COIN);
		net_release(r);
		PS.redeem = NULL;
		if (ok) PS.sum = steam_req_points(auth_info()->steamid, auth_token());   // el saldo nuevo
	}
}

static void fmt_points(long long p, char *out, size_t n)
{
	// Con separador de miles, como en Steam
	char raw[24];
	snprintf(raw, sizeof raw, "%lld", p < 0 ? 0 : p);
	int len = strlen(raw), o = 0;
	for (int i = 0; i < len && o < (int)n - 2; i++) {
		if (i && (len - i) % 3 == 0) out[o++] = '.';
		out[o++] = raw[i];
	}
	out[o] = 0;
}

static void draw_points_top(void)
{
	const float X = 16, W = TOP_W - 32;
	char t[96], pts[24];
	ui_icon(IC_COIN, X, 26, 13, COL_ACC);
	ui_text(X + 18, 24, _("Tienda de puntos"), SZ_LARGE, COL_WHITE, AL_LEFT, 200);
	if (PS.points >= 0) {
		fmt_points(PS.points, pts, sizeof pts);
		snprintf(t, sizeof t, _("Tienes %s puntos"), pts);
		ui_text(TOP_W - X, 28, t, SZ_SMALL, COL_LIME, AL_RIGHT, 180);
	}
	if (!PS.n) {
		if (PS.failed) ui_text(TOP_W / 2, 120, _("No se pudo cargar la tienda de puntos"), SZ_BODY, COL_DIM, AL_CENTER, 380);
		else draw_net_wait(TOP_W / 2, 120, NULL);
		return;
	}
	const PointItem *p = &PS.it[PS.sel < PS.n ? PS.sel : 0];
	char url[200];
	snprintf(url, sizeof url, "https://cdn.akamai.steamstatic.com/steamcommunity/public/images/items/%lu/%s", (unsigned long)p->appid, p->img);
	const float iy = 48, ih = 112;
	const Pic *pic = p->img[0] ? img_get(url, 400) : NULL;
	ui_rect(X, iy, W, ih, C2D_Color32(0, 0, 0, 0x50));
	if (pic) img_draw_fit(pic, X, iy, W, ih);
	else ui_spinner(TOP_W / 2, iy + ih / 2, 6, COL_DIM);
	ui_text(X, iy + ih + 6, p->name, SZ_BODY, COL_WHITE, AL_LEFT, W - 110);
	fmt_points(p->cost, pts, sizeof pts);
	snprintf(t, sizeof t, _("%s puntos"), pts);
	ui_text(TOP_W - X, iy + ih + 7, t, SZ_SMALL, PS.points >= 0 && PS.points < p->cost ? COL_NEG : COL_LIME, AL_RIGHT, 110);
	if (PS.confirm == p->defid && osGetTime() < PS.confirm_at)
		ui_text_wrap(X, iy + ih + 26, W, _("Pulsa A otra vez para canjearlo. Los puntos se descuentan de tu cuenta de Steam."), SZ_SMALL, COL_ACC, 2, 14);
	else if (p->desc[0]) ui_text_wrap(X, iy + ih + 26, W, p->desc, SZ_SMALL, COL_TXT, 3, 14);
}

static void draw_points_bottom(void)
{
	fx_bar(0, 0, BOT_W, LIST_Y, true);
	ui_text(10, 4, "‹", SZ_LARGE, COL_ACC, AL_LEFT, 0);
	ui_icon(IC_COIN, 28, 6, 12, COL_ACC);
	ui_text(46, 5, _(pt_cls_name[PS.cls]), SZ_SMALL, COL_WHITE, AL_LEFT, BOT_W - 120);
	char t[32];
	if (PS.total) {
		snprintf(t, sizeof t, "%d", PS.total);
		ui_text(BOT_W - 10, 6, t, SZ_TINY, COL_DIM, AL_RIGHT, 0);
	}
	const float RH = 24, top = LIST_Y + 2;
	const int vis = (int)((SCR_H - 18 - top) / RH);
	int first = PS.sel - (vis - 2);
	if (first < 0) first = 0;
	for (int i = first; i < PS.n && i < first + vis; i++) {
		const PointItem *p = &PS.it[i];
		float y = top + (i - first) * RH;
		bool on = i == PS.sel;
		if (on) {
			float br = 0.075f + 0.035f * sinf(g_time * 2.4f);
			ui_rrect(3, y + 1, BOT_W - 8, RH - 2, 6, col_alpha(COL_WHITE, br));
			ui_rrect(3, y + 4, 2, RH - 8, 1, col_alpha(COL_ACC, 0.8f));
		}
		char pts[24];
		fmt_points(p->cost, pts, sizeof pts);
		ui_text(12, y + 5, p->name, SZ_SMALL, on ? COL_WHITE : COL_TXT, AL_LEFT, BOT_W - 90);
		ui_text(BOT_W - 12, y + 6, pts, SZ_TINY, COL_LIME, AL_RIGHT, 70);
	}
	if (PS.items) ui_spinner(BOT_W / 2, SCR_H - 30, 5, COL_DIM);
	static const char *h[] = {"A", "Canjear", "↑↓", "Elegir", "L R", "Categoría", "B", "Volver"};
	draw_hints(h, 4);
}

static void input_points(u32 kd, u32 kh, touchPosition *tp)
{
	if ((kd & KEY_B) || ((kd & KEY_TOUCH) && tp->py < LIST_Y && tp->px < 120)) {
		go_scene(SC_PROFILE, -1);
		return;
	}
	if (kd & KEY_L) points_set_cls(PS.cls - 1);
	if (kd & KEY_R) points_set_cls(PS.cls + 1);
	if (key_repeat(kd, kh, KEY_DOWN) && PS.sel + 1 < PS.n) PS.sel++;
	if (key_repeat(kd, kh, KEY_UP) && PS.sel > 0) PS.sel--;
	if (PS.sel >= PS.n - 6) points_load_more();
	if (!(kd & KEY_A) || PS.sel >= PS.n || PS.redeem) return;
	const PointItem *p = &PS.it[PS.sel];
	// Dos veces para canjear: gasta puntos de tu cuenta
	if (PS.confirm == p->defid && osGetTime() < PS.confirm_at) {
		PS.confirm = 0;
		PS.redeem = steam_req_redeem(p->defid, auth_token());
	} else if (PS.points >= 0 && PS.points < p->cost) {
		toast(_("No tienes puntos suficientes"));
	} else {
		PS.confirm = p->defid;
		PS.confirm_at = osGetTime() + 4000;
	}
}

// ---------------------------------------------------------------------------- Inventario

static struct {
	Req *req;
	InvItem *it;
	int n, total, sel;
	bool done, failed;
} IV;

static void open_inv(void)
{
	if (IV.req) net_release(IV.req);
	free(IV.it);
	memset(&IV, 0, sizeof IV);
	char ck[1200];
	if (auth_web_cookies("steamcommunity.com", ck, sizeof ck)) IV.req = steam_req_inventory(auth_info()->steamid, ck);
	else IV.failed = IV.done = true;
	memset(ck, 0, sizeof ck);
	go_scene(SC_INV, 1);
}

static void inv_poll(void)
{
	if (!IV.req || IV.req->state < REQ_DONE) return;
	int n = IV.req->state == REQ_DONE ? steam_parse_inventory(IV.req->data, &IV.it, &IV.total) : -1;
	netlog("inventario: %d objetos distintos de %d (HTTP %ld)", n, IV.total, IV.req->http);
	IV.n = n < 0 ? 0 : n;
	IV.failed = n < 0;
	IV.done = true;
	net_release(IV.req);
	IV.req = NULL;
}

static void inv_icon(const InvItem *it, char *out, size_t n, int px)
{
	snprintf(out, n, "https://community.akamai.steamstatic.com/economy/image/%s/%dfx%df", it->icon, px, px);
}

#define INV_COLS 6
#define INV_CELL 48

static void draw_inv_top(void)
{
	const float X = 16, W = TOP_W - 32;
	char t[96];
	ui_icon(IC_CARD, X, 26, 13, COL_ACC);
	ui_text(X + 18, 24, _("Inventario de Steam"), SZ_LARGE, COL_WHITE, AL_LEFT, 220);
	if (IV.total) {
		snprintf(t, sizeof t, IV.total == 1 ? _("%d objeto") : _("%d objetos"), IV.total);
		ui_text(TOP_W - X, 28, t, SZ_SMALL, COL_DIM, AL_RIGHT, 120);
	}
	if (!IV.done) {
		draw_net_wait(TOP_W / 2, 120, NULL);
		return;
	}
	if (IV.failed || !IV.n) {
		ui_text(TOP_W / 2, 110, IV.failed ? _("No se pudo cargar tu inventario") : _("Tu inventario está vacío"), SZ_BODY, COL_DIM, AL_CENTER, 380);
		if (IV.failed) ui_text_wrap(X, 134, W, _("Si es privado, cámbialo en Configuración → Privacidad y perfil. Steam también limita las consultas seguidas: prueba en un momento."), SZ_SMALL, COL_DIM, 4, 15);
		return;
	}
	const InvItem *it = &IV.it[IV.sel < IV.n ? IV.sel : 0];
	char url[300];
	inv_icon(it, url, sizeof url, 184);
	const Pic *p = img_get(url, 200);
	ui_rect(X, 48, 120, 120, C2D_Color32(0, 0, 0, 0x50));
	if (p) img_draw_fit(p, X, 48, 120, 120);
	else ui_spinner(X + 60, 108, 6, COL_DIM);
	ui_text_wrap(X + 132, 50, W - 132, it->name, SZ_BODY, COL_WHITE, 3, 16);
	ui_text_wrap(X + 132, 104, W - 132, it->type, SZ_SMALL, COL_DIM, 2, 14);
	if (it->amount > 1) {
		snprintf(t, sizeof t, _("Tienes %d"), it->amount);
		ui_text(X + 132, 140, t, SZ_SMALL, COL_ACC, AL_LEFT, 0);
	}
	if (IV.total > IV.n) {
		snprintf(t, sizeof t, _("Se muestran los primeros %d tipos de objetos"), IV.n);
		ui_text(X, 206, t, SZ_TINY, COL_DIM, AL_LEFT, W);
	}
}

static void draw_inv_bottom(void)
{
	fx_bar(0, 0, BOT_W, LIST_Y, true);
	ui_text(10, 4, "‹", SZ_LARGE, COL_ACC, AL_LEFT, 0);
	ui_icon(IC_CARD, 28, 6, 12, COL_ACC);
	ui_text(46, 5, _("Inventario"), SZ_SMALL, COL_WHITE, AL_LEFT, BOT_W - 60);
	const float gx = (BOT_W - INV_COLS * INV_CELL) / 2, top = LIST_Y + 4;
	const int rows_vis = (int)((SCR_H - 18 - top) / INV_CELL);
	int row = IV.sel / INV_COLS, first = row - (rows_vis - 1);
	if (first < 0) first = 0;
	for (int i = first * INV_COLS; i < IV.n && i < (first + rows_vis) * INV_COLS; i++) {
		float x = gx + (i % INV_COLS) * INV_CELL, y = top + (i / INV_COLS - first) * INV_CELL;
		if (i == IV.sel) fx_select(x + 1, y + 1, INV_CELL - 2, INV_CELL - 2, 6, 1);
		char url[300];
		inv_icon(&IV.it[i], url, sizeof url, 96);
		const Pic *p = img_get(url, 64);
		if (p) img_draw_fit(p, x + 5, y + 5, INV_CELL - 10, INV_CELL - 10);
		else ui_rect(x + 5, y + 5, INV_CELL - 10, INV_CELL - 10, col_alpha(COL_WHITE, 0.06f));
		if (IV.it[i].amount > 1) {
			char a[12];
			snprintf(a, sizeof a, "%d", IV.it[i].amount);
			ui_text(x + INV_CELL - 5, y + INV_CELL - 15, a, SZ_TINY, COL_WHITE, AL_RIGHT, 0);
		}
	}
	static const char *h[] = {"↑↓", "Elegir", "B", "Volver"};
	draw_hints(h, 2);
}

static void input_inv(u32 kd, u32 kh, touchPosition *tp)
{
	if ((kd & KEY_B) || ((kd & KEY_TOUCH) && tp->py < LIST_Y && tp->px < 120)) {
		go_scene(SC_PROFILE, -1);
		return;
	}
	if (!IV.n) return;
	if (key_repeat(kd, kh, KEY_RIGHT) && IV.sel + 1 < IV.n) IV.sel++;
	if (key_repeat(kd, kh, KEY_LEFT) && IV.sel > 0) IV.sel--;
	if (key_repeat(kd, kh, KEY_DOWN)) IV.sel = IV.sel + INV_COLS < IV.n ? IV.sel + INV_COLS : IV.n - 1;
	if (key_repeat(kd, kh, KEY_UP) && IV.sel >= INV_COLS) IV.sel -= INV_COLS;
	if ((kd & KEY_TOUCH) && tp->py >= LIST_Y + 4) {
		const float gx = (BOT_W - INV_COLS * INV_CELL) / 2, top = LIST_Y + 4;
		const int rows_vis = (int)((SCR_H - 18 - top) / INV_CELL);
		int row = IV.sel / INV_COLS, first = row - (rows_vis - 1);
		if (first < 0) first = 0;
		int c = (int)((tp->px - gx) / INV_CELL), r = (int)((tp->py - top) / INV_CELL);
		int i = (first + r) * INV_COLS + c;
		if (c >= 0 && c < INV_COLS && i >= 0 && i < IV.n) IV.sel = i;
	}
}

// ---------------------------------------------------------------------------- Historial de compras

#define HIST_MAX 60
static struct {
	Req *req;
	Purchase p[HIST_MAX];
	int n, sel;
	bool done, failed;
} HI;

static void open_hist(void)
{
	if (HI.req) net_release(HI.req);
	memset(&HI, 0, sizeof HI);
	char ck[1200];
	if (auth_web_cookies("store.steampowered.com", ck, sizeof ck)) HI.req = steam_req_history(ck);
	else HI.failed = HI.done = true;
	memset(ck, 0, sizeof ck);
	go_scene(SC_HIST, 1);
}

static void hist_poll(void)
{
	if (!HI.req || HI.req->state < REQ_DONE) return;
	int n = HI.req->state == REQ_DONE ? steam_parse_history(HI.req->data, HI.p, HIST_MAX) : -1;
	netlog("historial de compras: %d (HTTP %ld)", n, HI.req->http);
	HI.n = n < 0 ? 0 : n;
	HI.failed = n < 0;
	HI.done = true;
	net_release(HI.req);
	HI.req = NULL;
}

static void draw_hist_top(void)
{
	const float X = 16, W = TOP_W - 32;
	ui_icon(IC_DOC, X, 26, 13, COL_ACC);
	ui_text(X + 18, 24, _("Historial de compras"), SZ_LARGE, COL_WHITE, AL_LEFT, W - 18);
	if (!HI.done) {
		draw_net_wait(TOP_W / 2, 120, NULL);
		return;
	}
	if (HI.failed || !HI.n) {
		ui_text(TOP_W / 2, 116, HI.failed ? _("No se pudo cargar tu historial de compras") : _("Todavía no hay compras en tu cuenta"), SZ_BODY, COL_DIM, AL_CENTER, 380);
		return;
	}
	const Purchase *p = &HI.p[HI.sel < HI.n ? HI.sel : 0];
	fx_glass(X - 4, 50, W + 8, 120, 12, 1, 0);
	ui_text(X + 6, 58, p->date, SZ_SMALL, COL_DIM, AL_LEFT, 200);
	ui_text(TOP_W - X - 6, 56, p->total, SZ_LARGE, COL_LIME, AL_RIGHT, 160);
	ui_text_wrap(X + 6, 82, W - 12, p->items, SZ_BODY, COL_WHITE, 3, 17);
	ui_text(X + 6, 146, p->type, SZ_SMALL, COL_ACC, AL_LEFT, W - 12);
	char t[64];
	snprintf(t, sizeof t, _("Se muestran tus últimas %d operaciones"), HI.n);
	ui_text(X, 206, t, SZ_TINY, COL_DIM, AL_LEFT, W);
}

static void draw_hist_bottom(void)
{
	fx_bar(0, 0, BOT_W, LIST_Y, true);
	ui_text(10, 4, "‹", SZ_LARGE, COL_ACC, AL_LEFT, 0);
	ui_icon(IC_DOC, 28, 6, 12, COL_ACC);
	ui_text(46, 5, _("Historial de compras"), SZ_SMALL, COL_WHITE, AL_LEFT, BOT_W - 60);
	const float RH = 30, top = LIST_Y + 2;
	const int vis = (int)((SCR_H - 18 - top) / RH);
	int first = HI.sel - (vis - 2);
	if (first < 0) first = 0;
	for (int i = first; i < HI.n && i < first + vis; i++) {
		const Purchase *p = &HI.p[i];
		float y = top + (i - first) * RH;
		bool on = i == HI.sel;
		if (on) {
			float br = 0.075f + 0.035f * sinf(g_time * 2.4f);
			ui_rrect(3, y + 1, BOT_W - 8, RH - 2, 6, col_alpha(COL_WHITE, br));
			ui_rrect(3, y + 4, 2, RH - 8, 1, col_alpha(COL_ACC, 0.8f));
		}
		ui_text(12, y + 3, p->items, SZ_SMALL, on ? COL_WHITE : COL_TXT, AL_LEFT, BOT_W - 100);
		ui_text(BOT_W - 12, y + 3, p->total, SZ_SMALL, COL_TXT, AL_RIGHT, 80);
		char t[96];
		snprintf(t, sizeof t, "%s · %s", p->date, p->type);
		ui_text(12, y + 16, t, SZ_TINY, COL_DIM, AL_LEFT, BOT_W - 24);
	}
	static const char *h[] = {"↑↓", "Elegir", "B", "Volver"};
	draw_hints(h, 2);
}

static void input_hist(u32 kd, u32 kh, touchPosition *tp)
{
	if ((kd & KEY_B) || ((kd & KEY_TOUCH) && tp->py < LIST_Y && tp->px < 120)) {
		go_scene(SC_SETTINGS, -1);
		return;
	}
	if (key_repeat(kd, kh, KEY_DOWN) && HI.sel + 1 < HI.n) HI.sel++;
	if (key_repeat(kd, kh, KEY_UP) && HI.sel > 0) HI.sel--;
}

// ---------------------------------------------------------------------------- Premios a las reseñas

#define AW_MAX 16
static struct {
	bool open;
	float anim;
	SteamAward a[AW_MAX];
	int n, sel;
	bool loaded;
	long long points;
	Req *cfg, *pts, *give;
	u64 target;
	u32 confirm;
	u64 confirm_at;
} AW;

static const char *award_name(const SteamAward *a)
{
	static const char *es[] = {"Ganador", "Precioso", "Útil", "En llamas", "Gracioso", "Cien", "Salvavidas", "Perfecto", "Más uno", "Inteligente", "Oro puro", "Entrañable"};
	if (a->id >= 24 && a->id <= 35) return _(es[a->id - 24]);
	return a->title;
}

static void award_close(void)
{
	AW.open = false;
}

static void award_open(u64 review_id)
{
	AW.open = true;
	AW.target = review_id;
	AW.sel = 0;
	AW.confirm = 0;
	if (!AW.loaded && !AW.cfg) AW.cfg = steam_req_award_config();
	if (!AW.pts) AW.pts = steam_req_points(auth_info()->steamid, auth_token());
	AW.points = -1;
}

static void award_poll(void)
{
	if (AW.cfg && AW.cfg->state >= REQ_DONE) {
		int n = AW.cfg->state == REQ_DONE ? steam_parse_award_config(AW.cfg->data, AW.a, AW_MAX) : -1;
		AW.n = n < 0 ? 0 : n;
		AW.loaded = n > 0;
		net_release(AW.cfg);
		AW.cfg = NULL;
	}
	if (AW.pts && AW.pts->state >= REQ_DONE) {
		AW.points = AW.pts->state == REQ_DONE ? steam_parse_points(AW.pts->data) : -1;
		net_release(AW.pts);
		AW.pts = NULL;
	}
	if (AW.give && AW.give->state >= REQ_DONE) {
		Req *r = AW.give;
		bool ok = r->state == REQ_DONE && r->http == 200 && (r->eresult == 1 || r->eresult == 0);
		netlog("premio: -> %s (HTTP %ld, eresult %d)", ok ? "ok" : "fallo", r->http, r->eresult);
		toast_icon(ok ? _("Premio enviado. ¡Gracias por apoyar a la comunidad!") : r->eresult == 107 || r->eresult == 6 ? _("No tienes puntos suficientes")
			: r->eresult == 29 ? _("Ya le has dado ese premio") : _("No se pudo enviar el premio"), IC_TROPHY);
		net_release(r);
		AW.give = NULL;
		if (ok) AW.open = false;
	}
}

static void award_icon(u32 id, char *out, size_t n)
{
	snprintf(out, n, "https://store.akamai.steamstatic.com/public/images/loyalty/reactions/still/%lu.png", (unsigned long)id);
}

static void draw_award(void)
{
	float an = ease_out_cubic(AW.anim);
	if (an <= 0.01f) return;
	ui_rect(0, 0, BOT_W, SCR_H, C2D_Color32(0, 0, 0, (u8)(0x90 * an)));
	float y0 = 58 + (1 - an) * 60;
	fx_panel(6, y0, BOT_W - 12, SCR_H - y0 + 16, 16, an);
	ui_text(18, y0 + 10, _("Dar un premio a esta reseña"), SZ_BODY, col_alpha(COL_WHITE, an), AL_LEFT, 200);
	if (AW.points >= 0) {
		char pts[24], t[48];
		fmt_points(AW.points, pts, sizeof pts);
		snprintf(t, sizeof t, _("Tienes %s puntos"), pts);
		ui_text(BOT_W - 18, y0 + 12, t, SZ_TINY, col_alpha(COL_LIME, an), AL_RIGHT, 110);
	}
	if (!AW.loaded) {
		ui_spinner(BOT_W / 2, y0 + 80, 6, COL_DIM);
		return;
	}
	const int cols = 6;
	const float cell = 46, gx = (BOT_W - cols * cell) / 2, gy = y0 + 34;
	for (int i = 0; i < AW.n; i++) {
		float x = gx + (i % cols) * cell, y = gy + (i / cols) * cell;
		if (i == AW.sel) fx_select(x + 1, y + 1, cell - 2, cell - 2, 8, an);
		char url[160];
		award_icon(AW.a[i].id, url, sizeof url);
		const Pic *p = img_get(url, 64);
		if (p) img_draw_fit(p, x + 6, y + 6, cell - 12, cell - 12);
	}
	const SteamAward *a = &AW.a[AW.sel < AW.n ? AW.sel : 0];
	char t[96], pts[24];
	fmt_points(a->cost, pts, sizeof pts);
	float ty = gy + ((AW.n + cols - 1) / cols) * cell + 6;
	snprintf(t, sizeof t, _("%s · %s puntos"), award_name(a), pts);
	ui_text(BOT_W / 2, ty, t, SZ_SMALL, col_alpha(COL_WHITE, an), AL_CENTER, 290);
	bool confirm = AW.confirm == a->id && osGetTime() < AW.confirm_at;
	ui_text(BOT_W / 2, ty + 18, AW.give ? _("Enviando…") : confirm ? _("Pulsa A otra vez para darlo") : _("A: dar el premio · B: cancelar"), SZ_TINY,
		col_alpha(confirm ? COL_ACC : COL_DIM, an), AL_CENTER, 290);
}

static void input_award(u32 kd, u32 kh, touchPosition *tp)
{
	if (kd & KEY_B) {
		AW.open = false;
		return;
	}
	const int cols = 6;
	if (key_repeat(kd, kh, KEY_RIGHT) && AW.sel + 1 < AW.n) AW.sel++;
	if (key_repeat(kd, kh, KEY_LEFT) && AW.sel > 0) AW.sel--;
	if (key_repeat(kd, kh, KEY_DOWN) && AW.sel + cols < AW.n) AW.sel += cols;
	if (key_repeat(kd, kh, KEY_UP) && AW.sel >= cols) AW.sel -= cols;
	if ((kd & KEY_TOUCH)) {
		const float cell = 46, gx = (BOT_W - cols * cell) / 2, gy = 58 + 34;
		int c = (int)((tp->px - gx) / cell), r = (int)((tp->py - gy) / cell);
		int i = r * cols + c;
		if (tp->py >= gy && c >= 0 && c < cols && i >= 0 && i < AW.n) {
			if (i == AW.sel) kd |= KEY_A;
			AW.sel = i;
		}
	}
	if (!(kd & KEY_A) || !AW.loaded || AW.give || AW.sel >= AW.n) return;
	const SteamAward *a = &AW.a[AW.sel];
	if (AW.points >= 0 && AW.points < a->cost) {
		toast(_("No tienes puntos suficientes"));
	} else if (AW.confirm == a->id && osGetTime() < AW.confirm_at) {
		AW.confirm = 0;
		AW.give = steam_req_award(1, AW.target, a->id, auth_token());
	} else {
		AW.confirm = a->id;
		AW.confirm_at = osGetTime() + 4000;
	}
}

// ---------------------------------------------------------------------------- Editor de tu reseña

static const Box B_RUP = {12, 64, 145, 30};
static const Box B_RDOWN = {163, 64, 145, 30};
static const Box B_RTEXT = {12, 102, 296, 44};
static const Box B_RVIS = {12, 152, 296, 26};
static const Box B_RCANCEL = {12, 186, 145, 30};
static const Box B_RSEND = {163, 186, 145, 30};

static void review_editor_close(void)
{
	RE.open = false;
}

static void review_editor_open(const AppInfo *a)
{
	(void)a;
	RE.open = true;
	RE.up = MR.r.has ? MR.r.up : true;
	RE.pub = true;
	snprintf(RE.text, sizeof RE.text, "%s", MR.r.has && MR.r.text ? MR.r.text : "");
	RE.focus = MR.r.has ? 1 : 0;
	RE.on_publish = true;
}

static void open_user(u64 id, const SteamPerson *person);

// El código de amigo (o el SteamID entero) de alguien: se abre su perfil, desde donde se le añade
static void open_fcode_keyboard(void)
{
	SwkbdState kb;
	char buf[24] = "";
	swkbdInit(&kb, SWKBD_TYPE_NUMPAD, 2, 17);
	swkbdSetHintText(&kb, _("Código de amigo"));
	swkbdSetButton(&kb, SWKBD_BUTTON_LEFT, _("Cancelar"), false);
	swkbdSetButton(&kb, SWKBD_BUTTON_RIGHT, _("Buscar"), true);
	swkbdSetFeatures(&kb, SWKBD_DARKEN_TOP_SCREEN | SWKBD_ALLOW_HOME | SWKBD_ALLOW_RESET | SWKBD_ALLOW_POWER);
	swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	if (swkbdInputText(&kb, buf, sizeof buf) != SWKBD_BUTTON_RIGHT) return;
	unsigned long long v = strtoull(buf, NULL, 10);
	u64 id = v >= 76561197960265728ULL ? v : v + 76561197960265728ULL;
	if (!v || v > 76561202255233023ULL) {
		toast(_("Ese código de amigo no es válido"));
		return;
	}
	if (auth_logged() && id == auth_info()->steamid) {
		toast(_("Ese es tu propio código de amigo"));
		return;
	}
	open_user(id, NULL);
}

static void open_review_keyboard(void)
{
	SwkbdState kb;
	swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, 2000);
	swkbdSetHintText(&kb, _("Escribe tu reseña"));
	swkbdSetInitialText(&kb, RE.text);
	swkbdSetButton(&kb, SWKBD_BUTTON_LEFT, _("Cancelar"), false);
	swkbdSetButton(&kb, SWKBD_BUTTON_RIGHT, _("Aceptar"), true);
	swkbdSetFeatures(&kb, SWKBD_DARKEN_TOP_SCREEN | SWKBD_ALLOW_HOME | SWKBD_ALLOW_RESET | SWKBD_ALLOW_POWER | SWKBD_MULTILINE);
	static char buf[REV_MAX];
	if (swkbdInputText(&kb, buf, sizeof buf) != SWKBD_BUTTON_RIGHT) return;
	snprintf(RE.text, sizeof RE.text, "%s", buf);
	RE.focus = 2;
}

static bool review_text_empty(void)
{
	for (const char *p = RE.text; *p; p++)
		if (*p != ' ' && *p != '\n') return false;
	return true;
}

// Lo que se va a publicar, en grande arriba
static void draw_review_preview(const AppInfo *a)
{
	ui_text(12, 24, _("VISTA PREVIA DE TU RESEÑA"), SZ_SMALL, COL_ACC, AL_LEFT, 0);
	ui_text(TOP_W - 12, 25, a->name, SZ_SMALL, COL_DIM, AL_RIGHT, 220);
	fx_glass(10, 44, TOP_W - 20, SCR_H - 52, 12, 1, 0);
	ui_icon(RE.up ? IC_THUMB_UP : IC_THUMB_DOWN, 20, 53, 14, RE.up ? COL_ACC : COL_NEG);
	ui_text(40, 52, RE.up ? _("Recomendado") : _("No recomendado"), SZ_BODY, COL_WHITE, AL_LEFT, 0);
	ui_text(TOP_W - 22, 54, RE.pub ? _("Pública") : _("Solo amigos"), SZ_TINY, COL_DIM, AL_RIGHT, 0);
	if (review_text_empty()) ui_text(20, 78, _("Todavía no has escrito nada"), SZ_SMALL, COL_DIM, AL_LEFT, 0);
	else ui_text_wrap(20, 78, TOP_W - 40, RE.text, SZ_SMALL, COL_TXT, 10, 14);
}

static void draw_review_editor(const AppInfo *a)
{
	ui_rect(0, 0, BOT_W, SCR_H, col_alpha(COL_BG, 0.97f));
	ui_text(BOT_W / 2, 8, MR.r.has ? _("Editar tu reseña") : _("Escribir una reseña"), SZ_LARGE, COL_WHITE, AL_CENTER, 300);
	ui_text(BOT_W / 2, 30, a->name, SZ_TINY, COL_DIM, AL_CENTER, 300);
	ui_text(BOT_W / 2, 47, _("¿Recomiendas este juego?"), SZ_SMALL, COL_TXT, AL_CENTER, 300);
	u32 g1 = C2D_Color32(0x3a, 0x6f, 0x96, 0xff), g2 = C2D_Color32(0x2a, 0x55, 0x78, 0xff);
	u32 r1 = C2D_Color32(0x8a, 0x34, 0x2c, 0xff), r2 = C2D_Color32(0x6a, 0x26, 0x22, 0xff);
	if (RE.up) draw_button_k(&B_RUP, NULL, RE.focus == 0, IC_THUMB_UP, _("Sí"), g1, g2, COL_WHITE, 1, S.pressed == BT_RUP);
	else draw_button_k(&B_RUP, NULL, RE.focus == 0, IC_THUMB_UP, _("Sí"), COL_BTN2, COL_BTN2, COL_DIM, 1, S.pressed == BT_RUP);
	if (!RE.up) draw_button_k(&B_RDOWN, NULL, RE.focus == 0, IC_THUMB_DOWN, _("No"), r1, r2, COL_WHITE, 1, S.pressed == BT_RDOWN);
	else draw_button_k(&B_RDOWN, NULL, RE.focus == 0, IC_THUMB_DOWN, _("No"), COL_BTN2, COL_BTN2, COL_DIM, 1, S.pressed == BT_RDOWN);
	// El texto: un recuadro que abre el teclado
	FxButton fb = {0, 1, S.pressed == BT_RTEXT, RE.focus == 1};
	fx_button(B_RTEXT.x, B_RTEXT.y, B_RTEXT.w, B_RTEXT.h, 10, &fb);
	ui_icon(IC_DOC, B_RTEXT.x + 10, B_RTEXT.y + 6, 12, COL_ACC);
	if (review_text_empty()) ui_text(B_RTEXT.x + 28, B_RTEXT.y + 5, _("Escribe tu reseña…"), SZ_SMALL, COL_DIM, AL_LEFT, B_RTEXT.w - 40);
	else ui_text_wrap(B_RTEXT.x + 28, B_RTEXT.y + 5, B_RTEXT.w - 40, RE.text, SZ_TINY, COL_TXT, 3, 12);
	char vis[64];
	snprintf(vis, sizeof vis, _("Visibilidad: %s"), RE.pub ? _("Pública") : _("Solo amigos"));
	draw_button_k(&B_RVIS, NULL, RE.focus == 2, RE.pub ? IC_USERS : IC_LOCK, vis, COL_BTN2, COL_BTN2, COL_ACC, 1, S.pressed == BT_RVIS);
	draw_button_k(&B_RCANCEL, NULL, RE.focus == 3 && !RE.on_publish, IC_POWER, _("Cancelar"), COL_BTN2, COL_BTN2, COL_DIM, 1, S.pressed == BT_RCANCEL);
	bool busy = shop_busy_with(SH_REVIEW, a->appid);
	draw_button_k(&B_RSEND, NULL, RE.focus == 3 && RE.on_publish, IC_CHECK, busy ? _("Publicando…") : MR.r.has ? _("Guardar cambios") : _("Publicar"),
		C2D_Color32(0x75, 0xb0, 0x22, 0xff), C2D_Color32(0x58, 0x8a, 0x1b, 0xff), COL_WHITE, review_text_empty() ? 0.45f : 1, S.pressed == BT_RSEND);
	static const char *h[] = {"↑↓", "Elegir", "A", "Aceptar", "B", "Cancelar"};
	draw_hints(h, 3);
}

static void input_review_editor(const AppInfo *a, u32 kd, u32 kh, u32 ku, touchPosition *tp)
{
	if (kd & KEY_B) {
		RE.open = false;
		return;
	}
	static const int ids[] = {BT_RUP, BT_RDOWN, BT_RTEXT, BT_RVIS, BT_RCANCEL, BT_RSEND};
	int b = press_update(kd, kh, ku, tp, ids, 6);
	if (key_repeat(kd, kh, KEY_DOWN) && RE.focus < 3) RE.focus++;
	if (key_repeat(kd, kh, KEY_UP) && RE.focus > 0) RE.focus--;
	if (kd & (KEY_LEFT | KEY_RIGHT)) {
		if (RE.focus == 0) RE.up = !(kd & KEY_RIGHT);
		if (RE.focus == 3) RE.on_publish = (kd & KEY_RIGHT) != 0;
	}
	if (kd & KEY_A) {
		static const int by_focus[] = {0, BT_RTEXT, BT_RVIS, 0};
		b = RE.focus == 3 ? (RE.on_publish ? BT_RSEND : BT_RCANCEL) : by_focus[RE.focus];
		if (RE.focus == 0) RE.focus = 1;   // elegido: al texto
	}
	if (b == BT_RUP) RE.up = true, RE.focus = 0;
	if (b == BT_RDOWN) RE.up = false, RE.focus = 0;
	if (b == BT_RTEXT) RE.want_kb = true;
	if (b == BT_RVIS) RE.pub = !RE.pub;
	if (b == BT_RCANCEL) RE.open = false;
	if (b == BT_RSEND) {
		if (review_text_empty()) toast(_("Escribe algo en tu reseña antes de publicarla"));
		else if (shop_busy()) toast(_("Espera a que termine la acción anterior"));
		else {
			shop_review(a->appid, a->name, RE.up, RE.text, RE.pub);
			RE.open = false;
		}
	}
}

static void draw_game_bottom(void)
{
	InfoEnt *e = info_get(S.app, false);
	const AppInfo *a = (e && e->done) ? &e->info : NULL;

	// Selector de apartados
	fx_glass(B_SEG.x, B_SEG.y, B_SEG.w, B_SEG.h, B_SEG.h / 2, 1, 0);
	static const char *secs[] = {"Resumen", "Requisitos", "Opiniones", "Logros", "Extras", "Similares"};
	float sw = (B_SEG.w - 4) / SEC_COUNT;
	// La píldora del apartado se desliza hasta su sitio
	{
		float px = B_SEG.x + 2 + S.seg_x * sw;
		fx_select(px, B_SEG.y + 2, sw, B_SEG.h - 4, (B_SEG.h - 4) / 2, 1);
	}
	for (int i = 0; i < SEC_COUNT; i++) {
		float x = B_SEG.x + 2 + i * sw;
		float on = 1 - clampf(fabsf(S.seg_x - i), 0, 1);
		// Con seis apartados algún nombre no cabe: se encoge un poco en vez de cortarse
		float tw = ui_text_w(_(secs[i]), SZ_TINY), tsz = tw > sw - 6 ? SZ_TINY * (sw - 6) / tw : SZ_TINY;
		ui_text(x + sw / 2, B_SEG.y + (B_SEG.h - ui_line_h(tsz)) / 2 + 1, _(secs[i]), tsz, col_lerp(COL_DIM, COL_WHITE, on), AL_CENTER, 0);
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
		else if (!own && in && family_app(a->appid)) snprintf(top, sizeof top, "%s", _("En tu biblioteca familiar"));
		else snprintf(top, sizeof top, "%s", own ? _("En tu biblioteca") : a->is_free ? _("Jugar") : _("Comprar"));
		bool fam = !own && in && family_app(a->appid);
		ui_text(12, 36, top, SZ_SMALL, own ? COL_LIME : fam ? COL_ACC : COL_DIM, AL_LEFT, 0);
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
		if (S.has_demo_btn && S.demo_is_pt) {
			// El playtest: se pide acceso (a veces entras al momento; si no, te avisan por correo)
			u32 pt = playtest_for(a->appid);
			bool in_pt = owned_app(pt), asked = pt_asked(a->appid), busy = shop_busy_with(SH_PLAYTEST, a->appid);
			const char *pl = in_pt ? _("Ya tienes acceso al playtest") : busy ? _("Solicitando acceso…") : asked ? _("Has solicitado acceso al playtest")
				: _("Solicitar acceso al playtest");
			bool on = !in_pt && !asked;
			draw_button_k(&B_DEMO, on ? "SELECT" : NULL, false, IC_PAD, pl, C2D_Color32(0x8f, 0x6b, 0xd8, 0xff), C2D_Color32(0x5e, 0x45, 0x9c, 0xff),
				COL_WHITE, on ? 1 : 0.45f, S.pressed == BT_DEMO);
		} else if (S.has_demo_btn) {
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
		int mine = my_shots_for(a->appid), nsh = a->nshots + mine;
		if (!a->nshots && mine) snprintf(shots, sizeof shots, _("Tus capturas (%d)"), mine);
		else snprintf(shots, sizeof shots, _("Capturas (%d)"), nsh);
		draw_button_k(&B_SHOTS, nsh ? "X+→" : NULL, false, IC_IMAGE, shots, COL_BTN2, COL_BTN2, COL_ACC, nsh ? 1 : 0.45f, S.pressed == BT_SHOTS);
		if (!has_ignore_btn)
			draw_button_k(&B_INST, own ? "X+↓" : NULL, false, IC_PC, has_follow_btn ? _("Instalar en el PC") : _("Instalar en tu PC de forma remota"),
				C2D_Color32(0x47, 0xbf, 0xff, 0xff), C2D_Color32(0x1a, 0x44, 0xc2, 0xff), COL_WHITE, own ? 1 : 0.4f, S.pressed == BT_INST);
		if (has_follow_btn) {
			bool fo = shop_followed(a->appid);
			bool busy_fo = shop_busy_with(fo ? SH_UNFOLLOW : SH_FOLLOW, a->appid);
			const char *fk = has_ignore_btn ? "X+↓" : "X+↑";
			if (fo) draw_button_k(&B_FOLLOW, fk, false, IC_CHECK, busy_fo ? _("Quitando…") : _("Siguiendo"), C2D_Color32(0x3a, 0x6f, 0x96, 0xff), C2D_Color32(0x2a, 0x55, 0x78, 0xff), COL_WHITE, 1, S.pressed == BT_FOLLOW);
			else draw_button_k(&B_FOLLOW, fk, false, IC_BELL, busy_fo ? _("Siguiendo…") : _("Seguir"), COL_BTN2, COL_BTN2, COL_ACC, 1, S.pressed == BT_FOLLOW);
		}
		if (has_ignore_btn) {
			bool ign = shop_ignored(a->appid);
			bool busy_ign = shop_busy_with(ign ? SH_UNIGNORE : SH_IGNORE, a->appid);
			if (ign) draw_button_k(&B_IGNORE, "X+↑", false, IC_HIDE, busy_ign ? _("Quitando…") : _("Ignorado"), C2D_Color32(0x5a, 0x5f, 0x66, 0xff), C2D_Color32(0x40, 0x44, 0x4a, 0xff), COL_WHITE, 1, S.pressed == BT_IGNORE);
			else draw_button_k(&B_IGNORE, "X+↑", false, IC_HIDE, busy_ign ? _("Ignorando…") : _("Ignorar"), COL_BTN2, COL_BTN2, COL_ACC, 1, S.pressed == BT_IGNORE);
		}
		float ny = B_INST.y + B_INST.h + 5;
		if (!in) ui_text(BOT_W / 2, ny, _("Inicia sesión (START) para comprar y usar tu lista de deseos"), SZ_TINY, COL_DIM, AL_CENTER, 300);
		else if (!own) ui_text(BOT_W / 2, ny, _("Sigue un juego para enterarte de sus novedades en Steam"), SZ_TINY, COL_DIM, AL_CENTER, 300);
	}

	if (S.show_qr) {
		static const char *h[] = {"B", "Volver"};
		draw_hints(h, 1);
	} else if (S.sec >= SEC_EXTRAS) {
		static const char *h[] = {"↑↓", "Elegir", "A", "Abrir", "L R", "Apartado", "B", "Volver", "START", "Menú"};
		draw_hints(h, 5);
	} else if (S.sec == SEC_ACH && ach_cmp_count()) {
		static const char *h[] = {"↑↓", "Elegir", "Y", "Comparar", "L R", "Apartado", "B", "Volver"};
		draw_hints(h, 4);
	} else if (S.sec == SEC_ACH) {
		static const char *h[] = {"↑↓", "Elegir", "L R", "Apartado", "B", "Volver", "START", "Menú"};
		draw_hints(h, 4);
	} else if (S.sec == SEC_REVIEWS && S.rsel == -1 && MR.loaded) {
		static const char *hw[] = {"A", "Escribir reseña", "↑↓", "Leer", "Y", "Filtrar", "B", "Volver"};
		static const char *he[] = {"A", "Editar reseña", "↑↓", "Leer", "Y", "Filtrar", "B", "Volver"};
		draw_hints(MR.r.has ? he : hw, 4);
	} else if (S.sec == SEC_REVIEWS && auth_logged()) {
		static const char *h[] = {"A", "Premiar", "↑↓", "Leer", "Y", "Filtrar", "B", "Volver"};
		draw_hints(h, 4);
	} else if (S.sec == SEC_REVIEWS) {
		static const char *h[] = {"↑↓", "Leer", "Y", "Filtrar", "L R", "Apartado", "B", "Volver"};
		draw_hints(h, 4);
	} else if (S.sec == SEC_SUMMARY) {
		static const char *h[] = {"↑↓", "Desplazar", "L R", "Apartado", "B", "Volver", "START", "Menú"};
		draw_hints(h, 4);
	} else {
		static const char *h[] = {"L R", "Apartado", "B", "Volver", "START", "Menú"};
		draw_hints(h, 3);
	}
	if (a) draw_pick(a);
	if (a && RE.open) draw_review_editor(a);
	if (a) draw_remote(a);
	draw_award();
}

// ============================================================================
// Capturas

static void draw_shots_top(void)
{
	InfoEnt *e = info_get(S.app, false);
	ui_rect(0, 0, TOP_W, SCR_H, C2D_Color32(0, 0, 0, 0xff));
	if (!e || !e->done || shots_count(&e->info) == 0) return;
	const char *url = shot_url(&e->info, S.shot);
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
	if (S.shot + 1 < shots_count(&e->info)) img_get(shot_url(&e->info, S.shot + 1), 400);
	if (S.shot > 0) img_get(shot_url(&e->info, S.shot - 1), 400);
}

#define THUMB_W 92
#define THUMB_H 52
#define THUMB_STEP 100
#define STRIP_Y 76
static const Box B_PREV = {12, 156, 140, 34};
static const Box B_NEXT = {168, 156, 140, 34};
static const Box B_SHSRC = {70, 3, 180, 20};

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

static void fmt_date(u32 t, char *out, size_t n);

static void draw_shots_bottom(void)
{
	InfoEnt *e = info_get(S.app, false);
	if (e && e->done) {
		const AppInfo *a = &e->info;
		int count = shots_count(a), mine = my_shots_for(a->appid);
		if (mine && a->nshots) {
			// De la tienda o tuyas (Y cambia)
			char t1[40], t2[40];
			snprintf(t1, sizeof t1, _("Tienda (%d)"), a->nshots);
			snprintf(t2, sizeof t2, _("Tuyas (%d)"), mine);
			fx_glass(B_SHSRC.x, B_SHSRC.y, B_SHSRC.w, B_SHSRC.h, B_SHSRC.h / 2, 1, 0);
			float sw = (B_SHSRC.w - 4) / 2;
			fx_select(B_SHSRC.x + 2 + (shots_mine ? sw : 0), B_SHSRC.y + 2, sw, B_SHSRC.h - 4, (B_SHSRC.h - 4) / 2, 1);
			float ty = B_SHSRC.y + (B_SHSRC.h - ui_line_h(SZ_TINY)) / 2 + 1;
			ui_text(B_SHSRC.x + 2 + sw / 2, ty, t1, SZ_TINY, shots_mine ? COL_DIM : COL_WHITE, AL_CENTER, sw - 4);
			ui_text(B_SHSRC.x + 2 + sw * 1.5f, ty, t2, SZ_TINY, shots_mine ? COL_WHITE : COL_DIM, AL_CENTER, sw - 4);
		} else {
			ui_text(BOT_W / 2, 10, shots_mine ? _("Tus capturas") : a->name, SZ_SMALL, COL_DIM, AL_CENTER, 296);
		}
		// «3 / 19», con el total más apagado
		char cur[16], tot[16];
		snprintf(cur, sizeof cur, "%d", S.shot + 1);
		snprintf(tot, sizeof tot, " / %d", count);
		float w1 = ui_text_w(cur, SZ_TITLE), w2 = ui_text_w(tot, SZ_LARGE);
		float x = (BOT_W - w1 - w2) / 2;
		ui_text(x, 28, cur, SZ_TITLE, COL_WHITE, AL_LEFT, 0);
		ui_text(x + w1, 32, tot, SZ_LARGE, COL_DIM, AL_LEFT, 0);

		// Tira de miniaturas: se desliza hasta la actual, que va marcada
		for (int i = 0; i < count; i++) {
			float cx = BOT_W / 2 + (i - S.strip) * THUMB_STEP;
			if (cx < -THUMB_W || cx > BOT_W + THUMB_W) continue;
			float on = 1 - clampf(fabsf(S.strip - i), 0, 1);
			float sc = 1 + 0.14f * on;
			float w = THUMB_W * sc, hh = THUMB_H * sc;
			float x0 = cx - w / 2, y0 = STRIP_Y + (THUMB_H * 1.14f - hh) / 2;
			if (i == S.shot) fx_select(x0 - 4, y0 - 4, w + 8, hh + 8, 6, on);
			ui_rect(x0, y0, w, hh, C2D_Color32(0x0b, 0x10, 0x16, 0xff));
			const Pic *p = img_get(shot_url(a, i), 150);
			if (p) img_draw_fit_a(p, x0, y0, w, hh, 0.45f + 0.55f * on);
			else ui_spinner(cx, y0 + hh / 2, 5, COL_DIM);
		}
		draw_round_button(&B_PREV, "L", _("Anterior"), S.shot > 0, S.pressed == BT_PREV);
		draw_round_button(&B_NEXT, "R", _("Siguiente"), S.shot + 1 < count, S.pressed == BT_NEXT);
		// Las tuyas: cuándo la hiciste y su descripción
		if (shots_mine && S.shot < count) {
			const MyShot *m = &MS.s[S.shot];
			char d[40], t[160];
			d[0] = 0;
			if (m->when) fmt_date(m->when, d, sizeof d);
			if (d[0] && m->caption[0]) snprintf(t, sizeof t, "%s · %s", d, m->caption);
			else snprintf(t, sizeof t, "%s", d[0] ? d : m->caption);
			if (t[0]) ui_text(BOT_W / 2, 138, t, SZ_TINY, COL_DIM, AL_CENTER, 296);
		}
		if (mine && a->nshots) {
			static const char *h2[] = {"← →", "Cambiar", "Y", "Tienda / Tuyas", "B", "Volver"};
			draw_hints(h2, 3);
			return;
		}
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

enum { MI_LOGIN, MI_PROFILE, MI_NOTIF, MI_LIBRARY, MI_WISHLIST, MI_CART, MI_LOGOUT, MI_REQS, MI_ABOUT, MI_SETTINGS, MI_EXIT };
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
		out[n++] = MI_NOTIF;
		out[n++] = MI_LIBRARY;
		out[n++] = MI_WISHLIST;
		out[n++] = MI_CART;
		out[n++] = MI_LOGOUT;
	}
	out[n++] = MI_SETTINGS;
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


static Scene acct_back = SC_STORE;   // de dónde se abrió (el perfil vuelve al perfil)

static void open_account_list(int ti)
{
	if (S.scene != SC_STORE) acct_back = S.scene == SC_PROFILE ? SC_PROFILE : SC_STORE;
	else if (S.tab < TAB_COUNT) acct_back = SC_STORE;
	if (S.tab < TAB_COUNT) S.prev_tab = S.tab;
	S.tab = ti;
	go_scene(SC_STORE, 1);
	Tab *t = CUR;
	if (!t->started || ti == TAB_CART) tab_more(ti);   // el carrito puede haber cambiado en el móvil
	t->sel_changed = osGetTime();
	S.cart_qr = false;
}

// ---------------------------------------------------------------------------- Perfil

static const Box B_PLIB = {12, 70, 145, 26};
static const Box B_PDL = {163, 70, 145, 26};
static const Box B_PWISH = {12, 99, 145, 26};
static const Box B_PVIEW = {163, 99, 145, 26};
static const Box B_PFRIENDS = {12, 128, 145, 26};
static const Box B_PACT = {163, 128, 145, 26};
static const Box B_PPOINTS = {12, 157, 145, 26};
static const Box B_PINV = {163, 157, 145, 26};
static const Box B_PSWITCH = {12, 186, 145, 26};
static const Box B_PLOGOUT = {163, 186, 145, 26};

static bool draw_user_bg(float dim);
static void draw_user_avatar(float x, float y, float sz);

// Tus puntos de experiencia (para el siguiente nivel) y cuántas insignias tienes
static struct {
	SteamXp xp;
	bool ok;
	Req *req;
} XP;

static void xp_poll(void)
{
	if (!XP.req || XP.req->state < REQ_DONE) return;
	XP.ok = XP.req->state == REQ_DONE && steam_parse_badges(XP.req->data, &XP.xp);
	netlog("insignias: %s (nivel %d, %d XP, %d insignias)", XP.ok ? "ok" : "fallo", XP.xp.level, XP.xp.xp, XP.xp.nbadges);
	net_release(XP.req);
	XP.req = NULL;
}
static bool user_loaded_is(u64 id);

static void draw_profile_top(void)
{
	const AuthInfo *ai = auth_info();
	// El fondo y el marco que llevas puestos en Steam (solo si lo cargado es tu perfil)
	bool mine = user_loaded_is(ai->steamid);
	if (!mine || !draw_user_bg(0.62f)) ui_vgrad(0, 18, TOP_W, 96, C2D_Color32(0x2a, 0x47, 0x5e, 0xc0), C2D_Color32(0x2a, 0x47, 0x5e, 0x00));
	if (mine) draw_user_avatar(18, 36, 72);
	else {
		const Pic *p = ai->avatar[0] ? img_get(ai->avatar, 64) : NULL;
		ui_rect(16, 34, 76, 76, C2D_Color32(0xff, 0xff, 0xff, 0x30));
		if (p) img_draw_cover(p, 18, 36, 72, 72, 0.5f);
		else ui_rect(18, 36, 72, 72, COL_PANEL);
	}
	ui_text(108, 38, ai->persona[0] ? ai->persona : ai->account, SZ_TITLE, COL_WHITE, AL_LEFT, TOP_W - 120);
	if (ai->level) {
		ui_circle(122, 82, 12, C2D_Color32(0x9b, 0x9b, 0x9b, 0xff));
		ui_circle(122, 82, 10, COL_BG);
		char lv[8];
		snprintf(lv, sizeof lv, "%d", ai->level);
		ui_text(122, 76, lv, SZ_SMALL, COL_WHITE, AL_CENTER, 0);
		ui_text(140, 76, _("Nivel de Steam"), SZ_SMALL, COL_DIM, AL_LEFT, 0);
	}
	if (XP.ok && XP.xp.xp_next > 0) {
		// Lo que falta para el siguiente nivel, como en la página de insignias
		char xt[96];
		snprintf(xt, sizeof xt, _("%d XP · faltan %d para el nivel %d · %d insignias"), XP.xp.xp, XP.xp.xp_next, XP.xp.level + 1, XP.xp.nbadges);
		ui_text(108, 96, xt, SZ_TINY, COL_DIM, AL_LEFT, TOP_W - 120);
		int span = XP.xp.xp + XP.xp.xp_next - XP.xp.xp_cur;
		float f = span > 0 ? (float)(XP.xp.xp - XP.xp.xp_cur) / span : 0;
		ui_rrect(108, 110, TOP_W - 124, 3, 1.5f, col_alpha(COL_WHITE, 0.12f));
		ui_rrect(108, 110, (TOP_W - 124) * clampf(f, 0, 1), 3, 1.5f, COL_ACC);
	} else {
		ui_text(108, 96, ai->account, SZ_TINY, COL_DIM, AL_LEFT, TOP_W - 120);
	}

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
	ui_text(BOT_W / 2, 18, _("Tu cuenta de Steam"), SZ_LARGE, COL_WHITE, AL_CENTER, 300);
	ui_text(BOT_W / 2, 42, _("Consulta tu biblioteca, tus amigos y tu cuenta"), SZ_SMALL, COL_DIM, AL_CENTER, 300);
	char lib[48], wish[48];
	Tab *l = &tabs[TAB_LIBRARY], *w = &tabs[TAB_WISHLIST];
	if (l->list.count) snprintf(lib, sizeof lib, _("Biblioteca (%d)"), l->list.count);
	else snprintf(lib, sizeof lib, "%s", _("Biblioteca"));
	if (w->wish_loaded) snprintf(wish, sizeof wish, _("Lista de deseos (%d)"), w->nwish);
	else snprintf(wish, sizeof wish, "%s", _("Lista de deseos"));
	draw_button_k(&B_PLIB, NULL, S.pfocus == 0, IC_PAD, lib, COL_BTN2, COL_BTN2, COL_ACC, 1, S.pressed == BT_PLIB);
	draw_button_k(&B_PDL, NULL, S.pfocus == 1, IC_PC, _("Descargas del PC"), COL_BTN2, COL_BTN2, COL_ACC, 1, S.pressed == BT_PDL);
	draw_button_k(&B_PWISH, NULL, S.pfocus == 2, IC_HEART, wish, COL_BTN2, COL_BTN2, COL_ACC, 1, S.pressed == BT_PWISH);
	draw_button_k(&B_PVIEW, NULL, S.pfocus == 3, IC_USER, _("Ver tu perfil"), COL_BTN2, COL_BTN2, COL_ACC, 1, S.pressed == BT_PVIEW);
	bool act_busy = shop_busy_with(SH_ACTIVATE, 0) || shop_busy_with(SH_WALLET, 0);
	draw_button_k(&B_PFRIENDS, NULL, S.pfocus == 4, IC_USERS, _("Amigos"), COL_BTN2, COL_BTN2, COL_ACC, 1, S.pressed == BT_PFRIENDS);
	draw_button_k(&B_PACT, NULL, S.pfocus == 5, IC_CODE, act_busy ? _("Activando…") : _("Activar un producto"), COL_BTN2, COL_BTN2, COL_ACC, 1, S.pressed == BT_PACT);
	u32 red = C2D_Color32(0x6a, 0x26, 0x22, 0xff);
	draw_button_k(&B_PPOINTS, NULL, S.pfocus == 6, IC_COIN, _("Tienda de puntos"), COL_BTN2, COL_BTN2, COL_ACC, 1, S.pressed == BT_PPOINTS);
	draw_button_k(&B_PINV, NULL, S.pfocus == 7, IC_CARD, _("Inventario"), COL_BTN2, COL_BTN2, COL_ACC, 1, S.pressed == BT_PINV);
	draw_button_k(&B_PSWITCH, NULL, S.pfocus == 8, IC_USERS, _("Cambiar de cuenta"), COL_BTN2, COL_BTN2, COL_ACC, 1, S.pressed == BT_PSWITCH);
	draw_button_k(&B_PLOGOUT, NULL, S.pfocus == 9, IC_LOCK, _("Cerrar sesión"), red, red, C2D_Color32(0xff, 0x9a, 0x8c, 0xff), 1, S.pressed == BT_PLOGOUT);
	static const char *hh[] = {"↑↓", "Elegir", "A", "Abrir", "B", "Volver", "START", "Menú"};
	draw_hints(hh, 4);
}

static int press_update(u32 kd, u32 kh, u32 ku, touchPosition *tp, const int *ids, int n);

static void do_logout(void)
{
	if (MS.req) net_release(MS.req);
	memset(&MS, 0, sizeof MS);
	auth_logout();
	account_lists_clear();
	friends_reset();
	shop_reset();
	S.cart_seen = 0;
	S.pick_open = false;
	toast_icon(_("Sesión cerrada"), IC_LOCK);
}

// Al cambiar de cuenta se olvida todo lo de la anterior (sin cerrar su sesión en Steam)
static void do_switch_cleanup(void)
{
	if (MS.req) net_release(MS.req);
	memset(&MS, 0, sizeof MS);
	if (auth_logged() || auth_info()->state == AUTH_STARTING) auth_switch_out();
	account_lists_clear();
	friends_reset();
	shop_reset();
	S.cart_seen = 0;
	S.pick_open = false;
}

static void open_friends(void);
static void open_accounts(void);
static void open_user(u64 id, const SteamPerson *person);
static void open_pcdl(void);
static void open_points(void);
static void open_inv(void);

static void input_profile(u32 kd, u32 kh, u32 ku, touchPosition *tp)
{
	if (kd & KEY_B) {
		go_scene(SC_STORE, -1);
		return;
	}
	static const int ids[] = {BT_PLIB, BT_PDL, BT_PWISH, BT_PVIEW, BT_PFRIENDS, BT_PACT, BT_PPOINTS, BT_PINV, BT_PSWITCH, BT_PLOGOUT};
	int b = press_update(kd, kh, ku, tp, ids, 10);
	// Con la cruceta se elige un botón (cinco filas de dos) y A lo pulsa
	if (S.pfocus > 9) S.pfocus = 0;
	if (key_repeat(kd, kh, KEY_DOWN)) S.pfocus = (S.pfocus + 2) % 10;
	if (key_repeat(kd, kh, KEY_UP)) S.pfocus = (S.pfocus + 8) % 10;
	if (kd & (KEY_LEFT | KEY_RIGHT)) S.pfocus ^= 1;
	if (kd & KEY_A) b = ids[S.pfocus];
	if (b == BT_PDL) open_pcdl();
	if (b == BT_PPOINTS) open_points();
	if (b == BT_PINV) open_inv();
	if (b == BT_PVIEW) open_user(auth_info()->steamid, NULL);
	if (b == BT_PLIB) open_account_list(TAB_LIBRARY);
	if (b == BT_PWISH) open_account_list(TAB_WISHLIST);
	if (b == BT_PFRIENDS) open_friends();
	if (b == BT_PSWITCH) {
		do_switch_cleanup();
		open_accounts();
		return;
	}
	if (b == BT_PACT) {
		if (shop_busy()) toast(_("Espera a que termine la acción anterior"));
		else S.want_key = true;
	}
	if (b == BT_PLOGOUT) {
		do_logout();
		go_scene(SC_STORE, -1);
	}
}

// ---------------------------------------------------------------------------- Acerca de

#define URL_GITHUB  "https://github.com/venomshine"
#define URL_DISCORD "https://discord.com/invite/yVKp8JwV4e"
#define URL_KOFI    "https://ko-fi.com/venomshine"

enum { AB_APP, AB_GITHUB, AB_DISCORD, AB_KOFI, AB_POLICIES, AB_CREDITS, AB_LEGAL, AB_COUNT };
static Scene about_back;
static int about_sel;
static bool about_pol;     // dentro de «Políticas y normas de Steam»
static int pol_sel;

// Los documentos oficiales de Steam: un resumen y el QR para leerlos enteros en el móvil
static const struct { const char *name, *sub, *desc, *url; } policies[] = {
	{"Acuerdo de Suscriptor de Steam", "Tu cuenta y las licencias de lo que compras",
	 "El contrato entre tú y Valve para usar Steam: tu cuenta, las licencias del contenido que compras, las suscripciones y las normas de uso del servicio.",
	 "https://store.steampowered.com/subscriber_agreement/"},
	{"Política de privacidad", "Qué datos se recogen y para qué",
	 "Qué datos recoge Valve cuando usas Steam, para qué los usa, con quién los comparte y cómo ejercer tus derechos sobre ellos.",
	 "https://store.steampowered.com/privacy_agreement/"},
	{"Reembolsos de Steam", "Cuándo y cómo pedir un reembolso",
	 "Puedes pedir el reembolso de casi cualquier compra en los 14 días siguientes si has jugado menos de 2 horas. Se solicita desde la ayuda de Steam.",
	 "https://store.steampowered.com/steam_refunds/"},
	{"Normas de conducta en línea", "Reseñas, foros, perfiles y chat",
	 "Lo que no está permitido en las partes de Steam con contenido de los usuarios: reseñas, foros, perfiles, chat y Workshop.",
	 "https://store.steampowered.com/online_conduct/"},
	{"Información legal", "Marcas y derechos de autor",
	 "Avisos de marcas, derechos de autor y el resto de la información legal de Valve y de Steam.",
	 "https://store.steampowered.com/legal/"},
};
#define NPOLICIES (int)(sizeof policies / sizeof *policies)

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
	about_pol = false;
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
	if (about_pol) {
		draw_qr_panel(policies[pol_sel].url, _(policies[pol_sel].name), _(policies[pol_sel].desc), NULL,
			policies[pol_sel].url + 8, COL_ACC);
	} else if (about_sel == AB_POLICIES) {
		ui_text(X, 28, _("Políticas y normas de Steam"), SZ_LARGE, COL_WHITE, AL_LEFT, W);
		ui_rrect(X, 47, ui_text_w(_("Políticas y normas de Steam"), SZ_LARGE), 2, 1, col_alpha(COL_ACC, 0.8f));
		ui_text_wrap(X, 62, W, _("Los documentos oficiales de Steam: el Acuerdo de Suscriptor, la privacidad, los reembolsos y las normas de conducta. Al usar Steam desde la consola se aplican igual que en el PC o en el móvil."), SZ_SMALL, COL_TXT, 5, 15);
		ui_text_wrap(X, 150, W, _("Pulsa A para verlos y escanea el código de cada uno para leerlo entero en el móvil."), SZ_SMALL, COL_ACC, 2, 15);
		ui_text_wrap(X, 196, W, _("Los publica y actualiza Valve; Steam3DS solo enlaza a ellos."), SZ_SMALL, COL_DIM, 2, 15);
	} else if (about_sel >= AB_GITHUB && about_sel <= AB_KOFI) {
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

// Abajo, dentro de «Políticas y normas de Steam»: los documentos
static void draw_policies_bottom(void)
{
	fx_bar(0, 0, BOT_W, LIST_Y, true);
	ui_text(10, 4, "‹", SZ_LARGE, COL_ACC, AL_LEFT, 0);
	ui_icon(IC_CHECK, 28, 6, 12, COL_ACC);
	ui_text(46, 5, _("Políticas y normas de Steam"), SZ_SMALL, COL_WHITE, AL_LEFT, BOT_W - 56);
	const float RH = 32;
	for (int i = 0; i < NPOLICIES; i++) {
		float y = LIST_Y + 2 + i * RH;
		if (i == pol_sel) {
			float br = 0.075f + 0.035f * sinf(g_time * 2.4f);
			ui_rrect(3, y + 1, BOT_W - 8, RH - 2, 6, col_alpha(COL_WHITE, br));
			ui_rrect(3, y + 5, 2, RH - 10, 1, col_alpha(COL_ACC, 0.8f));
			ui_icon(IC_PHONE, BOT_W - 24, y + 10, 12, COL_ACC);
		}
		ui_icon(IC_DOC, 14, y + 10, 12, COL_DIM);
		ui_text(36, y + 3, _(policies[i].name), SZ_BODY - 0.02f, i == pol_sel ? COL_WHITE : COL_TXT, AL_LEFT, BOT_W - 64);
		ui_text(36, y + 17, _(policies[i].sub), SZ_TINY, COL_DIM, AL_LEFT, BOT_W - 64);
		if (i < NPOLICIES - 1) ui_hline(36, y + RH - 1, BOT_W - 44, C2D_Color32(0xff, 0xff, 0xff, 0x0c));
	}
	static const char *h[] = {"↑↓", "Elegir", "B", "Volver"};
	draw_hints(h, 2);
}

// Abajo: la lista (la app, GitHub, Discord, Ko-fi, las políticas, créditos y aviso legal)
static void draw_about_bottom(void)
{
	if (about_pol) {
		draw_policies_bottom();
		return;
	}
	fx_bar(0, 0, BOT_W, LIST_Y, true);
	ui_text(10, 4, "‹", SZ_LARGE, COL_ACC, AL_LEFT, 0);
	ui_icon(IC_INFO, 28, 6, 12, COL_ACC);
	ui_text(46, 5, _("Acerca de Steam3DS"), SZ_SMALL, COL_WHITE, AL_LEFT, BOT_W - 56);
	static const Icon ic[AB_COUNT] = {IC_PAD, IC_CODE, IC_CHAT, IC_CUP, IC_CHECK, IC_DOC, IC_DOC};
	const u32 col[AB_COUNT] = {COL_ACC, C2D_Color32(0xc9, 0xd1, 0xd9, 0xff), C2D_Color32(0x7f, 0x8c, 0xff, 0xff),
		C2D_Color32(0xff, 0x6e, 0x6b, 0xff), COL_ACC, COL_DIM, COL_DIM};
	char ver[48];
	snprintf(ver, sizeof ver, "Steam3DS %s", APP_VERSION);
	const char *title[AB_COUNT] = {ver, "GitHub", "Discord", "Ko-fi", _("Políticas y normas de Steam"), _("Créditos y licencias"), _("Aviso legal")};
	const char *sub[AB_COUNT] = {_("Gracias por usar Steam3DS"), _("Código y avisos de errores"), _("Comunidad y ayuda"),
		_("Invítame a un café"), _("Acuerdo de suscriptor, privacidad y reembolsos"), _("Librerías que usa la app"),
		_("Steam3DS no es una app oficial de Steam")};
	// Siete filas: justas entre la barra de arriba y la de botones
	const float RH = 28;
	for (int i = 0; i < AB_COUNT; i++) {
		float y = LIST_Y + 1 + i * RH;
		if (i == about_sel) {
			float br = 0.075f + 0.035f * sinf(g_time * 2.4f);
			ui_rrect(3, y + 1, BOT_W - 8, RH - 2, 6, col_alpha(COL_WHITE, br));
			ui_rrect(3, y + 5, 2, RH - 10, 1, col_alpha(COL_ACC, 0.8f));
		}
		ui_icon(ic[i], 14, y + 8, 12, col[i]);
		ui_text(36, y + 1, title[i], SZ_BODY - 0.02f, i == about_sel ? COL_WHITE : COL_TXT, AL_LEFT, BOT_W - 60);
		ui_text(36, y + 15, sub[i], SZ_TINY, COL_DIM, AL_LEFT, BOT_W - 60);
		if (i >= AB_GITHUB && i <= AB_KOFI && i == about_sel) ui_icon(IC_PHONE, BOT_W - 24, y + 8, 12, COL_ACC);
		if (i == AB_POLICIES && i == about_sel) ui_text(BOT_W - 20, y + 6, "›", SZ_LARGE, COL_ACC, AL_LEFT, 0);
		if (i < AB_COUNT - 1) ui_hline(36, y + RH - 1, BOT_W - 44, C2D_Color32(0xff, 0xff, 0xff, 0x0c));
	}
	static const char *h[] = {"↑↓", "Elegir", "B", "Volver"};
	static const char *ha[] = {"A", "Abrir", "↑↓", "Elegir", "B", "Volver"};
	if (about_sel == AB_POLICIES) draw_hints(ha, 3);
	else draw_hints(h, 2);
}

static void input_about(u32 kd, u32 kh)
{
	if (about_pol) {
		if (kd & KEY_B) about_pol = false;
		if (key_repeat(kd, kh, KEY_DOWN)) pol_sel = (pol_sel + 1) % NPOLICIES;
		if (key_repeat(kd, kh, KEY_UP)) pol_sel = (pol_sel + NPOLICIES - 1) % NPOLICIES;
		return;
	}
	if (kd & KEY_B) {
		go_scene(about_back, -1);
		return;
	}
	if ((kd & KEY_A) && about_sel == AB_POLICIES) {
		about_pol = true;
		pol_sel = 0;
		return;
	}
	if (key_repeat(kd, kh, KEY_DOWN)) about_sel = (about_sel + 1) % AB_COUNT;
	if (key_repeat(kd, kh, KEY_UP)) about_sel = (about_sel + AB_COUNT - 1) % AB_COUNT;
}

// ---------------------------------------------------------------------------- Notificaciones

#define MAX_NOTIF 40
static struct {
	Req *req, *names_req, *mark;
	SteamNotif n[MAX_NOTIF];
	int count, unread;
	bool loaded, failed;
	u64 at;                      // cuándo se pidieron (se vuelven a pedir cada 30 segundos)
	u64 seen[MAX_NOTIF];         // los que ya había la última vez (para avisar solo de los nuevos)
	RowList names;               // nombres de los juegos de los que hablan
	int sel;
	bool unread_at_open[MAX_NOTIF];
	Scene back;
} NT;

static int notif_unread(void)
{
	return auth_logged() ? NT.unread : 0;
}

static void notif_reset(void)
{
	Req *all[] = {NT.req, NT.names_req, NT.mark};
	for (int i = 0; i < 3; i++)
		if (all[i]) net_release(all[i]);
	rowlist_free(&NT.names);
	memset(&NT, 0, sizeof NT);
}

static const char *notif_game(u32 appid)
{
	for (int i = 0; appid && i < NT.names.count; i++)
		if (NT.names.rows[i].appid == appid) return NT.names.rows[i].name;
	return NULL;
}

// El texto de cada aviso, como en la campana de Steam (los números son los de
// ESteamNotificationType: 1 es la prueba, 2 regalo, 3 comentario, 4 objetos, 5 solicitud…)
static void notif_text(const SteamNotif *n, char *out, size_t sz)
{
	const char *g = notif_game(n->appid);
	switch (n->type) {
	case 2: snprintf(out, sz, "%s", _("Has recibido un regalo")); break;
	case 3: snprintf(out, sz, "%s", _("Nuevo comentario en tu perfil")); break;
	case 4: snprintf(out, sz, "%s", _("Tienes objetos nuevos en tu inventario")); break;
	case 5: snprintf(out, sz, "%s", _("Nueva solicitud de amistad")); break;
	case 6: snprintf(out, sz, "%s", _("Han empezado unas rebajas en Steam")); break;
	case 7:
		if (g) snprintf(out, sz, _("Ya puedes precargar %s"), g);
		else snprintf(out, sz, "%s", _("Ya puedes precargar un juego"));
		break;
	case 8:
		if (g) snprintf(out, sz, _("%s, de tu lista de deseos, está en oferta"), g);
		else snprintf(out, sz, "%s", _("Un juego de tu lista de deseos está en oferta"));
		break;
	case 9: snprintf(out, sz, "%s", _("Nueva oferta de intercambio")); break;
	case 12: snprintf(out, sz, "%s", _("Es tu turno en una partida")); break;
	case 13: snprintf(out, sz, "%s", _("Nuevo mensaje en el chat")); break;
	case 16: snprintf(out, sz, "%s", _("Te han invitado a una Familia de Steam")); break;
	case 17: snprintf(out, sz, "%s", _("Petición de compra en tu familia")); break;
	case 22:
		if (g) snprintf(out, sz, _("Ya tienes %s en tu biblioteca"), g);
		else snprintf(out, sz, "%s", _("Tienes un juego nuevo en tu biblioteca"));
		break;
	case 27:
		if (n->title[0]) snprintf(out, sz, "%s", n->title);
		else if (g) snprintf(out, sz, _("Novedades de %s"), g);
		else snprintf(out, sz, "%s", _("Novedades de un juego que sigues"));
		break;
	case 28:
		if (g) snprintf(out, sz, _("Invitación a la prueba de %s"), g);
		else snprintf(out, sz, "%s", _("Invitación a una prueba de juego"));
		break;
	default:
		snprintf(out, sz, "%s", n->title[0] ? n->title : _("Aviso de Steam"));
		break;
	}
}

static Icon notif_icon(int type)
{
	switch (type) {
	case 2: return IC_CARD;
	case 3: case 13: return IC_CHAT;
	case 5: case 16: case 17: return IC_USERS;
	case 6: case 9: return IC_COIN;
	case 7: return IC_PC;
	case 8: return IC_HEART;
	case 22: case 28: return IC_PAD;
	default: return IC_BELL;
	}
}

// «hace 5 min», «hace 3 h», «hace 2 días» o la fecha
static void notif_when(u32 t, char *out, size_t sz)
{
	long long d = (long long)time(NULL) + g_server_delta - t;
	if (d < 0) d = 0;
	if (d < 3600) snprintf(out, sz, _("hace %d min"), (int)(d / 60 < 1 ? 1 : d / 60));
	else if (d < 86400) snprintf(out, sz, _("hace %d h"), (int)(d / 3600));
	else if (d < 30 * 86400) snprintf(out, sz, d < 2 * 86400 ? _("hace %d día") : _("hace %d días"), (int)(d / 86400));
	else {
		time_t tt = t;
		struct tm *tm = gmtime(&tt);
		static const char *mes[] = {"ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic"};
		snprintf(out, sz, "%d %s %d", tm->tm_mday, _(mes[tm->tm_mon]), tm->tm_year + 1900);
	}
}

// Se piden al iniciar sesión y luego cada 30 segundos (para que la campana avise al momento);
// los nombres de los juegos, aparte
static void notif_poll(void)
{
	if (!auth_logged()) return;
	u64 now = osGetTime();
	if (!NT.req && S.ctx_ready && (!NT.at || now - NT.at > 30 * 1000)) {
		NT.at = now;
		NT.req = steam_req_notifications(auth_token());
	}
	if (NT.req && NT.req->state >= REQ_DONE) {
		int unread = 0;
		int n = NT.req->state == REQ_DONE ? steam_parse_notifications(NT.req->data, NT.n, MAX_NOTIF, &unread) : -1;
		if (n >= 0) {
			// Un aviso nuevo mientras usas la app: sale al momento, como en Steam
			if (NT.loaded && S.scene != SC_NOTIF) {
				for (int i = 0; i < n; i++) {
					if (NT.n[i].read) continue;
					bool known = false;
					for (int j = 0; j < MAX_NOTIF && NT.seen[j] && !known; j++) known = NT.seen[j] == NT.n[i].id;
					if (!known) {
						char t[160];
						notif_text(&NT.n[i], t, sizeof t);
						toast_icon(t, notif_icon(NT.n[i].type));
						break;
					}
				}
			}
			memset(NT.seen, 0, sizeof NT.seen);
			for (int i = 0; i < n && i < MAX_NOTIF; i++) NT.seen[i] = NT.n[i].id;
			NT.count = n;
			NT.unread = S.scene == SC_NOTIF ? 0 : unread;
			NT.loaded = true;
			NT.failed = false;
			if (NT.sel >= n) NT.sel = n ? n - 1 : 0;
		} else if (!NT.loaded) {
			NT.failed = true;
		}
		netlog("notificaciones: %d (%d sin leer, HTTP %ld)", n, unread, NT.req->http);
		net_release(NT.req);
		NT.req = NULL;
		// Los juegos que se nombran y aún no tienen nombre
		u32 ids[MAX_NOTIF];
		int k = 0;
		for (int i = 0; i < NT.count; i++) {
			u32 a = NT.n[i].appid;
			bool have = !a || notif_game(a);
			for (int j = 0; j < k && !have; j++) have = ids[j] == a;
			if (!have) ids[k++] = a;
		}
		if (k && !NT.names_req) NT.names_req = steam_req_items(ids, k, false);
	}
	if (NT.names_req && NT.names_req->state >= REQ_DONE) {
		if (NT.names_req->state == REQ_DONE) {
			RowList got = {0};
			if (!steam_take_rows(NT.names_req, &got, false)) steam_parse_rows(NT.names_req->data, &got, false);
			for (int i = 0; i < got.count; i++) add_row(&NT.names, &got.rows[i]);
			rowlist_free(&got);
		}
		net_release(NT.names_req);
		NT.names_req = NULL;
	}
	if (NT.mark && NT.mark->state >= REQ_DONE) {
		net_release(NT.mark);
		NT.mark = NULL;
	}
}

static void open_notifs(void)
{
	NT.back = S.scene == SC_NOTIF ? SC_STORE : S.scene;
	NT.sel = 0;
	// Al abrirlas cuentan como vistas, como en Steam (el punto se queda hasta que sales)
	for (int i = 0; i < NT.count; i++) {
		NT.unread_at_open[i] = !NT.n[i].read;
		NT.n[i].read = true;
	}
	if (NT.unread && !NT.mark) NT.mark = steam_req_notifications_read(auth_token());
	NT.unread = 0;
	if (!NT.req) NT.at = 0;   // y se piden de nuevo
	go_scene(SC_NOTIF, 1);
}

static void draw_notifs_top(void)
{
	if (!NT.loaded) {
		if (NT.failed) {
			ui_icon(IC_BELL, TOP_W / 2 - 14, 80, 28, COL_DIM);
			ui_text(TOP_W / 2, 120, _("No se pudieron cargar tus notificaciones"), SZ_LARGE, COL_TXT, AL_CENTER, 380);
			ui_text(TOP_W / 2, 144, _("Se volverá a intentar en unos minutos"), SZ_SMALL, COL_DIM, AL_CENTER, 380);
		} else {
			draw_net_wait(TOP_W / 2, 120, _("Cargando…"));
		}
		return;
	}
	if (!NT.count) {
		ui_icon(IC_BELL, TOP_W / 2 - 14, 80, 28, COL_ACC);
		ui_text(TOP_W / 2, 120, _("No tienes notificaciones"), SZ_LARGE, COL_TXT, AL_CENTER, 380);
		ui_text(TOP_W / 2, 144, _("Aquí verás los regalos, las ofertas de tu lista de deseos y más"), SZ_SMALL, COL_DIM, AL_CENTER, 380);
		return;
	}
	const SteamNotif *n = &NT.n[NT.sel];
	char t[160], when[40];
	notif_text(n, t, sizeof t);
	notif_when(n->time, when, sizeof when);
	const float X = 16, W = TOP_W - 32;
	ui_icon(notif_icon(n->type), X, 30, 18, COL_ACC);
	int nl = ui_text_lines(t, SZ_LARGE, W - 28);
	ui_text_wrap(X + 28, 28, W - 28, t, SZ_LARGE, COL_WHITE, 2, 20);
	float y = 28 + (nl > 2 ? 2 : nl) * 20 + 4;
	ui_text(X + 28, y, when, SZ_SMALL, COL_DIM, AL_LEFT, W);
	if (n->appid) {
		// El juego del que habla, con su cabecera
		char url[160];
		snprintf(url, sizeof url, "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/%lu/header.jpg", (unsigned long)n->appid);
		const float iw = 230, ih = floorf(iw * 215 / 460);
		float iy = y + 24;
		const Pic *p = img_get(url, 460);
		ui_rect(X - 1, iy - 1, iw + 2, ih + 2, C2D_Color32(0xff, 0xff, 0xff, 0x30));
		if (p) img_draw_cover(p, X, iy, iw, ih, 0.5f);
		else {
			ui_rect(X, iy, iw, ih, COL_PANEL);
			if (!img_failed(url)) ui_spinner(X + iw / 2, iy + ih / 2, 6, COL_DIM);
		}
		const char *g = notif_game(n->appid);
		if (g) ui_text_wrap(X + iw + 12, iy, W - iw - 12, g, SZ_BODY, COL_WHITE, 3, 16);
		ui_text_wrap(X + iw + 12, iy + ih - 30, W - iw - 12, _("Pulsa A para ver su página"), SZ_SMALL, COL_ACC, 2, 14);
	} else if (n->type == 2 || n->type == 5 || n->type == 9 || n->type == 16) {
		ui_text_wrap(X + 28, y + 24, W - 28, _("Ábrelo desde la app de Steam en el móvil o en el PC para responder."), SZ_SMALL, COL_TXT, 3, 15);
	}
}

static void draw_notifs_bottom(void)
{
	fx_bar(0, 0, BOT_W, LIST_Y, true);
	ui_text(10, 4, "‹", SZ_LARGE, COL_ACC, AL_LEFT, 0);
	ui_icon(IC_BELL, 28, 6, 12, COL_ACC);
	ui_text(46, 5, _("Notificaciones"), SZ_SMALL, COL_WHITE, AL_LEFT, BOT_W - 56);
	const float RH = 32;
	const int vis = (int)((SCR_H - 18 - LIST_Y) / RH);
	int first = NT.sel - (vis - 1);
	if (first < 0) first = 0;
	for (int i = first; i < NT.count && i < first + vis; i++) {
		const SteamNotif *n = &NT.n[i];
		float y = LIST_Y + 2 + (i - first) * RH;
		bool on = i == NT.sel;
		if (on) {
			float br = 0.075f + 0.035f * sinf(g_time * 2.4f);
			ui_rrect(3, y + 1, BOT_W - 8, RH - 2, 6, col_alpha(COL_WHITE, br));
			ui_rrect(3, y + 5, 2, RH - 10, 1, col_alpha(COL_ACC, 0.8f));
		}
		char t[160], when[40];
		notif_text(n, t, sizeof t);
		notif_when(n->time, when, sizeof when);
		bool fresh = NT.unread_at_open[i];
		ui_icon(notif_icon(n->type), 14, y + 10, 12, fresh ? COL_ACC : COL_DIM);
		ui_text(36, y + 3, t, SZ_BODY - 0.02f, on || fresh ? COL_WHITE : COL_TXT, AL_LEFT, BOT_W - 64);
		ui_text(36, y + 17, when, SZ_TINY, COL_DIM, AL_LEFT, BOT_W - 64);
		if (fresh) {
			float cx = BOT_W - 14, cy = y + RH / 2;
			ui_circle(cx, cy, 3.2f, COL_ACC);
		}
		if (i < NT.count - 1) ui_hline(36, y + RH - 1, BOT_W - 44, C2D_Color32(0xff, 0xff, 0xff, 0x0c));
	}
	if (!NT.count && NT.loaded)
		ui_text(BOT_W / 2, LIST_Y + 30, _("No tienes notificaciones"), SZ_SMALL, COL_DIM, AL_CENTER, 0);
	static const char *h[] = {"↑↓", "Elegir", "B", "Volver"};
	static const char *ha[] = {"A", "Abrir", "↑↓", "Elegir", "B", "Volver"};
	if (NT.count && NT.n[NT.sel].appid) draw_hints(ha, 3);
	else draw_hints(h, 2);
}

static void input_notifs(u32 kd, u32 kh, touchPosition *tp)
{
	if (kd & KEY_B) {
		go_scene(NT.back == SC_NOTIF ? SC_STORE : NT.back, -1);
		return;
	}
	if (key_repeat(kd, kh, KEY_DOWN) && NT.sel + 1 < NT.count) NT.sel++;
	if (key_repeat(kd, kh, KEY_UP) && NT.sel > 0) NT.sel--;
	if ((kd & KEY_TOUCH) && tp->py < LIST_Y && tp->px < 120) {
		go_scene(NT.back == SC_NOTIF ? SC_STORE : NT.back, -1);
		return;
	}
	if ((kd & KEY_TOUCH) && tp->py >= LIST_Y) {
		const float RH = 32;
		const int vis = (int)((SCR_H - 18 - LIST_Y) / RH);
		int first = NT.sel - (vis - 1);
		if (first < 0) first = 0;
		int i = first + (int)((tp->py - LIST_Y - 2) / RH);
		if (i >= 0 && i < NT.count) {
			if (i == NT.sel && NT.n[i].appid) open_game(NT.n[i].appid, true);
			else NT.sel = i;
			return;
		}
	}
	if ((kd & KEY_A) && NT.count && NT.n[NT.sel].appid) open_game(NT.n[NT.sel].appid, true);
}

// ---------------------------------------------------------------------------- Amigos

#define MAX_FRIENDS 250
static struct {
	Req *req, *people;
	bool fallback;               // la lista se pidió con ISteamUser (IFriendsListService no respondió)
	u64 ids[MAX_FRIENDS];
	int nids, pending, next;     // next: hasta dónde se han pedido sus datos (de 100 en 100)
	SteamPerson p[MAX_FRIENDS];
	int np;
	bool loaded, failed;
	u64 at;
	int view[MAX_FRIENDS + 4];   // lo que se ve: >= 0 un amigo; -1, -2, -3 los títulos de cada grupo
	int nview, sel;
	bool qr;                     // arriba, el QR para que te añadan
	u64 rq[20];                  // quienes te han mandado solicitud
	int nrq;
	Req *rq_people;
	bool want_code;              // abrir el teclado del código de amigo
	Req *live;                   // el estado de todos, pedido otra vez mientras miras la lista
	int live_next;
	u64 live_at;
} FL;

static void flist_reset(void)
{
	if (FL.req) net_release(FL.req);
	if (FL.people) net_release(FL.people);
	if (FL.rq_people) net_release(FL.rq_people);
	if (FL.live) net_release(FL.live);
	memset(&FL, 0, sizeof FL);
}

static int person_group(const SteamPerson *p)
{
	if (p->request) return 0;   // las solicitudes, arriba del todo
	return p->state > 0 && p->game ? 1 : p->state > 0 ? 2 : 3;   // jugando, en línea, desconectado
}

// Los amigos por grupos (jugando, en línea, desconectados) y por nombre dentro de cada uno
static int cmp_person(const void *a, const void *b)
{
	const SteamPerson *x = a, *y = b;
	int gx = person_group(x), gy = person_group(y);
	if (gx != gy) return gx - gy;
	return strcasecmp(x->name, y->name);
}

static void flist_build(void)
{
	qsort(FL.p, FL.np, sizeof FL.p[0], cmp_person);
	FL.nview = 0;
	int last = -1;
	for (int i = 0; i < FL.np; i++) {
		int g = person_group(&FL.p[i]);
		if (g != last) FL.view[FL.nview++] = -1 - g;
		last = g;
		FL.view[FL.nview++] = i;
	}
	if (FL.sel >= FL.nview) FL.sel = FL.nview - 1;
	while (FL.sel >= 0 && FL.sel < FL.nview && FL.view[FL.sel] < 0) FL.sel++;
	if (FL.sel >= FL.nview) FL.sel = 0;
}

static void flist_poll(void)
{
	if (!auth_logged()) return;
	if (FL.req && FL.req->state >= REQ_DONE) {
		int n = FL.req->state == REQ_DONE ? steam_parse_friends(FL.req->data, FL.ids, MAX_FRIENDS, &FL.pending) : -1;
		FL.nrq = n >= 0 ? steam_parse_friend_requests(FL.req->data, FL.rq, 20) : 0;
		netlog("amigos: lista %s: %d (%d solicitudes, HTTP %ld)", FL.fallback ? "ISteamUser" : "IFriendsListService", n, FL.pending, FL.req->http);
		net_release(FL.req);
		FL.req = NULL;
		if (n < 0 && !FL.fallback) {
			FL.fallback = true;
			FL.req = steam_req_friends(auth_info()->steamid, auth_token(), true);
			return;
		}
		if (n < 0) {
			FL.failed = !FL.loaded;
			return;
		}
		FL.nids = n;
		FL.next = 0;
		FL.np = 0;
		FL.loaded = n == 0;
		FL.failed = false;
	}
	if (!FL.req && !FL.people && FL.next < FL.nids) {
		int k = FL.nids - FL.next;
		if (k > 100) k = 100;
		FL.people = steam_req_people(FL.ids + FL.next, k, auth_token());
		FL.next += k;
	}
	if (FL.people && FL.people->state >= REQ_DONE) {
		if (FL.people->state == REQ_DONE) {
			int n = steam_parse_people(FL.people->data, FL.p + FL.np, MAX_FRIENDS - FL.np);
			if (n > 0) FL.np += n;
		}
		net_release(FL.people);
		FL.people = NULL;
		if (FL.next >= FL.nids) {
			FL.loaded = true;
			flist_build();
			if (FL.nrq) FL.rq_people = steam_req_people(FL.rq, FL.nrq, auth_token());
		}
	}
	// En tiempo real: con la lista de amigos (o un perfil) abierta, el estado de todos se vuelve
	// a pedir cada 15 segundos y se actualiza en su sitio (sin vaciar la lista)
	bool watching = S.scene == SC_FRIENDS || S.scene == SC_USER || gift_is_open();
	if (watching && FL.loaded && !FL.req && !FL.people && !FL.live && FL.nids &&
		osGetTime() - (FL.live_at > FL.at ? FL.live_at : FL.at) > 15000) {
		if (FL.live_next >= FL.nids) FL.live_next = 0;
		int k = FL.nids - FL.live_next;
		if (k > 100) k = 100;
		FL.live = steam_req_people(FL.ids + FL.live_next, k, auth_token());
		FL.live_next += k;
		if (FL.live_next >= FL.nids) FL.live_at = osGetTime();
	}
	if (FL.live && FL.live->state >= REQ_DONE) {
		if (FL.live->state == REQ_DONE) {
			static SteamPerson got[100];
			int n = steam_parse_people(FL.live->data, got, 100);
			u64 cur = FL.sel >= 0 && FL.sel < FL.nview && FL.view[FL.sel] >= 0 ? FL.p[FL.view[FL.sel]].id : 0;
			bool changed = false;
			for (int i = 0; i < n; i++) user_person_live(&got[i]);
			for (int i = 0; i < n; i++)
				for (int j = 0; j < FL.np; j++)
					if (FL.p[j].id == got[i].id && !FL.p[j].request) {
						if (memcmp(&FL.p[j], &got[i], sizeof got[i])) changed = true;
						FL.p[j] = got[i];
						break;
					}
			if (changed) {
				flist_build();
				// La selección sigue en la misma persona aunque cambie de grupo
				for (int v = 0; cur && v < FL.nview; v++)
					if (FL.view[v] >= 0 && FL.p[FL.view[v]].id == cur) FL.sel = v;
			}
		}
		net_release(FL.live);
		FL.live = NULL;
	}
	// Quienes te han mandado solicitud: con su nombre y su avatar, en su propio grupo
	if (FL.rq_people && FL.rq_people->state >= REQ_DONE) {
		if (FL.rq_people->state == REQ_DONE) {
			int n = steam_parse_people(FL.rq_people->data, FL.p + FL.np, MAX_FRIENDS - FL.np);
			for (int i = 0; i < n; i++) FL.p[FL.np + i].request = true;
			if (n > 0) FL.np += n;
			flist_build();
		}
		net_release(FL.rq_people);
		FL.rq_people = NULL;
	}
}

static Scene flist_back;

static void open_friends(void)
{
	flist_back = S.scene == SC_FRIENDS ? SC_PROFILE : S.scene;
	FL.qr = false;
	// La lista se vuelve a pedir si ya tiene un minuto (el estado de cada uno se refresca aparte)
	if (!FL.req && !FL.people && (!FL.at || osGetTime() - FL.at > 60000)) {
		FL.at = osGetTime();
		FL.fallback = false;
		FL.req = steam_req_friends(auth_info()->steamid, auth_token(), false);
	}
	go_scene(SC_FRIENDS, 1);
}

static void person_status(const SteamPerson *p, char *out, size_t n, u32 *col)
{
	static const char *st[] = {"Desconectado", "En línea", "Ocupado", "Ausente", "Durmiendo", "Quiere intercambiar", "Quiere jugar"};
	if (p->request) {
		snprintf(out, n, "%s", _("Quiere ser tu amigo · Pulsa A para responder"));
		*col = COL_ACC;
		return;
	}
	if (p->state > 0 && p->game) {
		snprintf(out, n, _("Jugando a %s"), p->game_name[0] ? p->game_name : _("un juego"));
		*col = COL_LIME;
	} else if (p->state > 0) {
		snprintf(out, n, "%s", _(st[p->state < 7 ? p->state : 1]));
		*col = COL_ACC;
	} else {
		if (p->last_off) {
			char w[40];
			notif_when(p->last_off, w, sizeof w);
			snprintf(out, n, _("Desconectado · %s"), w);
		} else {
			snprintf(out, n, "%s", _("Desconectado"));
		}
		*col = COL_DIM;
	}
}

// ---------------------------------------------------------------------------- Regalar (carrito)

// SELECT sobre un juego del carrito: eliges a qué amigo se lo regalas y escribes la dedicatoria
static struct {
	bool open, want_kb;
	int line, sel;
	int who[MAX_FRIENDS];        // los amigos que se pueden elegir (sin las solicitudes)
	int n;
	u64 id;
	char name[40];
} GF;

static bool gift_is_open(void) { return GF.open; }

static void gift_build(void)
{
	GF.n = 0;
	for (int i = 0; i < FL.np; i++)
		if (!FL.p[i].request) GF.who[GF.n++] = i;
	if (GF.sel >= GF.n) GF.sel = GF.n ? GF.n - 1 : 0;
}

static void gift_open(int line)
{
	const CartInfo *c = shop_cart();
	if (line < 0 || line >= c->n) return;
	if (shop_busy()) {
		toast(_("Espera a que termine la acción anterior"));
		return;
	}
	// Ya es un regalo: SELECT otra vez lo vuelve a dejar para ti
	if (c->line[line].giftee) {
		shop_cart_gift(line, 0, NULL, NULL, NULL);
		return;
	}
	GF.open = true;
	GF.line = line;
	GF.sel = 0;
	if (!FL.req && !FL.people && (!FL.at || osGetTime() - FL.at > 60000)) {
		FL.at = osGetTime();
		FL.fallback = false;
		FL.req = steam_req_friends(auth_info()->steamid, auth_token(), false);
	}
}

static void open_gift_keyboard(void)
{
	SwkbdState kb;
	static char buf[800];
	buf[0] = 0;
	swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, 250);
	swkbdSetHintText(&kb, _("Escribe una dedicatoria (opcional)"));
	swkbdSetButton(&kb, SWKBD_BUTTON_LEFT, _("Cancelar"), false);
	swkbdSetButton(&kb, SWKBD_BUTTON_RIGHT, _("Regalar"), true);
	swkbdSetFeatures(&kb, SWKBD_DARKEN_TOP_SCREEN | SWKBD_ALLOW_HOME | SWKBD_ALLOW_RESET | SWKBD_ALLOW_POWER | SWKBD_MULTILINE);
	swkbdSetValidation(&kb, SWKBD_ANYTHING, 0, 0);
	if (swkbdInputText(&kb, buf, sizeof buf) != SWKBD_BUTTON_RIGHT) return;
	GF.open = false;
	shop_cart_gift(GF.line, (u32)(GF.id - 76561197960265728ULL), GF.name, buf, auth_info()->persona);
}

static void draw_gift_bottom(void)
{
	gift_build();
	fx_bar(0, 0, BOT_W, LIST_Y, true);
	ui_text(10, 4, "‹", SZ_LARGE, COL_ACC, AL_LEFT, 0);
	ui_icon(IC_HEART, 28, 6, 12, COL_ACC);
	const CartInfo *c = shop_cart();
	char t[120];
	snprintf(t, sizeof t, _("Regalar %s"), GF.line < c->n ? c->line[GF.line].name : "");
	ui_text(46, 5, t, SZ_SMALL, COL_WHITE, AL_LEFT, BOT_W - 56);
	const float RH = 26, top = LIST_Y + 22;
	ui_text(10, LIST_Y + 5, _("¿Para quién es?"), SZ_SMALL, COL_ACC, AL_LEFT, 0);
	const int vis = (int)((SCR_H - 18 - top) / RH);
	int first = GF.sel - (vis - 1);
	if (first < 0) first = 0;
	for (int v = first; v < GF.n && v < first + vis; v++) {
		float y = top + (v - first) * RH;
		const SteamPerson *p = &FL.p[GF.who[v]];
		bool on = v == GF.sel;
		if (on) {
			float br = 0.075f + 0.035f * sinf(g_time * 2.4f);
			ui_rrect(3, y + 1, BOT_W - 8, RH - 2, 6, col_alpha(COL_WHITE, br));
			ui_rrect(3, y + 4, 2, RH - 8, 1, col_alpha(COL_ACC, 0.8f));
		}
		char av[128];
		const Pic *pic = auth_avatar_url(p->avatar_sha, av, sizeof av) ? img_get(av, 32) : NULL;
		if (pic) img_draw_cover(pic, 10, y + 3, 19, 19, 0.5f);
		else ui_rect(10, y + 3, 19, 19, col_alpha(COL_WHITE, 0.07f));
		char st[96];
		u32 col;
		person_status(p, st, sizeof st, &col);
		ui_text(36, y + 2, p->name, SZ_SMALL, on ? COL_WHITE : COL_TXT, AL_LEFT, BOT_W - 48);
		ui_text(36, y + 14, st, SZ_TINY, col, AL_LEFT, BOT_W - 48);
	}
	if (FL.loaded && !GF.n) ui_text(BOT_W / 2, top + 30, _("Todavía no tienes amigos en Steam"), SZ_SMALL, COL_DIM, AL_CENTER, 0);
	else if (!FL.loaded && !FL.failed) ui_spinner(BOT_W / 2, top + 30, 6, COL_DIM);
	static const char *h[] = {"A", "Regalar", "↑↓", "Elegir", "B", "Cancelar"};
	static const char *h0[] = {"B", "Cancelar"};
	if (GF.n) draw_hints(h, 3);
	else draw_hints(h0, 1);
}

static void input_gift(u32 kd, u32 kh, touchPosition *tp)
{
	if ((kd & (KEY_B | KEY_SELECT)) || ((kd & KEY_TOUCH) && tp->py < LIST_Y && tp->px < 120)) {
		GF.open = false;
		return;
	}
	if (!GF.n) return;
	if (key_repeat(kd, kh, KEY_DOWN) && GF.sel < GF.n - 1) GF.sel++;
	if (key_repeat(kd, kh, KEY_UP) && GF.sel > 0) GF.sel--;
	bool pick = kd & KEY_A;
	const float RH = 26, top = LIST_Y + 22;
	if ((kd & KEY_TOUCH) && tp->py >= top) {
		const int vis = (int)((SCR_H - 18 - top) / RH);
		int first = GF.sel - (vis - 1);
		if (first < 0) first = 0;
		int v = first + (int)((tp->py - top) / RH);
		if (v >= 0 && v < GF.n) {
			pick = v == GF.sel;
			GF.sel = v;
		}
	}
	if (pick) {
		const SteamPerson *p = &FL.p[GF.who[GF.sel]];
		GF.id = p->id;
		snprintf(GF.name, sizeof GF.name, "%s", p->name);
		GF.want_kb = true;
	}
}

static void draw_friends_top(void)
{
	const AuthInfo *ai = auth_info();
	if (FL.qr || (FL.loaded && !FL.np)) {
		// Para que te añadan: el QR abre tu perfil (allí está «Añadir amigo») y el código de amigo
		char url[96], body[256], foot[64];
		snprintf(url, sizeof url, "https://steamcommunity.com/profiles/%llu", (unsigned long long)ai->steamid);
		unsigned long code = (unsigned long)(ai->steamid - 76561197960265728ULL);
		snprintf(body, sizeof body, "%s", FL.np ? _("Escanea el código con el móvil: se abrirá tu perfil de Steam, donde tu amigo puede pulsar «Añadir amigo».")
			: _("Todavía no tienes amigos en Steam. Escanea el código con el móvil de un amigo: se abrirá tu perfil, donde puede pulsar «Añadir amigo»."));
		snprintf(foot, sizeof foot, _("Código de amigo: %lu"), code);
		draw_qr_panel(url, _("Añadir amigo"), body, NULL, foot, COL_ACC);
		ui_text(TOP_W - 12, SCR_H - 16, _("X: escribir el código de amigo de otra persona"), SZ_TINY, COL_DIM, AL_RIGHT, 380);
		return;
	}
	if (!FL.loaded) {
		if (FL.failed) {
			ui_icon(IC_USERS, TOP_W / 2 - 14, 80, 28, COL_DIM);
			ui_text(TOP_W / 2, 120, _("No se pudo cargar tu lista de amigos"), SZ_LARGE, COL_TXT, AL_CENTER, 380);
			ui_text(TOP_W / 2, 144, _("Comprueba la conexión y vuelve a entrar"), SZ_SMALL, COL_DIM, AL_CENTER, 380);
		} else {
			draw_net_wait(TOP_W / 2, 120, _("Cargando tus amigos…"));
		}
		return;
	}
	int i = FL.sel >= 0 && FL.sel < FL.nview ? FL.view[FL.sel] : -1;
	if (i < 0) return;
	const SteamPerson *p = &FL.p[i];
	char av[128];
	const Pic *pic = auth_avatar_url(p->avatar_sha, av, sizeof av) ? img_get(av, 64) : NULL;
	ui_rect(16, 32, 76, 76, C2D_Color32(0xff, 0xff, 0xff, 0x30));
	if (pic) img_draw_cover(pic, 18, 34, 72, 72, 0.5f);
	else ui_rect(18, 34, 72, 72, COL_PANEL);
	ui_text(108, 38, p->name, SZ_TITLE, COL_WHITE, AL_LEFT, TOP_W - 120);
	char st[96];
	u32 col;
	person_status(p, st, sizeof st, &col);
	ui_text(108, 70, st, SZ_BODY, col, AL_LEFT, TOP_W - 120);
	if (p->state > 0 && p->game) {
		char url[160];
		snprintf(url, sizeof url, "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/%lu/header.jpg", (unsigned long)p->game);
		const float iw = 230, ih = floorf(iw * 215 / 460), iy = 124;
		const Pic *g = img_get(url, 460);
		ui_rect(15, iy - 1, iw + 2, ih + 2, C2D_Color32(0xff, 0xff, 0xff, 0x30));
		if (g) img_draw_cover(g, 16, iy, iw, ih, 0.5f);
		else {
			ui_rect(16, iy, iw, ih, COL_PANEL);
			if (!img_failed(url)) ui_spinner(16 + iw / 2, iy + ih / 2, 6, COL_DIM);
		}
		ui_text_wrap(16 + iw + 12, iy + ih - 30, TOP_W - iw - 44, _("Pulsa X para ver su página"), SZ_SMALL, COL_ACC, 2, 14);
	}
}

static void draw_friends_bottom(void)
{
	fx_bar(0, 0, BOT_W, LIST_Y, true);
	ui_text(10, 4, "‹", SZ_LARGE, COL_ACC, AL_LEFT, 0);
	ui_icon(IC_USERS, 28, 6, 12, COL_ACC);
	char t[64];
	if (FL.np) snprintf(t, sizeof t, _("Amigos · %d"), FL.np - FL.nrq < 0 ? 0 : FL.np - FL.nrq);
	else snprintf(t, sizeof t, "%s", _("Amigos"));
	ui_text(46, 5, t, SZ_SMALL, COL_WHITE, AL_LEFT, BOT_W - 140);
	// Solicitudes que te han mandado: se responden en la app de Steam
	if (FL.pending) {
		char pq[48];
		snprintf(pq, sizeof pq, FL.pending == 1 ? _("%d solicitud") : _("%d solicitudes"), FL.pending);
		ui_text(BOT_W - 10, 6, pq, SZ_TINY, COL_ACC, AL_RIGHT, 0);
	}
	const float RH = 26, top = LIST_Y + 2;
	const int vis = (int)((SCR_H - 18 - top) / RH);
	int first = FL.sel - (vis - 2);
	if (first < 0) first = 0;
	static const char *groups[] = {"Solicitudes de amistad", "Jugando", "En línea", "Desconectados"};
	for (int v = first; v < FL.nview && v < first + vis; v++) {
		float y = top + (v - first) * RH;
		int i = FL.view[v];
		if (i < 0) {
			ui_text(10, y + 8, _(groups[-1 - i]), SZ_SMALL, COL_ACC, AL_LEFT, 0);
			ui_hline(8, y + RH - 1, BOT_W - 16, C2D_Color32(0xff, 0xff, 0xff, 0x14));
			continue;
		}
		const SteamPerson *p = &FL.p[i];
		bool on = v == FL.sel && !FL.qr;
		if (on) {
			float br = 0.075f + 0.035f * sinf(g_time * 2.4f);
			ui_rrect(3, y + 1, BOT_W - 8, RH - 2, 6, col_alpha(COL_WHITE, br));
			ui_rrect(3, y + 4, 2, RH - 8, 1, col_alpha(COL_ACC, 0.8f));
		}
		char av[128];
		const Pic *pic = auth_avatar_url(p->avatar_sha, av, sizeof av) ? img_get(av, 32) : NULL;
		if (pic) img_draw_cover(pic, 10, y + 3, 19, 19, 0.5f);
		else ui_rect(10, y + 3, 19, 19, col_alpha(COL_WHITE, 0.07f));
		char st[96];
		u32 col;
		person_status(p, st, sizeof st, &col);
		ui_text(36, y + 2, p->name, SZ_SMALL, on ? COL_WHITE : COL_TXT, AL_LEFT, BOT_W - 48);
		ui_text(36, y + 14, st, SZ_TINY, col, AL_LEFT, BOT_W - 48);
	}
	if (FL.loaded && !FL.np) ui_text(BOT_W / 2, LIST_Y + 40, _("Todavía no tienes amigos en Steam"), SZ_SMALL, COL_DIM, AL_CENTER, 0);
	else if (!FL.loaded && !FL.failed) ui_spinner(BOT_W / 2, LIST_Y + 40, 6, COL_DIM);
	bool game = FL.sel >= 0 && FL.sel < FL.nview && FL.view[FL.sel] >= 0 && FL.p[FL.view[FL.sel]].state > 0 && FL.p[FL.view[FL.sel]].game;
	static const char *hq[] = {"X", "Código de amigo", "Y", "Ver la lista", "B", "Volver"};
	static const char *hg[] = {"A", "Ver perfil", "X", "Ver el juego", "Y", "Añadir amigo", "B", "Volver"};
	static const char *h[] = {"A", "Ver perfil", "↑↓", "Elegir", "Y", "Añadir amigo", "B", "Volver"};
	static const char *h0[] = {"Y", "Añadir amigo", "B", "Volver"};
	if (FL.qr && FL.np) draw_hints(hq, 3);
	else if (!FL.np) draw_hints(h0, 2);
	else if (game && !FL.qr) draw_hints(hg, 4);
	else draw_hints(h, 4);
}

static void flist_move(int d)
{
	int v = FL.sel;
	do v += d; while (v >= 0 && v < FL.nview && FL.view[v] < 0);
	if (v >= 0 && v < FL.nview) FL.sel = v;
}

static void input_friends(u32 kd, u32 kh, touchPosition *tp)
{
	if ((kd & KEY_B) || ((kd & KEY_TOUCH) && tp->py < LIST_Y && tp->px < 120)) {
		if (FL.qr && FL.np) FL.qr = false;
		else go_scene(flist_back == SC_FRIENDS ? SC_PROFILE : flist_back, -1);
		return;
	}
	if (kd & KEY_Y) FL.qr = !FL.qr;
	if (FL.qr || (FL.loaded && !FL.np)) {
		if (kd & KEY_X) FL.want_code = true;   // añadir a alguien por su código de amigo
		if (FL.qr) return;
	}
	if (key_repeat(kd, kh, KEY_DOWN)) flist_move(1);
	if (key_repeat(kd, kh, KEY_UP)) flist_move(-1);
	if ((kd & KEY_TOUCH) && tp->py >= LIST_Y) {
		const float RH = 26, top = LIST_Y + 2;
		const int vis = (int)((SCR_H - 18 - top) / RH);
		int first = FL.sel - (vis - 2);
		if (first < 0) first = 0;
		int v = first + (int)((tp->py - top) / RH);
		if (v >= 0 && v < FL.nview && FL.view[v] >= 0) {
			if (v == FL.sel) kd |= KEY_A;
			else FL.sel = v;
		}
	}
	if ((kd & KEY_A) && FL.sel >= 0 && FL.sel < FL.nview && FL.view[FL.sel] >= 0) {
		open_user(FL.p[FL.view[FL.sel]].id, &FL.p[FL.view[FL.sel]]);
		return;
	}
	if ((kd & KEY_X) && FL.sel >= 0 && FL.sel < FL.nview && FL.view[FL.sel] >= 0) {
		const SteamPerson *p = &FL.p[FL.view[FL.sel]];
		if (p->state > 0 && p->game) open_game(p->game, true);
	}
}

// ---------------------------------------------------------------------------- Perfil de Steam (de un amigo o el tuyo)

#define UP_FILES 24
static struct {
	u64 id;
	SteamProfile p;
	Req *items, *shows, *xml, *lvreq, *files, *page;
	int level;                   // -1 mientras no se sabe
	SteamPerson person;          // su estado, si viene de la lista de amigos
	bool has_person;
	RowList apps;                // los juegos de sus vitrinas (para los nombres)
	u64 fid[UP_FILES];           // capturas e ilustraciones de las vitrinas
	char furl[UP_FILES][200];
	int nf, page_i;              // page_i: la siguiente que se busca en su página (-1 aún no toca)
	u64 page_at;                 // no antes de esta hora (la comunidad limita las peticiones seguidas)
	Req *fhead[UP_FILES];        // la cabecera de cada imagen: si es un GIF se pide el original
	Req *fr_req;                 // aceptar, ignorar o mandar una solicitud de amistad
	// Tu Familia de Steam (solo en tu perfil)
	SteamFamily fam;
	SteamPerson famp[8];
	int nfamp;
	Req *fam_req, *fam_people;
	bool fam_done;
	ProfileComment com[6];       // sus comentarios (los últimos)
	int ncom, com_total;
	bool com_done, want_comment;
	Req *com_req, *com_post;
	int fr_kind, rel;
	bool rel_set;
	bool fready[UP_FILES];
	// Sus juegos: cuántos, horas, lo más jugado y lo último que ha jugado (con sus logros)
	RowList games;
	Req *games_req, *prog_req[3];
	bool games_done, games_private;
	int top[3], recent[3], prog_un[3], prog_tot[3];
	long total_min;
	TopAch ach[96];              // sus logros de los juegos de su vitrina de logros
	int nach;
	Req *ach_req[3];
	int page_retry;
	float scroll, scroll_t, content;
	bool qr;
	Scene back;
} UP;

// Las imágenes de las ilustraciones ya encontradas en esta sesión (así no se vuelve a pedir su página)
static struct { u64 id; char url[200]; } known[32];
static int nknown;

static const char *known_file(u64 id)
{
	for (int i = 0; i < nknown; i++)
		if (known[i].id == id) return known[i].url;
	return NULL;
}

static void known_add(u64 id, const char *url)
{
	if (known_file(id) || url[0] == '-') return;
	int i = nknown < 32 ? nknown++ : (int)(id % 32);
	known[i].id = id;
	snprintf(known[i].url, sizeof known[i].url, "%s", url);
}

static void user_reset(void)
{
	Req **all[] = {&UP.items, &UP.shows, &UP.xml, &UP.lvreq, &UP.files, &UP.page, &UP.ach_req[0], &UP.ach_req[1], &UP.ach_req[2],
		&UP.games_req, &UP.prog_req[0], &UP.prog_req[1], &UP.prog_req[2], &UP.fr_req, &UP.com_req, &UP.com_post, &UP.fam_req, &UP.fam_people};
	for (size_t i = 0; i < sizeof all / sizeof *all; i++)
		if (*all[i]) net_release(*all[i]);
	for (int i = 0; i < UP_FILES; i++)
		if (UP.fhead[i]) net_release(UP.fhead[i]);
	rowlist_free(&UP.apps);
	rowlist_free(&UP.games);
	steam_profile_free(&UP.p);
	Scene back = UP.back;
	memset(&UP, 0, sizeof UP);
	UP.back = back;
}

static bool user_loaded_is(u64 id)
{
	return UP.id == id;
}

static bool user_is_me(void)
{
	return auth_logged() && UP.id == auth_info()->steamid;
}

// Pide todo lo de un perfil (si ya es el que está cargado, no hace nada)
static void user_load(u64 id)
{
	if (UP.id == id) return;
	user_reset();
	UP.id = id;
	UP.level = -1;
	UP.page_i = -1;
	UP.items = steam_req_profile_items(id);
	UP.shows = steam_req_profile_showcases(id);
	UP.xml = steam_req_profile_xml(id);
	if (auth_logged()) UP.lvreq = steam_req_level(id, auth_token());
	if (auth_logged()) UP.games_req = steam_req_owned(id, auth_token());
	UP.com_req = steam_req_comments(id, 6);
	for (int i = 0; i < 3; i++) UP.top[i] = UP.recent[i] = UP.prog_tot[i] = -1;
}

static bool user_loading(void)
{
	return UP.items || UP.shows || UP.xml;
}

static void user_poll(void)
{
	if (!UP.id) return;
	if (UP.items && UP.items->state >= REQ_DONE) {
		bool ok = UP.items->state == REQ_DONE && steam_parse_profile_items(UP.items->data, &UP.p);
		netlog("perfil %llu: fondo %s marco %s%s", (unsigned long long)UP.id, UP.p.bg[0] ? "sí" : "no", UP.p.frame[0] ? "sí" : "no", ok ? "" : " (falló)");
		net_release(UP.items);
		UP.items = NULL;
	}
	if (UP.xml && UP.xml->state >= REQ_DONE) {
		bool ok = UP.xml->state == REQ_DONE && steam_parse_profile_xml(UP.xml->data, &UP.p);
		netlog("perfil %llu: ficha %s", (unsigned long long)UP.id, ok ? (UP.p.is_private ? "privada" : "pública") : "falló");
		net_release(UP.xml);
		UP.xml = NULL;
	}
	if (UP.lvreq && UP.lvreq->state >= REQ_DONE) {
		if (UP.lvreq->state == REQ_DONE) UP.level = steam_parse_level(UP.lvreq->data);
		net_release(UP.lvreq);
		UP.lvreq = NULL;
	}
	if (UP.shows && UP.shows->state >= REQ_DONE) {
		bool ok = UP.shows->state == REQ_DONE && steam_parse_profile_showcases(UP.shows->data, &UP.p);
		netlog("perfil %llu: %d vitrinas (+%d que aquí no se ven)%s", (unsigned long long)UP.id, UP.p.nshow, UP.p.nother, ok ? "" : " (falló)");
		net_release(UP.shows);
		UP.shows = NULL;
		// Los nombres de sus juegos y las imágenes de sus capturas e ilustraciones
		u32 apps[48];
		int na = 0;
		for (int i = 0; i < UP.p.nshow; i++) {
			const Showcase *s = &UP.p.show[i];
			for (int k = 0; k < s->n; k++) {
				if (s->appid[k] && na < 48 && s->type != SHOW_SHOTS && s->type != SHOW_ARTWORK && s->type != SHOW_ONE_ARTWORK) {
					bool dup = false;
					for (int j = 0; j < na; j++) dup |= apps[j] == s->appid[k];
					if (!dup) apps[na++] = s->appid[k];
				}
				if (s->file[k] && UP.nf < UP_FILES) UP.fid[UP.nf++] = s->file[k];
			}
		}
		if (na) UP.apps.req = steam_req_items(apps, na, false);
		// La vitrina de logros: sus logros de esos juegos (hasta tres juegos)
		int nreq = 0;
		for (int i = 0; i < UP.p.nshow && auth_logged(); i++) {
			const Showcase *sc = &UP.p.show[i];
			if (sc->type != SHOW_ACHIEVEMENTS) continue;
			for (int k = 0; k < sc->n && nreq < 3; k++) {
				bool dup = false;
				for (int j = 0; j < k; j++) dup |= sc->appid[j] == sc->appid[k];
				if (!dup) UP.ach_req[nreq++] = steam_req_my_achievements(sc->appid[k], UP.id, auth_token());
			}
		}
		// La imagen de cada una sale de su página en la comunidad (IPublishedFileService/GetDetails no
		// acepta la sesión). Las que ya se vieron en esta sesión no se vuelven a pedir
		if (UP.nf) {
			for (int i = 0; i < UP.nf; i++) {
				const char *k = known_file(UP.fid[i]);
				if (k) {
					snprintf(UP.furl[i], sizeof UP.furl[0], "%s", k);
					UP.fready[i] = true;
				}
			}
			UP.page_i = 0;
		}
	}
	list_poll(&UP.apps, false);
	if (UP.games_req && UP.games_req->state >= REQ_DONE) {
		bool ok = UP.games_req->state == REQ_DONE && steam_parse_owned(UP.games_req->data, &UP.games);
		// Steam contesta {"response":{}} si sus juegos no son visibles para ti
		UP.games_private = !ok || (!UP.games.count && !strstr(UP.games_req->data ? UP.games_req->data : "", "game_count"));
		netlog("perfil %llu: %d juegos%s (HTTP %ld)", (unsigned long long)UP.id, UP.games.count, UP.games_private ? " (privados)" : "", UP.games_req->http);
		net_release(UP.games_req);
		UP.games_req = NULL;
		UP.games_done = true;
		// Lo más jugado y lo último jugado (tres de cada)
		UP.total_min = 0;
		for (int i = 0; i < UP.games.count; i++) {
			const StoreRow *r = &UP.games.rows[i];
			UP.total_min += r->minutes;
			for (int k = 0; k < 3; k++) {
				if (r->minutes > 0 && (UP.top[k] < 0 || r->minutes > UP.games.rows[UP.top[k]].minutes)) {
					for (int j = 2; j > k; j--) UP.top[j] = UP.top[j - 1];
					UP.top[k] = i;
					break;
				}
			}
			for (int k = 0; k < 3; k++) {
				if (r->last_played && (UP.recent[k] < 0 || r->last_played > UP.games.rows[UP.recent[k]].last_played)) {
					for (int j = 2; j > k; j--) UP.recent[j] = UP.recent[j - 1];
					UP.recent[k] = i;
					break;
				}
			}
		}
		for (int k = 0; k < 3 && auth_logged(); k++)
			if (UP.recent[k] >= 0) UP.prog_req[k] = steam_req_achievement_progress(UP.games.rows[UP.recent[k]].appid, UP.id, auth_token());
	}
	for (int k = 0; k < 3; k++) {
		Req *r = UP.prog_req[k];
		if (!r || r->state < REQ_DONE) continue;
		int un = 0, tot = 0;
		if (r->state == REQ_DONE && steam_parse_achievement_progress(r->data, &un, &tot)) {
			UP.prog_un[k] = un;
			UP.prog_tot[k] = tot;
		}
		net_release(r);
		UP.prog_req[k] = NULL;
	}
	for (int i = 0; i < 3; i++) {
		Req *r = UP.ach_req[i];
		if (!r || r->state < REQ_DONE) continue;
		int n = r->state == REQ_DONE ? steam_parse_top_achievements(r->data, UP.ach + UP.nach, 96 - UP.nach) : -1;
		netlog("perfil %llu: logros de su vitrina %d", (unsigned long long)UP.id, n);
		if (n > 0) UP.nach += n;
		net_release(r);
		UP.ach_req[i] = NULL;
	}
	if (UP.files && UP.files->state >= REQ_DONE) {
		int n = UP.files->state == REQ_DONE ? steam_parse_files(UP.files->data, UP.fid, UP.nf, UP.furl) : -1;
		netlog("perfil %llu: imágenes de vitrinas %d de %d (HTTP %ld)", (unsigned long long)UP.id, n, UP.nf, UP.files->http);
		net_release(UP.files);
		UP.files = NULL;
		UP.page_i = 0;   // las que falten, de su página
	}
	// Las que no dio el servicio se sacan de su página en la comunidad, de una en una
	if (!UP.page && UP.page_i >= 0 && osGetTime() >= UP.page_at) {
		while (UP.page_i < UP.nf && UP.furl[UP.page_i][0]) UP.page_i++;
		if (UP.page_i < UP.nf) UP.page = steam_req_file_page(UP.fid[UP.page_i]);
		else UP.page_i = -1;
	}
	// Un GIF reducido por Steam tarda muchísimo la primera vez (y pesa casi lo mismo): se pide el
	// original y lo reduce la consola. Los demás formatos, ya reducidos
	for (int i = 0; i < UP.nf; i++) {
		char *u = UP.furl[i];
		if (!u[0] || u[0] == '-' || UP.fready[i]) continue;
		if (!UP.fhead[i]) {
			char base[200];
			snprintf(base, sizeof base, "%s", u);
			char *q = strchr(base, '?');
			if (q && strstr(base, "steamusercontent.com")) {
				*q = 0;
				UP.fhead[i] = net_head(base);
			}
			if (!UP.fhead[i]) {
				UP.fready[i] = true;
				known_add(UP.fid[i], u);
			}
			continue;
		}
		Req *h = UP.fhead[i];
		if (h->state < REQ_DONE) continue;
		bool gif = h->state == REQ_DONE && strstr(h->ctype, "gif") && (h->clen < 0 || h->clen <= (4 << 20));
		netlog("perfil: ilustración %llu es %s (%ld bytes)%s", (unsigned long long)UP.fid[i], h->ctype[0] ? h->ctype : "?", h->clen, gif ? ": se pide el original" : "");
		if (gif) *strchr(u, '?') = 0;
		UP.fready[i] = true;
		known_add(UP.fid[i], u);
		net_release(h);
		UP.fhead[i] = NULL;
	}
	if (UP.page && UP.page->state >= REQ_DONE) {
		if (UP.page->http == 429 && UP.page_retry < 3) {
			// Demasiadas seguidas: se espera un poco y se vuelve a intentar la misma
			UP.page_retry++;
			UP.page_at = osGetTime() + 4000 * UP.page_retry;
		} else {
			if (UP.page->state != REQ_DONE || !steam_parse_file_page(UP.page->data, UP.furl[UP.page_i], sizeof UP.furl[0]))
				snprintf(UP.furl[UP.page_i], sizeof UP.furl[0], "-");   // no hay forma: se queda en blanco
			UP.page_i++;
			UP.page_retry = 0;
			UP.page_at = osGetTime() + 1200;
		}
		net_release(UP.page);
		UP.page = NULL;
	}
}

static const char *user_file_url(u64 id)
{
	for (int i = 0; i < UP.nf; i++)
		if (UP.fid[i] == id) return UP.furl[i][0] == '-' ? UP.furl[i] : UP.furl[i][0] && UP.fready[i] ? UP.furl[i] : NULL;   // «-»: no se pudo
	return NULL;
}

static const char *user_app_name(u32 appid)
{
	for (int i = 0; i < UP.apps.count; i++)
		if (UP.apps.rows[i].appid == appid) return UP.apps.rows[i].name;
	return NULL;
}

static void user_name(char *out, size_t n)
{
	if (user_is_me() && auth_info()->persona[0]) snprintf(out, n, "%s", auth_info()->persona);
	else if (UP.has_person && UP.person.name[0]) snprintf(out, n, "%s", UP.person.name);
	else snprintf(out, n, "%s", UP.p.name);
}

static void user_avatar(char *out, size_t n)
{
	out[0] = 0;
	if (user_is_me()) snprintf(out, n, "%s", auth_info()->avatar);
	else if (UP.p.avatar[0]) snprintf(out, n, "%s", UP.p.avatar);
	else if (UP.has_person) auth_avatar_url(UP.person.avatar_sha, out, n);
}

static void community_img(const char *path, char *out, size_t n)
{
	snprintf(out, n, "https://cdn.akamai.steamstatic.com/steamcommunity/public/images/%s", path);
}

// El fondo de su perfil a toda pantalla, oscurecido para que se lea lo de encima
static bool draw_user_bg(float dim)
{
	if (!UP.p.bg[0]) return false;
	char url[200];
	community_img(UP.p.bg, url, sizeof url);
	const Pic *p = img_get(url, 400);
	if (!p) return false;
	img_draw_cover(p, 0, 0, TOP_W, SCR_H, 0.3f);
	ui_rect(0, 0, TOP_W, SCR_H, C2D_Color32(0x0b, 0x10, 0x18, (u8)(255 * dim)));
	return true;
}

// Avatar con su marco (el marco es algo más grande que el avatar y lo rodea)
static void draw_user_avatar(float x, float y, float sz)
{
	char url[200];
	user_avatar(url, sizeof url);
	const Pic *p = url[0] ? img_get(url, 64) : NULL;
	bool framed = false;
	if (UP.p.frame[0]) {
		char fu[200];
		community_img(UP.p.frame, fu, sizeof fu);
		const Pic *f = img_get(fu, 128);
		if (p) img_draw_cover(p, x, y, sz, sz, 0.5f);
		else ui_rect(x, y, sz, sz, COL_PANEL);
		if (f) {
			float fs = sz * 1.22f;
			img_draw_fit(f, x - (fs - sz) / 2, y - (fs - sz) / 2, fs, fs);
			framed = true;
		}
	}
	if (!framed) {
		ui_rect(x - 2, y - 2, sz + 4, sz + 4, C2D_Color32(0xff, 0xff, 0xff, 0x30));
		if (p) img_draw_cover(p, x, y, sz, sz, 0.5f);
		else ui_rect(x, y, sz, sz, COL_PANEL);
	}
}

static void draw_level(float cx, float cy, int lv)
{
	ui_circle(cx, cy, 12, C2D_Color32(0x9b, 0x9b, 0x9b, 0xff));
	ui_circle(cx, cy, 10, COL_BG);
	char t[12];
	snprintf(t, sizeof t, "%d", lv);
	ui_text(cx, cy - 6, t, SZ_SMALL, COL_WHITE, AL_CENTER, 0);
}

static void user_status(char *out, size_t n, u32 *col)
{
	if (UP.has_person) {
		person_status(&UP.person, out, n, col);
		return;
	}
	*col = COL_DIM;
	if (!strcmp(UP.p.state, "in-game")) {
		*col = COL_LIME;
		const char *g = strchr(UP.p.state_msg, '\n');
		snprintf(out, n, _("Jugando a %s"), g ? g + 1 : _("un juego"));
	} else if (!strcmp(UP.p.state, "online")) {
		*col = COL_ACC;
		snprintf(out, n, "%s", _("En línea"));
	} else if (UP.p.state[0]) {
		snprintf(out, n, "%s", _("Desconectado"));
	} else {
		out[0] = 0;
	}
}

static void fmt_date(u32 t, char *out, size_t n)
{
	time_t tt = t;
	struct tm *tm = gmtime(&tt);
	static const char *mes[] = {"ene", "feb", "mar", "abr", "may", "jun", "jul", "ago", "sep", "oct", "nov", "dic"};
	snprintf(out, n, "%d %s %d", tm->tm_mday, _(mes[tm->tm_mon]), tm->tm_year + 1900);
}

static const char *show_title(int type)
{
	switch (type) {
	case SHOW_FAV_GAME: return _("JUEGO FAVORITO");
	case SHOW_COLLECTOR: return _("COLECCIONISTA DE JUEGOS");
	case SHOW_COMPLETIONIST: return _("JUEGOS CON TODOS LOS LOGROS");
	case SHOW_SHOTS: return _("CAPTURAS");
	case SHOW_ARTWORK: return _("ILUSTRACIONES");
	case SHOW_ONE_ARTWORK: return _("ILUSTRACIÓN DESTACADA");
	case SHOW_ACHIEVEMENTS: return _("VITRINA DE LOGROS");
	default: return "";
	}
}

static void draw_img_slot(const char *url, float x, float y, float w, float h, bool cover)
{
	bool none = url && url[0] == '-';
	const Pic *p = url && !none ? img_get(url, w > 200 ? 400 : 200) : NULL;
	if (none || (url && !p && img_failed(url))) {
		ui_rect(x, y, w, h, col_alpha(COL_WHITE, 0.06f));
		ui_icon(IC_IMAGE, x + w / 2 - 8, y + h / 2 - 8, 16, COL_DIM);
	} else if (p) {
		ui_rect(x, y, w, h, C2D_Color32(0, 0, 0, 0x60));
		if (cover) img_draw_cover(p, x, y, w, h, 0.5f);
		else img_draw_fit(p, x, y, w, h);
	} else {
		ui_rect(x, y, w, h, col_alpha(COL_WHITE, 0.06f));
		ui_spinner(x + w / 2, y + h / 2, 5, COL_DIM);
	}
}

static void app_img(u32 appid, const char *file, char *out, size_t n)
{
	snprintf(out, n, "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/%lu/%s", (unsigned long)appid, file);
}

// Una vitrina; devuelve lo que ocupa (si draw es false solo mide)
static float draw_showcase(const Showcase *s, float y, bool draw)
{
	const float x = 12, w = TOP_W - 24;
	float h = 0;
	char url[200];
	if (s->type == SHOW_TEXT) {
		const char *t = s->title && s->title[0] ? s->title : NULL;
		int nl = s->text && s->text[0] ? ui_text_lines(s->text, SZ_SMALL, w - 20) : 0;
		if (nl > 12) nl = 12;
		h = 10 + (t ? 18 : 0) + nl * 14 + 6;
		if (draw) {
			fx_glass(x, y, w, h, 10, 1, 0);
			if (t) ui_text(x + 10, y + 8, t, SZ_BODY, COL_WHITE, AL_LEFT, w - 20);
			if (nl) ui_text_wrap(x + 10, y + 8 + (t ? 18 : 0), w - 20, s->text, SZ_SMALL, COL_TXT, 12, 14);
		}
		return h;
	}
	h = 22;
	if (s->type == SHOW_FAV_GAME) {
		const float iw = 184, ih = 86;
		if (draw) {
			fx_glass(x, y, w, 22 + ih + 10, 10, 1, 0);
			ui_text(x + 10, y + 6, show_title(s->type), SZ_TINY, COL_ACC, AL_LEFT, 0);
			app_img(s->appid[0], "header.jpg", url, sizeof url);
			draw_img_slot(url, x + 10, y + 22, iw, ih, true);
			const char *nm = user_app_name(s->appid[0]);
			if (nm) ui_text_wrap(x + 20 + iw, y + 24, w - iw - 30, nm, SZ_BODY, COL_WHITE, 3, 16);
		}
		return 22 + ih + 10;
	}
	if (s->type == SHOW_ACHIEVEMENTS) {
		// Los iconos en fila; debajo, el nombre del logro más raro de los que enseña y su juego
		const int cols = 9;
		const float gs = 36, gap = (w - 20 - cols * gs) / (cols - 1);
		int rows = (s->n + cols - 1) / cols;
		h = 22 + rows * (gs + gap) + 20;
		if (draw) {
			fx_glass(x, y, w, h, 10, 1, 0);
			ui_text(x + 10, y + 6, show_title(s->type), SZ_TINY, COL_ACC, AL_LEFT, 0);
			const TopAch *rare = NULL;
			for (int i = 0; i < s->n; i++) {
				const TopAch *t = NULL;
				for (int j = 0; j < UP.nach; j++)
					if (UP.ach[j].appid == s->appid[i] && !strcmp(UP.ach[j].key, s->ach[i])) t = &UP.ach[j];
				float ix = x + 10 + (i % cols) * (gs + gap), iy = y + 22 + (i / cols) * (gs + gap);
				if (t && t->icon[0]) {
					snprintf(url, sizeof url, "https://cdn.akamai.steamstatic.com/steamcommunity/public/images/apps/%lu/%s", (unsigned long)t->appid, t->icon);
					draw_img_slot(url, ix, iy, gs, gs, true);
					if (!rare || t->pct < rare->pct) rare = t;
				} else {
					ui_rect(ix, iy, gs, gs, col_alpha(COL_WHITE, 0.06f));
					ui_icon(IC_TROPHY, ix + 10, iy + 10, 16, COL_DIM);
				}
			}
			char t[160];
			const char *game = user_app_name(s->appid[0]);
			if (rare) snprintf(t, sizeof t, _("%s · lo tiene el %.1f %% de los jugadores"), rare->name, rare->pct);
			else snprintf(t, sizeof t, "%s", auth_logged() ? (game ? game : "") : _("Inicia sesión para ver sus logros"));
			ui_text(x + 10, y + h - 17, t, SZ_TINY, COL_DIM, AL_LEFT, w - 20);
		}
		return h;
	}
	if (s->type == SHOW_COLLECTOR || s->type == SHOW_COMPLETIONIST) {
		const int cols = 4;
		const float gw = (w - 20 - 6 * (cols - 1)) / cols, gh = floorf(gw * 69 / 184);
		int rows = (s->n + cols - 1) / cols;
		h = 22 + rows * (gh + 6) + 4;
		if (draw) {
			fx_glass(x, y, w, h, 10, 1, 0);
			ui_text(x + 10, y + 6, show_title(s->type), SZ_TINY, COL_ACC, AL_LEFT, 0);
			for (int i = 0; i < s->n; i++) {
				app_img(s->appid[i], "capsule_184x69.jpg", url, sizeof url);
				draw_img_slot(url, x + 10 + (i % cols) * (gw + 6), y + 22 + (i / cols) * (gh + 6), gw, gh, true);
			}
		}
		return h;
	}
	// Capturas e ilustraciones: la primera grande y las demás en miniatura. El recuadro de la grande
	// toma la forma de la imagen (una ilustración larga hacia abajo sale alta y a todo el ancho)
	const float th = 52, maxw = w - 20;
	float big_w = maxw, big_h = 170;
	const char *u0 = user_file_url(s->file[0]);
	const Pic *p0 = u0 && u0[0] != '-' ? img_get(u0, 400) : NULL;
	if (p0 && p0->w > 0 && p0->h > 0) {
		// Siempre a todo el ancho del marco; si es muy alta, se baja para verla entera
		big_h = maxw * p0->h / p0->w;
		if (big_h < 90) {   // muy apaisada: un mínimo de alto, centrada
			big_h = 90;
			big_w = big_h * p0->w / p0->h;
			if (big_w > maxw) big_w = maxw;
		}
	}
	int rest = s->n - 1;
	h = 22 + big_h + (rest > 0 ? 6 + th : 0) + 10;
	if (draw) {
		fx_glass(x, y, w, h, 10, 1, 0);
		ui_text(x + 10, y + 6, show_title(s->type), SZ_TINY, COL_ACC, AL_LEFT, 0);
		draw_img_slot(u0, x + 10 + (maxw - big_w) / 2, y + 22, big_w, big_h, false);
		float tw = (w - 20 - 6 * 2) / 3;
		for (int i = 0; i < rest && i < 3; i++)
			draw_img_slot(user_file_url(s->file[i + 1]), x + 10 + i * (tw + 6), y + 22 + big_h + 6, tw, th, true);
	}
	return h;
}

// Sus juegos: totales, lo más jugado y su actividad reciente (con el progreso de logros)
static float draw_user_games(float y)
{
	const float x = 12, w = TOP_W - 24;
	char t[160];
	if (!auth_logged()) return 0;
	if (!UP.games_done) {
		fx_glass(x, y, w, 40, 10, 1, 0);
		ui_spinner(TOP_W / 2, y + 20, 6, COL_DIM);
		return 48;
	}
	if (UP.games_private || !UP.games.count) {
		fx_glass(x, y, w, 34, 10, 1, 0);
		ui_icon(IC_LOCK, x + 10, y + 11, 12, COL_DIM);
		ui_text(x + 28, y + 10, UP.games_private ? _("Sus juegos no son visibles para ti") : _("Todavía no tiene juegos"), SZ_SMALL, COL_DIM, AL_LEFT, w - 40);
		return 42;
	}
	int ntop = 0, nrec = 0;
	for (int k = 0; k < 3; k++) ntop += UP.top[k] >= 0, nrec += UP.recent[k] >= 0;
	float h = 30 + (ntop ? 16 + ntop * 22 : 0) + (nrec ? 18 + nrec * 44 : 0) + 4;
	if (y > SCR_H || y + h < 18) return h + 8;
	fx_glass(x, y, w, h, 10, 1, 0);
	snprintf(t, sizeof t, _("%d juegos · %ld h jugadas"), UP.games.count, (UP.total_min + 30) / 60);
	ui_text(x + 10, y + 8, t, SZ_BODY, COL_TXT, AL_LEFT, w - 20);
	float cy = y + 30;
	if (ntop) {
		ui_text(x + 10, cy, _("LO MÁS JUGADO"), SZ_TINY, COL_ACC, AL_LEFT, 0);
		cy += 16;
		int maxm = UP.games.rows[UP.top[0]].minutes;
		for (int k = 0; k < ntop; k++) {
			const StoreRow *r = &UP.games.rows[UP.top[k]];
			char hrs[16];
			snprintf(hrs, sizeof hrs, "%d h", (r->minutes + 30) / 60);
			ui_text(x + 10, cy, r->name, SZ_SMALL, COL_WHITE, AL_LEFT, w - 80);
			ui_text(x + w - 10, cy, hrs, SZ_SMALL, COL_DIM, AL_RIGHT, 0);
			ui_rrect(x + 10, cy + 14, w - 20, 4, 2, C2D_Color32(0xff, 0xff, 0xff, 0x14));
			ui_rrect(x + 10, cy + 14, (w - 20) * r->minutes / (float)(maxm ? maxm : 1), 4, 2, COL_ACC);
			cy += 22;
		}
	}
	if (nrec) {
		ui_text(x + 10, cy + 2, _("ACTIVIDAD RECIENTE"), SZ_TINY, COL_ACC, AL_LEFT, 0);
		cy += 18;
		for (int k = 0; k < nrec; k++) {
			const StoreRow *r = &UP.games.rows[UP.recent[k]];
			char url[200], when[40], hrs[32];
			app_img(r->appid, "capsule_184x69.jpg", url, sizeof url);
			draw_img_slot(url, x + 10, cy + 2, 100, 37, true);
			ui_text(x + 118, cy + 1, r->name, SZ_SMALL, COL_WHITE, AL_LEFT, w - 128);
			notif_when(r->last_played, when, sizeof when);
			snprintf(hrs, sizeof hrs, _("%d h en total"), (r->minutes + 30) / 60);
			snprintf(t, sizeof t, "%s · %s", hrs, when);
			ui_text(x + 118, cy + 15, t, SZ_TINY, COL_DIM, AL_LEFT, w - 128);
			if (UP.prog_tot[k] > 0) {
				float bw = w - 128 - 112;
				snprintf(t, sizeof t, _("%d de %d logros"), UP.prog_un[k], UP.prog_tot[k]);
				ui_rrect(x + 118, cy + 31, bw, 4, 2, C2D_Color32(0xff, 0xff, 0xff, 0x14));
				ui_rrect(x + 118, cy + 31, bw * UP.prog_un[k] / (float)UP.prog_tot[k], 4, 2, COL_LIME);
				ui_text(x + w - 10, cy + 27, t, SZ_TINY, COL_DIM, AL_RIGHT, 106);
			}
			cy += 44;
		}
	}
	return h + 8;
}

static void draw_user_top(void)
{
	if (UP.qr) {
		char url[96];
		snprintf(url, sizeof url, "https://steamcommunity.com/profiles/%llu", (unsigned long long)UP.id);
		draw_qr_panel(url, _("Ver en Steam"), _("Escanea el código con la cámara del móvil para abrir este perfil en la comunidad de Steam."), NULL, NULL, 0);
		return;
	}
	bool bg = draw_user_bg(0.55f);
	if (!bg) ui_vgrad(0, 18, TOP_W, 96, C2D_Color32(0x2a, 0x47, 0x5e, 0xc0), C2D_Color32(0x2a, 0x47, 0x5e, 0x00));
	ui_scissor(GFX_TOP, 0, 18, TOP_W, SCR_H - 18);
	float y = 30 - UP.scroll;
	char name[64], st[112], t[160];
	u32 col;
	user_name(name, sizeof name);
	draw_user_avatar(20, y + 4, 64);
	ui_text(100, y, name[0] ? name : "…", SZ_TITLE, COL_WHITE, AL_LEFT, TOP_W - 112 - (UP.level >= 0 ? 34 : 0));
	if (UP.level >= 0) draw_level(TOP_W - 26, y + 12, UP.level);
	user_status(st, sizeof st, &col);
	if (st[0]) ui_text(100, y + 28, st, SZ_SMALL, col, AL_LEFT, TOP_W - 112);
	// Nombre real y país, como bajo el nombre en Steam
	t[0] = 0;
	if (UP.p.realname[0] && UP.p.location[0]) snprintf(t, sizeof t, "%s · %s", UP.p.realname, UP.p.location);
	else snprintf(t, sizeof t, "%s", UP.p.realname[0] ? UP.p.realname : UP.p.location);
	if (t[0]) ui_text(100, y + 46, t, SZ_TINY, COL_DIM, AL_LEFT, TOP_W - 112);
	if (UP.p.since) {
		char d[40];
		fmt_date(UP.p.since, d, sizeof d);
		snprintf(t, sizeof t, _("Miembro desde el %s"), d);
		ui_text(100, y + 60, t, SZ_TINY, COL_DIM, AL_LEFT, TOP_W - 112);
	}
	y += 84;
	if (UP.p.summary) {
		int nl = ui_text_lines(UP.p.summary, SZ_SMALL, TOP_W - 40);
		if (nl > 14) nl = 14;
		ui_text_wrap(20, y, TOP_W - 40, UP.p.summary, SZ_SMALL, COL_TXT, 14, 14);
		y += nl * 14 + 10;
	}
	if (!UP.p.is_private) y += draw_user_games(y);
	if (UP.p.is_private) {
		ui_icon(IC_LOCK, TOP_W / 2 - 9, y + 4, 18, COL_DIM);
		ui_text(TOP_W / 2, y + 28, _("Este perfil es privado"), SZ_BODY, COL_DIM, AL_CENTER, 0);
		y += 50;
	}
	for (int i = 0; i < UP.p.nshow; i++) {
		const Showcase *s = &UP.p.show[i];
		float h = draw_showcase(s, y, false);
		if (y < SCR_H && y + h > 18) draw_showcase(s, y, true);
		y += h + 8;
	}
	// Tu Familia de Steam
	if (user_is_me() && UP.fam.n) {
		float fh = 24 + UP.fam.n * 30 + 4;
		if (y < SCR_H && y + fh > 18) {
			fx_glass(12, y, TOP_W - 24, fh, 10, 1, 0);
			snprintf(t, sizeof t, _("FAMILIA DE STEAM · %s"), UP.fam.name);
			ui_text(22, y + 6, t, SZ_TINY, COL_ACC, AL_LEFT, TOP_W - 44);
			for (int i = 0; i < UP.fam.n; i++) {
				float my = y + 24 + i * 30;
				const SteamPerson *pp = NULL;
				for (int k = 0; k < UP.nfamp; k++)
					if (UP.famp[k].id == UP.fam.member[i]) pp = &UP.famp[k];
				char av[160];
				const Pic *ap = pp && auth_avatar_url(pp->avatar_sha, av, sizeof av) ? img_get(av, 32) : NULL;
				if (ap) img_draw_cover(ap, 22, my, 24, 24, 0.5f);
				else ui_rect(22, my, 24, 24, col_alpha(COL_WHITE, 0.06f));
				ui_text(54, my + 4, pp ? pp->name : "…", SZ_SMALL, COL_WHITE, AL_LEFT, TOP_W - 170);
				ui_text(TOP_W - 22, my + 5, UP.fam.role[i] == 2 ? _("Menor") : _("Adulto"), SZ_TINY, COL_DIM, AL_RIGHT, 0);
			}
		}
		y += fh + 8;
	}
	// Sus grupos
	if (UP.p.ngroups && !UP.p.is_private) {
		float gh = 24 + UP.p.ngroups * 34 + 4;
		if (y < SCR_H && y + gh > 18) {
			fx_glass(12, y, TOP_W - 24, gh, 10, 1, 0);
			ui_text(22, y + 6, _("GRUPOS"), SZ_TINY, COL_ACC, AL_LEFT, 0);
			for (int i = 0; i < UP.p.ngroups; i++) {
				float gy = y + 24 + i * 34;
				const Pic *gp = UP.p.group[i].avatar[0] ? img_get(UP.p.group[i].avatar, 64) : NULL;
				if (gp) img_draw_cover(gp, 22, gy, 28, 28, 0.5f);
				else ui_rect(22, gy, 28, 28, col_alpha(COL_WHITE, 0.06f));
				ui_text(58, gy + 1, UP.p.group[i].name, SZ_SMALL, COL_WHITE, AL_LEFT, TOP_W - 160);
				char mt[48];
				snprintf(mt, sizeof mt, UP.p.group[i].members == 1 ? _("%d miembro") : _("%d miembros"), UP.p.group[i].members);
				ui_text(58, gy + 15, mt, SZ_TINY, COL_DIM, AL_LEFT, 0);
				if (UP.p.group[i].primary) ui_text(TOP_W - 22, gy + 8, _("Grupo principal"), SZ_TINY, COL_ACC, AL_RIGHT, 0);
			}
		}
		y += gh + 8;
	}
	// Sus comentarios (los últimos)
	if (UP.com_done && !UP.p.is_private) {
		float ch = 24 + (UP.ncom ? 0 : 20);
		for (int i = 0; i < UP.ncom; i++) {
			int nl = ui_text_lines(UP.com[i].text, SZ_TINY, TOP_W - 82);
			ch += 22 + (nl > 3 ? 3 : nl < 1 ? 1 : nl) * 12 + 6;
		}
		ch += 4;
		if (y < SCR_H && y + ch > 18) {
			fx_glass(12, y, TOP_W - 24, ch, 10, 1, 0);
			snprintf(t, sizeof t, _("COMENTARIOS (%d)"), UP.com_total);
			ui_text(22, y + 6, t, SZ_TINY, COL_ACC, AL_LEFT, 0);
			if (auth_logged()) ui_text(TOP_W - 22, y + 6, _("SELECT: comentar"), SZ_TINY, COL_DIM, AL_RIGHT, 0);
			float cy = y + 24;
			if (!UP.ncom) ui_text(22, cy, _("Todavía no hay comentarios"), SZ_SMALL, COL_DIM, AL_LEFT, 0);
			for (int i = 0; i < UP.ncom; i++) {
				const ProfileComment *c = &UP.com[i];
				const Pic *ap = c->avatar[0] ? img_get(c->avatar, 64) : NULL;
				if (ap) img_draw_cover(ap, 22, cy + 2, 30, 30, 0.5f);
				else ui_rect(22, cy + 2, 30, 30, col_alpha(COL_WHITE, 0.06f));
				char w[40];
				notif_when(c->when, w, sizeof w);
				float aw = ui_text(60, cy, c->author, SZ_SMALL, COL_WHITE, AL_LEFT, 200);
				ui_text(60 + aw + 8, cy + 2, w, SZ_TINY, COL_DIM, AL_LEFT, 0);
				int nl = ui_text_wrap(60, cy + 16, TOP_W - 82, c->text, SZ_TINY, COL_TXT, 3, 12);
				cy += 22 + (nl < 1 ? 1 : nl) * 12 + 6;
			}
		}
		y += ch + 8;
	}
	if (UP.p.nother) {
		snprintf(t, sizeof t, UP.p.nother == 1 ? _("Tiene %d vitrina más que solo se puede ver en Steam (Y)")
			: _("Tiene %d vitrinas más que solo se pueden ver en Steam (Y)"), UP.p.nother);
		ui_text(TOP_W / 2, y + 2, t, SZ_TINY, COL_DIM, AL_CENTER, TOP_W - 24);
		y += 20;
	}
	if (user_loading()) {
		ui_spinner(TOP_W / 2, y + 14, 6, COL_DIM);
		y += 30;
	}
	UP.content = y + UP.scroll - 18;
	ui_scissor_off();
}

static const Box B_UGAME = {12, 120, 296, 30};
static const Box B_UQR = {12, 156, 296, 30};
static const Box B_UADD = {12, 190, 145, 26};      // Añadir amigo / Aceptar
static const Box B_UADDW = {12, 190, 296, 26};
static const Box B_UIGN = {163, 190, 145, 26};     // Ignorar la solicitud

// Qué es para ti: 3 amigo, 2 te ha mandado solicitud, 1 se la has mandado tú, 0 nada, -1 no se sabe
static int user_relation(void)
{
	if (user_is_me() || !UP.id) return -1;
	if (UP.rel_set) return UP.rel;
	for (int i = 0; i < FL.np; i++)
		if (FL.p[i].id == UP.id) return FL.p[i].request ? 2 : 3;
	return FL.loaded ? 0 : -1;
}

static void user_family_poll(void)
{
	// En tu perfil, cuando ya se sabe tu familia (llega con la biblioteca)
	if (user_is_me() && FAM.gid && !UP.fam_req && !UP.fam_done) UP.fam_req = steam_req_family(FAM.gid, auth_token());
	if (UP.fam_req && UP.fam_req->state >= REQ_DONE) {
		bool ok = UP.fam_req->state == REQ_DONE && steam_parse_family(UP.fam_req->data, &UP.fam);
		netlog("familia: %s, %d miembros", ok ? UP.fam.name : "no", UP.fam.n);
		net_release(UP.fam_req);
		UP.fam_req = NULL;
		UP.fam_done = true;
		if (ok) UP.fam_people = steam_req_people(UP.fam.member, UP.fam.n, auth_token());
	}
	if (UP.fam_people && UP.fam_people->state >= REQ_DONE) {
		if (UP.fam_people->state == REQ_DONE) {
			int n = steam_parse_people(UP.fam_people->data, UP.famp, 8);
			UP.nfamp = n < 0 ? 0 : n;
		}
		net_release(UP.fam_people);
		UP.fam_people = NULL;
	}
}

static void user_comments_poll(void)
{
	if (UP.com_req && UP.com_req->state >= REQ_DONE) {
		int n = UP.com_req->state == REQ_DONE ? steam_parse_comments(UP.com_req->data, UP.com, 6, &UP.com_total) : -1;
		netlog("perfil %llu: %d comentarios de %d", (unsigned long long)UP.id, n, UP.com_total);
		UP.ncom = n < 0 ? 0 : n;
		UP.com_done = true;
		net_release(UP.com_req);
		UP.com_req = NULL;
	}
	if (UP.com_post && UP.com_post->state >= REQ_DONE) {
		cJSON *root = UP.com_post->state == REQ_DONE ? cJSON_Parse(UP.com_post->data) : NULL;
		const cJSON *sc = cJSON_GetObjectItemCaseSensitive(root, "success");
		const cJSON *er = cJSON_GetObjectItemCaseSensitive(root, "error");
		bool ok = cJSON_IsTrue(sc);
		char t[160];
		if (ok) snprintf(t, sizeof t, "%s", _("Comentario publicado"));
		else snprintf(t, sizeof t, _("No se pudo publicar el comentario: %s"), cJSON_IsString(er) ? er->valuestring : _("Steam no lo aceptó"));
		toast_icon(t, IC_CHAT);
		cJSON_Delete(root);
		net_release(UP.com_post);
		UP.com_post = NULL;
		if (ok) UP.com_req = steam_req_comments(UP.id, 6);   // con el tuyo arriba
	}
}

static void open_comment_keyboard(void)
{
	SwkbdState kb;
	static char buf[3000];
	buf[0] = 0;
	swkbdInit(&kb, SWKBD_TYPE_NORMAL, 2, 1000);
	swkbdSetHintText(&kb, _("Escribe un comentario"));
	swkbdSetButton(&kb, SWKBD_BUTTON_LEFT, _("Cancelar"), false);
	swkbdSetButton(&kb, SWKBD_BUTTON_RIGHT, _("Publicar"), true);
	swkbdSetFeatures(&kb, SWKBD_DARKEN_TOP_SCREEN | SWKBD_ALLOW_HOME | SWKBD_ALLOW_RESET | SWKBD_ALLOW_POWER | SWKBD_MULTILINE);
	swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	if (swkbdInputText(&kb, buf, sizeof buf) != SWKBD_BUTTON_RIGHT) return;
	char ck[1200];
	if (UP.com_post || !auth_web_cookies("steamcommunity.com", ck, sizeof ck)) return;
	UP.com_post = steam_req_post_comment(UP.id, buf, auth_sessionid(), ck);
	memset(ck, 0, sizeof ck);
}

static void user_friend_poll(void)
{
	Req *r = UP.fr_req;
	if (!r || r->state < REQ_DONE) return;
	bool ok = steam_friend_action_ok(r);
	netlog("amigos: acción %d con %llu -> %s (HTTP %ld)", UP.fr_kind, (unsigned long long)UP.id, ok ? "ok" : "fallo", r->http);
	char name[64], t[160];
	user_name(name, sizeof name);
	if (!ok) snprintf(t, sizeof t, "%s", _("Steam no aceptó la acción. Inténtalo de nuevo"));
	else if (UP.fr_kind == FRIEND_ACCEPT) snprintf(t, sizeof t, _("%s y tú ya sois amigos"), name);
	else if (UP.fr_kind == FRIEND_IGNORE) snprintf(t, sizeof t, _("Has ignorado la solicitud de %s"), name);
	else snprintf(t, sizeof t, _("Solicitud de amistad enviada a %s"), name);
	toast_icon(t, IC_USERS);
	if (ok) {
		UP.rel_set = true;
		UP.rel = UP.fr_kind == FRIEND_ACCEPT ? 3 : UP.fr_kind == FRIEND_IGNORE ? 0 : 1;
		if (UP.fr_kind == FRIEND_ACCEPT) UP.person.request = false;   // ya es tu amigo
		if (UP.fr_kind != FRIEND_ADD) {
			// La lista de amigos se vuelve a pedir ya (con el nuevo amigo, o sin la solicitud)
			flist_reset();
			FL.at = osGetTime();
			FL.req = steam_req_friends(auth_info()->steamid, auth_token(), false);
		}
	}
	net_release(r);
	UP.fr_req = NULL;
}

static void user_friend_action(int kind)
{
	char ck[1200];
	if (UP.fr_req || !auth_web_cookies("steamcommunity.com", ck, sizeof ck)) return;
	UP.fr_kind = kind;
	UP.fr_req = steam_req_friend_action(kind, UP.id, auth_sessionid(), ck);
	memset(ck, 0, sizeof ck);
}

static bool user_playing(void)
{
	return UP.has_person && UP.person.state > 0 && UP.person.game;
}

static void draw_user_bottom(void)
{
	fx_bar(0, 0, BOT_W, LIST_Y, true);
	ui_text(10, 4, "‹", SZ_LARGE, COL_ACC, AL_LEFT, 0);
	ui_icon(IC_USER, 28, 6, 12, COL_ACC);
	ui_text(46, 5, user_is_me() ? _("Tu perfil") : _("Perfil"), SZ_SMALL, COL_WHITE, AL_LEFT, BOT_W - 60);
	char name[64], st[112];
	u32 col;
	user_name(name, sizeof name);
	ui_text(BOT_W / 2, 40, name[0] ? name : "…", SZ_LARGE, COL_WHITE, AL_CENTER, 300);
	user_status(st, sizeof st, &col);
	if (st[0]) ui_text(BOT_W / 2, 64, st, SZ_SMALL, col, AL_CENTER, 300);
	if (!user_is_me() && UP.id) {
		char code[48];
		snprintf(code, sizeof code, _("Código de amigo: %lu"), (unsigned long)(UP.id - 76561197960265728ULL));
		ui_text(BOT_W / 2, 84, code, SZ_TINY, COL_DIM, AL_CENTER, 300);
	}
	if (user_playing())
		draw_button_k(&B_UGAME, "X", false, IC_PAD, _("Ver el juego"), C2D_Color32(0x75, 0xb0, 0x22, 0xff), C2D_Color32(0x58, 0x8a, 0x1b, 0xff), COL_WHITE, 1, S.pressed == BT_UGAME);
	draw_button_k(&B_UQR, "Y", false, IC_PHONE, UP.qr ? _("Volver al perfil") : _("Ver en Steam"), COL_BTN2, COL_BTN2, COL_ACC, 1, S.pressed == BT_UQR);
	int rel = user_relation();
	bool busy = UP.fr_req != NULL;
	u32 g1 = C2D_Color32(0x75, 0xb0, 0x22, 0xff), g2 = C2D_Color32(0x58, 0x8a, 0x1b, 0xff);
	if (rel == 2) {
		draw_button_k(&B_UADD, "A", false, IC_CHECK, busy && UP.fr_kind == FRIEND_ACCEPT ? _("Aceptando…") : _("Aceptar"), g1, g2, COL_WHITE, 1, S.pressed == BT_UADD);
		draw_button_k(&B_UIGN, "SELECT", false, IC_HIDE, busy && UP.fr_kind == FRIEND_IGNORE ? _("Ignorando…") : _("Ignorar"), COL_BTN2, COL_BTN2, COL_DIM, 1, S.pressed == BT_UIGN);
	} else if (rel == 0) {
		draw_button_k(&B_UADDW, "A", false, IC_USERS, busy ? _("Enviando…") : _("Añadir amigo"), g1, g2, COL_WHITE, 1, S.pressed == BT_UADDW);
	} else if (rel == 1) {
		draw_button_k(&B_UADDW, NULL, false, IC_CHECK, _("Solicitud enviada"), COL_BTN2, COL_BTN2, COL_DIM, 0.6f, false);
	}
	static const char *h[] = {"↑↓", "Desplazar", "Y", "Ver en Steam", "B", "Volver"};
	draw_hints(h, 3);
}

// El estado del amigo cuyo perfil tienes abierto, al día (lo pide la lista de amigos)
static void user_person_live(const SteamPerson *p)
{
	if (!UP.has_person || UP.person.id != p->id || UP.person.request) return;
	UP.person = *p;
}

static void open_user(u64 id, const SteamPerson *person)
{
	UP.back = S.scene == SC_USER ? UP.back : S.scene;
	user_load(id);
	UP.has_person = person != NULL;
	if (person) UP.person = *person;
	UP.scroll = UP.scroll_t = 0;
	UP.qr = false;
	go_scene(SC_USER, 1);
}

static void input_user(u32 kd, u32 kh, u32 ku, touchPosition *tp)
{
	if ((kd & KEY_B) || ((kd & KEY_TOUCH) && tp->py < LIST_Y && tp->px < 120)) {
		if (UP.qr) UP.qr = false;
		else go_scene(UP.back, -1);
		return;
	}
	int ids[5] = {BT_UQR}, nids = 1, rel = user_relation();
	if (user_playing()) ids[nids++] = BT_UGAME;
	if (rel == 2) ids[nids++] = BT_UADD, ids[nids++] = BT_UIGN;
	if (rel == 0) ids[nids++] = BT_UADDW;
	int b = press_update(kd, kh, ku, tp, ids, nids);
	if (kd & KEY_Y) b = BT_UQR;
	if ((kd & KEY_X) && user_playing()) b = BT_UGAME;
	if ((kd & KEY_A) && rel == 2) b = BT_UADD;
	if ((kd & KEY_A) && rel == 0) b = BT_UADDW;
	if ((kd & KEY_SELECT) && rel == 2) b = BT_UIGN;
	else if ((kd & KEY_SELECT) && auth_logged()) UP.want_comment = true;   // escribir un comentario
	if (b == BT_UADD) user_friend_action(FRIEND_ACCEPT);
	if (b == BT_UIGN) user_friend_action(FRIEND_IGNORE);
	if (b == BT_UADDW) user_friend_action(FRIEND_ADD);
	if (b == BT_UQR) UP.qr = !UP.qr;
	if (b == BT_UGAME) {
		open_game(UP.person.game, true);
		return;
	}
	float max = UP.content - (SCR_H - 18);
	if (max < 0) max = 0;
	if (kh & KEY_DOWN) UP.scroll_t += 260 * g_dt;
	if (kh & KEY_UP) UP.scroll_t -= 260 * g_dt;
	UP.scroll_t = clampf(UP.scroll_t, 0, max);
}

// ---------------------------------------------------------------------------- Elegir cuenta

static int acc_sel;
static u64 acc_forget_at;    // Y pulsado una vez: se pide otra para quitar la cuenta
static int acc_forget_i = -1;

static void open_accounts(void)
{
	acc_sel = 0;
	acc_forget_i = -1;
	go_scene(SC_ACCOUNTS, 1);
}

static void draw_accounts_top(void)
{
	AuthAccount a[AUTH_MAX_ACCOUNTS];
	int n = auth_accounts(a);
	draw_logo(TOP_W / 2, 62, 64, 1);
	ui_text(TOP_W / 2, 104, _("Elige una cuenta de Steam"), SZ_LARGE, COL_WHITE, AL_CENTER, 380);
	if (acc_sel < n) {
		char t[160];
		snprintf(t, sizeof t, _("Entrarás como %s"), a[acc_sel].persona[0] ? a[acc_sel].persona : a[acc_sel].account);
		ui_text(TOP_W / 2, 132, t, SZ_BODY, COL_ACC, AL_CENTER, 380);
	} else if (n < AUTH_MAX_ACCOUNTS) {
		ui_text(TOP_W / 2, 132, _("Inicia sesión con otra cuenta escaneando un QR con la app de Steam"), SZ_SMALL, COL_ACC, AL_CENTER, 380);
	} else {
		ui_text(TOP_W / 2, 132, _("Seguirás en la tienda sin iniciar sesión"), SZ_SMALL, COL_DIM, AL_CENTER, 380);
	}
	char m[96];
	snprintf(m, sizeof m, _("Puedes guardar hasta %d cuentas en la consola"), AUTH_MAX_ACCOUNTS);
	ui_text(TOP_W / 2, 196, m, SZ_SMALL, COL_DIM, AL_CENTER, 380);
}

// Las cuentas, «Añadir otra cuenta» (si cabe) y «Seguir sin iniciar sesión»
static int accounts_rows(int n)
{
	return n + (n < AUTH_MAX_ACCOUNTS ? 1 : 0) + 1;
}

static void draw_accounts_bottom(void)
{
	AuthAccount a[AUTH_MAX_ACCOUNTS];
	int n = auth_accounts(a), rows = accounts_rows(n);
	if (acc_sel >= rows) acc_sel = rows - 1;
	const float RH = 44, y0 = 18;
	for (int i = 0; i < rows; i++) {
		float y = y0 + i * (RH + 6);
		bool on = i == acc_sel;
		FxButton fb = {0, 1, false, on};
		fx_button(12, y, BOT_W - 24, RH, 12, &fb);
		if (i < n) {
			const Pic *p = a[i].avatar[0] ? img_get(a[i].avatar, 64) : NULL;
			ui_rect(21, y + 5, 34, 34, col_alpha(COL_WHITE, 0.25f));
			if (p) img_draw_cover(p, 22, y + 6, 32, 32, 0.5f);
			else ui_icon(IC_USER, 30, y + 14, 16, COL_DIM);
			ui_text(64, y + 7, a[i].persona[0] ? a[i].persona : a[i].account, SZ_BODY, on ? COL_WHITE : COL_TXT, AL_LEFT, BOT_W - 90);
			ui_text(64, y + 24, a[i].account, SZ_TINY, COL_DIM, AL_LEFT, BOT_W - 90);
			if (acc_forget_i == i && osGetTime() - acc_forget_at < 3000)
				ui_text(BOT_W - 24, y + 15, _("Pulsa Y otra vez"), SZ_TINY, C2D_Color32(0xff, 0x9a, 0x8c, 0xff), AL_RIGHT, 0);
		} else if (i == n && n < AUTH_MAX_ACCOUNTS) {
			ui_icon(IC_PHONE, 32, y + 16, 12, COL_ACC);
			ui_text(64, y + 14, _("Añadir otra cuenta"), SZ_BODY, on ? COL_WHITE : COL_ACC, AL_LEFT, BOT_W - 90);
		} else {
			ui_icon(IC_HOME, 32, y + 16, 12, COL_DIM);
			ui_text(64, y + 14, _("Seguir sin iniciar sesión"), SZ_BODY, on ? COL_WHITE : COL_TXT, AL_LEFT, BOT_W - 90);
		}
	}
	static const char *h[] = {"A", "Entrar", "↑↓", "Elegir", "Y", "Quitar cuenta"};
	static const char *h2[] = {"A", "Aceptar", "↑↓", "Elegir"};
	if (acc_sel < n) draw_hints(h, 3);
	else draw_hints(h2, 2);
}

static void input_accounts(u32 kd, u32 kh, touchPosition *tp)
{
	AuthAccount a[AUTH_MAX_ACCOUNTS];
	int n = auth_accounts(a), rows = accounts_rows(n);
	if (key_repeat(kd, kh, KEY_DOWN) && acc_sel + 1 < rows) acc_sel++;
	if (key_repeat(kd, kh, KEY_UP) && acc_sel > 0) acc_sel--;
	const float RH = 44, y0 = 18;
	if ((kd & KEY_TOUCH) && tp->py >= y0) {
		int i = (int)((tp->py - y0) / (RH + 6));
		if (i >= 0 && i < rows) {
			if (i == acc_sel) kd |= KEY_A;
			acc_sel = i;
		}
	}
	if ((kd & KEY_Y) && acc_sel < n) {
		// Quitar una cuenta de la consola pide pulsarlo dos veces
		if (acc_forget_i == acc_sel && osGetTime() - acc_forget_at < 3000) {
			char t[128];
			snprintf(t, sizeof t, _("Se ha quitado %s de la consola"), a[acc_sel].persona[0] ? a[acc_sel].persona : a[acc_sel].account);
			auth_forget_account(acc_sel);
			acc_forget_i = -1;
			toast_icon(t, IC_USER);
			if (acc_sel >= accounts_rows(auth_accounts(NULL))) acc_sel = 0;
		} else {
			acc_forget_i = acc_sel;
			acc_forget_at = osGetTime();
		}
		return;
	}
	if (kd & KEY_A) {
		if (acc_sel < n) {
			do_switch_cleanup();
			auth_use(acc_sel);
			go_scene(SC_STORE, 1);
		} else if (acc_sel == n && n < AUTH_MAX_ACCOUNTS) {
			do_switch_cleanup();
			open_login();
		} else {
			go_scene(SC_STORE, 1);
		}
	}
}

// ---------------------------------------------------------------------------- Configuración

enum { ST_LANG, ST_REQS, ST_SOUND, ST_CACHE, ST_ACCOUNT, ST_HISTORY, ST_ABOUT, ST_COUNT };
static int set_sel;
static Scene set_back;

static void open_settings(void)
{
	set_back = S.scene == SC_SETTINGS ? SC_STORE : S.scene;
	set_sel = 0;
	go_scene(SC_SETTINGS, 1);
}

static const char *const lang_codes[] = {"sys", "es", "en", "ja"};
static const char *const lang_names[] = {"Idioma de la consola", "Español", "English", "日本語"};

static int lang_index(void)
{
	for (int i = 0; i < 4; i++)
		if (!strcmp(g_cfg.lang, lang_codes[i])) return i;
	return 0;
}

// El idioma se aplica al momento: los textos de la app y, al volver a pedirlas, las listas de la tienda
static void set_lang(int i)
{
	snprintf(g_cfg.lang, sizeof g_cfg.lang, "%s", lang_codes[(i + 4) % 4]);
	config_save();
	Lang l = !strcmp(g_cfg.lang, "es") ? LANG_ES : !strcmp(g_cfg.lang, "en") ? LANG_EN : !strcmp(g_cfg.lang, "ja") ? LANG_JA : i18n_system();
	i18n_set(l);
	const char *cc = g_cfg.country[0] ? g_cfg.country : "US";
	steam_set_context(i18n_steam_lang(cc), cc);
	store_tabs_reload();
}

static void settings_value(int i, char *out, size_t n)
{
	switch (i) {
	case ST_LANG: snprintf(out, n, "%s", _(lang_names[lang_index()])); break;
	case ST_REQS: snprintf(out, n, "%s", g_cfg.reqs_mac ? "macOS" : "Windows"); break;
	case ST_SOUND: snprintf(out, n, "%s", g_cfg.quiet ? _("Desactivado") : _("Activado")); break;
	case ST_CACHE: {
		s64 b = net_cache_bytes();
		if (b < 0) snprintf(out, n, "%s", _("Calculando…"));
		else if (b < (1 << 20)) snprintf(out, n, "%lld KB", (long long)(b >> 10));
		else snprintf(out, n, "%.1f MB", b / 1048576.0);
		break;
	}
	default: out[0] = 0;
	}
}

static void draw_settings_top(void)
{
	static const char *title[ST_COUNT] = {"Idioma", "Requisitos del sistema", "Sonido de la pantalla de carga", "Caché de imágenes", "Privacidad y perfil", "Historial de compras", "Acerca de Steam3DS"};
	static const char *desc[ST_COUNT] = {
		"El idioma de la app y de la tienda. Con «Idioma de la consola» se usa el que tenga configurado tu Nintendo 3DS.",
		"Qué requisitos se muestran en la ficha de cada juego. También puedes cambiarlo en el apartado Requisitos de la ficha.",
		"El golpe de la válvula, el bombillo, el óxido y el vapor al abrir la app. Se aplica la próxima vez que la abras.",
		"Las imágenes de la tienda se guardan en la tarjeta SD para que aparezcan al momento la próxima vez. Ocupa como mucho 64 MB y lo más antiguo se borra solo. Vaciarla no borra nada de tu cuenta.",
		"Quién puede ver tu perfil, tus juegos, tu lista de amigos y tu inventario, quién puede comentar, y el tema de tu perfil. Los cambios se guardan en tu cuenta de Steam.",
		"Tus compras, regalos y fondos añadidos al monedero en Steam, con la fecha y el importe.",
		"La versión de Steam3DS, los enlaces del proyecto, las políticas de Steam, los créditos y el aviso legal.",
	};
	const float X = 16, W = TOP_W - 32;
	ui_text(X, 28, _(title[set_sel]), SZ_LARGE, COL_WHITE, AL_LEFT, W);
	ui_rrect(X, 47, ui_text_w(_(title[set_sel]), SZ_LARGE), 2, 1, col_alpha(COL_ACC, 0.8f));
	ui_text_wrap(X, 62, W, _(desc[set_sel]), SZ_SMALL, COL_TXT, 6, 15);
	char v[64];
	settings_value(set_sel, v, sizeof v);
	if (v[0]) {
		fx_glass(X, 170, W, 36, 12, 1, 0);
		ui_text(X + 14, 179, v, SZ_BODY, COL_ACC, AL_LEFT, W - 28);
	}
}

static void draw_settings_bottom(void)
{
	fx_bar(0, 0, BOT_W, LIST_Y, true);
	ui_text(10, 4, "‹", SZ_LARGE, COL_ACC, AL_LEFT, 0);
	ui_icon(IC_WRENCH, 28, 6, 12, COL_ACC);
	ui_text(46, 5, _("Configuración"), SZ_SMALL, COL_WHITE, AL_LEFT, BOT_W - 56);
	static const Icon ic[ST_COUNT] = {IC_CHAT, IC_PC, IC_BELL, IC_IMAGE, IC_LOCK, IC_DOC, IC_INFO};
	static const char *label[ST_COUNT] = {"Idioma", "Requisitos del sistema", "Sonido de la pantalla de carga", "Caché de imágenes", "Privacidad y perfil", "Historial de compras", "Acerca de Steam3DS"};
	const float RH = 27;
	for (int i = 0; i < ST_COUNT; i++) {
		float y = LIST_Y + 4 + i * RH;
		bool on = i == set_sel;
		if (on) {
			float br = 0.075f + 0.035f * sinf(g_time * 2.4f);
			ui_rrect(3, y + 1, BOT_W - 8, RH - 2, 6, col_alpha(COL_WHITE, br));
			ui_rrect(3, y + 6, 2, RH - 12, 1, col_alpha(COL_ACC, 0.8f));
		}
		ui_icon(ic[i], 14, y + 8, 12, on ? COL_ACC : COL_DIM);
		ui_text(36, y + 6, _(label[i]), SZ_BODY - 0.02f, on ? COL_WHITE : COL_TXT, AL_LEFT, 170);
		char v[64];
		settings_value(i, v, sizeof v);
		if (i == ST_ABOUT || i == ST_ACCOUNT || i == ST_HISTORY) ui_text(BOT_W - 20, y + 4, "›", SZ_LARGE, COL_ACC, AL_LEFT, 0);
		else ui_text(BOT_W - 14, y + 7, v, SZ_SMALL, COL_ACC, AL_RIGHT, 110);
		if (i < ST_COUNT - 1) ui_hline(36, y + RH - 1, BOT_W - 44, C2D_Color32(0xff, 0xff, 0xff, 0x0c));
	}
	static const char *hv[] = {"← →", "Cambiar", "B", "Volver"};
	static const char *hc[] = {"A", "Vaciar", "↑↓", "Elegir", "B", "Volver"};
	static const char *ha[] = {"A", "Abrir", "↑↓", "Elegir", "B", "Volver"};
	bool v = set_sel != ST_CACHE && set_sel != ST_ABOUT && set_sel != ST_ACCOUNT && set_sel != ST_HISTORY;
	draw_hints(set_sel == ST_CACHE ? hc : v ? hv : ha, v ? 2 : 3);
}

static void open_accset(void);
static void open_hist(void);

static void input_settings(u32 kd, u32 kh, touchPosition *tp)
{
	if ((kd & KEY_B) || ((kd & KEY_TOUCH) && tp->py < LIST_Y && tp->px < 120)) {
		go_scene(set_back == SC_SETTINGS ? SC_STORE : set_back, -1);
		return;
	}
	if (key_repeat(kd, kh, KEY_DOWN)) set_sel = (set_sel + 1) % ST_COUNT;
	if (key_repeat(kd, kh, KEY_UP)) set_sel = (set_sel + ST_COUNT - 1) % ST_COUNT;
	if ((kd & KEY_TOUCH) && tp->py >= LIST_Y + 4) {
		int i = (int)((tp->py - LIST_Y - 4) / 27);
		if (i >= 0 && i < ST_COUNT) {
			if (i == set_sel) kd |= KEY_A;
			set_sel = i;
		}
	}
	int d = (kd & KEY_LEFT) ? -1 : (kd & (KEY_RIGHT | KEY_A)) ? 1 : 0;
	if (!d) return;
	switch (set_sel) {
	case ST_LANG:
		set_lang(lang_index() + d);
		break;
	case ST_REQS:
		g_cfg.reqs_mac = !g_cfg.reqs_mac;
		config_save();
		break;
	case ST_SOUND:
		g_cfg.quiet = !g_cfg.quiet;
		config_save();
		break;
	case ST_CACHE:
		if (kd & KEY_A) {
			net_cache_clear();
			toast_icon(_("Caché de imágenes vaciada"), IC_IMAGE);
		}
		break;
	case ST_ABOUT:
		if (kd & KEY_A) open_about();
		break;
	case ST_ACCOUNT:
		if (!(kd & KEY_A)) break;
		if (auth_logged()) open_accset();
		else toast(_("Disponible al iniciar sesión"));
		break;
	case ST_HISTORY:
		if (!(kd & KEY_A)) break;
		if (auth_logged()) open_hist();
		else toast(_("Disponible al iniciar sesión"));
		break;
	}
}


// ---------------------------------------------------------------------------- Privacidad y perfil

enum { AS_PROFILE, AS_GAMES, AS_PLAYTIME, AS_FRIENDS, AS_INVENTORY, AS_COMMENTS, AS_THEME, AS_COUNT };
#define AS_THEMES 12
static struct {
	SteamPrivacy pv;
	Req *req, *save, *themes_req, *theme_save;
	bool loaded, failed, dirty, saved;
	char themes[AS_THEMES][24];
	int nthemes, sel;
	bool themes_loaded;
	char theme_want[24];
} AS;

static void open_accset(void)
{
	Req **all[] = {&AS.req, &AS.save, &AS.themes_req, &AS.theme_save};
	for (size_t i = 0; i < sizeof all / sizeof *all; i++)
		if (*all[i]) net_release(*all[i]), *all[i] = NULL;
	steam_privacy_free(&AS.pv);
	AS.loaded = AS.failed = AS.dirty = AS.saved = AS.themes_loaded = false;
	AS.nthemes = AS.sel = 0;
	char ck[1200];
	if (auth_web_cookies("steamcommunity.com", ck, sizeof ck)) AS.req = steam_req_privacy(auth_info()->steamid, ck);
	else AS.failed = true;
	memset(ck, 0, sizeof ck);
	AS.themes_req = steam_req_themes(auth_token());
	user_load(auth_info()->steamid);   // el tema que llevas puesto
	go_scene(SC_ACCSET, 1);
}

// Comentarios a la escala de la privacidad (3 todos, 2 amigos, 1 solo tú) y al revés
static int comments_level(int c) { return c == 1 ? 3 : c == 0 ? 2 : 1; }
static int comments_from(int lv) { return lv == 3 ? 1 : lv == 2 ? 0 : 2; }

// Como en Steam: nada puede ser más público que el perfil
static void privacy_clamp(SteamPrivacy *p)
{
	if (p->games > p->profile) p->games = p->profile;
	if (p->friends > p->profile) p->friends = p->profile;
	if (p->inventory > p->profile) p->inventory = p->profile;
	if (comments_level(p->comments) > p->profile) p->comments = comments_from(p->profile);
}

static void accset_poll(void)
{
	if (AS.req && AS.req->state >= REQ_DONE) {
		bool ok = AS.req->state == REQ_DONE && steam_parse_privacy(AS.req->data, &AS.pv);
		netlog("privacidad: %s (HTTP %ld)", ok ? "leída" : "no se pudo leer", AS.req->http);
		AS.loaded = ok;
		AS.failed = !ok;
		net_release(AS.req);
		AS.req = NULL;
	}
	if (AS.themes_req && AS.themes_req->state >= REQ_DONE) {
		int n = AS.themes_req->state == REQ_DONE ? steam_parse_themes(AS.themes_req->data, AS.themes, AS_THEMES) : -1;
		netlog("temas del perfil: %d", n);
		AS.nthemes = n < 0 ? 0 : n;
		AS.themes_loaded = true;
		net_release(AS.themes_req);
		AS.themes_req = NULL;
	}
	// Se guarda al momento, como en la web; si cambias algo mientras se guarda, se manda después
	if (AS.save && AS.save->state >= REQ_DONE) {
		cJSON *root = AS.save->state == REQ_DONE ? cJSON_Parse(AS.save->data) : NULL;
		const cJSON *sc = cJSON_GetObjectItemCaseSensitive(root, "success");
		bool ok = cJSON_IsNumber(sc) ? sc->valueint == 1 : cJSON_IsTrue(sc);
		netlog("privacidad: guardar -> %s (HTTP %ld)", ok ? "ok" : "fallo", AS.save->http);
		cJSON_Delete(root);
		net_release(AS.save);
		AS.save = NULL;
		AS.saved = ok;
		if (!ok) {
			toast_icon(_("No se pudo guardar tu privacidad. Se vuelve a cargar la de tu cuenta"), IC_LOCK);
			AS.dirty = false;
			char ck[1200];
			if (auth_web_cookies("steamcommunity.com", ck, sizeof ck)) AS.req = steam_req_privacy(auth_info()->steamid, ck);
			memset(ck, 0, sizeof ck);
			AS.loaded = false;
		}
	}
	if (AS.dirty && !AS.save && AS.loaded) {
		char ck[1200];
		if (auth_web_cookies("steamcommunity.com", ck, sizeof ck))
			AS.save = steam_req_set_privacy(auth_info()->steamid, &AS.pv, auth_sessionid(), ck);
		memset(ck, 0, sizeof ck);
		AS.dirty = false;
		AS.saved = false;
	}
	if (AS.theme_save && AS.theme_save->state >= REQ_DONE) {
		bool ok = AS.theme_save->state == REQ_DONE && (AS.theme_save->eresult == 1 || AS.theme_save->eresult == 0) && AS.theme_save->http == 200;
		netlog("tema del perfil: %s -> %s", AS.theme_want, ok ? "ok" : "fallo");
		if (ok && user_loaded_is(auth_info()->steamid)) snprintf(UP.p.theme, sizeof UP.p.theme, "%s", AS.theme_want);
		toast_icon(ok ? _("Tema del perfil cambiado") : _("No se pudo cambiar el tema del perfil"), IC_USER);
		net_release(AS.theme_save);
		AS.theme_save = NULL;
	}
}

static const char *privacy_name(int v)
{
	return v == 3 ? _("Público") : v == 2 ? _("Solo amigos") : _("Privado");
}

static const char *theme_name(const char *id)
{
	static const struct { const char *id, *name; } names[] = {{"Summer", "Verano"}, {"Midnight", "Medianoche"}, {"Steel", "Acero"},
		{"Cosmic", "Cósmico"}, {"DarkMode", "Modo oscuro"}};
	if (!id || !*id) return _("Predeterminado");
	for (size_t i = 0; i < sizeof names / sizeof *names; i++)
		if (!strcmp(id, names[i].id)) return _(names[i].name);
	return id;
}

static const char *current_theme(void)
{
	return AS.theme_save ? AS.theme_want : user_loaded_is(auth_info()->steamid) ? UP.p.theme : "";
}

static void accset_value(int i, char *out, size_t n)
{
	const SteamPrivacy *p = &AS.pv;
	switch (i) {
	case AS_PROFILE: snprintf(out, n, "%s", privacy_name(p->profile)); break;
	case AS_GAMES: snprintf(out, n, "%s", privacy_name(p->games)); break;
	case AS_PLAYTIME: snprintf(out, n, "%s", p->playtime_private ? _("Sí") : _("No")); break;
	case AS_FRIENDS: snprintf(out, n, "%s", privacy_name(p->friends)); break;
	case AS_INVENTORY: snprintf(out, n, "%s", privacy_name(p->inventory)); break;
	case AS_COMMENTS: snprintf(out, n, "%s", p->comments == 1 ? _("Todos") : p->comments == 0 ? _("Solo amigos") : _("Privado")); break;
	case AS_THEME: snprintf(out, n, "%s", theme_name(current_theme())); break;
	}
}

static const char *const as_label[AS_COUNT] = {"Mi perfil", "Detalles de juego", "Tiempo de juego siempre privado", "Lista de amigos", "Inventario",
	"Comentarios en el perfil", "Tema del perfil"};
static const char *const as_desc[AS_COUNT] = {
	"Quién puede ver tu perfil: el nivel, las vitrinas, el «sobre mí» y lo demás. Lo que elijas aquí es el máximo para todo lo de abajo.",
	"Quién puede ver los juegos que tienes, los que tienes en tu lista de deseos, tus logros y tu tiempo de juego. Afecta también a lo que ven tus amigos en su actividad.",
	"Aunque tus juegos sean visibles, tu tiempo de juego total no se mostrará a nadie más que a ti.",
	"Quién puede ver tu lista de amigos en tu perfil.",
	"Quién puede ver los objetos de tu inventario de Steam (cromos, fondos, objetos de juegos…).",
	"Quién puede escribir comentarios en tu perfil.",
	"El tema de colores de tu perfil. Solo aparecen los que tienes; se consiguen en la tienda de puntos de Steam.",
};

static void draw_accset_top(void)
{
	const float X = 16, W = TOP_W - 32;
	if (!AS.loaded) {
		if (AS.failed) {
			ui_icon(IC_LOCK, TOP_W / 2 - 12, 80, 24, COL_DIM);
			ui_text(TOP_W / 2, 116, _("No se pudo cargar tu privacidad"), SZ_LARGE, COL_TXT, AL_CENTER, 380);
			ui_text_wrap(X, 142, W, _("No se cambiará nada hasta poder leer la que tienes ahora. Vuelve a entrar en un momento."), SZ_SMALL, COL_DIM, 3, 15);
		} else {
			draw_net_wait(TOP_W / 2, 120, _("Cargando tu privacidad…"));
		}
		return;
	}
	ui_text(X, 28, _(as_label[AS.sel]), SZ_LARGE, COL_WHITE, AL_LEFT, W);
	ui_rrect(X, 47, ui_text_w(_(as_label[AS.sel]), SZ_LARGE), 2, 1, col_alpha(COL_ACC, 0.8f));
	ui_text_wrap(X, 62, W, _(as_desc[AS.sel]), SZ_SMALL, COL_TXT, 6, 15);
	if (AS.sel == AS_THEME && AS.themes_loaded && !AS.nthemes)
		ui_text_wrap(X, 140, W, _("Todavía no tienes ningún tema."), SZ_SMALL, COL_DIM, 2, 15);
	const char *st = AS.save || AS.dirty || AS.theme_save ? _("Guardando en tu cuenta de Steam…") : AS.saved ? _("Guardado en tu cuenta de Steam") : NULL;
	if (st) {
		ui_icon(AS.saved && !AS.save ? IC_CHECK : IC_CLOUD, X, 207, 12, AS.saved && !AS.save ? COL_LIME : COL_DIM);
		ui_text(X + 18, 206, st, SZ_SMALL, AS.saved && !AS.save ? COL_LIME : COL_DIM, AL_LEFT, W - 18);
	}
}

static void draw_accset_bottom(void)
{
	fx_bar(0, 0, BOT_W, LIST_Y, true);
	ui_text(10, 4, "‹", SZ_LARGE, COL_ACC, AL_LEFT, 0);
	ui_icon(IC_LOCK, 28, 6, 12, COL_ACC);
	ui_text(46, 5, _("Privacidad y perfil"), SZ_SMALL, COL_WHITE, AL_LEFT, BOT_W - 56);
	if (!AS.loaded) {
		if (!AS.failed) ui_spinner(BOT_W / 2, 110, 7, COL_DIM);
		static const char *h[] = {"B", "Volver"};
		draw_hints(h, 1);
		return;
	}
	const float RH = 27;
	for (int i = 0; i < AS_COUNT; i++) {
		float y = LIST_Y + 3 + i * RH;
		bool on = i == AS.sel;
		bool off = i == AS_PLAYTIME && AS.pv.games == 1;   // con los juegos privados no cambia nada
		if (on) {
			float br = 0.075f + 0.035f * sinf(g_time * 2.4f);
			ui_rrect(3, y + 1, BOT_W - 8, RH - 2, 6, col_alpha(COL_WHITE, br));
			ui_rrect(3, y + 5, 2, RH - 10, 1, col_alpha(COL_ACC, 0.8f));
		}
		ui_text(14, y + 6, _(as_label[i]), SZ_SMALL, off ? COL_DIM : on ? COL_WHITE : COL_TXT, AL_LEFT, 190);
		char v[48];
		accset_value(i, v, sizeof v);
		float vw = ui_text_w(v, SZ_SMALL);
		ui_text(BOT_W - 26, y + 6, v, SZ_SMALL, off ? COL_DIM : COL_ACC, AL_RIGHT, 100);
		if (on && !off) {
			// Flechas a los lados del valor: se cambia con ← →
			ui_text(BOT_W - 30 - vw - 10, y + 4, "‹", SZ_BODY, COL_DIM, AL_LEFT, 0);
			ui_text(BOT_W - 20, y + 4, "›", SZ_BODY, COL_DIM, AL_LEFT, 0);
		}
		if (i < AS_COUNT - 1) ui_hline(14, y + RH - 1, BOT_W - 28, C2D_Color32(0xff, 0xff, 0xff, 0x0c));
	}
	static const char *h[] = {"← →", "Cambiar", "B", "Volver"};
	draw_hints(h, 2);
}

// Siguiente valor de privacidad hacia más público (d > 0) o más privado, sin pasar del máximo
static int privacy_step(int v, int d, int max)
{
	v += d;
	if (v < 1) v = 1;
	if (v > max) v = max;
	return v;
}

static void input_accset(u32 kd, u32 kh, touchPosition *tp)
{
	if ((kd & KEY_B) || ((kd & KEY_TOUCH) && tp->py < LIST_Y && tp->px < 120)) {
		go_scene(SC_SETTINGS, -1);
		return;
	}
	if (!AS.loaded) return;
	if (key_repeat(kd, kh, KEY_DOWN)) AS.sel = (AS.sel + 1) % AS_COUNT;
	if (key_repeat(kd, kh, KEY_UP)) AS.sel = (AS.sel + AS_COUNT - 1) % AS_COUNT;
	int d = (kd & KEY_LEFT) ? -1 : (kd & (KEY_RIGHT | KEY_A)) ? 1 : 0;
	if ((kd & KEY_TOUCH) && tp->py >= LIST_Y + 3) {
		int i = (int)((tp->py - LIST_Y - 3) / 27);
		if (i >= 0 && i < AS_COUNT) {
			if (i == AS.sel) d = tp->px < BOT_W / 2 ? -1 : 1;
			AS.sel = i;
		}
	}
	if (!d) return;
	SteamPrivacy *p = &AS.pv;
	SteamPrivacy before = *p;
	// En la lista, de izquierda a derecha: Privado, Solo amigos, Público (→ abre más)
	switch (AS.sel) {
	case AS_PROFILE: p->profile = privacy_step(p->profile, d, 3); break;
	case AS_GAMES: p->games = privacy_step(p->games, d, p->profile); break;
	case AS_PLAYTIME:
		if (p->games == 1) return;
		p->playtime_private = !p->playtime_private;
		break;
	case AS_FRIENDS: p->friends = privacy_step(p->friends, d, p->profile); break;
	case AS_INVENTORY: p->inventory = privacy_step(p->inventory, d, p->profile); break;
	case AS_COMMENTS: p->comments = comments_from(privacy_step(comments_level(p->comments), d, p->profile)); break;
	case AS_THEME: {
		if (!AS.themes_loaded || AS.theme_save) return;
		// «Predeterminado» y luego los tuyos
		const char *cur = current_theme();
		int idx = 0;
		for (int i = 0; i < AS.nthemes; i++)
			if (!strcmp(AS.themes[i], cur)) idx = i + 1;
		int total = AS.nthemes + 1;
		if (total < 2) return;
		idx = (idx + d + total) % total;
		snprintf(AS.theme_want, sizeof AS.theme_want, "%s", idx ? AS.themes[idx - 1] : "");
		AS.theme_save = steam_req_set_theme(AS.theme_want, auth_token());
		return;
	}
	}
	privacy_clamp(p);
	if (memcmp(&before, p, offsetof(SteamPrivacy, raw))) AS.dirty = true;
}

static void menu_choose(int item)
{
	S.menu_open = false;
	switch (item) {
	case MI_LOGIN:
		// Con cuentas guardadas, primero se elige (o se añade otra)
		if (auth_accounts(NULL)) open_accounts();
		else open_login();
		break;
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
		user_load(auth_info()->steamid);   // tu fondo y tu marco
		if (!XP.req) XP.req = steam_req_badges(auth_info()->steamid, auth_token());
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
	case MI_NOTIF: open_notifs(); break;
	case MI_ABOUT: open_about(); break;
	case MI_SETTINGS: open_settings(); break;
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
		[MI_NOTIF] = {IC_BELL, "Notificaciones"},
		[MI_LIBRARY] = {IC_PAD, "Biblioteca"},
		[MI_WISHLIST] = {IC_HEART, "Lista de deseos"},
		[MI_CART] = {IC_CART, "Carrito"},
		[MI_REQS] = {IC_WRENCH, "Requisitos"},
		[MI_LOGOUT] = {IC_LOCK, "Cerrar sesión"},
		[MI_ABOUT] = {IC_INFO, "Acerca de Steam3DS"},
		[MI_SETTINGS] = {IC_WRENCH, "Configuración"},
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
		if (items[i] == MI_NOTIF && notif_unread()) {
			// Las que no has leído, en una píldora azul
			char nc[8];
			snprintf(nc, sizeof nc, "%d", notif_unread());
			float w = ui_text_w(nc, SZ_SMALL) + 12;
			ui_rrect(BOT_W - 24 - w, y + 3, w, 16, 8, col_alpha(C2D_Color32(0x1a, 0x6d, 0xb0, 0xff), a));
			ui_text(BOT_W - 24 - w / 2, y + 4, nc, SZ_SMALL, col_alpha(COL_WHITE, a), AL_CENTER, 0);
		}
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
		if (S.login_done > 1.8f || (kd & (KEY_A | KEY_B))) go_scene(S.login_back == SC_ACCOUNTS ? SC_STORE : S.login_back, -1);
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

// El golpe en la pared: el yeso se raja desde el centro, donde se clava el vástago (las grietas
// corren en tres etapas por detrás de los radios y pasan del aro), sale una nube de polvo de yeso
// que se abre entre los radios y sube despacio, y caen cascotes. Las grietas y el polvo van
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
		float r = 16 + 66 * (1 - expf(-age * 2.4f));
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
		float x = cx + cosf(a) * (16 + v * age), y = cy + sinf(a) * 16 - up * age + 0.5f * 520 * age * age;
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

// El sonido de la carga, cada cosa en su canal y en su momento de la animación
enum { CH_IMPACT, CH_HUM, CH_FLICKER, CH_CREAK, CH_STEAM };

static void splash_sound(float t0, float t1)
{
#define CROSS(T) (t0 < (T) && t1 >= (T))
	if (t0 <= 0) sfx_loop(CH_HUM, SFX_HUM);
	if (CROSS(SPLASH_HIT)) {
		sfx_play(CH_IMPACT, SFX_IMPACT, -1);
		sfx_play(CH_FLICKER, SFX_FLICKER, -1);   // el golpe hace parpadear el bombillo
	}
	if (CROSS(SPLASH_SPIN)) sfx_play(CH_CREAK, SFX_CREAK0, SFX_CREAK);
	if (CROSS(SPLASH_STEAM)) sfx_play(CH_STEAM, SFX_STEAM0, SFX_STEAM);
	float at[2], depth[2], len[2];
	int nd = bulb_dips((u32)(t1 / BULB_PERIOD), at, depth, len);
	for (int i = 0; i < nd; i++)
		if (CROSS(at[i])) sfx_play(CH_FLICKER, SFX_FLICKER, -1);
#undef CROSS
	// Todo se apaga con el fundido a negro del final
	float g = splash_out >= 0 ? clampf(1 - splash_out / 0.35f, 0, 1) : 1;
	// El bombillo y su chisporroteo, arriba a la derecha; el zumbido baja cuando flojea
	sfx_volume(CH_HUM, 0.16f * g * clampf(t1 / 0.6f, 0, 1) * (0.4f + 0.6f * bulb.power), 0.45f);
	sfx_volume(CH_FLICKER, 0.45f * g, 0.45f);
	sfx_volume(CH_IMPACT, 0.95f * g, 0);
	sfx_volume(CH_CREAK, 0.65f * g, -0.05f);
	sfx_volume(CH_STEAM, 0.28f * g * clampf((t1 - SPLASH_STEAM) / 0.02f, 0, 1), 0.1f);
}

static void splash_update(float dt, u32 kd)
{
	// Los primeros fotogramas tardan (se está cargando todo): sin este tope la entrada se saltaba
	float prev = splash_t;
	splash_t += dt < 0.034f ? dt : 0.034f;
	splash_sound(prev, splash_t);
	if (splash_out < 0) {
		bool skip = splash_t > 0.6f && (kd & (KEY_A | KEY_B | KEY_TOUCH));
		if ((splash_t > 3.5f && splash_ready()) || splash_t > 6.5f || skip) splash_out = 0;
	} else {
		splash_out += dt;
		if (splash_out > 0.35f) {
			// Con varias cuentas guardadas, primero se elige con cuál entrar
			go_scene(auth_choosing() ? SC_ACCOUNTS : SC_STORE, 0);
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
		S.price_focus = false;
		if (!t->term[0] && !t->list.req && !t->list.count) search_show_browse();
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
	else if (r && S.tab == TAB_SEARCH && r->tagid) search_tag(r->tagid, r->name);   // búsqueda popular
	else if (r && S.tab == TAB_SEARCH) search_start(r->name);       // búsqueda reciente
	else if (S.tab == TAB_SEARCH && t->browse && t->list.err[0] && !t->list.req) {
		t->fail_at = 0;
		t->list.err[0] = 0;
		tab_more(TAB_SEARCH);
	} else if (S.tab == TAB_SEARCH && !t->term[0]) S.want_kb = true;
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
	if (acct_back == SC_PROFILE) {
		acct_back = SC_STORE;
		go_scene(SC_PROFILE, -1);
		return;
	}
	go_scene(SC_STORE, -1);
}

static void input_store(u32 kd, u32 kh, u32 ku, touchPosition *tp)
{
	if (S.tab == TAB_CART) {
		if (S.cart_qr) {
			if (kd & (KEY_B | KEY_X)) S.cart_qr = false;
			return;
		}
		if (gift_is_open()) {
			input_gift(kd, kh, tp);
			return;
		}
		const CartInfo *c = shop_cart();
		int sel = CUR->sel;
		if ((kd & KEY_SELECT) && sel >= 0 && sel < c->n) gift_open(sel);
		if ((kd & KEY_Y) && sel >= 0 && sel < c->n) {
			if (shop_busy()) toast(_("Espera a que termine la acción anterior"));
			else shop_cart_remove(sel);
		}
		if ((kd & KEY_X) && c->n) open_pay();
	}
	if ((S.tab == TAB_LIBRARY || S.tab == TAB_WISHLIST) && (kd & KEY_Y)) {
		list_mode_next();
		return;
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
		// Foco en la píldora de búsqueda: A o Y escriben; abajo baja a la barra de precio
		if (key_repeat(kd, kh, KEY_DOWN)) {
			S.search_focus = false;
			S.price_focus = true;
		}
		if (kd & (KEY_A | KEY_Y)) S.want_kb = true;
		if (kd & KEY_X) {
			// Explorar la tienda sin escribir nada (con el precio elegido): se baja a la lista
			search_show_browse();
			S.search_focus = false;
		}
		kd &= ~(KEY_A | KEY_X);
	} else if (S.tab == TAB_SEARCH && S.price_focus) {
		// Foco en la barra de precio: izquierda y derecha la mueven; arriba, la búsqueda; abajo, la lista
		if (key_repeat(kd, kh, KEY_LEFT)) price_set(S.price - 1);
		if (key_repeat(kd, kh, KEY_RIGHT)) price_set(S.price + 1);
		if (key_repeat(kd, kh, KEY_UP)) {
			S.price_focus = false;
			S.search_focus = true;
		}
		if (key_repeat(kd, kh, KEY_DOWN) && cur_row_any(t)) {
			S.price_focus = false;
			store_select(0, 1);
			t->sel_changed = osGetTime();
		}
		if (kd & KEY_Y) S.want_kb = true;
		kd &= ~KEY_A;
	} else {
		if (key_repeat(kd, kh, KEY_DOWN)) store_select(t->sel + 1, 1);
		if (key_repeat(kd, kh, KEY_UP)) {
			if (S.tab == TAB_SEARCH && first_row(t) >= t->sel) S.price_focus = true;   // arriba del todo: a la barra de precio
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
	if ((kd & KEY_B) && S.tab == TAB_SEARCH && (t->term[0] || t->tag)) {
		// B borra la búsqueda y vuelve a las recientes y al catálogo
		search_show_browse();
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
		// La barra de precio de Buscar: se toca o se arrastra el tirador
		S.price_drag = S.tab == TAB_SEARCH && tp->py >= LIST_Y && tp->py < LIST_Y + SUB_H;
		if (S.price_drag) {
			S.search_focus = false;
			S.price_focus = true;
		}
	}
	if ((kh & KEY_TOUCH) && S.price_drag) {
		price_set(price_step_at(tp->px));
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
	if ((ku & KEY_TOUCH) && S.price_drag) {
		S.price_drag = false;
		ku &= ~KEY_TOUCH;
	}
	if (ku & KEY_TOUCH) {
		if (S.touch_moved < 8) {
			int x = S.touch_x0, y = S.touch_y0;
			if (y < LIST_Y && S.tab < TAB_COUNT) {
				if (!(S.tab == TAB_SEARCH && S.search_focus)) {
					int nt = tab_at(x);
					if (nt == TAB_SEARCH && S.tab == TAB_SEARCH) {
						S.search_focus = true;   // tocar la lupa sube el foco
						S.price_focus = false;
					}
					else tab_switch(nt);
				}
			} else if (S.tab == TAB_HOME && y < LIST_Y + SUB_H) {
				int i = (int)((x - 10) / ((BOT_W - 20) / (float)HOME_PARTS));
				home_switch(i < 0 ? 0 : i);
			} else if (S.dragging) {
				int i = (int)((y - top + t->scroll) / ROW_H);
				if (i >= 0 && i < t->list.count && !t->list.rows[i].header) {
					if (S.tab == TAB_SEARCH && (S.search_focus || S.price_focus)) {
						S.search_focus = S.price_focus = false;
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
// Teclado para activar un producto: la clave de un juego o un código del monedero
static void open_key_keyboard(void)
{
	SwkbdState kb;
	char buf[48] = "";
	swkbdInit(&kb, SWKBD_TYPE_QWERTY, 2, 40);
	swkbdSetHintText(&kb, _("Clave del producto o código del monedero"));
	swkbdSetButton(&kb, SWKBD_BUTTON_LEFT, _("Cancelar"), false);
	swkbdSetButton(&kb, SWKBD_BUTTON_RIGHT, _("Activar"), true);
	swkbdSetFeatures(&kb, SWKBD_DARKEN_TOP_SCREEN | SWKBD_ALLOW_HOME | SWKBD_ALLOW_RESET | SWKBD_ALLOW_POWER);
	swkbdSetValidation(&kb, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	if (swkbdInputText(&kb, buf, sizeof buf) != SWKBD_BUTTON_RIGHT) return;
	shop_activate(buf);
}

static void open_keyboard(void)
{
	SwkbdState kb;
	char buf[64] = "";
	// Tres botones: el teclado de la consola no deja confirmar en vacío, así que «Ver todo»
	// (que no confirma texto) explora la tienda sin escribir nada
	swkbdInit(&kb, SWKBD_TYPE_NORMAL, 3, 60);
	swkbdSetHintText(&kb, _("Nombre del juego"));
	swkbdSetInitialText(&kb, CUR->term);
	swkbdSetButton(&kb, SWKBD_BUTTON_LEFT, _("Cancelar"), false);
	swkbdSetButton(&kb, SWKBD_BUTTON_MIDDLE, _("Ver todo"), false);
	swkbdSetButton(&kb, SWKBD_BUTTON_RIGHT, _("Buscar"), true);
	swkbdSetFeatures(&kb, SWKBD_DARKEN_TOP_SCREEN | SWKBD_ALLOW_HOME | SWKBD_ALLOW_RESET | SWKBD_ALLOW_POWER);
	swkbdSetValidation(&kb, SWKBD_ANYTHING, 0, 0);
	SwkbdButton b = swkbdInputText(&kb, buf, sizeof buf);
	if (b == SWKBD_BUTTON_MIDDLE) {
		search_show_browse();
		S.search_focus = S.price_focus = false;
		return;
	}
	if (b != SWKBD_BUTTON_RIGHT) return;
	char *s = buf;
	while (*s == ' ') s++;
	size_t n = strlen(s);
	while (n > 0 && s[n - 1] == ' ') s[--n] = 0;
	// Vacío: como en la tienda de Steam, se explora todo el catálogo (con el filtro de precio)
	if (*s) search_start(s);
	else search_show_browse();
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
	case BT_IGNORE: return &B_IGNORE;
	case BT_PREV: return &B_PREV;
	case BT_NEXT: return &B_NEXT;
	case BT_EXIT_NO: return &B_EXIT_NO;
	case BT_EXIT_YES: return &B_EXIT_YES;
	case BT_SEARCH: return &B_SEARCHBAR;
	case BT_BACKBAR: return &B_BACKBAR;
	case BT_PLIB: return &B_PLIB;
	case BT_PWISH: return &B_PWISH;
	case BT_PLOGOUT: return &B_PLOGOUT;
	case BT_PFRIENDS: return &B_PFRIENDS;
	case BT_PACT: return &B_PACT;
	case BT_PSWITCH: return &B_PSWITCH;
	case BT_PAYW: return &B_PAYW;
	case BT_PAYC: return &B_PAYC;
	case BT_PAYQ: return &B_PAYQ;
	case BT_CVV: return &B_CVV;
	case BT_PAYGO: return &B_PAYGO;
	case BT_DEMO: return &B_DEMO;
	case BT_UGAME: return &B_UGAME;
	case BT_UQR: return &B_UQR;
	case BT_PVIEW: return &B_PVIEW;
	case BT_PDL: return &B_PDL;
	case BT_UADD: return &B_UADD;
	case BT_UADDW: return &B_UADDW;
	case BT_UIGN: return &B_UIGN;
	case BT_FOLLOW: return &B_FOLLOW;
	case BT_PPOINTS: return &B_PPOINTS;
	case BT_PINV: return &B_PINV;
	case BT_RUP: return &B_RUP;
	case BT_RDOWN: return &B_RDOWN;
	case BT_RTEXT: return &B_RTEXT;
	case BT_RVIS: return &B_RVIS;
	case BT_RCANCEL: return &B_RCANCEL;
	case BT_RSEND: return &B_RSEND;
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
	if (RE.open && a) {
		input_review_editor(a, kd, kh, ku, tp);
		return;
	}
	if (RP.open && a) {
		input_remote(a, kd, kh, tp);
		return;
	}
	if (AW.open) {
		input_award(kd, kh, tp);
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
		if ((kd & KEY_Y) && !(S.sec == SEC_ACH && ach_cmp_count()) && S.sec != SEC_REVIEWS) key_b = auth_logged() ? BT_CART : -1;   // en Logros Y compara; en Opiniones, filtra
		if (combo && (kd & KEY_RIGHT)) key_b = BT_SHOTS;
		if (combo && (kd & KEY_LEFT)) key_b = BT_WISH;
		if (combo && (kd & KEY_DOWN)) key_b = has_ignore_btn ? BT_FOLLOW : BT_INST;
		if (combo && (kd & KEY_UP)) key_b = has_ignore_btn ? BT_IGNORE : has_follow_btn ? BT_FOLLOW : BT_NONE;
		if ((kd & KEY_SELECT) && S.has_demo_btn) key_b = BT_DEMO;
	}
	if (combo) {
		kd &= ~(KEY_LEFT | KEY_RIGHT | KEY_UP | KEY_DOWN);   // la cruceta es de la combinación
		kh &= ~(KEY_UP | KEY_DOWN);
	}
	if (kd & (KEY_L | KEY_LEFT)) game_set_sec(S.sec - 1);
	if (kd & (KEY_R | KEY_RIGHT)) game_set_sec(S.sec + 1);
	if (a && S.sec == SEC_REQS && a->mac && (kd & (KEY_UP | KEY_DOWN))) {
		// Requisitos de Windows o de macOS (se queda para las demás fichas)
		g_cfg.reqs_mac = !g_cfg.reqs_mac;
		config_save();
		toast(g_cfg.reqs_mac ? _("Se mostrarán los requisitos de macOS") : _("Se mostrarán los requisitos de Windows"));
	}
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
	if (S.sec == SEC_ACH && a && (kd & KEY_Y) && ach_cmp_count()) {
		ach_cmp_next(a);
		return;
	}
	if (S.sec == SEC_ACH) {
		if (key_repeat(kd, kh, KEY_DOWN) && S.lsel + 1 < S.nach) S.lsel++;
		if (key_repeat(kd, kh, KEY_UP) && S.lsel > 0) S.lsel--;
	}
	if (S.sec == SEC_REVIEWS && (kd & KEY_Y)) {
		reviews_set_filter(rv_filter + 1);
		return;
	}
	if (S.sec == SEC_REVIEWS) {
		bool mine = a && my_review_shown(a);
		if (key_repeat(kd, kh, KEY_DOWN) && S.rsel + 1 < S.nreviews) S.rsel++;
		if (key_repeat(kd, kh, KEY_UP) && S.rsel > (mine ? -1 : 0)) S.rsel--;
		if (!mine && S.rsel < 0) S.rsel = 0;
		if (mine && S.rsel == -1 && (kd & KEY_A) && MR.loaded) {
			review_editor_open(a);
			return;
		}
		// A en la reseña de otra persona: darle un premio con tus puntos de Steam
		if (S.rsel >= 0 && S.rsel < S.nreviews && (kd & KEY_A) && auth_logged() && S.reviews[S.rsel].id) {
			award_open(S.reviews[S.rsel].id);
			return;
		}
		if (S.rsel >= S.nreviews - 3) reviews_more();
	}

	if ((kd & KEY_TOUCH) && hit(&B_SEG, tp->px, tp->py))
		game_set_sec((int)((tp->px - B_SEG.x) / (B_SEG.w / SEC_COUNT)));
	int ids[7] = {BT_CART, BT_WISH, BT_SHOTS, has_ignore_btn ? BT_FOLLOW : BT_INST}, nids = 4;
	if (has_follow_btn && !has_ignore_btn) ids[nids++] = BT_FOLLOW;
	if (S.has_demo_btn) ids[nids++] = BT_DEMO;
	if (has_ignore_btn) ids[nids++] = BT_IGNORE;
	int b = a ? press_update(kd, kh, ku, tp, ids, nids) : BT_NONE;
	if (key_b) b = key_b;
	if (b == BT_SHOTS && (a->nshots || my_shots_for(a->appid))) {
		shots_mine = !a->nshots;
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
	} else if (b == BT_IGNORE) {
		if (shop_busy()) toast(_("Espera a que termine la acción anterior"));
		else shop_ignore(a->appid, a->name, !shop_ignored(a->appid));
	} else if (b == BT_FOLLOW) {
		if (shop_busy()) toast(_("Espera a que termine la acción anterior"));
		else shop_follow(a->appid, a->name, !shop_followed(a->appid));
	} else if (b == BT_INST) {
		if (!owned_app(a->appid)) toast(_("La instalación remota requiere tener el juego en tu biblioteca"));
		else remote_open(a);
	} else if (b == BT_DEMO && S.demo_is_pt) {
		if (owned_app(playtest_for(a->appid))) toast(_("Ya tienes acceso al playtest"));
		else if (pt_asked(a->appid)) toast(_("Ya has solicitado acceso. Steam te avisará por correo cuando te lo den"));
		else if (shop_busy()) toast(_("Espera a que termine la acción anterior"));
		else shop_playtest(a->appid, a->name);
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
	int n = (e && e->done) ? shots_count(&e->info) : 0;
	bool both = e && e->done && e->info.nshots && my_shots_for(e->info.appid);
	if (both && ((kd & KEY_Y) || ((kd & KEY_TOUCH) && hit(&B_SHSRC, tp->px, tp->py)))) {
		bool m = (kd & KEY_Y) ? !shots_mine : tp->px >= B_SHSRC.x + B_SHSRC.w / 2;
		if (m != shots_mine) {
			shots_mine = m;
			S.shot = 0;
			S.strip = 0;
			S.shot_anim = 0;
			S.shot_dir = m ? 1 : -1;
		}
		return;
	}
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

// Por qué no se activó un código (2000 + el detalle de la compra que da Steam)
static const char *activate_err(int code)
{
	switch (code - 2000) {
	case 9: return _("Ya tienes este producto en tu cuenta");
	case 13: return _("Este código no se puede activar en tu país");
	case 14: return _("El código no es válido. Revisa que esté bien escrito");
	case 15: return _("Este código ya lo ha activado otra cuenta");
	case 24: return _("Necesitas tener el juego base para activar este código");
	case 53: return _("Demasiados intentos. Espera un rato antes de volver a probar");
	}
	if (code < 0) return _("No se puede conectar con Steam");
	return _("No se pudo activar el código. Inténtalo de nuevo más tarde");
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
		case SH_ACTIVATE:
		case SH_WALLET:
			if (ev.ok && ev.kind == SH_ACTIVATE) {
				library_changed();
				if (ev.what[0]) snprintf(t, sizeof t, _("Producto activado: %s. Ya está en tu biblioteca"), ev.what);
				else snprintf(t, sizeof t, "%s", _("Producto activado. Ya está en tu biblioteca"));
			} else if (ev.ok) {
				if (ev.what[0]) snprintf(t, sizeof t, _("Fondos añadidos a tu monedero. Saldo: %s"), ev.what);
				else snprintf(t, sizeof t, "%s", _("Fondos añadidos a tu monedero de Steam"));
			} else {
				snprintf(t, sizeof t, "%s", activate_err(ev.code));
			}
			break;
		case SH_PLAYTEST:
			if (ev.ok) {
				if (PT.nasked < 16) PT.asked[PT.nasked++] = ev.appid;
				if (ev.code == 1) {
					library_changed();
					snprintf(t, sizeof t, _("Ya tienes acceso al playtest de %s"), ev.what);
				} else {
					snprintf(t, sizeof t, _("Te has apuntado al playtest de %s. Steam te avisará por correo"), ev.what);
				}
			} else {
				snprintf(t, sizeof t, _("No se pudo solicitar el acceso: %s"), shop_err(ev.code));
			}
			break;
		case SH_REVIEW:
			if (ev.ok) {
				MR.appid = 0;   // se vuelve a pedir para enseñarla tal cual quedó
				snprintf(t, sizeof t, "%s", _("Tu reseña se ha publicado"));
			} else if (ev.what[0]) {
				snprintf(t, sizeof t, _("No se pudo publicar tu reseña: %s"), ev.what);
			} else {
				snprintf(t, sizeof t, _("No se pudo publicar tu reseña: %s"), shop_err(ev.code));
			}
			break;
		case SH_FOLLOW:
		case SH_UNFOLLOW:
			if (ev.ok && ev.kind == SH_FOLLOW) snprintf(t, sizeof t, _("Sigues %s: verás sus novedades en Steam"), ev.what);
			else if (ev.ok) snprintf(t, sizeof t, _("Ya no sigues %s"), ev.what);
			else snprintf(t, sizeof t, _("No se pudo cambiar: %s"), shop_err(ev.code));
			break;
		case SH_CART_GIFT:
			if (ev.ok && ev.what[0]) snprintf(t, sizeof t, _("Regalo para %s: se le enviará cuando pagues"), ev.what);
			else if (ev.ok) snprintf(t, sizeof t, "%s", _("Ya no es un regalo: el juego será para ti"));
			else snprintf(t, sizeof t, _("No se pudo cambiar: %s"), shop_err(ev.code));
			break;
		case SH_IGNORE:
		case SH_UNIGNORE:
			if (ev.ok && ev.kind == SH_IGNORE) snprintf(t, sizeof t, _("Has ignorado %s: no saldrá en tus recomendaciones"), ev.what);
			else if (ev.ok) snprintf(t, sizeof t, _("Ya no ignoras %s"), ev.what);
			else snprintf(t, sizeof t, _("No se pudo cambiar: %s"), shop_err(ev.code));
			break;
		}
		static const Icon ic[] = {[SH_CART_ADD] = IC_CART, [SH_CART_REMOVE] = IC_CART, [SH_WISH_ADD] = IC_HEART,
			[SH_WISH_REMOVE] = IC_HEART, [SH_FREE] = IC_PAD, [SH_IGNORE] = IC_HIDE, [SH_UNIGNORE] = IC_HIDE,
			[SH_ACTIVATE] = IC_CODE, [SH_WALLET] = IC_COIN, [SH_REVIEW] = IC_CHAT, [SH_PLAYTEST] = IC_PAD, [SH_FOLLOW] = IC_BELL, [SH_UNFOLLOW] = IC_BELL, [SH_CART_GIFT] = IC_HEART};
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
	S.price = PRICE_ANY;   // Buscar: sin filtro de precio
	S.price_x = PRICE_ANY;
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
	if (g_cfg.quiet) netlog("sonido: desactivado en Configuración");
	else if (!sfx_init()) netlog("sonido: sin DSP (falta sdmc:/3ds/dspfirm.cdc)");
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
		u64 tick0 = svcGetSystemTick();
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
			else if (ti == TAB_LIBRARY) {
				int before = t->list.count;
				owned_poll(t);
				if (lib_mode && t->list.count != before) lib_apply(t);
			} else if (ti == TAB_WISHLIST) {
				int before = t->list.count;
				wish_poll(t);
				if ((wish_mode || wish_stash.count) && t->list.count != before) wish_apply(t);
				// Con un orden o para los avisos de ofertas hace falta la lista entera: se sigue pidiendo sola
				if (auth_logged() && t->wish_loaded && !t->list.req && t->wish_next < t->nwish && (wish_mode || !deals_checked)) tab_more(TAB_WISHLIST);
				if (auth_logged() && S.scene != SC_SPLASH && S.scene != SC_ACCOUNTS && S.scene_anim >= 1) deals_check(t);   // con la tienda ya a la vista
			}
			else if (ti == TAB_SEARCH) search_poll(t);
			else {
				// Betas: si una página falla, se vuelve a pedir la misma
				if (ti == TAB_BETAS && t->list.req && t->list.req->state == REQ_FAILED) t->raw_next -= PAGE;
				list_poll(&t->list, tab_paged(ti));
				if (ti == TAB_FORYOU && !t->list.err[0]) t->list.end = DQ.pages >= 6 && !DQ.req;   // cada cola es una página
			}
			if (ti == TAB_BETAS) {
				// La etiqueta «Acceso anticipado» la ponen los usuarios y se queda en juegos que ya
				// salieron (PUBG): solo se dejan los que Steam marca como acceso anticipado de verdad
				int total = t->list.total, n = 0;
				for (int k = 0; k < t->list.count; k++)
					if (t->list.rows[k].early) t->list.rows[n++] = t->list.rows[k];
				t->list.count = n;
				if (total && t->raw_next >= total) t->list.end = true;
				if (t->sel >= n) t->sel = n ? n - 1 : 0;
			}
			if (t->list.err[0] && !t->list.req) {
				if (!t->fail_at) {
					t->fail_at = now;
					t->list.end = false;
				} else if (!t->list.count && t->started) {
					tab_more(ti);   // espera sola lo que toque
				} else if (ti == TAB_SEARCH && t->browse) {
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
		tags_poll();
		family_poll();
		notif_poll();
		flist_poll();
		info_poll();
		for (int i = 0; i < CACHE; i++) {
			InfoEnt *e = &cache[i];
			const StoreRow *cr = S.scene == SC_STORE ? cur_row() : NULL;
			bool shown = e->appid == S.app || (cr && e->appid == cr->appid);
			if (e->failed && shown && now - e->fail_at > (u64)retry_delay(e->err) * 1000) info_retry(e);
		}
		// Las fotos que lleguen se suben cuando acaba la animación de pantalla, del menú o del apartado
		img_hold(S.scene_anim < 0.97f || (S.menu_anim > 0.03f && S.menu_anim < 0.97f) || S.sec_anim < 0.97f);
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
		if (FL.want_code) {
			FL.want_code = false;
			open_fcode_keyboard();
			kd = kh = ku = 0;
		}
		if (GF.want_kb) {
			GF.want_kb = false;
			open_gift_keyboard();
			kd = kh = ku = 0;
		}
		if (RE.want_kb) {
			RE.want_kb = false;
			open_review_keyboard();
			kd = kh = ku = 0;
		}
		if (S.want_key) {
			S.want_key = false;
			open_key_keyboard();
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
		case SC_NOTIF: input_notifs(kd, kh, &tp); break;
		case SC_FRIENDS: input_friends(kd, kh, &tp); break;
		case SC_ACCOUNTS: input_accounts(kd, kh, &tp); break;
		case SC_SETTINGS: input_settings(kd, kh, &tp); break;
		case SC_USER: input_user(kd, kh, ku, &tp); break;
		case SC_ACCSET: input_accset(kd, kh, &tp); break;
		case SC_PCDL: input_pcdl(kd, kh, &tp); break;
		case SC_POINTS: input_points(kd, kh, &tp); break;
		case SC_INV: input_inv(kd, kh, &tp); break;
		case SC_HIST: input_hist(kd, kh, &tp); break;
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
		// Tu cuenta enseña siempre tu fondo y tu marco (aunque vengas del perfil de un amigo)
		if (S.scene == SC_PROFILE && auth_logged() && UP.id != auth_info()->steamid) user_load(auth_info()->steamid);
		user_poll();
		if (DQ.req && DQ.req->state >= REQ_DONE) {
			Tab *t = &tabs[TAB_FORYOU];
			u32 ids[40];
			int n = DQ.req->state == REQ_DONE ? steam_parse_discovery(DQ.req->data, ids, 40) : -1;
			netlog("para ti: cola %d con %d juegos (HTTP %ld)", DQ.pages + 1, n, DQ.req->http);
			if (n > 0 && !t->list.req) {
				t->list.req = steam_req_items(ids, n, false);
				DQ.pages++;
				t->list.end = DQ.pages >= 6;   // hasta seis colas seguidas
			} else if (n <= 0) {
				snprintf(t->list.err, sizeof t->list.err, "%s", n == 0 ? _("Steam no tiene recomendaciones para ti ahora mismo") : _("No se pudo cargar tu cola de descubrimiento"));
				t->list.end = true;
			}
			net_release(DQ.req);
			DQ.req = NULL;
		}
		user_friend_poll();
		user_comments_poll();
		user_family_poll();
		if (UP.want_comment) {
			UP.want_comment = false;
			open_comment_keyboard();
			kd = kh = ku = 0;
		}
		accset_poll();
		pcdl_poll();
		points_poll();
		award_poll();
		inv_poll();
		hist_poll();
		xp_poll();

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
		RP.anim = approach(RP.anim, RP.open && S.scene == SC_GAME ? 1 : 0, RP.open ? 12 : 18);
		AW.anim = approach(AW.anim, AW.open && S.scene == SC_GAME ? 1 : 0, AW.open ? 12 : 18);
		S.sub_anim = approach(S.sub_anim, S.scene == SC_STORE && (S.tab == TAB_HOME || S.tab == TAB_SEARCH) ? 1 : 0, 14);
		S.price_x = approach(S.price_x, S.price, 16);
		price_apply();
		S.sub_x = approach(S.sub_x, S.home_sub, 16);
		S.oscroll = approach(S.oscroll, S.oscroll_t, 18);
		UP.scroll = approach(UP.scroll, UP.scroll_t, 18);
		// La píldora primero llega a la lupa y solo entonces se estira
		bool at_lupa = fabsf(S.tab_x - TAB_SEARCH) < 0.03f;
		S.search_x = approach(S.search_x, S.tab == TAB_SEARCH && S.search_focus && at_lupa ? 1 : 0, 14);
		S.sec_anim = clampf(S.sec_anim + dt / 0.22f, 0, 1);
		S.scene_anim = clampf(S.scene_anim + dt / 0.28f, 0, 1);
		S.shot_anim = clampf(S.shot_anim + dt / 0.30f, 0, 1);
		{
			RowList *l = game_list();
			float rh = 22, vis = SCR_H - 4 - 42;
			if (S.sec == SEC_ACH) {
				l = NULL;
				float y = S.lsel * ACH_RH, t = S.lscroll, avis = SCR_H - 4 - ach_top;
				if (y < t) t = y;
				if (y + ACH_RH > t + avis) t = y + ACH_RH - avis;
				S.lscroll = approach(S.lscroll, t, 18);
			}
			if (l) {
				float y = S.lsel * rh, t = S.lscroll;
				if (y < t) t = y;
				if (y + rh > t + vis) t = y + rh - vis;
				S.lscroll = approach(S.lscroll, t, 18);
			}
			float rvis = SCR_H - 20 - 52, ry = 0, rt = S.rscroll;
			float mine = ge && ge->done ? my_review_h(&ge->info) : 0;
			if (mine > 0 && S.rsel >= 0) ry += mine + 5;
			for (int i = 0; i < S.rsel && i < S.nreviews; i++) ry += review_h(i) + 5;
			float card = S.rsel < 0 ? mine + 5 : S.rsel < S.nreviews ? review_h(S.rsel) + 5 : 0;
			if (ry < rt) rt = ry;
			if (ry + card > rt + rvis) rt = ry + card - rvis;
			S.rscroll = approach(S.rscroll, rt, 18);
		}

		u64 tick_upd = svcGetSystemTick();
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
		case SC_NOTIF: draw_notifs_top(); break;
		case SC_FRIENDS: draw_friends_top(); break;
		case SC_ACCOUNTS: draw_accounts_top(); break;
		case SC_SETTINGS: draw_settings_top(); break;
		case SC_USER: draw_user_top(); break;
		case SC_ACCSET: draw_accset_top(); break;
		case SC_PCDL: draw_pcdl_top(); break;
		case SC_POINTS: draw_points_top(); break;
		case SC_INV: draw_inv_top(); break;
		case SC_HIST: draw_hist_top(); break;
		case SC_PAY: draw_pay_top(); break;
		}
		C2D_ViewReset();
		u32 fade_col = S.scene == SC_SHOTS ? C2D_Color32(0, 0, 0, 0xff) : COL_BG;
		if (ce < 1) ui_rect(0, 0, TOP_W, SCR_H, col_alpha(fade_col, (1 - ce) * 0.9f));
		if (S.scene == SC_STORE || S.scene == SC_GAME || S.scene == SC_LOGIN || S.scene == SC_PROFILE || S.scene == SC_PAY || S.scene == SC_ABOUT || S.scene == SC_NOTIF || S.scene == SC_FRIENDS || S.scene == SC_ACCOUNTS || S.scene == SC_SETTINGS || S.scene == SC_USER || S.scene == SC_ACCSET || S.scene == SC_PCDL || S.scene == SC_POINTS || S.scene == SC_INV || S.scene == SC_HIST)
			draw_status(auth_logged() && auth_info()->persona[0] ? auth_info()->persona : NULL);
		if (S.scene == SC_SPLASH && splash_out > 0) ui_rect(0, 0, TOP_W, SCR_H, C2D_Color32(0, 0, 0, (u8)(255 * clampf(splash_out / 0.35f, 0, 1))));
		if (S.exit_anim > 0.01f) ui_rect(0, 0, TOP_W, SCR_H, C2D_Color32(0, 0, 0, (u8)(0x80 * ease_out_cubic(S.exit_anim))));
		if (S.quit_fade >= 0) ui_rect(0, 0, TOP_W, SCR_H, C2D_Color32(0, 0, 0, (u8)(255 * clampf(S.quit_fade / 0.3f, 0, 1))));

		u64 tick_top = svcGetSystemTick();
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
		case SC_NOTIF: draw_notifs_bottom(); break;
		case SC_FRIENDS: draw_friends_bottom(); break;
		case SC_ACCOUNTS: draw_accounts_bottom(); break;
		case SC_SETTINGS: draw_settings_bottom(); break;
		case SC_USER: draw_user_bottom(); break;
		case SC_ACCSET: draw_accset_bottom(); break;
		case SC_PCDL: draw_pcdl_bottom(); break;
		case SC_POINTS: draw_points_bottom(); break;
		case SC_INV: draw_inv_bottom(); break;
		case SC_HIST: draw_hist_bottom(); break;
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
		// Un frame lento: en qué parte se fue el tiempo y cuántas imágenes se estaban cargando (también
		// en la versión normal, como mucho una línea por segundo, para poder verlo en la consola)
		{
			static u64 last_slow;
			u64 tick_end = svcGetSystemTick();
			double k = 1000.0 / SYSCLOCK_ARM11, ms = (tick_end - tick0) * k;
			if (ms > 45 && S.scene != SC_SPLASH && now - last_slow > 1000) {
				last_slow = now;
				netlog("lento: %.1fms escena=%d actualizar=%.1f arriba=%.1f abajo=%.1f imagenes=%d", ms, S.scene,
					(tick_upd - tick0) * k, (tick_top - tick_upd) * k, (tick_end - tick_top) * k, img_pending());
			}
		}
#ifdef DEV_RELAY
		// Solo en pruebas: tiempo de CPU por frame (ticks de la consola), medio cada 2 s en net.log
		{
			static double acc, worst;
			static int n;
			static u64 last_log;
			u64 tick_end = svcGetSystemTick();
			double ms = (tick_end - tick0) / (double)SYSCLOCK_ARM11 * 1000.0;
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
			sfx_exit();   // solo suena la carga
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
	sfx_exit();
	util_exit();
	C2D_Fini();
	C3D_Fini();
	romfsExit();
	gfxExit();
	return 0;
}
