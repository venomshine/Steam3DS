#include "shop.h"
#include "auth.h"
#include "cJSON.h"
#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct {
	// Opciones de compra del juego abierto
	u32 opt_app;
	BuyOpt opt[8];
	int nopt;
	bool opt_done, opt_failed;
	Req *opt_req;

	// Carrito
	CartInfo cart;
	bool cart_loaded;
	u32 cart_ver;
	Req *cart_req, *names_req;

	// La acción en curso
	Req *act;
	ShopKind kind;
	u32 appid;
	char what[96];

	ShopEvent ev[4];
	int nev;

	// Juegos ignorados
	u32 *ign;
	int nign, ign_cap;
} H;

// Los juegos que sigues
#define MAX_FOLLOW 512
static u32 fol[MAX_FOLLOW];
static int nfol;

static void push_event(bool ok, int code)
{
	if (H.nev >= 4) memmove(H.ev, H.ev + 1, sizeof H.ev[0] * 3), H.nev = 3;
	ShopEvent *e = &H.ev[H.nev++];
	e->kind = H.kind;
	e->ok = ok;
	e->appid = H.appid;
	e->code = code;
	snprintf(e->what, sizeof e->what, "%s", H.what);
	netlog("cuenta: accion %d -> %s (codigo %d)", H.kind, ok ? "ok" : "fallo", code);
}

bool shop_event(ShopEvent *ev)
{
	if (!H.nev) return false;
	*ev = H.ev[0];
	memmove(H.ev, H.ev + 1, sizeof H.ev[0] * (H.nev - 1));
	H.nev--;
	return true;
}

bool shop_busy(void) { return H.act != NULL; }

void shop_reset(void)
{
	Req **all[] = {&H.opt_req, &H.cart_req, &H.names_req, &H.act};
	for (size_t i = 0; i < sizeof all / sizeof *all; i++)
		if (*all[i]) net_release(*all[i]);
	free(H.ign);
	memset(&H, 0, sizeof H);
	nfol = 0;
}

// ---------------------------------------------------------------------------- Opciones de compra

void shop_buyopts_load(u32 appid)
{
	if (H.opt_app == appid && (H.opt_req || H.opt_done)) return;
	if (H.opt_req) net_release(H.opt_req);
	H.opt_app = appid;
	H.nopt = 0;
	H.opt_done = H.opt_failed = false;
	H.opt_req = steam_req_buyopts(appid);
}

const BuyOpt *shop_buyopts(u32 appid, int *n, bool *failed)
{
	*n = 0;
	if (failed) *failed = H.opt_app == appid && H.opt_failed;
	if (H.opt_app != appid || !H.opt_done) return NULL;
	*n = H.nopt;
	return H.opt;
}

// ---------------------------------------------------------------------------- Carrito

static cJSON *country_input(void)
{
	cJSON *in = cJSON_CreateObject();
	cJSON_AddStringToObject(in, "user_country", steam_country());
	return in;
}

void shop_cart_load(void)
{
	if (!auth_logged() || H.cart_req) return;
	H.cart_req = steam_get_service("IAccountCartService/GetCart", country_input(), auth_token());   // por POST da 405
}

const CartInfo *shop_cart(void) { return &H.cart; }
bool shop_cart_loaded(void) { return H.cart_loaded; }
bool shop_cart_loading(void) { return H.cart_req || H.names_req; }
u32 shop_cart_version(void) { return H.cart_ver; }

bool shop_in_cart(u32 appid, const BuyOpt *opts, int nopts)
{
	for (int i = 0; i < H.cart.n; i++) {
		const CartLine *l = &H.cart.line[i];
		if (appid && l->appid == appid) return true;
		for (int k = 0; k < nopts; k++)
			if ((opts[k].packageid && opts[k].packageid == l->packageid && !l->bundleid) ||
			    (opts[k].bundleid && opts[k].bundleid == l->bundleid))
				return true;
	}
	return false;
}

static void cart_changed(void)
{
	H.cart_loaded = true;
	H.cart_ver++;
	// Los nombres y el juego de cada línea se piden aparte (el carrito solo trae los números)
	bool need = false;
	for (int i = 0; i < H.cart.n; i++)
		if (!H.cart.line[i].name[0] || !H.cart.line[i].appid) need = true;
	if (need && H.cart.n) {
		if (H.names_req) net_release(H.names_req);
		H.names_req = steam_req_cart_names(&H.cart);
	}
}

static bool start_action(ShopKind k, u32 appid, const char *what)
{
	if (H.act || !auth_logged()) return false;
	H.kind = k;
	H.appid = appid;
	snprintf(H.what, sizeof H.what, "%s", what ? what : "");
	return true;
}

void shop_cart_add(u32 appid, const BuyOpt *o)
{
	if (!start_action(SH_CART_ADD, appid, o->name)) return;
	cJSON *in = country_input();
	cJSON *items = cJSON_AddArrayToObject(in, "items");
	cJSON *it = cJSON_CreateObject();
	if (o->bundleid) cJSON_AddNumberToObject(it, "bundleid", o->bundleid);
	else cJSON_AddNumberToObject(it, "packageid", o->packageid);
	cJSON_AddItemToArray(items, it);
	H.act = steam_post_service("IAccountCartService/AddItemsToCart", in, auth_token());
}

void shop_cart_remove(int line)
{
	if (line < 0 || line >= H.cart.n) return;
	const CartLine *l = &H.cart.line[line];
	if (!start_action(SH_CART_REMOVE, l->appid, l->name)) return;
	cJSON *in = country_input();
	char id[24];
	snprintf(id, sizeof id, "%llu", (unsigned long long)l->line_id);
	cJSON_AddStringToObject(in, "line_item_id", id);
	H.act = steam_post_service("IAccountCartService/RemoveItemFromCart", in, auth_token());
}

// ---------------------------------------------------------------------------- Deseados y gratis

void shop_wish(u32 appid, const char *name, bool add)
{
	if (!start_action(add ? SH_WISH_ADD : SH_WISH_REMOVE, appid, name)) return;
	cJSON *in = cJSON_CreateObject();
	cJSON_AddNumberToObject(in, "appid", appid);
	H.act = steam_post_service(add ? "IWishlistService/AddToWishlist" : "IWishlistService/RemoveFromWishlist", in, auth_token());
}

void shop_free(u32 appid, const char *name)
{
	if (!start_action(SH_FREE, appid, name)) return;
	cJSON *in = cJSON_CreateObject();
	cJSON *id = cJSON_AddObjectToObject(in, "item_id");
	cJSON_AddNumberToObject(id, "appid", appid);
	H.act = steam_post_service("ICheckoutService/AddFreeLicense", in, auth_token());
}

// ---------------------------------------------------------------------------- Ignorados

bool shop_ignored(u32 appid)
{
	for (int i = 0; i < H.nign; i++)
		if (H.ign[i] == appid) return true;
	return false;
}

static void ignored_mark(u32 appid, bool on)
{
	for (int i = 0; i < H.nign; i++)
		if (H.ign[i] == appid) {
			if (!on) H.ign[i] = H.ign[--H.nign];
			return;
		}
	if (!on) return;
	if (H.nign == H.ign_cap) {
		int cap = H.ign_cap ? H.ign_cap * 2 : 32;
		u32 *n = realloc(H.ign, cap * sizeof *n);
		if (!n) return;
		H.ign = n;
		H.ign_cap = cap;
	}
	H.ign[H.nign++] = appid;
}

void shop_set_ignored(const u32 *ids, int n)
{
	H.nign = 0;
	for (int i = 0; i < n; i++) ignored_mark(ids[i], true);
}

// Es el mismo formulario que usa la tienda web (recommended/ignorerecommendation): no hay servicio
// de la API para esto. Motivo 0: «no me interesa», el de por defecto
void shop_ignore(u32 appid, const char *name, bool ignore)
{
	char ck[1200];
	if (!auth_web_cookies("store.steampowered.com", ck, sizeof ck)) return;
	if (!start_action(ignore ? SH_IGNORE : SH_UNIGNORE, appid, name)) return;
	char body[160];
	snprintf(body, sizeof body, "sessionid=%s&appid=%lu&remove=%d&snr=1_5_9__403&ignore_reason=0",
		auth_sessionid(), (unsigned long)appid, ignore ? 0 : 1);
	H.act = net_request("https://store.steampowered.com/recommended/ignorerecommendation", body, ck);
	memset(ck, 0, sizeof ck);
}

// ---------------------------------------------------------------------------- Seguir un juego

static void followed_mark(u32 appid, bool on)
{
	for (int i = 0; i < nfol; i++)
		if (fol[i] == appid) {
			if (!on) fol[i] = fol[--nfol];
			return;
		}
	if (on && nfol < MAX_FOLLOW) fol[nfol++] = appid;
}

bool shop_followed(u32 appid)
{
	for (int i = 0; i < nfol; i++)
		if (fol[i] == appid) return true;
	return false;
}

void shop_set_followed(const u32 *ids, int n)
{
	nfol = 0;
	for (int i = 0; i < n; i++) followed_mark(ids[i], true);
}

void shop_follow(u32 appid, const char *name, bool follow)
{
	char ck[1200];
	if (!auth_web_cookies("store.steampowered.com", ck, sizeof ck)) return;
	if (!start_action(follow ? SH_FOLLOW : SH_UNFOLLOW, appid, name)) return;
	char body[128];
	snprintf(body, sizeof body, "sessionid=%s&appid=%lu%s", auth_sessionid(), (unsigned long)appid, follow ? "" : "&unfollow=1");
	H.act = net_request("https://store.steampowered.com/explore/followgame/", body, ck);
	memset(ck, 0, sizeof ck);
	if (!H.act) push_event(false, -1);
}

// ---------------------------------------------------------------------------- Activar un producto

static char act_code[48];

// Los mismos formularios que la tienda web (account/ajaxregisterkey y ajaxredeemwalletcode)
static bool activate_send(bool wallet)
{
	char ck[1200];
	if (!auth_web_cookies("store.steampowered.com", ck, sizeof ck)) return false;
	char body[160];
	snprintf(body, sizeof body, "%s=%s&sessionid=%s", wallet ? "wallet_code" : "product_key", act_code, auth_sessionid());
	H.act = net_request(wallet ? "https://store.steampowered.com/account/ajaxredeemwalletcode/"
	                           : "https://store.steampowered.com/account/ajaxregisterkey/", body, ck);
	memset(ck, 0, sizeof ck);
	return H.act != NULL;
}

void shop_activate(const char *code)
{
	// Solo letras, números y guiones (lo demás sobra o rompería el formulario), en mayúsculas
	int n = 0;
	for (const char *p = code; *p && n < (int)sizeof act_code - 1; p++) {
		char c = *p;
		if (c >= 'a' && c <= 'z') c -= 32;
		if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-') act_code[n++] = c;
	}
	act_code[n] = 0;
	if (!n || !start_action(SH_ACTIVATE, 0, act_code)) return;
	if (!activate_send(false)) push_event(false, -1);
}

// ---------------------------------------------------------------------------- Tu reseña

void shop_review(u32 appid, const char *name, bool up, const char *text, bool is_public)
{
	char ck[1200];
	if (!auth_web_cookies("store.steampowered.com", ck, sizeof ck)) return;
	if (!start_action(SH_REVIEW, appid, name)) return;
	char *esc = url_escape(text);
	size_t n = strlen(esc) + 400;
	char *body = malloc(n);
	if (body) {
		snprintf(body, n, "appid=%lu&steamworksappid=%lu&comment=%s&rated_up=%s&is_public=%s&language=%s"
			"&received_compensation=0&disable_comments=0&sessionid=%s",
			(unsigned long)appid, (unsigned long)appid, esc, up ? "true" : "false", is_public ? "true" : "false",
			steam_lang(), auth_sessionid());
		H.act = net_request("https://store.steampowered.com/friends/recommendgame", body, ck);
	}
	free(body);
	free(esc);
	memset(ck, 0, sizeof ck);
	if (!H.act) push_event(false, -1);
}

// ---------------------------------------------------------------------------- Playtests

void shop_playtest(u32 appid, const char *name)
{
	char ck[1200];
	if (!auth_web_cookies("store.steampowered.com", ck, sizeof ck)) return;
	if (!start_action(SH_PLAYTEST, appid, name)) return;
	char url[96], body[64];
	snprintf(url, sizeof url, "https://store.steampowered.com/ajaxrequestplaytestaccess/%lu", (unsigned long)appid);
	snprintf(body, sizeof body, "sessionid=%s", auth_sessionid());
	H.act = net_request(url, body, ck);
	memset(ck, 0, sizeof ck);
	if (!H.act) push_event(false, -1);
}

// ---------------------------------------------------------------------------- Cada frame

// ¿Salió bien? Steam dice el resultado en la cabecera x-eresult (1 = OK)
static bool req_ok(Req *r)
{
	if (r->state != REQ_DONE) return false;
	return r->eresult == 1 || (r->eresult == 0 && r->data && strstr(r->data, "\"response\""));
}

static int req_code(Req *r)
{
	if (r->eresult) return r->eresult;
	return r->http ? (int)-r->http : -1;
}

void shop_update(void)
{
	if (H.opt_req && H.opt_req->state >= REQ_DONE) {
		int n = H.opt_req->state == REQ_DONE ? steam_parse_buyopts(H.opt_req->data, H.opt_app, H.opt, 8) : -1;
		H.opt_failed = n < 0;
		H.nopt = n < 0 ? 0 : n;
		H.opt_done = n >= 0;
		net_release(H.opt_req);
		H.opt_req = NULL;
	}

	if (H.cart_req && H.cart_req->state >= REQ_DONE) {
		if (req_ok(H.cart_req) && steam_parse_cart(H.cart_req->data, &H.cart)) cart_changed();
		else netlog("cuenta: no se pudo leer el carrito (codigo %d)", req_code(H.cart_req));
		net_release(H.cart_req);
		H.cart_req = NULL;
	}
	if (H.names_req && H.names_req->state >= REQ_DONE) {
		if (H.names_req->state == REQ_DONE) {
			steam_parse_cart_names(H.names_req->data, &H.cart);
			H.cart_ver++;
		}
		net_release(H.names_req);
		H.names_req = NULL;
	}

	if (H.act && H.act->state >= REQ_DONE) {
		Req *r = H.act;
		bool ok = req_ok(r);
		int code = req_code(r);
		if (H.kind == SH_ACTIVATE || H.kind == SH_WALLET) {
			cJSON *root = r->state == REQ_DONE ? cJSON_Parse(r->data) : NULL;
			int success = root ? (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(root, "success")) : 0;
			const cJSON *d = cJSON_GetObjectItemCaseSensitive(root, H.kind == SH_WALLET ? "detail" : "purchase_result_details");
			int detail = cJSON_IsNumber(d) ? d->valueint : 0;
			netlog("cuenta: activar %s -> success %d, detalle %d (HTTP %ld)", H.kind == SH_WALLET ? "monedero" : "clave", success, detail, r->http);
			ok = success == 1;
			code = ok ? 0 : root ? 2000 + detail : req_code(r);
			if (ok && H.kind == SH_ACTIVATE) {
				// Lo que se ha activado, como lo llama Steam
				const cJSON *ri = cJSON_GetObjectItemCaseSensitive(root, "purchase_receipt_info");
				const cJSON *li = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(ri, "line_items"), 0);
				const cJSON *nm = li ? cJSON_GetObjectItemCaseSensitive(li, "line_item_description") : NULL;
				snprintf(H.what, sizeof H.what, "%s", cJSON_IsString(nm) ? nm->valuestring : "");
			} else if (ok) {
				const cJSON *b = cJSON_GetObjectItemCaseSensitive(root, "formattednewwalletbalance");
				snprintf(H.what, sizeof H.what, "%s", cJSON_IsString(b) ? b->valuestring : "");
			}
			cJSON_Delete(root);
			if (!ok && H.kind == SH_ACTIVATE && detail == 14) {
				// No es una clave de producto: quizá sea un código del monedero
				net_release(r);
				H.act = NULL;
				H.kind = SH_WALLET;
				if (activate_send(true)) return;
				push_event(false, code);
				return;
			}
		} else if (H.kind == SH_PLAYTEST) {
			// {"success":1,"granted":1} (dentro) o granted 0 (apuntado, te avisan por correo)
			cJSON *root = r->state == REQ_DONE ? cJSON_Parse(r->data) : NULL;
			const cJSON *sc = cJSON_GetObjectItemCaseSensitive(root, "success");
			const cJSON *gr = cJSON_GetObjectItemCaseSensitive(root, "granted");
			ok = r->state == REQ_DONE && r->http == 200 && root && (!sc || cJSON_IsTrue(sc) || (cJSON_IsNumber(sc) && sc->valueint == 1));
			int granted = cJSON_IsTrue(gr) || (cJSON_IsNumber(gr) && gr->valueint) ? 1 : 0;
			netlog("cuenta: playtest -> %s, concedido %d (HTTP %ld)", ok ? "ok" : "fallo", granted, r->http);
			code = ok ? granted : req_code(r);
			cJSON_Delete(root);
		} else if (H.kind == SH_REVIEW) {
			// {"success":true} o {"success":false,"strError":"…"}
			cJSON *root = r->state == REQ_DONE ? cJSON_Parse(r->data) : NULL;
			const cJSON *sc = cJSON_GetObjectItemCaseSensitive(root, "success");
			ok = cJSON_IsTrue(sc) || (cJSON_IsNumber(sc) && sc->valueint == 1);
			const cJSON *er = cJSON_GetObjectItemCaseSensitive(root, "strError");
			if (!ok && cJSON_IsString(er)) snprintf(H.what, sizeof H.what, "%s", er->valuestring);
			else if (!ok) H.what[0] = 0;
			netlog("cuenta: reseña -> %s (HTTP %ld)", ok ? "publicada" : "rechazada", r->http);
			code = ok ? 0 : req_code(r);
			cJSON_Delete(root);
		} else if (H.kind == SH_FOLLOW || H.kind == SH_UNFOLLOW) {
			// La tienda contesta «true» con un 200
			ok = r->state == REQ_DONE && r->http == 200 && r->data && !strstr(r->data, "false");
			if (ok) followed_mark(H.appid, H.kind == SH_FOLLOW);
		} else if (H.kind == SH_IGNORE || H.kind == SH_UNIGNORE) {
			// La tienda web contesta con un 200 (sin x-eresult)
			ok = r->state == REQ_DONE && r->http == 200;
			if (ok) ignored_mark(H.appid, H.kind == SH_IGNORE);
		} else if (H.kind == SH_CART_ADD || H.kind == SH_CART_REMOVE) {
			if (ok && steam_parse_cart(r->data, &H.cart)) cart_changed();
		} else if (H.kind == SH_FREE && ok) {
			// Si Steam no añadió nada, el detalle de la compra dice por qué
			cJSON *root = cJSON_Parse(r->data);
			const cJSON *resp = cJSON_GetObjectItemCaseSensitive(root, "response");
			int added = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(resp, "appids_added")) +
			            cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(resp, "packageids_added"));
			const cJSON *d = cJSON_GetObjectItemCaseSensitive(resp, "purchase_result_detail");
			if (!added && cJSON_IsNumber(d) && d->valueint != 0) {
				ok = false;
				code = 1000 + d->valueint;
			}
			cJSON_Delete(root);
		}
		H.act = NULL;
		push_event(ok, ok ? 0 : code);
		net_release(r);
	}
}

bool shop_busy_with(ShopKind k, u32 appid)
{
	return H.act && H.kind == k && H.appid == appid;
}
