#include "net.h"
#include "app.h"

#include <curl/curl.h>
#include <jpeglib.h>
#include <png.h>
#include <setjmp.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>

#define SOC_BUFSIZE  0x100000
#define MAX_BODY     (4 * 1024 * 1024)

typedef struct {
	Req *head, *tail;
	LightLock lock;
	LightEvent wake;
	Thread thread;
	CURL *curl;
	CURLM *multi;   // las conexiones abiertas viven aquí (se reutilizan)
	NetLane lane;
} Lane;

static Lane lanes[LANE_COUNT];
static volatile bool quitting;
static u32 *soc_buf;
static volatile bool soc_ready;
// ¿Hay Wi-Fi con IP? Lo mira el hilo del registro: preguntárselo al servicio de red desde la
// interfaz la dejaba esperando detrás de las descargas (el servicio atiende de una en una, y una
// conexión nueva lo ocupa hasta un segundo), y se notaba como tirones al cargar fotos
static volatile bool wifi_up;
static LightLock soc_lock;
static LightLock log_lock;
static LightLock ref_lock;
static volatile const char *status_text = "";
long long g_server_delta;
bool g_server_known;

// ----------------------------------------------------------------------------
// Registro en la SD

// Escribir en la SD tarda decenas de milisegundos: las líneas se guardan en memoria y un hilo
// aparte las vuelca al archivo, así el registro nunca da tirones en la interfaz
#define LOG_BUF 16384
static char log_buf[LOG_BUF];
static size_t log_len;
static LightEvent log_wake;
static Thread log_thread;

static void log_flush(void)
{
	static char out[LOG_BUF];
	LightLock_Lock(&log_lock);
	size_t n = log_len;
	memcpy(out, log_buf, n);
	log_len = 0;
	LightLock_Unlock(&log_lock);
	if (!n) return;
	FILE *f = fopen(APP_DIR "/net.log", "a");
	if (f) {
		fwrite(out, 1, n, f);
		fclose(f);
	}
}

// ----------------------------------------------------------------------------
// Caché de imágenes en la SD: los JPEG tal cual llegan (comprimidos), para no volver a bajarlos.
// Hasta CACHE_MAX; al pasarse se borran los más antiguos. La cuenta y la limpieza las hace el hilo
// del registro, nunca la interfaz

#define CACHE_DIR  APP_DIR "/cache"
#define CACHE_MAX  (64LL << 20)
#define CACHE_DAYS 30
static volatile s64 cache_bytes = -1;      // -1: aún sin contar
static volatile bool cache_clear_req;

static void cache_name(const char *url, char *out, size_t n)
{
	u64 h = 1469598103934665603ULL;   // FNV-1a
	for (const unsigned char *p = (const unsigned char *)url; *p; p++) h = (h ^ *p) * 1099511628211ULL;
	snprintf(out, n, CACHE_DIR "/%016llx.jpg", (unsigned long long)h);
}

// Cada archivo empieza con «S3C1» y la hora en que se guardó (la fecha del archivo en la SD no es
// de fiar), y luego el JPEG tal cual
#define CACHE_MAGIC 0x31433353u   // "S3C1"

static time_t cache_time_of(const char *path)
{
	u32 h[2] = {0, 0};
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	size_t n = fread(h, 4, 2, f);
	fclose(f);
	return n == 2 && h[0] == CACHE_MAGIC ? (time_t)h[1] : 0;
}

static bool cache_get(Req *r)
{
	char path[96];
	cache_name(r->url, path, sizeof path);
	struct stat st;
	if (stat(path, &st) != 0 || st.st_size <= 8 || st.st_size > (1 << 20)) return false;
	FILE *f = fopen(path, "rb");
	if (!f) return false;
	u32 h[2];
	char *d = malloc(st.st_size);
	size_t n = 0;
	bool ok = d && fread(h, 4, 2, f) == 2 && h[0] == CACHE_MAGIC && time(NULL) - (time_t)h[1] <= CACHE_DAYS * 86400;
	if (ok) n = fread(d, 1, st.st_size - 8, f);
	fclose(f);
	if (!ok || n != (size_t)st.st_size - 8) {   // vieja, de otro formato o a medias: se vuelve a pedir
		free(d);
		return false;
	}
	d[n] = 0;
	free(r->data);
	r->data = d;
	r->len = r->cap = n;
	return true;
}

static void cache_put(const Req *r)
{
	if (!r->data || !r->len || r->len > (1 << 20) || cache_bytes < 0 || cache_clear_req) return;
	char path[96], tmp[100];
	cache_name(r->url, path, sizeof path);
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	FILE *f = fopen(tmp, "wb");
	if (!f) return;
	u32 h[2] = {CACHE_MAGIC, (u32)time(NULL)};
	bool ok = fwrite(h, 4, 2, f) == 2 && fwrite(r->data, 1, r->len, f) == r->len;
	ok = fclose(f) == 0 && ok;
	if (!ok) {
		remove(tmp);
		return;
	}
	remove(path);
	rename(tmp, path);
	__atomic_add_fetch(&cache_bytes, (s64)r->len + 8, __ATOMIC_RELAXED);
}

typedef struct { char name[24]; time_t when; long size; } CacheFile;

static int cmp_cache(const void *a, const void *b)
{
	const CacheFile *x = a, *y = b;
	return x->when < y->when ? -1 : x->when > y->when;
}

// Cuenta lo que ocupa y, si hace falta, borra (todo, o lo más antiguo hasta bajar del 80 %)
static void cache_maintain(void)
{
	bool clear = cache_clear_req;
	if (cache_bytes >= 0 && cache_bytes <= CACHE_MAX && !clear) return;
	DIR *d = opendir(CACHE_DIR);
	if (!d) {
		cache_bytes = 0;
		cache_clear_req = false;
		return;
	}
	int cap = 256, n = 0;
	CacheFile *files = malloc(cap * sizeof *files);
	s64 total = 0;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof files[0].name) continue;
		char path[96];
		snprintf(path, sizeof path, CACHE_DIR "/%s", e->d_name);
		struct stat st;
		if (stat(path, &st) != 0) continue;
		if (clear) {
			remove(path);
			continue;
		}
		total += st.st_size;
		if (n == cap) {
			CacheFile *m = realloc(files, (cap *= 2) * sizeof *files);
			if (!m) break;
			files = m;
		}
		if (files) {
			snprintf(files[n].name, sizeof files[n].name, "%s", e->d_name);
			files[n].when = cache_time_of(path);
			files[n].size = st.st_size;
			n++;
		}
	}
	closedir(d);
	if (!clear && files && total > CACHE_MAX) {
		qsort(files, n, sizeof *files, cmp_cache);
		for (int i = 0; i < n && total > CACHE_MAX * 8 / 10; i++) {
			char path[96];
			snprintf(path, sizeof path, CACHE_DIR "/%s", files[i].name);
			if (remove(path) == 0) total -= files[i].size;
		}
		netlog("cache: limpiada (quedan %lld KB)", (long long)(total >> 10));
	}
	free(files);
	cache_bytes = clear ? 0 : total;
	if (clear) netlog("cache: vaciada");
	cache_clear_req = false;
}

s64 net_cache_bytes(void) { return cache_clear_req ? 0 : cache_bytes; }

void net_cache_clear(void)
{
	cache_clear_req = true;
	LightEvent_Signal(&log_wake);
}

static void log_main(void *arg)
{
	while (!quitting) {
		LightEvent_WaitTimeout(&log_wake, 500000000LL);
		wifi_up = soc_ready && gethostid() != 0;
		log_flush();
		cache_maintain();
	}
	log_flush();
}

void netlog(const char *fmt, ...)
{
	char line[320];
	time_t t = time(NULL);
	struct tm *tm = gmtime(&t);
	int n = snprintf(line, sizeof line, "%02d:%02d:%02d ", tm->tm_hour, tm->tm_min, tm->tm_sec);
	va_list ap;
	va_start(ap, fmt);
	n += vsnprintf(line + n, sizeof line - n - 1, fmt, ap);
	va_end(ap);
	if (n > (int)sizeof line - 2) n = sizeof line - 2;
	line[n++] = '\n';
	LightLock_Lock(&log_lock);
	if (log_len + n <= LOG_BUF) {
		memcpy(log_buf + log_len, line, n);
		log_len += n;
	}
	bool full = log_len > LOG_BUF / 2;
	LightLock_Unlock(&log_lock);
	if (full) LightEvent_Signal(&log_wake);
}

static void log_rotate(void)
{
	struct stat st;
	if (stat(APP_DIR "/net.log", &st) == 0 && st.st_size > 256 * 1024) {
		remove(APP_DIR "/net.old.log");
		rename(APP_DIR "/net.log", APP_DIR "/net.old.log");
	}
}

// ----------------------------------------------------------------------------
// Wi-Fi y soc:U (pueden no estar listos al arrancar: se reintenta solo)

static bool soc_try_init(void)
{
	LightLock_Lock(&soc_lock);
	if (!soc_ready) {
		if (!soc_buf) soc_buf = (u32 *)memalign(0x1000, SOC_BUFSIZE);
		Result rc = soc_buf ? socInit(soc_buf, SOC_BUFSIZE) : -1;
		if (R_SUCCEEDED(rc)) {
			soc_ready = true;
			netlog("soc: listo");
		} else {
			netlog("soc: socInit fallo %08lX", rc);
			socExit();
		}
	}
	LightLock_Unlock(&soc_lock);
	return soc_ready;
}

bool net_wifi_ok(void)
{
	return wifi_up;
}

const char *net_status_text(void)
{
	return (const char *)status_text;
}

// Espera a que haya red. Devuelve false si se canceló la petición o la app sale.
static bool wait_network(Req *r)
{
	int tries = 0;
	for (;;) {
		if (quitting || r->cancel) return false;
		if (soc_ready || soc_try_init()) {
			if (gethostid() != 0) {
				wifi_up = true;
				status_text = "";
				return true;
			}
			u32 st = 0;
			ACU_GetStatus(&st);
			status_text = (st == 3) ? "wifi_noip" : "wifi_off";
		} else {
			status_text = "soc";
		}
		if ((tries++ % 5) == 0) netlog("red: esperando (%s)", status_text);
		svcSleepThread(1000000000LL);
	}
}

// ----------------------------------------------------------------------------
// Peticiones

static void req_free(Req *r)
{
	free(r->url);
	if (r->post) {
		// Puede llevar tokens de la sesión: se borra antes de soltarlo
		memset(r->post, 0, strlen(r->post));
		free(r->post);
	}
	if (r->headers) {
		memset(r->headers, 0, strlen(r->headers));
		free(r->headers);
	}
	if (r->login_cookie) {
		memset(r->login_cookie, 0, strlen(r->login_cookie));
		free(r->login_cookie);
	}
	if (r->parse_out && r->parse_free) r->parse_free(r->parse_out);
	free(r->data);
	free(r->pixels);
	free(r);
}

void net_release(Req *r)
{
	if (!r) return;
	r->cancel = true;
	LightLock_Lock(&ref_lock);
	int left = --r->refs;
	LightLock_Unlock(&ref_lock);
	if (left == 0) req_free(r);
}

static volatile int active;   // peticiones en cola o en curso (para el logo que gira)

bool net_busy(void) { return __atomic_load_n(&active, __ATOMIC_RELAXED) > 0; }

static void lane_push(Lane *l, Req *r)
{
	__atomic_add_fetch(&active, 1, __ATOMIC_RELAXED);
	LightLock_Lock(&l->lock);
	r->next = NULL;
	if (l->tail) l->tail->next = r; else l->head = r;
	l->tail = r;
	LightLock_Unlock(&l->lock);
	LightEvent_Signal(&l->wake);
}

static Req *lane_pop(Lane *l)
{
	LightLock_Lock(&l->lock);
	Req *r = l->head;
	if (r) {
		l->head = r->next;
		if (!l->head) l->tail = NULL;
	}
	LightLock_Unlock(&l->lock);
	return r;
}

static Req *req_new(const char *url, NetLane lane)
{
	Req *r = calloc(1, sizeof(Req));
	if (!r) return NULL;
#ifdef DEV_RELAY
	// Solo en builds de prueba: https://host/ruta -> http://127.0.0.1:8099/host/ruta
	if (strncmp(url, "https://", 8) == 0) {
		size_t n = strlen(url) + 32;
		r->url = malloc(n);
		snprintf(r->url, n, "http://127.0.0.1:8099/%s", url + 8);
	} else
#endif
	r->url = strdup(url);
	r->lane = lane;
	r->refs = 2;   // quien la pide + el hilo de red
	return r;
}

Req *net_get(const char *url, NetLane lane)
{
	Req *r = req_new(url, lane);
	if (r) lane_push(&lanes[lane], r);
	return r;
}

Req *net_get_jpeg(const char *url, int max_w)
{
	Req *r = req_new(url, LANE_IMG);
	if (!r) return NULL;
	r->jpeg = true;
	r->jpeg_max_w = max_w;
	lane_push(&lanes[LANE_IMG], r);
	return r;
}

Req *net_get_parsed(const char *url, NetLane lane, bool (*parse)(Req *r), void *out, void (*out_free)(void *out))
{
	Req *r = req_new(url, lane);
	if (!r) {
		if (out && out_free) out_free(out);
		return NULL;
	}
	r->parse = parse;
	r->parse_out = out;
	r->parse_free = out_free;
	lane_push(&lanes[lane], r);
	return r;
}

Req *net_post(const char *url, const char *body)
{
	Req *r = req_new(url, LANE_API);
	if (!r) return NULL;
	r->post = strdup(body ? body : "");
	lane_push(&lanes[LANE_API], r);
	return r;
}

Req *net_request(const char *url, const char *body, const char *headers)
{
	Req *r = req_new(url, LANE_API);
	if (!r) return NULL;
	if (body) r->post = strdup(body);
	if (headers) r->headers = strdup(headers);
	lane_push(&lanes[LANE_API], r);
	return r;
}

Req *net_head(const char *url)
{
	Req *r = req_new(url, LANE_API);
	if (!r) return NULL;
	r->head = true;
	lane_push(&lanes[LANE_API], r);
	return r;
}

static size_t on_body(char *p, size_t sz, size_t n, void *ud)
{
	Req *r = ud;
	size_t add = sz * n;
	if (r->len + add + 1 > r->cap) {
		size_t cap = r->cap ? r->cap : 16384;
		while (cap < r->len + add + 1) cap *= 2;
		if (cap > MAX_BODY) return 0;
		char *d = realloc(r->data, cap);
		if (!d) return 0;
		r->data = d;
		r->cap = cap;
	}
	memcpy(r->data + r->len, p, add);
	r->len += add;
	r->data[r->len] = 0;
	return add;
}

static size_t on_header(char *p, size_t sz, size_t n, void *ud)
{
	Req *r = ud;
	size_t len = sz * n;
	static const char key[] = "set-cookie: steamCountry=";
	if (len > sizeof(key) + 2 && strncasecmp(p, key, sizeof(key) - 1) == 0) {
		const char *c = p + sizeof(key) - 1;
		if (c[0] >= 'A' && c[0] <= 'Z' && c[1] >= 'A' && c[1] <= 'Z') {
			r->country[0] = c[0];
			r->country[1] = c[1];
			r->country[2] = 0;
		}
	}
	// La hora de verdad (UTC) viene en la cabecera Date: la de la consola es la local y puede ir mal
	static const char dk[] = "date:";
	if (len > 30 && strncasecmp(p, dk, sizeof(dk) - 1) == 0) {
		static const char *mon[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
		char m[4] = "";
		int d, y, hh, mm, ss;
		if (sscanf(p + 5, " %*3s, %d %3s %d %d:%d:%d", &d, m, &y, &hh, &mm, &ss) == 6) {
			int mi = 0;
			while (mi < 12 && strcmp(m, mon[mi])) mi++;
			if (mi < 12) {
				// Días desde 1970 (algoritmo de días civiles)
				int yy = y - (mi < 2), era = yy / 400, yoe = yy - era * 400;
				int doy = (153 * (mi + (mi > 1 ? -2 : 10)) + 2) / 5 + d - 1;
				int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
				long long days = (long long)era * 146097 + doe - 719468;
				long long t = days * 86400 + hh * 3600 + mm * 60 + ss;
				g_server_delta = t - (long long)time(NULL);
				g_server_known = true;
			}
		}
	}
	static const char er[] = "x-eresult:";
	if (len > sizeof(er) && strncasecmp(p, er, sizeof(er) - 1) == 0) r->eresult = atoi(p + sizeof(er) - 1);
	static const char login[] = "set-cookie: steamLoginSecure=";
	if (len > sizeof(login) && strncasecmp(p, login, sizeof(login) - 1) == 0) {
		const char *v = p + sizeof(login) - 1;
		size_t n = 0;
		while (n < len - (sizeof(login) - 1) && v[n] != ';' && v[n] != '\r' && v[n] != '\n') n++;
		// «deleted» es Steam borrándola; solo vale la que trae token
		if (n > 20) {
			if (r->login_cookie) {
				memset(r->login_cookie, 0, strlen(r->login_cookie));
				free(r->login_cookie);
			}
			r->login_cookie = strndup(v, n);
		}
	}
	return len;
}

static int on_progress(void *ud, curl_off_t dt, curl_off_t dn, curl_off_t ut, curl_off_t un)
{
	Req *r = ud;
	return (r->cancel || quitting) ? 1 : 0;
}

// ----------------------------------------------------------------------------
// JPEG (libjpeg-turbo, con escalado al decodificar: mucho más rápido)

struct jerr { struct jpeg_error_mgr mgr; jmp_buf jb; };

static void jerr_exit(j_common_ptr c)
{
	longjmp(((struct jerr *)c->err)->jb, 1);
}

static bool decode_jpeg(Req *r);

// Imagen RGBA (fila a fila) a la textura: se reduce a la mitad hasta caber en max_w y va en mosaico
// RGBA8 (en memoria: A, B, G, R). Suelta src
static bool tile_rgba(Req *r, u8 *src, int sw0, int sh0)
{
	int f = 1;
	while (r->jpeg_max_w > 0 && sw0 / (f * 2) >= r->jpeg_max_w) f *= 2;
	int w = sw0 / f, h = sh0 / f;
	int tw = 8, th = 8;
	while (tw < w) tw <<= 1;
	while (th < h) th <<= 1;
	u8 *dst = w > 0 && h > 0 && tw <= 1024 && th <= 1024 ? calloc((size_t)tw * th, 4) : NULL;
	if (!dst) {
		free(src);
		return false;
	}
	for (int y = 0; y < h; y++) {
		u32 rowbase = ((y >> 3) * (tw >> 3)) << 6;
		u32 ybits = ((y & 1) << 1) | ((y & 2) << 2) | ((y & 4) << 3);
		for (int x = 0; x < w; x++) {
			// Media del bloque f×f, con el color pesado por su alfa (sin bordes oscuros)
			u32 c[4] = {0};
			for (int j = 0; j < f; j++) {
				const u8 *q = src + ((size_t)(y * f + j) * sw0 + x * f) * 4;
				for (int i = 0; i < f; i++, q += 4) {
					c[0] += q[0] * q[3];
					c[1] += q[1] * q[3];
					c[2] += q[2] * q[3];
					c[3] += q[3];
				}
			}
			u32 off = (rowbase + ((x >> 3) << 6) + ((x & 1) | ((x & 2) << 1) | ((x & 4) << 2) | ybits)) * 4;
			u8 *o = dst + off;
			o[0] = c[3] / (f * f);
			if (c[3]) {
				o[1] = c[2] / c[3];
				o[2] = c[1] / c[3];
				o[3] = c[0] / c[3];
			}
		}
		if ((y & 15) == 15) svcSleepThread(250000);
	}
	free(src);
	r->pixels = dst;
	r->alpha = true;
	r->w = w;
	r->h = h;
	r->tw = tw;
	r->th = th;
	return true;
}

// PNG (los marcos de avatar de Steam, con transparencia; de los animados solo el primer fotograma)
static bool decode_png(Req *r)
{
	png_image im;
	memset(&im, 0, sizeof im);
	im.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_memory(&im, r->data, r->len)) return false;
	im.format = PNG_FORMAT_RGBA;
	int sw0 = im.width, sh0 = im.height;
	if (sw0 > 2048 || sh0 > 2048) {
		png_image_free(&im);
		return false;
	}
	u8 *src = malloc(PNG_IMAGE_SIZE(im));
	if (!src || !png_image_finish_read(&im, NULL, src, 0, NULL)) {
		free(src);
		png_image_free(&im);
		return false;
	}
	return tile_rgba(r, src, sw0, sh0);
}

// GIF (ilustraciones de los perfiles): el primer fotograma, con su transparencia
static bool decode_gif(Req *r)
{
	const u8 *d = (const u8 *)r->data;
	size_t n = r->len, p = 13;
	if (n < 13 || memcmp(d, "GIF8", 4)) return false;
	int W = d[6] | d[7] << 8, H = d[8] | d[9] << 8;
	if (W <= 0 || H <= 0 || W > 2048 || H > 2048) return false;
	const u8 *gct = NULL;
	int gsize = 0, transp = -1;
	if (d[10] & 0x80) {
		gsize = 2 << (d[10] & 7);
		gct = d + p;
		p += 3 * gsize;
	}
	while (p < n) {
		u8 b = d[p++];
		if (b == 0x21) {   // extensión (de la de control sale el color transparente)
			if (p >= n) return false;
			u8 label = d[p++];
			if (label == 0xF9 && p + 4 < n && d[p] == 4 && (d[p + 1] & 1)) transp = d[p + 4];
			while (p < n && d[p]) p += d[p] + 1;
			p++;
			continue;
		}
		if (b != 0x2C || p + 9 > n) return false;
		int ix = d[p] | d[p + 1] << 8, iy = d[p + 2] | d[p + 3] << 8, iw = d[p + 4] | d[p + 5] << 8, ih = d[p + 6] | d[p + 7] << 8;
		u8 lf = d[p + 8];
		p += 9;
		const u8 *ct = gct;
		int csize = gsize;
		if (lf & 0x80) {
			csize = 2 << (lf & 7);
			ct = d + p;
			p += 3 * csize;
		}
		if (!ct || p >= n || iw <= 0 || ih <= 0 || (size_t)(ct - d) + 3 * (size_t)csize > n) return false;
		int minc = d[p++];
		if (minc < 2 || minc > 8) return false;
		size_t npix = (size_t)iw * ih;
		u8 *idx = calloc(npix, 1);
		u16 *prefix = malloc(4096 * sizeof *prefix);
		u8 *suffix = malloc(4096), *stack = malloc(4097);
		if (!idx || !prefix || !suffix || !stack) {
			free(idx), free(prefix), free(suffix), free(stack);
			return false;
		}
		// LZW
		int clear = 1 << minc, eoi = clear + 1, avail = clear + 2, bits_c = minc + 1, mask = (1 << bits_c) - 1, old = -1;
		u8 first = 0;
		for (int i = 0; i < clear; i++) suffix[i] = (u8)i, prefix[i] = 0;
		u32 datum = 0;
		int bits = 0, blk = 0;
		size_t out = 0;
		while (out < npix) {
			while (bits < bits_c) {
				if (!blk) {
					if (p >= n || !(blk = d[p++])) goto done;
				}
				if (p >= n) goto done;
				datum |= (u32)d[p++] << bits;
				bits += 8;
				blk--;
			}
			int code = datum & mask;
			datum >>= bits_c;
			bits -= bits_c;
			if (code == clear) {
				bits_c = minc + 1;
				mask = (1 << bits_c) - 1;
				avail = clear + 2;
				old = -1;
				continue;
			}
			if (code == eoi) break;
			if (old < 0) {
				if (code >= clear) break;
				idx[out++] = first = suffix[code];
				old = code;
				continue;
			}
			if (code > avail) break;
			int in = code, sp = 0;
			if (code == avail) {
				stack[sp++] = first;
				code = old;
			}
			while (code >= clear && sp < 4096) {
				stack[sp++] = suffix[code];
				code = prefix[code];
			}
			first = suffix[code];
			stack[sp++] = first;
			if (avail < 4096) {
				prefix[avail] = (u16)old;
				suffix[avail] = first;
				avail++;
				if (!(avail & mask) && avail < 4096) {
					bits_c++;
					mask = (1 << bits_c) - 1;
				}
			}
			old = in;
			while (sp && out < npix) idx[out++] = stack[--sp];
		}
	done:
		free(prefix), free(suffix), free(stack);
		u8 *rgba = calloc((size_t)W * H, 4);
		if (!rgba) {
			free(idx);
			return false;
		}
		// Entrelazado: las filas llegan en cuatro pasadas
		int pass = 0, ystep = 8, yy = 0;
		for (int row = 0; row < ih; row++) {
			int y = row;
			if (lf & 0x40) {
				y = yy;
				yy += ystep;
				while (yy >= ih && pass < 3) {
					pass++;
					static const int start[] = {0, 4, 2, 1}, step[] = {8, 8, 4, 2};
					yy = start[pass];
					ystep = step[pass];
				}
			}
			int Y = iy + y;
			if (Y < 0 || Y >= H) continue;
			for (int x = 0; x < iw; x++) {
				int X = ix + x, c = idx[(size_t)row * iw + x];
				if (X >= W || c == transp || c >= csize) continue;
				u8 *o = rgba + ((size_t)Y * W + X) * 4;
				o[0] = ct[c * 3];
				o[1] = ct[c * 3 + 1];
				o[2] = ct[c * 3 + 2];
				o[3] = 255;
			}
		}
		free(idx);
		return tile_rgba(r, rgba, W, H);
	}
	return false;
}

static bool decode_image(Req *r)
{
	if (r->len > 8 && !memcmp(r->data, "\x89PNG", 4)) return decode_png(r);
	if (r->len > 13 && !memcmp(r->data, "GIF8", 4)) return decode_gif(r);
	return decode_jpeg(r);
}

static bool decode_jpeg(Req *r)
{
	struct jpeg_decompress_struct ci;
	struct jerr je;
	u8 *volatile line = NULL;   // volatile: se suelta después del longjmp
	ci.err = jpeg_std_error(&je.mgr);
	je.mgr.error_exit = jerr_exit;
	if (setjmp(je.jb)) {
		jpeg_destroy_decompress(&ci);
		free(line);
		free(r->pixels);   // una imagen a medias no se sube
		r->pixels = NULL;
		return false;
	}
	jpeg_create_decompress(&ci);
	jpeg_mem_src(&ci, (const u8 *)r->data, r->len);
	jpeg_read_header(&ci, TRUE);
	ci.out_color_space = JCS_RGB;
	// Escala al decodificar en octavos (libjpeg-turbo): el ancho justo para lo que se muestra, no
	// menos. Una captura de 600 para la pantalla de 400 sale a 450 (textura de 512 en vez de 1024:
	// cuatro veces menos memoria y menos trabajo al subirla)
	ci.scale_num = 8;
	ci.scale_denom = 8;
	if ((int)ci.image_width > r->jpeg_max_w && r->jpeg_max_w > 0) {
		int n = (8 * r->jpeg_max_w + ci.image_width - 1) / ci.image_width;
		ci.scale_num = n < 1 ? 1 : n > 8 ? 8 : n;
	}
	jpeg_start_decompress(&ci);
	int w = ci.output_width, h = ci.output_height;
	int tw = 8, th = 8;
	while (tw < w) tw <<= 1;
	while (th < h) th <<= 1;
	if (tw > 1024 || th > 1024) longjmp(je.jb, 1);
	// Cada línea va directa al mosaico 8×8 (Morton) BGR que espera la GPU, sin pasar por una imagen
	// entera intermedia: una pasada menos por la memoria, que la 3DS comparte con la GPU. Y a tramos,
	// con una pausa corta, para no acapararla mientras la interfaz dibuja
	u8 *sw = calloc((size_t)tw * th, 3);
	r->pixels = sw;
	u8 *out = malloc((size_t)w * 3);
	line = out;
	if (!sw || !out) longjmp(je.jb, 1);
	while (ci.output_scanline < ci.output_height) {
		int y = ci.output_scanline;
		jpeg_read_scanlines(&ci, &out, 1);
		const u8 *p = out;
		u32 rowbase = ((y >> 3) * (tw >> 3)) << 6;
		u32 ybits = ((y & 1) << 1) | ((y & 2) << 2) | ((y & 4) << 3);
		for (int x = 0; x < w; x++, p += 3) {
			u32 off = (rowbase + ((x >> 3) << 6) + ((x & 1) | ((x & 2) << 1) | ((x & 4) << 2) | ybits)) * 3;
			sw[off + 0] = p[2];
			sw[off + 1] = p[1];
			sw[off + 2] = p[0];
		}
		if ((y & 15) == 15) svcSleepThread(250000);
	}
	jpeg_finish_decompress(&ci);
	jpeg_destroy_decompress(&ci);
	line = NULL;
	free(out);
	r->pixels = sw;
	r->w = w;
	r->h = h;
	r->tw = tw;
	r->th = th;
	return true;
}

// ----------------------------------------------------------------------------
// Los últimos JPEG descargados, tal cual: la misma imagen se pide a veces en dos tamaños (la
// captura grande y su miniatura) y así no se baja dos veces

#define RAW_SLOTS 8
#define RAW_MAX   (192 * 1024)
static struct { char *url; char *data; size_t len; u32 used; } raw[RAW_SLOTS];
static LightLock raw_lock;
static u32 raw_tick;

static bool raw_get(Req *r)
{
	bool hit = false;
	LightLock_Lock(&raw_lock);
	for (int i = 0; i < RAW_SLOTS; i++) {
		if (!raw[i].url || strcmp(raw[i].url, r->url)) continue;
		r->data = malloc(raw[i].len + 1);
		if (r->data) {
			memcpy(r->data, raw[i].data, raw[i].len + 1);
			r->len = r->cap = raw[i].len;
			raw[i].used = ++raw_tick;
			hit = true;
		}
		break;
	}
	LightLock_Unlock(&raw_lock);
	return hit;
}

static void raw_put(const Req *r)
{
	if (!r->data || r->len > RAW_MAX) return;
	char *d = malloc(r->len + 1), *u = strdup(r->url);
	if (!d || !u) {
		free(d);
		free(u);
		return;
	}
	memcpy(d, r->data, r->len + 1);
	LightLock_Lock(&raw_lock);
	int k = 0;
	for (int i = 0; i < RAW_SLOTS; i++) {
		if (!raw[i].url) { k = i; break; }
		if (raw[i].used < raw[k].used) k = i;
	}
	free(raw[k].url);
	free(raw[k].data);
	raw[k].url = u;
	raw[k].data = d;
	raw[k].len = r->len;
	raw[k].used = ++raw_tick;
	LightLock_Unlock(&raw_lock);
}

// curl_easy_perform espera hasta 1 s a que el sistema avise de que el socket está listo, y la pila
// de red de la 3DS no avisa al terminar de conectar: cada conexión nueva perdía justo ese segundo
// (tcp=1.0x en net.log). Llevando el bucle aquí se vuelve a mirar cada 20 ms
static CURLcode perform(Lane *l, CURL *c)
{
	if (!l->multi) l->multi = curl_multi_init();
	if (!l->multi || curl_multi_add_handle(l->multi, c) != CURLM_OK) return curl_easy_perform(c);
	CURLcode rc = CURLE_OK;
	int running = 1;
	while (running) {
		if (curl_multi_perform(l->multi, &running) != CURLM_OK) {
			rc = CURLE_RECV_ERROR;
			break;
		}
		if (running) curl_multi_poll(l->multi, NULL, 0, 20, NULL);
	}
	int left;
	CURLMsg *m;
	while ((m = curl_multi_info_read(l->multi, &left)))
		if (m->msg == CURLMSG_DONE && m->easy_handle == c) rc = m->data.result;
	curl_multi_remove_handle(l->multi, c);
	return rc;
}

static void run_req(Lane *l, Req *r)
{
	if (r->jpeg && (raw_get(r) || cache_get(r))) {
		// Ya la teníamos (en memoria o en la SD): solo se decodifica a este tamaño
		r->http = 200;
		if (!decode_image(r)) snprintf(r->err, sizeof r->err, "imagen no válida");
		free(r->data);
		r->data = NULL;
		r->len = r->cap = 0;
		r->state = r->err[0] ? REQ_FAILED : REQ_DONE;
		return;
	}
	if (!wait_network(r)) {
		snprintf(r->err, sizeof r->err, "cancelada");
		r->state = REQ_FAILED;
		return;
	}
	r->state = REQ_RUNNING;
	CURL *c = l->curl;
	curl_easy_reset(c);
	curl_easy_setopt(c, CURLOPT_URL, r->url);
	curl_easy_setopt(c, CURLOPT_USERAGENT, APP_USER_AGENT);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
	curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/certs/cacert.pem");
	curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 15L);
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
	curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 20L);
	// Abrir conexión cuesta ~0,6 s de TLS en la consola (net.log): se reutilizan más
	curl_easy_setopt(c, CURLOPT_MAXAGE_CONN, 120L);
	curl_easy_setopt(c, CURLOPT_IPRESOLVE, (long)CURL_IPRESOLVE_V4);
	curl_easy_setopt(c, CURLOPT_TCP_KEEPALIVE, 1L);
	curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 32768L);
	curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_body);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, r);
	curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, on_header);
	curl_easy_setopt(c, CURLOPT_HEADERDATA, r);
	curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, on_progress);
	curl_easy_setopt(c, CURLOPT_XFERINFODATA, r);
	if (r->head) curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
	if (r->post) {
		curl_easy_setopt(c, CURLOPT_POSTFIELDS, r->post);
		curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)strlen(r->post));
	}
	struct curl_slist *hl = NULL;
	if (r->headers) {
		char *copy = strdup(r->headers), *save = NULL;
		for (char *line = strtok_r(copy, "\n", &save); line; line = strtok_r(NULL, "\n", &save))
			hl = curl_slist_append(hl, line);
		memset(copy, 0, strlen(r->headers));
		free(copy);
		curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
	}

	u64 t0 = osGetTime();
	CURLcode rc = perform(l, c);
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r->http);
	double tdns = 0, ttcp = 0, ttls = 0, tfirst = 0;
	curl_easy_getinfo(c, CURLINFO_NAMELOOKUP_TIME, &tdns);
	curl_easy_getinfo(c, CURLINFO_CONNECT_TIME, &ttcp);
	curl_easy_getinfo(c, CURLINFO_APPCONNECT_TIME, &ttls);
	curl_easy_getinfo(c, CURLINFO_STARTTRANSFER_TIME, &tfirst);
	if (hl) {
		// curl copia cada línea en su lista: se borran antes de soltarla
		for (struct curl_slist *i = hl; i; i = i->next) memset(i->data, 0, strlen(i->data));
		curl_slist_free_all(hl);
		curl_easy_setopt(c, CURLOPT_HTTPHEADER, NULL);
	}

	if (rc != CURLE_OK) {
		snprintf(r->err, sizeof r->err, "curl=%d %s", rc, curl_easy_strerror(rc));
	} else if (r->http < 200 || r->http >= 300) {
		snprintf(r->err, sizeof r->err, "HTTP %ld", r->http);
	} else if (r->jpeg) {
		raw_put(r);
		if (!decode_image(r)) snprintf(r->err, sizeof r->err, "imagen no válida");
		else cache_put(r);
	} else if (r->parse && !r->cancel && r->data) {
		r->parsed = r->parse(r);
	}

	// La URL del registro, sin la consulta (puede ser larga)
	char shown[96];
	snprintf(shown, sizeof shown, "%s", r->url);
	char *q = strchr(shown, '?');
	if (q) *q = 0;
	netlog("%s %s -> %ld %luB dns=%.2f tcp=%.2f tls=%.2f 1er=%.2f total=%.2fs%s%s",
		r->head ? "HEAD" : r->post ? "POST" : "GET", shown, r->http, (unsigned long)r->len,
		tdns, ttcp, ttls, tfirst, (osGetTime() - t0) / 1000.0,
		r->err[0] ? " ERROR " : "", r->err);

	// Las imágenes ya decodificadas no necesitan el JPEG
	if (r->jpeg && r->pixels) {
		free(r->data);
		r->data = NULL;
		r->len = r->cap = 0;
	}
	r->state = r->err[0] ? REQ_FAILED : REQ_DONE;
}

static void lane_main(void *arg)
{
	Lane *l = arg;
	l->curl = curl_easy_init();
	while (!quitting) {
		Req *r = lane_pop(l);
		if (!r) {
			LightEvent_WaitTimeout(&l->wake, 500000000LL);
			continue;
		}
		if (r->cancel) {
			r->state = REQ_FAILED;
		} else {
			run_req(l, r);
		}
		__atomic_sub_fetch(&active, 1, __ATOMIC_RELAXED);
		net_release(r);
	}
	curl_easy_cleanup(l->curl);
	if (l->multi) curl_multi_cleanup(l->multi);
}

// Los hilos de red van en otro núcleo para no quitarle tiempo a la interfaz (que va en el 0): el 2
// de la New 3DS o, si no, el 1 (con un tope de CPU). Se apunta en net.log dónde quedó cada uno
static Thread start_thread(ThreadFunc f, void *arg, const char *name)
{
	s32 prio = 0x30;
	svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
	int core = 2;
	Thread t = threadCreate(f, arg, 64 * 1024, prio + 1, 2, false);
	if (!t) {
		APT_SetAppCpuTimeLimit(30);
		core = 1;
		t = threadCreate(f, arg, 64 * 1024, prio + 1, 1, false);
	}
	if (!t) {
		core = 0;
		t = threadCreate(f, arg, 64 * 1024, prio + 1, -2, false);
	}
	netlog("hilo %s: %s", name, !t ? "no se pudo crear" : core == 2 ? "núcleo 2" : core == 1 ? "núcleo 1 (30 %)" : "núcleo 0 (el de la interfaz)");
	return t;
}

void net_init(void)
{
	mkdir("sdmc:/3ds", 0777);
	mkdir(APP_DIR, 0777);
	mkdir(CACHE_DIR, 0777);
	LightLock_Init(&log_lock);
	LightLock_Init(&soc_lock);
	LightLock_Init(&ref_lock);
	LightLock_Init(&raw_lock);
	log_rotate();
	LightEvent_Init(&log_wake, RESET_ONESHOT);
	netlog("---- %s %s", APP_NAME, APP_VERSION);
	acInit();
	soc_try_init();
	curl_global_init(CURL_GLOBAL_DEFAULT);
	log_thread = start_thread(log_main, NULL, "registro");
	for (int i = 0; i < LANE_COUNT; i++) {
		Lane *l = &lanes[i];
		l->lane = i;
		LightLock_Init(&l->lock);
		LightEvent_Init(&l->wake, RESET_ONESHOT);
		l->thread = start_thread(lane_main, l, i == LANE_API ? "tienda" : "imágenes");
	}
}

void net_exit(void)
{
	quitting = true;
	for (int i = 0; i < LANE_COUNT; i++) {
		LightEvent_Signal(&lanes[i].wake);
		if (lanes[i].thread) {
			threadJoin(lanes[i].thread, U64_MAX);
			threadFree(lanes[i].thread);
		}
		Req *r;
		while ((r = lane_pop(&lanes[i]))) net_release(r);
	}
	LightEvent_Signal(&log_wake);
	if (log_thread) {
		threadJoin(log_thread, U64_MAX);
		threadFree(log_thread);
	} else {
		log_flush();
	}
	curl_global_cleanup();
	for (int i = 0; i < RAW_SLOTS; i++) {
		free(raw[i].url);
		free(raw[i].data);
	}
	if (soc_ready) socExit();
	acExit();
}

char *url_escape(const char *s)
{
	static const char hex[] = "0123456789ABCDEF";
	size_t n = strlen(s);
	char *o = malloc(n * 3 + 1), *p = o;
	if (!o) return NULL;
	for (; *s; s++) {
		unsigned char ch = *s;
		if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
		    ch == '-' || ch == '_' || ch == '.' || ch == '~') {
			*p++ = ch;
		} else {
			*p++ = '%';
			*p++ = hex[ch >> 4];
			*p++ = hex[ch & 15];
		}
	}
	*p = 0;
	return o;
}
