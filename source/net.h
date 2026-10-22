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
	char *login_cookie;     // valor de la cookie steamLoginSecure, si la manda (se borra al soltar)

	u8 *pixels;             // si jpeg: ya en el orden de la GPU (RGB8 en mosaico de tw×th)
	int w, h;               // tamaño de la imagen
	int tw, th;             // tamaño de la textura (potencias de 2)

	int refs;
	struct Req *next;
} Req;

void net_init(void);
void net_exit(void);

Req *net_get(const char *url, NetLane lane);
Req *net_get_jpeg(const char *url, int max_w);
Req *net_head(const char *url);
Req *net_post(const char *url, const char *body);
// GET (body NULL) o POST con cabeceras extra separadas por '\n' ("Cookie: …", "Content-Type: …")
Req *net_request(const char *url, const char *body, const char *headers);
void net_release(Req *r);

// Estado de la conexión, para mostrar el motivo real en pantalla.
bool net_wifi_ok(void);
bool net_busy(void);           // hay algo descargándose
// Hora UTC según los servidores de Steam = time(NULL) + g_server_delta (cuando g_server_known)
extern long long g_server_delta;
extern bool g_server_known;
const char *net_status_text(void);

char *url_escape(const char *s);   // malloc
void netlog(const char *fmt, ...);
