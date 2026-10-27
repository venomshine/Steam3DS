#pragma once
#include <3ds.h>
#include <stdbool.h>
#include <stddef.h>

// Peticiones HTTP en hilos propios. La interfaz nunca espera a la red:
// pide con net_get(), mira req->state en cada frame y suelta con net_release().

typedef enum { REQ_QUEUED, REQ_RUNNING, REQ_DONE, REQ_FAILED } ReqState;

typedef enum {
	LANE_API = 0,   // JSON de la tienda (prioridad)
	LANE_IMG = 1,   // imágenes
	LANE_COUNT
} NetLane;

typedef struct Req {
	char *url;
	NetLane lane;
	bool head;              // solo cabeceras (HEAD)
	char *post;             // cuerpo de un POST (application/x-www-form-urlencoded); nunca va al registro
	char *headers;          // cabeceras extra, una por línea (cookies de la sesión); nunca van al registro
	bool jpeg;              // decodificar como JPEG al terminar
	int jpeg_max_w;         // ancho máximo de la imagen decodificada

	volatile ReqState state;
	volatile bool cancel;
	long http;
	int eresult;            // cabecera x-eresult de la Web API (1 = OK; 0 si no vino)
	char err[112];

	char *data;             // cuerpo (terminado en '\0')
	size_t len, cap;
	char country[4];        // de la cookie steamCountry, si la manda
	char ctype[32];         // Content-Type de la respuesta (en minúsculas)
	long clen;              // Content-Length (-1 si no vino)
	char *login_cookie;     // valor de la cookie steamLoginSecure, si la manda (se borra al soltar)

	// Trabajo pesado que se hace en el hilo de red al terminar bien (leer un JSON grande): así la
	// interfaz no pierde frames. parse deja el resultado en parse_out y dice si salió bien;
	// parse_free suelta parse_out si nadie lo recoge (quien lo recoge pone parse_out a NULL)
	bool (*parse)(struct Req *r);
	void *parse_out;
	void (*parse_free)(void *out);
	bool parsed;

	u8 *pixels;             // si jpeg: ya en el orden de la GPU (RGB8 en mosaico de tw×th)
	int w, h;               // tamaño de la imagen
	int tw, th;             // tamaño de la textura (potencias de 2)
	bool alpha;             // PNG con transparencia: RGBA8 (4 bytes por píxel) en vez de RGB8

	int refs;
	struct Req *next;
} Req;

void net_init(void);
void net_exit(void);

Req *net_get(const char *url, NetLane lane);
Req *net_get_jpeg(const char *url, int max_w);
Req *net_head(const char *url);
Req *net_post(const char *url, const char *body);
// GET que además se lee en el hilo de red (ver parse en Req)
Req *net_get_parsed(const char *url, NetLane lane, bool (*parse)(Req *r), void *out, void (*out_free)(void *out));
// GET (body NULL) o POST con cabeceras extra separadas por '\n' ("Cookie: …", "Content-Type: …")
Req *net_request(const char *url, const char *body, const char *headers);
void net_release(Req *r);

// Estado de la conexión, para mostrar el motivo real en pantalla.
bool net_wifi_ok(void);
bool net_busy(void);           // hay algo descargándose
// Caché de imágenes en la SD: lo que ocupa (-1 mientras se cuenta al arrancar) y vaciarla
s64 net_cache_bytes(void);
void net_cache_clear(void);
// Hora UTC según los servidores de Steam = time(NULL) + g_server_delta (cuando g_server_known)
extern long long g_server_delta;
extern bool g_server_known;
const char *net_status_text(void);

char *url_escape(const char *s);   // malloc
void netlog(const char *fmt, ...);
