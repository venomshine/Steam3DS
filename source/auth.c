#include "auth.h"
#include "app.h"
#include "cJSON.h"
#include "net.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define API "https://api.steampowered.com/"
#define LOGIN "https://login.steampowered.com/"
#define SESSION_FILE APP_DIR "/session.ini"
#define ACCOUNTS_FILE APP_DIR "/accounts.ini"

static AuthInfo A;
static char refresh_tok[1024];
static char access_tok[1024];
static u64 access_got;          // osGetTime() al recibir el token de acceso
static u32 access_life;         // segundos de vida del token (exp - iat)

// QR en curso
static char client_id[24];
static char request_id[64];
static int poll_every = 5;
static u64 next_poll;

static Req *begin_req, *poll_req, *refresh_req, *profile_req, *level_req;
static u64 retry_at;

// Cookies de la web de Steam (steamLoginSecure por dominio). Desde 2025 una sesión de
// «navegador» no puede pedir tokens nuevos a la API: se renueva como la web, con
// jwt/finalizelogin, y el token de cada dominio viene dentro de su cookie.
#define MAX_WEB 6
typedef struct { char host[40]; char cookie[1100]; } WebCookie;
static WebCookie web[MAX_WEB];
static char sessionid[28];
static Req *xfer_req[MAX_WEB];
static char xfer_host[MAX_WEB][40];

// Las cuentas guardadas: la activa es además la de session.ini
typedef struct { AuthAccount pub; char refresh[1024]; } SavedAcc;
static SavedAcc acc[AUTH_MAX_ACCOUNTS];
static int nacc;
static bool choosing;

static void set_state(AuthState s)
{
	A.state = s;
	A.changed = osGetTime();
}

// ----------------------------------------------------------------------------
// Base64 (los JWT van en base64url; el avatar llega en base64 normal)

static int b64_val(char c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+' || c == '-') return 62;
	if (c == '/' || c == '_') return 63;
	return -1;
}

static size_t b64_decode(const char *in, size_t n, unsigned char *out, size_t cap)
{
	size_t o = 0;
	u32 acc = 0;
	int bits = 0;
	for (size_t i = 0; i < n && in[i] && in[i] != '='; i++) {
		int v = b64_val(in[i]);
		if (v < 0) continue;
		acc = (acc << 6) | v;
		bits += 6;
		if (bits >= 8) {
			bits -= 8;
			if (o < cap) out[o++] = (acc >> bits) & 0xff;
		}
	}
	return o;
}

// La parte central de un JWT (el «payload»), ya leída como JSON
static cJSON *jwt_payload(const char *jwt)
{
	const char *a = strchr(jwt, '.');
	const char *b = a ? strchr(a + 1, '.') : NULL;
	if (!a || !b) return NULL;
	size_t n = b - a - 1;
	unsigned char *buf = malloc(n + 4);
	if (!buf) return NULL;
	size_t len = b64_decode(a + 1, n, buf, n + 3);
	buf[len] = 0;
	cJSON *j = cJSON_Parse((const char *)buf);
	free(buf);
	return j;
}

static u64 jwt_steamid(const char *jwt)
{
	cJSON *p = jwt_payload(jwt);
	const cJSON *sub = p ? cJSON_GetObjectItemCaseSensitive(p, "sub") : NULL;
	u64 id = cJSON_IsString(sub) ? strtoull(sub->valuestring, NULL, 10) : 0;
	cJSON_Delete(p);
	return id;
}

static void take_access(const char *tok)
{
	snprintf(access_tok, sizeof access_tok, "%s", tok);
	access_got = osGetTime();
	access_life = 20 * 3600;
	cJSON *p = jwt_payload(tok);
	if (p) {
		double exp = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(p, "exp"));
		double iat = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(p, "iat"));
		// La hora de la consola es la local: se usa la vida del token, no la hora absoluta
		if (exp > iat && iat > 0) access_life = (u32)(exp - iat);
		cJSON_Delete(p);
	}
}

// ----------------------------------------------------------------------------
// Cuentas guardadas (accounts.ini: una por línea, campos separados por tabuladores)

static void acc_save(void)
{
	char buf[AUTH_MAX_ACCOUNTS * 1400];
	size_t len = 0;
	for (int i = 0; i < nacc; i++) {
		const SavedAcc *a = &acc[i];
		len += snprintf(buf + len, sizeof buf - len, "%llu\t%s\t%s\t%s\t%s\n", (unsigned long long)a->pub.steamid,
			a->pub.account, a->pub.persona, a->pub.avatar, a->refresh);
	}
	disk_save(ACCOUNTS_FILE, buf, len);
	memset(buf, 0, sizeof buf);
}

static void clean_field(char *s)
{
	for (; *s; s++)
		if (*s == '\t' || *s == '\n' || *s == '\r') *s = ' ';
}

static void acc_load(void)
{
	nacc = 0;
	FILE *f = fopen(ACCOUNTS_FILE, "r");
	if (!f) return;
	char line[1500];
	while (nacc < AUTH_MAX_ACCOUNTS && fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = 0;
		char *fld[5] = {0};
		char *p = line;
		for (int k = 0; k < 5 && p; k++) {
			fld[k] = p;
			p = strchr(p, '\t');
			if (p) *p++ = 0;
		}
		if (!fld[4] || !*fld[4]) continue;
		SavedAcc *a = &acc[nacc];
		memset(a, 0, sizeof *a);
		a->pub.steamid = strtoull(fld[0], NULL, 10);
		snprintf(a->pub.account, sizeof a->pub.account, "%s", fld[1]);
		snprintf(a->pub.persona, sizeof a->pub.persona, "%s", fld[2]);
		snprintf(a->pub.avatar, sizeof a->pub.avatar, "%s", fld[3]);
		snprintf(a->refresh, sizeof a->refresh, "%s", fld[4]);
		if (a->pub.steamid) nacc++;
	}
	fclose(f);
	memset(line, 0, sizeof line);
}

// La cuenta activa, al día en la lista (la última usada va la primera)
static void acc_store_current(void)
{
	if (!A.steamid || !refresh_tok[0]) return;
	SavedAcc cur;
	memset(&cur, 0, sizeof cur);
	cur.pub.steamid = A.steamid;
	snprintf(cur.pub.account, sizeof cur.pub.account, "%s", A.account);
	snprintf(cur.pub.persona, sizeof cur.pub.persona, "%s", A.persona[0] ? A.persona : A.account);
	snprintf(cur.pub.avatar, sizeof cur.pub.avatar, "%s", A.avatar);
	snprintf(cur.refresh, sizeof cur.refresh, "%s", refresh_tok);
	clean_field(cur.pub.account);
	clean_field(cur.pub.persona);
	int at = -1;
	for (int i = 0; i < nacc; i++)
		if (acc[i].pub.steamid == A.steamid) at = i;
	if (at < 0) at = nacc < AUTH_MAX_ACCOUNTS ? nacc++ : nacc - 1;   // llena: sale la que lleva más sin usarse
	memmove(&acc[1], &acc[0], at * sizeof acc[0]);
	acc[0] = cur;
	acc_save();
}

static void acc_remove(u64 steamid)
{
	for (int i = 0; i < nacc; i++)
		if (acc[i].pub.steamid == steamid) {
			memset(&acc[i], 0, sizeof acc[i]);
			memmove(&acc[i], &acc[i + 1], (nacc - i - 1) * sizeof acc[0]);
			nacc--;
			acc_save();
			return;
		}
}

int auth_accounts(AuthAccount *out)
{
	for (int i = 0; out && i < nacc; i++) out[i] = acc[i].pub;
	return nacc;
}

bool auth_choosing(void) { return choosing && A.state == AUTH_OUT; }

// ----------------------------------------------------------------------------
// Sesión guardada en la SD

static void session_save(void)
{
	FILE *f = fopen(APP_DIR "/session.tmp", "w");
	if (!f) return;
	fprintf(f, "steamid=%llu\naccount=%s\nrefresh=%s\n", (unsigned long long)A.steamid, A.account, refresh_tok);
	fclose(f);
	remove(SESSION_FILE);
	rename(APP_DIR "/session.tmp", SESSION_FILE);
	acc_store_current();
}

static void session_forget(void)
{
	remove(SESSION_FILE);
	memset(refresh_tok, 0, sizeof refresh_tok);
	memset(access_tok, 0, sizeof access_tok);
	memset(web, 0, sizeof web);
	A.steamid = 0;
	A.account[0] = A.persona[0] = A.avatar[0] = 0;
	A.level = 0;
}

static bool session_load(void)
{
	FILE *f = fopen(SESSION_FILE, "r");
	if (!f) return false;
	char line[1100];
	while (fgets(line, sizeof line, f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (!strncmp(line, "steamid=", 8)) A.steamid = strtoull(line + 8, NULL, 10);
		else if (!strncmp(line, "account=", 8)) snprintf(A.account, sizeof A.account, "%.*s", (int)sizeof A.account - 1, line + 8);
		else if (!strncmp(line, "refresh=", 8)) snprintf(refresh_tok, sizeof refresh_tok, "%.*s", (int)sizeof refresh_tok - 1, line + 8);
	}
	fclose(f);
	return A.steamid && refresh_tok[0];
}

// ----------------------------------------------------------------------------

static cJSON *response_of(Req *r, cJSON **root)
{
	*root = (r->state == REQ_DONE && r->data) ? cJSON_Parse(r->data) : NULL;
	return *root ? cJSON_GetObjectItemCaseSensitive(*root, "response") : NULL;
}

static const char *jstr(const cJSON *o, const char *k)
{
	const cJSON *v = o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
	return cJSON_IsString(v) ? v->valuestring : NULL;
}

static void fetch_profile(void)
{
	char url[1400];
	snprintf(url, sizeof url, API "IPlayerService/GetPlayerLinkDetails/v1/?access_token=%s&input_json=%%7B%%22steamids%%22%%3A%%5B%%22%llu%%22%%5D%%7D",
		access_tok, (unsigned long long)A.steamid);
	if (profile_req) net_release(profile_req);
	profile_req = net_get(url, LANE_API);
	snprintf(url, sizeof url, API "IPlayerService/GetSteamLevel/v1/?access_token=%s&steamid=%llu", access_tok, (unsigned long long)A.steamid);
	if (level_req) net_release(level_req);
	level_req = net_get(url, LANE_API);
}

static void host_of(const char *url, char *out, size_t sz)
{
	const char *h = strstr(url, "://");
	h = h ? h + 3 : url;
	size_t n = strcspn(h, "/?");
	snprintf(out, sz, "%.*s", (int)(n < sz ? n : sz - 1), h);
}

// Formulario multipart (como lo manda la web de Steam). keys/vals terminan en NULL.
static char *multipart(const char **keys, const char **vals, char *ctype, size_t ctype_sz)
{
	char boundary[40];
	snprintf(boundary, sizeof boundary, "----Steam3DS%08lx%08lx", (unsigned long)rand(), (unsigned long)svcGetSystemTick());
	snprintf(ctype, ctype_sz, "Content-Type: multipart/form-data; boundary=%s", boundary);
	size_t cap = 256;
	for (int i = 0; keys[i]; i++) cap += strlen(keys[i]) + strlen(vals[i]) + 128;
	char *b = malloc(cap), *p = b;
	if (!b) return NULL;
	for (int i = 0; keys[i]; i++)
		p += sprintf(p, "--%s\r\nContent-Disposition: form-data; name=\"%s\"\r\n\r\n%s\r\n", boundary, keys[i], vals[i]);
	sprintf(p, "--%s--\r\n", boundary);
	return b;
}

static void wipe_free(char *s)
{
	if (!s) return;
	memset(s, 0, strlen(s));
	free(s);
}

static void start_refresh(void)
{
	snprintf(sessionid, sizeof sessionid, "%08lx%08lx%08lx", (unsigned long)rand(), (unsigned long)(svcGetSystemTick() & 0xffffffff), (unsigned long)rand());
	const char *keys[] = {"nonce", "sessionid", "redir", NULL};
	const char *vals[] = {refresh_tok, sessionid, "https://steamcommunity.com/login/home/?goto=", NULL};
	char ctype[96], hdr[200];
	char *body = multipart(keys, vals, ctype, sizeof ctype);
	if (!body) return;
	snprintf(hdr, sizeof hdr, "%s\nOrigin: https://steamcommunity.com\nReferer: https://steamcommunity.com/", ctype);
	if (refresh_req) net_release(refresh_req);
	refresh_req = net_request(LOGIN "jwt/finalizelogin", body, hdr);
	wipe_free(body);
	netlog("sesion: renovando la sesion web");
}

// Cuando ya volvieron todas las transferencias: el token de la tienda pasa a ser el de la API
static void transfers_done(u64 now)
{
	const char *best = NULL;
	int got = 0;
	for (int i = 0; i < MAX_WEB; i++) {
		if (!web[i].cookie[0]) continue;
		got++;
		if (!best || strstr(web[i].host, "store.")) best = web[i].cookie;
	}
	const char *jwt = best ? strstr(best, "%7C%7C") : NULL;
	if (jwt) jwt += 6;
	else if (best && (jwt = strstr(best, "||"))) jwt += 2;
	if (jwt && strchr(jwt, '.')) {
		take_access(jwt);
		cJSON *p = jwt_payload(jwt);
		const cJSON *aud = p ? cJSON_GetObjectItemCaseSensitive(p, "aud") : NULL;
		char auds[64] = "";
		const cJSON *it;
		cJSON_ArrayForEach(it, aud) if (cJSON_IsString(it) && strlen(auds) + strlen(it->valuestring) + 2 < sizeof auds) {
			strcat(auds, it->valuestring);
			strcat(auds, " ");
		}
		cJSON_Delete(p);
		netlog("sesion: renovada (%d dominios, token: %s)", got, auds);
		if (A.state != AUTH_IN) {
			set_state(AUTH_IN);
			fetch_profile();
		}
	} else {
		netlog("sesion: ningun dominio devolvio la cookie, reintento");
		retry_at = now + 15000;
		if (A.state == AUTH_IN) access_got = now;   // el token del QR sigue valiendo un rato
	}
}

void auth_init(void)
{
	memset(&A, 0, sizeof A);
	acc_load();
	// Con varias cuentas guardadas se elige al arrancar; con una, se entra con ella como siempre
	if (nacc >= 2) {
		choosing = true;
		set_state(AUTH_OUT);
		return;
	}
	if (session_load()) {
		if (!nacc) acc_store_current();   // la sesión de antes pasa a ser la primera cuenta guardada
		set_state(AUTH_STARTING);
		start_refresh();
	} else {
		choosing = nacc > 0;   // hay una cuenta guardada pero ninguna activa: se elige
		set_state(AUTH_OUT);
	}
}

void auth_begin_qr(void)
{
	if (begin_req) return;
	A.err[0] = 0;
	A.qr_url[0] = 0;
	A.qr_scanned = false;
	client_id[0] = request_id[0] = 0;
	if (poll_req) net_release(poll_req);
	poll_req = NULL;
	// Para una sesión de navegador, Steam lee este nombre como un User-Agent (ua-parser) en
	// «Dispositivos autorizados»: así sale «NetFront en Nintendo» y no «navegador desconocido».
	// NetFront es el motor del navegador de la 3DS.
	char *name = url_escape("Mozilla/5.0 (Nintendo 3DS; U; ; es) NetFront/4.1 " APP_NAME "/" APP_VERSION);
	char body[256];
	snprintf(body, sizeof body, "device_friendly_name=%s&platform_type=2&website_id=Community", name);
	free(name);
	begin_req = net_post(API "IAuthenticationService/BeginAuthSessionViaQR/v1/", body);
	set_state(AUTH_QR);
	netlog("sesion: pidiendo un QR");
}

void auth_cancel_qr(void)
{
	if (A.state != AUTH_QR) return;
	if (begin_req) net_release(begin_req);
	if (poll_req) net_release(poll_req);
	begin_req = poll_req = NULL;
	set_state(AUTH_OUT);
}

void auth_logout(void)
{
	// Se intenta anular el token en Steam; pase lo que pase, en la consola se olvida
	if (refresh_tok[0] && access_tok[0]) {
		cJSON *p = jwt_payload(refresh_tok);
		const char *jti = jstr(p, "jti");
		if (jti) {
			char url[1300], body[96];
			snprintf(url, sizeof url, API "IAuthenticationService/RevokeRefreshToken/v1/?access_token=%s", access_tok);
			snprintf(body, sizeof body, "token_id=%s&steamid=%llu&revoke_action=1", jti, (unsigned long long)A.steamid);
			net_release(net_post(url, body));
		}
		cJSON_Delete(p);
	}
	netlog("sesion: cerrada");
	acc_remove(A.steamid);
	session_forget();
	choosing = nacc > 0;
	set_state(AUTH_OUT);
}

// Para cambiar de cuenta: se deja la sesión en Steam (sigue guardada) y se vuelve a elegir
static void drop_requests(void)
{
	Req **all[] = {&begin_req, &poll_req, &refresh_req, &profile_req, &level_req};
	for (size_t i = 0; i < sizeof all / sizeof *all; i++)
		if (*all[i]) {
			net_release(*all[i]);
			*all[i] = NULL;
		}
	for (int i = 0; i < MAX_WEB; i++)
		if (xfer_req[i]) {
			net_release(xfer_req[i]);
			xfer_req[i] = NULL;
		}
	retry_at = 0;
}

void auth_switch_out(void)
{
	drop_requests();
	memset(refresh_tok, 0, sizeof refresh_tok);
	memset(access_tok, 0, sizeof access_tok);
	memset(web, 0, sizeof web);
	A.steamid = 0;
	A.account[0] = A.persona[0] = A.avatar[0] = 0;
	A.level = 0;
	A.err[0] = 0;
	choosing = true;
	set_state(AUTH_OUT);
	netlog("sesion: cambio de cuenta");
}

void auth_use(int i)
{
	if (i < 0 || i >= nacc) return;
	SavedAcc a = acc[i];
	auth_switch_out();
	choosing = false;
	A.steamid = a.pub.steamid;
	snprintf(A.account, sizeof A.account, "%s", a.pub.account);
	snprintf(A.persona, sizeof A.persona, "%s", a.pub.persona);
	snprintf(A.avatar, sizeof A.avatar, "%s", a.pub.avatar);
	snprintf(refresh_tok, sizeof refresh_tok, "%s", a.refresh);
	memset(&a, 0, sizeof a);
	session_save();   // pasa a ser la activa (y la primera de la lista)
	set_state(AUTH_STARTING);
	start_refresh();
}

void auth_forget_account(int i)
{
	if (i < 0 || i >= nacc) return;
	acc_remove(acc[i].pub.steamid);
	if (!nacc) {
		remove(SESSION_FILE);
		choosing = false;
	}
}

static void finish_login(const char *refresh, const char *access, const char *account)
{
	snprintf(refresh_tok, sizeof refresh_tok, "%s", refresh);
	take_access(access);
	A.steamid = jwt_steamid(access);
	if (!A.steamid) A.steamid = jwt_steamid(refresh);
	snprintf(A.account, sizeof A.account, "%s", account ? account : "");
	snprintf(A.persona, sizeof A.persona, "%s", A.account);
	session_save();
	set_state(AUTH_IN);
	netlog("sesion: dentro (steamid termina en %04llu)", (unsigned long long)(A.steamid % 10000));
	fetch_profile();
	start_refresh();   // cookies de la web (tienda, pago)
}

void auth_update(void)
{
	u64 now = osGetTime();
	cJSON *root;

	// 1) QR pedido
	if (begin_req && begin_req->state >= REQ_DONE) {
		cJSON *r = response_of(begin_req, &root);
		const char *cid = jstr(r, "client_id"), *rid = jstr(r, "request_id"), *url = jstr(r, "challenge_url");
		if (cid && rid && url) {
			snprintf(client_id, sizeof client_id, "%s", cid);
			snprintf(request_id, sizeof request_id, "%s", rid);
			snprintf(A.qr_url, sizeof A.qr_url, "%s", url);
			double iv = cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(r, "interval"));
			poll_every = iv >= 1 && iv <= 30 ? (int)iv : 5;
			next_poll = now + poll_every * 1000;
		} else {
			snprintf(A.err, sizeof A.err, "%s", begin_req->err[0] ? begin_req->err : "json");
			retry_at = now + 8000;
			netlog("sesion: no se pudo pedir el QR (%s)", A.err);
		}
		cJSON_Delete(root);
		net_release(begin_req);
		begin_req = NULL;
	}
	if (A.state == AUTH_QR && !A.qr_url[0] && !begin_req && retry_at && now >= retry_at) {
		retry_at = 0;
		auth_begin_qr();
	}

	// 2) ¿Ya lo aprobaste en el móvil?
	if (A.state == AUTH_QR && client_id[0] && !poll_req && now >= next_poll) {
		char *rid = url_escape(request_id);
		char body[160];
		snprintf(body, sizeof body, "client_id=%s&request_id=%s", client_id, rid);
		free(rid);
		poll_req = net_post(API "IAuthenticationService/PollAuthSessionStatus/v1/", body);
	}
	if (poll_req && poll_req->state >= REQ_DONE) {
		cJSON *r = response_of(poll_req, &root);
		next_poll = now + poll_every * 1000;
		if (r) {
			const char *ncid = jstr(r, "new_client_id"), *nurl = jstr(r, "new_challenge_url");
			if (ncid) snprintf(client_id, sizeof client_id, "%s", ncid);
			if (nurl) snprintf(A.qr_url, sizeof A.qr_url, "%s", nurl);
			if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "had_remote_interaction"))) A.qr_scanned = true;
			const char *rt = jstr(r, "refresh_token"), *at = jstr(r, "access_token");
			if (rt && at) finish_login(rt, at, jstr(r, "account_name"));
		} else if (poll_req->http >= 400 && poll_req->http < 500) {
			// El código caducó (o Steam lo anuló): se pide otro sin que haga falta hacer nada
			netlog("sesion: el QR caduco (HTTP %ld), pido otro", poll_req->http);
			net_release(poll_req);
			poll_req = NULL;
			cJSON_Delete(root);
			auth_begin_qr();
			return;
		}
		cJSON_Delete(root);
		net_release(poll_req);
		poll_req = NULL;
	}

	// 3) Sesión web renovada (al arrancar con sesión guardada, o cuando el token va a caducar)
	if (refresh_req && refresh_req->state >= REQ_DONE) {
		cJSON *r = refresh_req->state == REQ_DONE && refresh_req->data ? cJSON_Parse(refresh_req->data) : NULL;
		const cJSON *xfers = r ? cJSON_GetObjectItemCaseSensitive(r, "transfer_info") : NULL;
		const cJSON *error = r ? cJSON_GetObjectItemCaseSensitive(r, "error") : NULL;
		if (cJSON_IsArray(xfers) && cJSON_GetArraySize(xfers) > 0) {
			memset(web, 0, sizeof web);
			int n = 0;
			const cJSON *x;
			char sid[24];
			snprintf(sid, sizeof sid, "%llu", (unsigned long long)A.steamid);
			cJSON_ArrayForEach(x, xfers) {
				const char *url = jstr(x, "url");
				const cJSON *pr = cJSON_GetObjectItemCaseSensitive(x, "params");
				const char *nonce = jstr(pr, "nonce"), *auth = jstr(pr, "auth");
				if (!url || !nonce || n >= MAX_WEB || strncmp(url, "https://", 8)) continue;
				const char *keys[] = {"steamID", "nonce", "auth", NULL};
				const char *vals[] = {sid, nonce, auth ? auth : "", NULL};
				if (!auth) keys[2] = NULL;
				char ctype[96];
				char *body = multipart(keys, vals, ctype, sizeof ctype);
				if (!body) continue;
				host_of(url, xfer_host[n], sizeof xfer_host[n]);
				xfer_req[n++] = net_request(url, body, ctype);
				wipe_free(body);
			}
			if (!n) retry_at = now + 15000;
		} else if (error || (refresh_req->http >= 400 && refresh_req->http < 500)) {
			// Steam ya no acepta la sesión (se cerró desde otro sitio o caducó)
			netlog("sesion: Steam no acepta la sesion guardada (HTTP %ld, error %d)", refresh_req->http,
				cJSON_IsNumber(error) ? error->valueint : -1);
			snprintf(A.err, sizeof A.err, "%s", "expired");
			acc_remove(A.steamid);   // esa cuenta ya no sirve: hay que volver a iniciar sesión con ella
			session_forget();
			choosing = nacc > 0;
			set_state(AUTH_OUT);
		} else {
			// Sin red o respuesta rara: se reintenta solo
			netlog("sesion: no se pudo renovar (%s)", refresh_req->err[0] ? refresh_req->err : "json");
			retry_at = now + 8000;
			if (A.state == AUTH_IN) access_got = now - (u64)(access_life > 900 ? access_life - 900 : 0) * 1000;
		}
		cJSON_Delete(r);
		net_release(refresh_req);
		refresh_req = NULL;
	}
	bool xfer_busy = false, xfer_any = false;
	for (int i = 0; i < MAX_WEB; i++) {
		if (!xfer_req[i]) continue;
		xfer_any = true;
		if (xfer_req[i]->state < REQ_DONE) xfer_busy = true;
	}
	if (xfer_any && !xfer_busy) {
		for (int i = 0; i < MAX_WEB; i++) {
			if (!xfer_req[i]) continue;
			if (xfer_req[i]->login_cookie) {
				memcpy(web[i].host, xfer_host[i], sizeof web[i].host);
				snprintf(web[i].cookie, sizeof web[i].cookie, "%s", xfer_req[i]->login_cookie);
			} else {
				netlog("sesion: %s sin cookie (%s)", xfer_host[i], xfer_req[i]->err[0] ? xfer_req[i]->err : "sin Set-Cookie");
			}
			net_release(xfer_req[i]);
			xfer_req[i] = NULL;
		}
		transfers_done(now);
	}
	if (!refresh_req && !xfer_any && retry_at && now >= retry_at && (A.state == AUTH_STARTING || A.state == AUTH_IN)) {
		retry_at = 0;
		start_refresh();
	}
	if (A.state == AUTH_IN && !refresh_req && !xfer_any && !retry_at && access_got &&
	    now - access_got > (u64)(access_life > 900 ? access_life - 600 : 300) * 1000)
		start_refresh();

	// 4) Perfil: nombre, avatar y nivel
	if (profile_req && profile_req->state >= REQ_DONE) {
		cJSON *r = response_of(profile_req, &root);
		const cJSON *acc = cJSON_GetArrayItem(r ? cJSON_GetObjectItemCaseSensitive(r, "accounts") : NULL, 0);
		const cJSON *pd = acc ? cJSON_GetObjectItemCaseSensitive(acc, "public_data") : NULL;
		const char *name = jstr(pd, "persona_name"), *av = jstr(pd, "sha_digest_avatar");
		if (name) snprintf(A.persona, sizeof A.persona, "%s", name);
		if (av) auth_avatar_url(av, A.avatar, sizeof A.avatar);
		if (name || av) acc_store_current();   // el nombre y el avatar, para elegir cuenta al arrancar
		if (!pd) netlog("sesion: perfil sin datos (%s)", profile_req->err[0] ? profile_req->err : "json");
		A.changed = now;
		cJSON_Delete(root);
		net_release(profile_req);
		profile_req = NULL;
	}
	if (level_req && level_req->state >= REQ_DONE) {
		cJSON *r = response_of(level_req, &root);
		const cJSON *lv = r ? cJSON_GetObjectItemCaseSensitive(r, "player_level") : NULL;
		if (cJSON_IsNumber(lv)) A.level = lv->valueint;
		netlog("sesion: nivel %d (%s)", A.level, r ? "ok" : level_req->err);
		cJSON_Delete(root);
		net_release(level_req);
		level_req = NULL;
	}
}

void auth_exit(void)
{
	Req **all[] = {&begin_req, &poll_req, &refresh_req, &profile_req, &level_req};
	for (size_t i = 0; i < sizeof all / sizeof *all; i++)
		if (*all[i]) net_release(*all[i]);
	for (int i = 0; i < MAX_WEB; i++)
		if (xfer_req[i]) net_release(xfer_req[i]);
	memset(web, 0, sizeof web);
	memset(refresh_tok, 0, sizeof refresh_tok);
	memset(access_tok, 0, sizeof access_tok);
}

bool auth_logged(void) { return A.state == AUTH_IN && access_tok[0]; }
const AuthInfo *auth_info(void) { return &A; }
const char *auth_token(void) { return auth_logged() ? access_tok : ""; }

bool auth_web_cookies(const char *host, char *out, size_t sz)
{
	if (!auth_logged()) return false;
	for (int i = 0; i < MAX_WEB; i++) {
		if (web[i].cookie[0] && !strcmp(web[i].host, host)) {
			snprintf(out, sz, "Cookie: steamLoginSecure=%s; sessionid=%s", web[i].cookie, sessionid);
			return true;
		}
	}
	return false;
}

bool auth_web_jar(char *out, size_t sz)
{
	if (!auth_logged()) return false;
	size_t u = 0;
	out[0] = 0;
	for (int i = 0; i < MAX_WEB; i++) {
		if (!web[i].cookie[0]) continue;
		int w = snprintf(out + u, sz - u, "%s\tFALSE\t/\tTRUE\t0\tsteamLoginSecure\t%s\n%s\tFALSE\t/\tTRUE\t0\tsessionid\t%s\n",
			web[i].host, web[i].cookie, web[i].host, sessionid);
		if (w < 0 || (size_t)w >= sz - u) return false;
		u += w;
	}
	if (refresh_tok[0]) {
		int w = snprintf(out + u, sz - u, "login.steampowered.com\tFALSE\t/\tTRUE\t0\tsteamRefresh_steam\t%llu%%7C%%7C%s\n",
			(unsigned long long)A.steamid, refresh_tok);
		if (w < 0 || (size_t)w >= sz - u) return false;
	}
	return u > 0;
}

void auth_web_update(const char *host, const char *cookie)
{
	if (!cookie || strlen(cookie) < 20 || strlen(cookie) >= sizeof web[0].cookie) return;
	for (int i = 0; i < MAX_WEB; i++)
		if (web[i].cookie[0] && !strcmp(web[i].host, host)) {
			if (strcmp(web[i].cookie, cookie)) {
				snprintf(web[i].cookie, sizeof web[i].cookie, "%s", cookie);
				netlog("sesion: %s renovada por el camino", host);
			}
			return;
		}
}

const char *auth_sessionid(void) { return sessionid; }

bool auth_avatar_url(const char *sha_b64, char *out, size_t n)
{
	unsigned char sha[20];
	if (!sha_b64 || b64_decode(sha_b64, strlen(sha_b64), sha, sizeof sha) != 20) return false;
	char hex[41];
	for (int i = 0; i < 20; i++) snprintf(hex + i * 2, 3, "%02x", sha[i]);
	snprintf(out, n, "https://avatars.steamstatic.com/%s_medium.jpg", hex);
	return true;
}
