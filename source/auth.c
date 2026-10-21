#include "auth.h"
#include "app.h"
#include "cJSON.h"
#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define API "https://api.steampowered.com/"
#define SESSION_FILE APP_DIR "/session.ini"

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
// Sesión guardada en la SD

static void session_save(void)
{
	FILE *f = fopen(APP_DIR "/session.tmp", "w");
	if (!f) return;
	fprintf(f, "steamid=%llu\naccount=%s\nrefresh=%s\n", (unsigned long long)A.steamid, A.account, refresh_tok);
	fclose(f);
	remove(SESSION_FILE);
	rename(APP_DIR "/session.tmp", SESSION_FILE);
}

static void session_forget(void)
{
	remove(SESSION_FILE);
	memset(refresh_tok, 0, sizeof refresh_tok);
	memset(access_tok, 0, sizeof access_tok);
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

static void start_refresh(void)
{
	char *rt = url_escape(refresh_tok);
	char body[1300];
	snprintf(body, sizeof body, "refresh_token=%s&steamid=%llu&renewal_type=0", rt, (unsigned long long)A.steamid);
	free(rt);
	if (refresh_req) net_release(refresh_req);
	refresh_req = net_post(API "IAuthenticationService/GenerateAccessTokenForApp/v1/", body);
	netlog("sesion: renovando el token de acceso");
}

void auth_init(void)
{
	memset(&A, 0, sizeof A);
	if (session_load()) {
		set_state(AUTH_STARTING);
		start_refresh();
	} else {
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
	char *name = url_escape("Nintendo 3DS (" APP_NAME ")");
	char body[160];
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
	session_forget();
	set_state(AUTH_OUT);
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

	// 3) Token de acceso renovado (al arrancar con sesión guardada, o cuando va a caducar)
	if (refresh_req && refresh_req->state >= REQ_DONE) {
		cJSON *r = response_of(refresh_req, &root);
		const char *at = jstr(r, "access_token");
		if (at) {
			take_access(at);
			const char *rt = jstr(r, "refresh_token");
			if (rt) {
				snprintf(refresh_tok, sizeof refresh_tok, "%s", rt);
				session_save();
			}
			if (A.state != AUTH_IN) {
				set_state(AUTH_IN);
				fetch_profile();
			}
			netlog("sesion: token renovado");
		} else if (refresh_req->http == 401 || refresh_req->http == 403 || (refresh_req->state == REQ_DONE && !at)) {
			// Steam ya no acepta la sesión (se cerró desde otro sitio o caducó)
			netlog("sesion: Steam no acepta la sesion guardada (HTTP %ld)", refresh_req->http);
			snprintf(A.err, sizeof A.err, "%s", "expired");
			session_forget();
			set_state(AUTH_OUT);
		} else {
			// Sin red: se reintenta solo
			retry_at = now + 8000;
		}
		cJSON_Delete(root);
		net_release(refresh_req);
		refresh_req = NULL;
	}
	if (A.state == AUTH_STARTING && !refresh_req && retry_at && now >= retry_at) {
		retry_at = 0;
		start_refresh();
	}
	if (A.state == AUTH_IN && !refresh_req && access_got && now - access_got > (u64)(access_life > 900 ? access_life - 600 : 300) * 1000)
		start_refresh();

	// 4) Perfil: nombre, avatar y nivel
	if (profile_req && profile_req->state >= REQ_DONE) {
		cJSON *r = response_of(profile_req, &root);
		const cJSON *acc = cJSON_GetArrayItem(r ? cJSON_GetObjectItemCaseSensitive(r, "accounts") : NULL, 0);
		const cJSON *pd = acc ? cJSON_GetObjectItemCaseSensitive(acc, "public_data") : NULL;
		const char *name = jstr(pd, "persona_name"), *av = jstr(pd, "sha_digest_avatar");
		if (name) snprintf(A.persona, sizeof A.persona, "%s", name);
		if (av) {
			unsigned char sha[20];
			size_t n = b64_decode(av, strlen(av), sha, sizeof sha);
			if (n == 20) {
				char hex[41];
				for (int i = 0; i < 20; i++) snprintf(hex + i * 2, 3, "%02x", sha[i]);
				snprintf(A.avatar, sizeof A.avatar, "https://avatars.steamstatic.com/%s_medium.jpg", hex);
			}
		}
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
	memset(refresh_tok, 0, sizeof refresh_tok);
	memset(access_tok, 0, sizeof access_tok);
}

bool auth_logged(void) { return A.state == AUTH_IN && access_tok[0]; }
const AuthInfo *auth_info(void) { return &A; }
const char *auth_token(void) { return auth_logged() ? access_tok : ""; }
