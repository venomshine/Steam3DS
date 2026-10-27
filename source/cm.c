#include "cm.h"
#include "cJSON.h"

#include <curl/curl.h>
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef CM_HOST_TEST
#include <pthread.h>
#include <unistd.h>
#include <stdarg.h>
static void netlog(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
}
static void nap(int ms) { usleep(ms * 1000); }
static unsigned long long now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (unsigned long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}
#define CM_CA NULL
#define CM_UA "Steam3DS"
#else
#include <3ds.h>
#include "app.h"
#include "net.h"
static void nap(int ms) { svcSleepThread((s64)ms * 1000000); }
static unsigned long long now_ms(void) { return osGetTime(); }
#define CM_CA "romfs:/certs/cacert.pem"
#define CM_UA APP_USER_AGENT
#endif

#define PROTOCOL_VERSION 65580
#define EMSG_MULTI 1
#define EMSG_SERVICE_RESPONSE 147
#define EMSG_SERVICE_CALL 151
#define EMSG_LOGOFF 706
#define EMSG_LICENSE_LIST 780
#define EMSG_LOGON_RESPONSE 751
#define EMSG_LOGON 5514
#define EMSG_PICS_INFO 8903
#define EMSG_PICS_INFO_RESPONSE 8904
#define EMSG_PICS_TOKENS 8905
#define EMSG_PICS_TOKENS_RESPONSE 8906
#define PROTO_MASK 0x80000000u

// ---------------------------------------------------------------------------- protobuf (lo justo)

typedef struct { uint8_t *b; size_t n, cap; bool bad; } Pb;

static void pb_put(Pb *p, const void *d, size_t n)
{
	if (p->bad) return;
	if (p->n + n > p->cap) {
		size_t cap = p->cap ? p->cap * 2 : 256;
		while (cap < p->n + n) cap *= 2;
		uint8_t *nb = realloc(p->b, cap);
		if (!nb) {
			p->bad = true;
			return;
		}
		p->b = nb;
		p->cap = cap;
	}
	memcpy(p->b + p->n, d, n);
	p->n += n;
}

static void pb_varint(Pb *p, uint64_t v)
{
	uint8_t t[10];
	int k = 0;
	do {
		t[k] = v & 0x7f;
		v >>= 7;
		if (v) t[k] |= 0x80;
		k++;
	} while (v);
	pb_put(p, t, k);
}

static void pb_u64(Pb *p, int field, uint64_t v)
{
	pb_varint(p, (uint64_t)field << 3);
	pb_varint(p, v);
}

static void pb_fixed64(Pb *p, int field, uint64_t v)
{
	pb_varint(p, ((uint64_t)field << 3) | 1);
	uint8_t t[8];
	for (int i = 0; i < 8; i++) t[i] = (uint8_t)(v >> (8 * i));
	pb_put(p, t, 8);
}

static void pb_bytes(Pb *p, int field, const void *d, size_t n)
{
	pb_varint(p, ((uint64_t)field << 3) | 2);
	pb_varint(p, n);
	pb_put(p, d, n);
}

static void pb_str(Pb *p, int field, const char *s) { pb_bytes(p, field, s, strlen(s)); }

// Lectura: un campo cada vez
typedef struct { const uint8_t *p, *end; } PbR;
typedef struct { int field, wire; uint64_t v; const uint8_t *d; size_t len; } PbF;

static bool pbr_varint(PbR *r, uint64_t *out)
{
	uint64_t v = 0;
	for (int s = 0; s < 64 && r->p < r->end; s += 7) {
		uint8_t c = *r->p++;
		v |= (uint64_t)(c & 0x7f) << s;
		if (!(c & 0x80)) {
			*out = v;
			return true;
		}
	}
	return false;
}

static bool pbr_next(PbR *r, PbF *f)
{
	uint64_t key;
	if (r->p >= r->end || !pbr_varint(r, &key)) return false;
	f->field = (int)(key >> 3);
	f->wire = (int)(key & 7);
	f->v = 0;
	f->d = NULL;
	f->len = 0;
	switch (f->wire) {
	case 0: return pbr_varint(r, &f->v);
	case 1:
		if (r->end - r->p < 8) return false;
		for (int i = 0; i < 8; i++) f->v |= (uint64_t)r->p[i] << (8 * i);
		r->p += 8;
		return true;
	case 5:
		if (r->end - r->p < 4) return false;
		for (int i = 0; i < 4; i++) f->v |= (uint64_t)r->p[i] << (8 * i);
		r->p += 4;
		return true;
	case 2: {
		uint64_t n;
		if (!pbr_varint(r, &n) || (uint64_t)(r->end - r->p) < n) return false;
		f->d = r->p;
		f->len = (size_t)n;
		r->p += n;
		return true;
	}
	}
	return false;
}

// ---------------------------------------------------------------------------- WebSocket sobre curl

typedef struct {
	CURL *c;
	uint8_t *in;
	size_t nin, capin;
	unsigned long long deadline;
	char *err;
	size_t errsz;
} Ws;

static bool ws_fail(Ws *w, const char *msg)
{
	if (w->err && !w->err[0]) snprintf(w->err, w->errsz, "%s", msg);
	return false;
}

static bool raw_send(Ws *w, const uint8_t *d, size_t n)
{
	while (n) {
		size_t sent = 0;
		CURLcode rc = curl_easy_send(w->c, d, n, &sent);
		if (rc == CURLE_AGAIN) {
			if (now_ms() > w->deadline) return ws_fail(w, "tiempo agotado al enviar");
			nap(5);
			continue;
		}
		if (rc != CURLE_OK) return ws_fail(w, "no se pudo enviar");
		d += sent;
		n -= sent;
	}
	return true;
}

// Lee lo que haya (espera hasta que llegue algo o se acabe el tiempo)
static bool raw_more(Ws *w)
{
	for (;;) {
		if (w->nin + 4096 > w->capin) {
			size_t cap = w->capin ? w->capin * 2 : 16384;
			uint8_t *nb = realloc(w->in, cap);
			if (!nb) return ws_fail(w, "sin memoria");
			w->in = nb;
			w->capin = cap;
		}
		size_t got = 0;
		CURLcode rc = curl_easy_recv(w->c, w->in + w->nin, w->capin - w->nin, &got);
		if (rc == CURLE_OK && got > 0) {
			w->nin += got;
			return true;
		}
		if (rc == CURLE_OK && got == 0) return ws_fail(w, "Steam cerró la conexión");
		if (rc != CURLE_AGAIN) return ws_fail(w, "error al recibir");
		if (now_ms() > w->deadline) return ws_fail(w, "Steam no contestó a tiempo");
		nap(10);
	}
}

static void ws_drop(Ws *w, size_t n)
{
	memmove(w->in, w->in + n, w->nin - n);
	w->nin -= n;
}

// Una trama del cliente: siempre con máscara
static bool ws_send_frame(Ws *w, int opcode, const uint8_t *d, size_t n)
{
	uint8_t h[14];
	size_t hn = 0;
	h[hn++] = 0x80 | (uint8_t)opcode;
	if (n < 126) h[hn++] = 0x80 | (uint8_t)n;
	else if (n < 65536) {
		h[hn++] = 0x80 | 126;
		h[hn++] = (uint8_t)(n >> 8);
		h[hn++] = (uint8_t)n;
	} else {
		h[hn++] = 0x80 | 127;
		for (int i = 7; i >= 0; i--) h[hn++] = (uint8_t)((uint64_t)n >> (8 * i));
	}
	uint8_t mask[4];
	for (int i = 0; i < 4; i++) mask[i] = (uint8_t)(rand() & 0xff);
	memcpy(h + hn, mask, 4);
	hn += 4;
	uint8_t *buf = malloc(hn + n);
	if (!buf) return ws_fail(w, "sin memoria");
	memcpy(buf, h, hn);
	for (size_t i = 0; i < n; i++) buf[hn + i] = d[i] ^ mask[i & 3];
	bool ok = raw_send(w, buf, hn + n);
	free(buf);
	return ok;
}

// El siguiente mensaje binario entero (*out se libera con free). Contesta los ping
static bool ws_recv_msg(Ws *w, uint8_t **out, size_t *outn)
{
	uint8_t *msg = NULL;
	size_t mn = 0;
	for (;;) {
		while (w->nin < 2)
			if (!raw_more(w)) goto fail;
		size_t need = 2;
		uint64_t len = w->in[1] & 0x7f;
		if (len == 126) need = 4;
		else if (len == 127) need = 10;
		if (w->in[1] & 0x80) need += 4;
		while (w->nin < need)
			if (!raw_more(w)) goto fail;
		if (len == 126) len = ((uint64_t)w->in[2] << 8) | w->in[3];
		else if (len == 127) {
			len = 0;
			for (int i = 0; i < 8; i++) len = (len << 8) | w->in[2 + i];
		}
		if (len > 16 * 1024 * 1024) {
			ws_fail(w, "mensaje demasiado grande");
			goto fail;
		}
		while (w->nin < need + len)
			if (!raw_more(w)) goto fail;
		int op = w->in[0] & 0x0f;
		bool fin = w->in[0] & 0x80;
		const uint8_t *pay = w->in + need;
		if (op == 0x9) {
			ws_send_frame(w, 0xA, pay, (size_t)len);
		} else if (op == 0x8) {
			ws_fail(w, "Steam cerró la conexión");
			goto fail;
		} else if (op == 0x2 || op == 0x0) {
			uint8_t *nm = realloc(msg, mn + (size_t)len + 1);
			if (!nm) {
				ws_fail(w, "sin memoria");
				goto fail;
			}
			msg = nm;
			memcpy(msg + mn, pay, (size_t)len);
			mn += (size_t)len;
			if (fin) {
				ws_drop(w, need + (size_t)len);
				*out = msg;
				*outn = mn;
				return true;
			}
		}
		ws_drop(w, need + (size_t)len);
	}
fail:
	free(msg);
	return false;
}

static void b64(const uint8_t *in, size_t n, char *out)
{
	static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	size_t o = 0;
	for (size_t i = 0; i < n; i += 3) {
		uint32_t v = in[i] << 16 | (i + 1 < n ? in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
		out[o++] = t[(v >> 18) & 63];
		out[o++] = t[(v >> 12) & 63];
		out[o++] = i + 1 < n ? t[(v >> 6) & 63] : '=';
		out[o++] = i + 2 < n ? t[v & 63] : '=';
	}
	out[o] = 0;
}

static bool ws_open(Ws *w, const char *endpoint)
{
	char host[96];
	snprintf(host, sizeof host, "%s", endpoint);
	char *colon = strchr(host, ':');
	int port = colon ? atoi(colon + 1) : 443;
	if (colon) *colon = 0;
	char url[160];
	snprintf(url, sizeof url, "https://%s:%d/", host, port);
	w->c = curl_easy_init();
	if (!w->c) return ws_fail(w, "curl");
	curl_easy_setopt(w->c, CURLOPT_URL, url);
	curl_easy_setopt(w->c, CURLOPT_CONNECT_ONLY, 1L);
	curl_easy_setopt(w->c, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(w->c, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(w->c, CURLOPT_NOSIGNAL, 1L);
	if (CM_CA) curl_easy_setopt(w->c, CURLOPT_CAINFO, CM_CA);
	CURLcode rc = curl_easy_perform(w->c);
	if (rc != CURLE_OK) {
		char e[96];
		snprintf(e, sizeof e, "no se pudo conectar con el servidor de Steam (curl=%d)", rc);
		return ws_fail(w, e);
	}
	// El saludo WebSocket, como el del chat web
	uint8_t key[16];
	for (int i = 0; i < 16; i++) key[i] = (uint8_t)(rand() & 0xff);
	char k64[32];
	b64(key, 16, k64);
	char req[512];
	int rn = snprintf(req, sizeof req,
		"GET /cmsocket/ HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
		"Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nOrigin: https://steamcommunity.com\r\n"
		"User-Agent: %s\r\n\r\n", host, k64, CM_UA);
	if (!raw_send(w, (const uint8_t *)req, (size_t)rn)) return false;
	for (;;) {
		if (!raw_more(w)) return false;
		uint8_t *end = NULL;
		for (size_t i = 3; i < w->nin && !end; i++)
			if (w->in[i - 3] == '\r' && w->in[i - 2] == '\n' && w->in[i - 1] == '\r' && w->in[i] == '\n') end = w->in + i + 1;
		if (!end) continue;
		bool ok = w->nin > 12 && !memcmp(w->in, "HTTP/1.1 101", 12);
		ws_drop(w, (size_t)(end - w->in));
		if (!ok) return ws_fail(w, "el servidor de Steam no aceptó la conexión");
		return true;
	}
}

static void ws_close(Ws *w)
{
	if (w->c) {
		ws_send_frame(w, 0x8, NULL, 0);
		curl_easy_cleanup(w->c);
	}
	free(w->in);
	memset(w, 0, sizeof *w);
}

// ---------------------------------------------------------------------------- mensajes de Steam

typedef struct {
	uint64_t steamid;
	int32_t session;
	// lo que se espera
	bool logged_on;
	int logon_eresult;
	uint64_t want_job;    // el trabajo cuya respuesta se espera
	bool got_response;
	int resp_eresult;
	uint8_t *resp;
	size_t resp_n;
	// tus licencias (las manda Steam solo al iniciar sesión): paquete y su token
	bool got_licenses;
	uint32_t *pkgs;
	uint64_t *pkg_tok;
	int npkgs;
} Cm;

static bool cm_send(Ws *w, Cm *c, uint32_t emsg, Pb *hdr_extra, Pb *body)
{
	Pb h = {0};
	if (c->steamid) pb_fixed64(&h, 1, c->steamid);
	if (c->session) pb_u64(&h, 2, (uint32_t)c->session);
	if (hdr_extra && hdr_extra->n) pb_put(&h, hdr_extra->b, hdr_extra->n);
	Pb m = {0};
	uint32_t e = emsg | PROTO_MASK, hl = (uint32_t)h.n;
	uint8_t t[8] = {e, e >> 8, e >> 16, e >> 24, hl, hl >> 8, hl >> 16, hl >> 24};
	pb_put(&m, t, 8);
	pb_put(&m, h.b, h.n);
	if (body && body->n) pb_put(&m, body->b, body->n);
	bool ok = !m.bad && !h.bad && ws_send_frame(w, 0x2, m.b, m.n);
	free(h.b);
	free(m.b);
	return ok;
}

static bool gunzip(const uint8_t *in, size_t n, uint8_t **out, size_t *outn, size_t hint)
{
	size_t cap = hint ? hint : n * 4;
	uint8_t *b = malloc(cap + 1);
	if (!b) return false;
	z_stream z;
	memset(&z, 0, sizeof z);
	if (inflateInit2(&z, 16 + MAX_WBITS) != Z_OK) {
		free(b);
		return false;
	}
	z.next_in = (Bytef *)in;
	z.avail_in = (uInt)n;
	int rc;
	do {
		if (z.total_out >= cap) {
			uint8_t *nb = realloc(b, cap * 2 + 1);
			if (!nb) break;
			b = nb;
			cap *= 2;
		}
		z.next_out = b + z.total_out;
		z.avail_out = (uInt)(cap - z.total_out);
		rc = inflate(&z, Z_NO_FLUSH);
	} while (rc == Z_OK);
	size_t got = z.total_out;
	inflateEnd(&z);
	if (rc != Z_STREAM_END) {
		free(b);
		return false;
	}
	*out = b;
	*outn = got;
	return true;
}

static void cm_packet(Cm *c, const uint8_t *d, size_t n, int depth);

static void cm_multi(Cm *c, const uint8_t *body, size_t n, int depth)
{
	PbR r = {body, body + n};
	PbF f;
	uint64_t unz = 0;
	const uint8_t *mb = NULL;
	size_t ml = 0;
	while (pbr_next(&r, &f)) {
		if (f.field == 1 && f.wire == 0) unz = f.v;
		if (f.field == 2 && f.wire == 2) {
			mb = f.d;
			ml = f.len;
		}
	}
	if (!mb) return;
	uint8_t *plain = NULL;
	size_t pn = ml;
	if (unz) {
		if (!gunzip(mb, ml, &plain, &pn, (size_t)unz)) return;
		mb = plain;
	}
	for (size_t i = 0; i + 4 <= pn;) {
		uint32_t len = mb[i] | mb[i + 1] << 8 | mb[i + 2] << 16 | (uint32_t)mb[i + 3] << 24;
		i += 4;
		if (len > pn - i) break;
		cm_packet(c, mb + i, len, depth + 1);
		i += len;
	}
	free(plain);
}

static void cm_packet(Cm *c, const uint8_t *d, size_t n, int depth)
{
	if (n < 8 || depth > 3) return;
	uint32_t raw = d[0] | d[1] << 8 | d[2] << 16 | (uint32_t)d[3] << 24;
	if (!(raw & PROTO_MASK)) return;   // los mensajes antiguos (sin protobuf) no hacen falta
	uint32_t emsg = raw & ~PROTO_MASK;
	uint32_t hl = d[4] | d[5] << 8 | d[6] << 16 | (uint32_t)d[7] << 24;
	if (hl > n - 8) return;
	const uint8_t *hdr = d + 8, *body = d + 8 + hl;
	size_t bn = n - 8 - hl;
	// La cabecera: tu cuenta, la sesión, el trabajo al que contesta y el resultado
	uint64_t sid = 0, job_target = 0;
	int64_t session = 0, eresult = 2;
	PbR r = {hdr, hdr + hl};
	PbF f;
	while (pbr_next(&r, &f)) {
		if (f.field == 1) sid = f.v;
		else if (f.field == 2) session = (int64_t)f.v;
		else if (f.field == 11) job_target = f.v;
		else if (f.field == 13) eresult = (int64_t)f.v;
	}
	if (emsg == EMSG_MULTI) {
		cm_multi(c, body, bn, depth);
	} else if (emsg == EMSG_LOGON_RESPONSE) {
		int er = 2;
		PbR b = {body, body + bn};
		while (pbr_next(&b, &f))
			if (f.field == 1) er = (int)(int32_t)f.v;
		c->logon_eresult = er;
		c->logged_on = true;
		if (sid) c->steamid = sid;
		c->session = (int32_t)session;
	} else if (emsg == EMSG_LICENSE_LIST && !c->got_licenses) {
		// Las caducadas, canceladas o prestadas por otro (familia) no cuentan
		PbR b = {body, body + bn};
		int cap = 0;
		while (pbr_next(&b, &f)) {
			if (f.field != 2 || f.wire != 2) continue;
			uint64_t pkg = 0, flags = 0, tok = 0, owner = 0;
			PbR l = {f.d, f.d + f.len};
			PbF g;
			while (pbr_next(&l, &g)) {
				if (g.field == 1) pkg = g.v;
				else if (g.field == 7) flags = g.v;
				else if (g.field == 12) owner = g.v;
				else if (g.field == 17) tok = g.v;
			}
			if (!pkg || (flags & (8 | 16 | 32 | 1024)) || (owner && owner != (uint32_t)c->steamid)) continue;
			if (c->npkgs == cap) {
				cap = cap ? cap * 2 : 128;
				uint32_t *np = realloc(c->pkgs, cap * sizeof(uint32_t));
				uint64_t *nt = realloc(c->pkg_tok, cap * sizeof(uint64_t));
				if (np) c->pkgs = np;
				if (nt) c->pkg_tok = nt;
				if (!np || !nt) break;
			}
			c->pkgs[c->npkgs] = (uint32_t)pkg;
			c->pkg_tok[c->npkgs++] = tok;
		}
		c->got_licenses = true;
	} else if (job_target && job_target == c->want_job) {
		c->got_response = true;
		c->resp_eresult = (int)eresult;
		free(c->resp);
		c->resp = malloc(bn + 1);
		if (c->resp) {
			memcpy(c->resp, body, bn);
			c->resp_n = bn;
		}
	}
}

// Espera (como mucho hasta el plazo) a que pase *flag
static bool cm_wait(Ws *w, Cm *c, bool *flag)
{
	while (!*flag) {
		uint8_t *m = NULL;
		size_t mn = 0;
		if (!ws_recv_msg(w, &m, &mn)) return false;
		cm_packet(c, m, mn, 0);
		free(m);
	}
	return true;
}

// ---------------------------------------------------------------------------- lo que se pide por HTTP

typedef struct { char *d; size_t n; } Buf;

static size_t buf_write(char *p, size_t sz, size_t nm, void *ud)
{
	Buf *b = ud;
	size_t add = sz * nm;
	if (b->n + add > 4 * 1024 * 1024) return 0;
	char *nb = realloc(b->d, b->n + add + 1);
	if (!nb) return 0;
	b->d = nb;
	memcpy(b->d + b->n, p, add);
	b->n += add;
	b->d[b->n] = 0;
	return add;
}

// GET con las cookies de la sesión (cada una a su dominio, como un navegador)
static char *http_get(const char *url, const char *jar)
{
	CURL *c = curl_easy_init();
	if (!c) return NULL;
	Buf b = {0};
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_USERAGENT, CM_UA);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
	if (CM_CA) curl_easy_setopt(c, CURLOPT_CAINFO, CM_CA);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, buf_write);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &b);
	if (jar && *jar) {
		curl_easy_setopt(c, CURLOPT_COOKIEFILE, "");
		char *copy = strdup(jar), *save = NULL;
		for (char *line = copy ? strtok_r(copy, "\n", &save) : NULL; line; line = strtok_r(NULL, "\n", &save))
			curl_easy_setopt(c, CURLOPT_COOKIELIST, line);
		if (copy) {
			memset(copy, 0, strlen(jar));
			free(copy);
		}
	}
	CURLcode rc = curl_easy_perform(c);
	long http = 0;
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
	if (jar) curl_easy_setopt(c, CURLOPT_COOKIELIST, "ALL");
	curl_easy_cleanup(c);
	char shown[96];
	snprintf(shown, sizeof shown, "%s", url);
	char *q = strchr(shown, '?');
	if (q) *q = 0;
	netlog("cm: GET %s -> %ld (curl=%d)", shown, http, rc);
	if (rc != CURLE_OK || http != 200) {
		free(b.d);
		return NULL;
	}
	return b.d;
}

// ---------------------------------------------------------------------------- tus ocultos

static int parse_hidden(Cm *c, uint32_t **ids)
{
	int n = 0, cap = 0;
	PbR r = {c->resp, c->resp + c->resp_n};
	PbF f, g, h;
	while (pbr_next(&r, &f)) {
		if (f.field != 1 || f.wire != 2) continue;   // data (CCloudConfigStore_NamespaceData)
		PbR rd = {f.d, f.d + f.len};
		while (pbr_next(&rd, &g)) {
			if (g.field != 3 || g.wire != 2) continue;   // entries
			const uint8_t *key = NULL, *val = NULL;
			size_t kl = 0, vl = 0;
			bool deleted = false;
			PbR re = {g.d, g.d + g.len};
			while (pbr_next(&re, &h)) {
				if (h.field == 1 && h.wire == 2) {
					key = h.d;
					kl = h.len;
				} else if (h.field == 2) {
					deleted = h.v != 0;
				} else if (h.field == 3 && h.wire == 2) {
					val = h.d;
					vl = h.len;
				}
			}
			static const char want[] = "user-collections.hidden";
			if (deleted || !key || kl != sizeof want - 1 || memcmp(key, want, kl) || !val) continue;
			char *js = malloc(vl + 1);
			if (!js) continue;
			memcpy(js, val, vl);
			js[vl] = 0;
			cJSON *o = cJSON_Parse(js);
			free(js);
			const cJSON *it;
			cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(o, "added")) {
				if (!cJSON_IsNumber(it) || it->valuedouble <= 0) continue;
				if (n == cap) {
					cap = cap ? cap * 2 : 64;
					uint32_t *nn = realloc(*ids, cap * sizeof(uint32_t));
					if (!nn) break;
					*ids = nn;
				}
				(*ids)[n++] = (uint32_t)it->valuedouble;
			}
			cJSON_Delete(o);
		}
	}
	return n;
}

// ---------------------------------------------------------------------------- playtests terminados

// ¿Qué dice la ficha de la app (la que lee el cliente de Steam)? En «common», releasestate: un playtest
// que el estudio ya cerró deja de estar «released» y el cliente no lo deja abrir
static bool appinfo_closed(const uint8_t *d, size_t n)
{
	static const char key[] = "\"releasestate\"";
	for (size_t i = 0; i + sizeof key < n; i++) {
		if (memcmp(d + i, key, sizeof key - 1)) continue;
		size_t j = i + sizeof key - 1;
		while (j < n && d[j] != '"') j++;
		size_t k = ++j;
		while (k < n && d[k] != '"') k++;
		if (k >= n) return false;
		return !(k - j == 8 && !memcmp(d + j, "released", 8));
	}
	return false;   // sin el dato (sin permiso para verla), no se toca
}

// Los tokens de acceso de tus apps (sin ellos, la ficha de un playtest llega vacía) y después las
// fichas. Devuelve cuántas están cerradas (en *out) o -1
static int pics_closed(Ws *w, Cm *c, const uint32_t *apps, int n, uint32_t **out, char *err, size_t errsz)
{
	Pb hdr = {0}, req = {0};
	c->want_job = 2;
	c->got_response = false;
	pb_fixed64(&hdr, 10, 2);
	for (int i = 0; i < n; i++) pb_u64(&req, 2, apps[i]);
	bool sent = cm_send(w, c, EMSG_PICS_TOKENS, &hdr, &req);
	free(hdr.b);
	free(req.b);
	if (!sent || !cm_wait(w, c, &c->got_response)) return -1;
	uint64_t *tok = calloc(n, sizeof(uint64_t));
	if (!tok) return -1;
	int ntok = 0;
	PbR r = {c->resp, c->resp + c->resp_n};
	PbF f, g;
	while (pbr_next(&r, &f)) {
		if (f.field != 3 || f.wire != 2) continue;   // app_access_tokens
		uint64_t id = 0, at = 0;
		PbR a = {f.d, f.d + f.len};
		while (pbr_next(&a, &g)) {
			if (g.field == 1) id = g.v;
			else if (g.field == 2) at = g.v;
		}
		for (int i = 0; i < n; i++)
			if (apps[i] == id) {
				tok[i] = at;
				ntok++;
			}
	}
	netlog("cm: %d de %d playtests con permiso para ver su ficha", ntok, n);
	hdr = (Pb){0};
	req = (Pb){0};
	c->want_job = 3;
	c->got_response = false;
	pb_fixed64(&hdr, 10, 3);
	for (int i = 0; i < n; i++) {
		Pb a = {0};
		pb_u64(&a, 1, apps[i]);
		if (tok[i]) pb_u64(&a, 2, tok[i]);
		pb_bytes(&req, 2, a.b, a.n);
		free(a.b);
	}
	pb_u64(&req, 7, 1);   // todo en una respuesta
	free(tok);
	sent = cm_send(w, c, EMSG_PICS_INFO, &hdr, &req);
	free(hdr.b);
	free(req.b);
	if (!sent || !cm_wait(w, c, &c->got_response)) return -1;
	int closed = 0, seen = 0;
	*out = malloc(n * sizeof(uint32_t));
	if (!*out) return -1;
	r = (PbR){c->resp, c->resp + c->resp_n};
	while (pbr_next(&r, &f)) {
		if (f.field != 1 || f.wire != 2) continue;   // apps
		uint64_t id = 0;
		const uint8_t *buf = NULL;
		size_t bl = 0;
		PbR a = {f.d, f.d + f.len};
		while (pbr_next(&a, &g)) {
			if (g.field == 1) id = g.v;
			else if (g.field == 5 && g.wire == 2) {
				buf = g.d;
				bl = g.len;
			}
		}
		if (!buf) continue;
		seen++;
		if (appinfo_closed(buf, bl) && closed < n) (*out)[closed++] = (uint32_t)id;
	}
	netlog("cm: %d fichas leídas, %d playtests cerrados", seen, closed);
	(void)err;
	(void)errsz;
	return closed;
}

// ---------------------------------------------------------------------------- tus licencias

// Como cm_wait, pero como mucho ms (lo que llega a medias se queda para después)
static bool cm_wait_ms(Ws *w, Cm *c, bool *flag, int ms)
{
	unsigned long long keep = w->deadline, until = now_ms() + ms;
	if (until < w->deadline) w->deadline = until;
	char *err = w->err, tmp[64] = "";
	size_t errsz = w->errsz;
	w->err = tmp;   // que no tarde no es un error de la sesión
	w->errsz = sizeof tmp;
	bool ok = cm_wait(w, c, flag);
	w->err = err;
	w->errsz = errsz;
	w->deadline = keep;
	return ok;
}

typedef struct { uint32_t *ids; int n, cap; } Ids;

static void ids_add(Ids *a, uint32_t id)
{
	for (int i = 0; i < a->n; i++)
		if (a->ids[i] == id) return;
	if (a->n == a->cap) {
		int cap = a->cap ? a->cap * 2 : 256;
		uint32_t *n = realloc(a->ids, cap * sizeof(uint32_t));
		if (!n) return;
		a->ids = n;
		a->cap = cap;
	}
	a->ids[a->n++] = id;
}

// La ficha de un paquete viene en KeyValues binario: se recogen los números de «appids»
static const uint8_t *kv_walk(const uint8_t *p, const uint8_t *end, bool in_apps, Ids *out, int depth)
{
	while (p && p < end) {
		uint8_t type = *p++;
		if (type == 8 || type == 11) return p;
		const uint8_t *name = p;
		while (p < end && *p) p++;
		if (p >= end) return NULL;
		size_t nl = (size_t)(p - name);
		p++;
		switch (type) {
		case 0:
			if (depth > 8) return NULL;
			p = kv_walk(p, end, nl == 6 && !memcmp(name, "appids", 6), out, depth + 1);
			break;
		case 1:
			while (p < end && *p) p++;
			p++;
			break;
		case 2: case 3: case 4: case 6:
			if (p + 4 > end) return NULL;
			if (in_apps && type == 2) ids_add(out, p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24);
			p += 4;
			break;
		case 7: case 10:
			p += 8;
			break;
		default:
			return NULL;   // (5: texto ancho, no se usa en los paquetes)
		}
	}
	return p;
}

// Las apps de tus licencias: lo que el cliente de Steam enseña en la biblioteca (también las demos
// que ni GetOwnedGames ni la tienda dan). -1 si Steam no mandó las licencias
static int licensed_apps(Ws *w, Cm *c, uint32_t **out)
{
	if (!c->got_licenses) cm_wait_ms(w, c, &c->got_licenses, 4000);
	if (!c->got_licenses) {
		netlog("cm: Steam no mandó tus licencias");
		return -1;
	}
	Ids apps = {0};
	for (int at = 0; at < c->npkgs; at += 250) {
		int k = c->npkgs - at < 250 ? c->npkgs - at : 250;
		Pb hdr = {0}, req = {0};
		c->want_job = 10 + at;
		c->got_response = false;
		pb_fixed64(&hdr, 10, c->want_job);
		for (int i = at; i < at + k; i++) {
			Pb a = {0};
			pb_u64(&a, 1, c->pkgs[i]);
			if (c->pkg_tok[i]) pb_u64(&a, 2, c->pkg_tok[i]);
			pb_bytes(&req, 1, a.b, a.n);
			free(a.b);
		}
		pb_u64(&req, 7, 1);
		bool sent = cm_send(w, c, EMSG_PICS_INFO, &hdr, &req);
		free(hdr.b);
		free(req.b);
		if (!sent || !cm_wait(w, c, &c->got_response)) break;
		PbR r = {c->resp, c->resp + c->resp_n};
		PbF f, g;
		while (pbr_next(&r, &f)) {
			if (f.field != 3 || f.wire != 2) continue;   // packages
			PbR a = {f.d, f.d + f.len};
			while (pbr_next(&a, &g))
				if (g.field == 5 && g.wire == 2 && g.len > 4) kv_walk(g.d + 4, g.d + g.len, false, &apps, 0);
		}
	}
	netlog("cm: %d licencias, %d apps", c->npkgs, apps.n);
	*out = apps.ids;
	return apps.n;
}

// ---------------------------------------------------------------------------- la sesión

// Lo que se pide en la sesión: tus ocultos, o qué playtests de la lista ya están cerrados
typedef struct {
	int kind;   // 0 ocultos (y las apps de tus licencias), 1 playtests
	const uint32_t *apps;
	int napps;
	uint32_t *lic;   // salida (kind 0)
	int nlic;
} Job;

static int hidden_call(Ws *w, Cm *c, uint32_t **ids, char *err, size_t errsz)
{
	// CloudConfigStore.Download#1, espacio 1 (las colecciones de tu biblioteca)
	Pb hdr = {0}, req = {0}, nv = {0};
	c->want_job = 1;
	pb_fixed64(&hdr, 10, 1);
	pb_str(&hdr, 12, "CloudConfigStore.Download#1");
	pb_u64(&nv, 1, 1);
	pb_u64(&nv, 2, 0);
	pb_bytes(&req, 1, nv.b, nv.n);
	bool sent = cm_send(w, c, EMSG_SERVICE_CALL, &hdr, &req);
	free(hdr.b);
	free(req.b);
	free(nv.b);
	if (!sent || !cm_wait(w, c, &c->got_response)) return -1;
	netlog("cm: colecciones -> eresult %d, %u bytes", c->resp_eresult, (unsigned)c->resp_n);
	if (c->resp_eresult != 1) {
		snprintf(err, errsz, "Steam no dio tus colecciones (eresult %d)", c->resp_eresult);
		return -1;
	}
	return parse_hidden(c, ids);
}

// La sesión con el servidor: con token del chat web (o anónima, solo para las pruebas sin cuenta)
static int cm_session(const char *endpoint, const char *account, const char *token, uint64_t steamid,
                      Job *job, uint32_t **ids, char *err, size_t errsz)
{
	Ws w = {0};
	w.err = err;
	w.errsz = errsz;
	w.deadline = now_ms() + 20000;
	Cm c = {0};
	int result = -1;
	if (!ws_open(&w, endpoint)) goto out;
	netlog("cm: conectado a %s", endpoint);
	// Iniciar sesión como el chat web (o anónimo)
	Pb body = {0};
	pb_u64(&body, 1, PROTOCOL_VERSION);
	if (token) {
		c.steamid = steamid;
		pb_u64(&body, 7, 4294966596u);   // client_os_type: la web
		pb_u64(&body, 32, 4);            // ui_mode: la web
		pb_u64(&body, 33, 2);            // chat_mode: chat nuevo
		pb_str(&body, 50, account);
		pb_str(&body, 103, token);       // web_logon_nonce
	} else {
		c.steamid = 0x01A0000000000000ULL;   // usuario anónimo
		pb_u64(&body, 7, 16);
		pb_str(&body, 80, "anonymous");
	}
	bool sent = cm_send(&w, &c, EMSG_LOGON, NULL, &body);
	free(body.b);
	if (!sent || !cm_wait(&w, &c, &c.logged_on)) goto out;
	netlog("cm: inicio de sesión -> eresult %d", c.logon_eresult);
	if (c.logon_eresult != 1) {
		snprintf(err, errsz, "Steam no aceptó la sesión (eresult %d)", c.logon_eresult);
		goto out;
	}
	if (job->kind == 1) result = pics_closed(&w, &c, job->apps, job->napps, ids, err, errsz);
	else {
		result = hidden_call(&w, &c, ids, err, errsz);
		job->nlic = licensed_apps(&w, &c, &job->lic);
	}
	cm_send(&w, &c, EMSG_LOGOFF, NULL, NULL);
out:
	free(c.resp);
	free(c.pkgs);
	free(c.pkg_tok);
	ws_close(&w);
	return result;
}

static int pick_endpoint(char *out, size_t n)
{
	char *js = http_get("https://api.steampowered.com/ISteamDirectory/GetCMListForConnect/v1/?cellid=0&cmtype=websockets&realm=steamglobal&maxcount=8", NULL);
	cJSON *root = js ? cJSON_Parse(js) : NULL;
	free(js);
	const cJSON *list = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "response"), "serverlist");
	int count = cJSON_GetArraySize(list);
	const cJSON *pick = count > 0 ? cJSON_GetArrayItem(list, rand() % (count < 3 ? count : 3)) : NULL;   // uno de los mejores
	const cJSON *ep = pick ? cJSON_GetObjectItemCaseSensitive(pick, "endpoint") : NULL;
	bool ok = cJSON_IsString(ep);
	if (ok) snprintf(out, n, "%s", ep->valuestring);
	cJSON_Delete(root);
	return ok;
}

// Con tu sesión web: el token del chat web, un servidor de Steam y la sesión con el trabajo
static int cm_run(const char *jar, Job *job, uint32_t **ids, char *err, size_t errsz)
{
	*ids = NULL;
	err[0] = 0;
	srand((unsigned)now_ms());
	// 1) El token del chat web (el mismo que pide steamcommunity.com/chat)
	char *js = http_get("https://steamcommunity.com/chat/clientjstoken", jar);
	cJSON *root = js ? cJSON_Parse(js) : NULL;
	if (js) memset(js, 0, strlen(js));
	free(js);
	const char *token = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "token"));
	const char *account = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "account_name"));
	const char *sid = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "steamid"));
	if (!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "logged_in")) || !token || !*token || !account || !sid) {
		snprintf(err, errsz, "%s", "la comunidad no dio el token del chat web");
		cJSON_Delete(root);
		return -1;
	}
	// 2) Un servidor de Steam y 3) la sesión
	char ep[96];
	int n = -1;
	if (!pick_endpoint(ep, sizeof ep)) snprintf(err, errsz, "%s", "no se pudo saber a qué servidor de Steam conectar");
	else n = cm_session(ep, account, token, strtoull(sid, NULL, 10), job, ids, err, errsz);
	cJSON_Delete(root);
	return n;
}

int cm_fetch_hidden(const char *jar, uint32_t **ids, char *err, size_t errsz)
{
	Job job = {0};
	int n = cm_run(jar, &job, ids, err, errsz);
	free(job.lic);
	return n;
}

int cm_fetch_closed(const char *jar, const uint32_t *apps, int n, uint32_t **ids, char *err, size_t errsz)
{
	Job job = {1, apps, n};
	return cm_run(jar, &job, ids, err, errsz);
}

// ---------------------------------------------------------------------------- en un hilo

static struct {
	volatile bool running, done;
	char *jar;
	Job job;
	uint32_t *apps;
	uint32_t *ids;
	int n;
	char err[128];
	uint32_t *lic;
	int nlic;
} T;

static void cm_thread(void *arg)
{
	(void)arg;
	T.n = cm_run(T.jar, &T.job, &T.ids, T.err, sizeof T.err);
	memset(T.jar, 0, strlen(T.jar));
	free(T.jar);
	T.jar = NULL;
	free(T.apps);
	T.apps = NULL;
	T.done = true;
}

#ifdef CM_HOST_TEST
static void *cm_thread_p(void *a)
{
	cm_thread(a);
	return NULL;
}
#else
static Thread th;
#endif

static bool cm_start(const char *jar, int kind, const uint32_t *apps, int n)
{
	if (T.running) return false;
	uint32_t *lic = T.lic;   // lo de las licencias se guarda hasta que se recoja
	int nlic = T.nlic;
	memset(&T, 0, sizeof T);
	T.lic = lic;
	T.nlic = nlic;
	T.jar = strdup(jar ? jar : "");
	if (n > 0) T.apps = malloc(n * sizeof(uint32_t));
	if (!T.jar || (n > 0 && !T.apps)) {
		free(T.jar);
		free(T.apps);
		T.jar = NULL;
		T.apps = NULL;
		return false;
	}
	if (n > 0) memcpy(T.apps, apps, n * sizeof(uint32_t));
	T.job = (Job){kind, T.apps, n};
	T.running = true;
#ifdef CM_HOST_TEST
	pthread_t p;
	pthread_create(&p, NULL, cm_thread_p, NULL);
	pthread_detach(p);
#else
	if (th) {
		threadJoin(th, U64_MAX);
		threadFree(th);
	}
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	th = threadCreate(cm_thread, NULL, 64 * 1024, prio + 1, -2, false);
	if (!th) {
		free(T.jar);
		free(T.apps);
		T.jar = NULL;
		T.apps = NULL;
		T.running = false;
		return false;
	}
#endif
	return true;
}

static bool cm_poll(int kind, uint32_t **ids, int *n, char *err, size_t errsz)
{
	if (!T.running || !T.done || T.job.kind != kind) return false;
	*ids = T.ids;
	*n = T.n;
	free(T.lic);
	T.lic = T.job.lic;   // se recoge con cm_licensed
	T.nlic = T.job.nlic;
	T.job.lic = NULL;
	snprintf(err, errsz, "%s", T.err);
	T.ids = NULL;
	T.running = false;
	return true;
}

bool cm_hidden_start(const char *jar) { return cm_start(jar, 0, NULL, 0); }
bool cm_hidden_poll(uint32_t **ids, int *n, char *err, size_t errsz) { return cm_poll(0, ids, n, err, errsz); }
int cm_licensed(uint32_t **apps)
{
	*apps = T.lic;
	int n = T.nlic;
	T.lic = NULL;
	T.nlic = 0;
	return *apps ? n : -1;
}

bool cm_closed_start(const char *jar, const uint32_t *apps, int n) { return n > 0 && cm_start(jar, 1, apps, n); }
bool cm_closed_poll(uint32_t **ids, int *n, char *err, size_t errsz) { return cm_poll(1, ids, n, err, errsz); }

#ifdef CM_HOST_TEST
// Prueba sin cuenta: sesión anónima contra un servidor de verdad. Sin appids, la llamada a las
// colecciones debe contestar con un error de permisos; con appids, se leen sus fichas (las de los
// playtests llegan vacías sin el token de quien los tiene)
int main(int argc, char **argv)
{
	curl_global_init(CURL_GLOBAL_ALL);
	char ep[96], err[128] = "";
	if (!pick_endpoint(ep, sizeof ep)) {
		printf("sin servidor\n");
		return 1;
	}
	uint32_t apps[64];
	int na = 0;
	for (int i = 1; i < argc && na < 64; i++) apps[na++] = (uint32_t)strtoul(argv[i], NULL, 10);
	Job job = {na ? 1 : 0, apps, na};
	uint32_t *ids = NULL;
	int n = cm_session(ep, NULL, NULL, 0, &job, &ids, err, sizeof err);
	printf("resultado %d, %s\n", n, err);
	for (int i = 0; na && i < n; i++) printf("cerrado %u\n", ids[i]);
	printf("licencias: %d apps\n", job.nlic);
	for (int i = 0; i < job.nlic && i < 20; i++) printf("app %u\n", job.lic[i]);
	free(job.lic);
	free(ids);
	return 0;
}
#endif
