#pragma once
#include <3ds.h>
#include <stdbool.h>
#include "net.h"
#include "cJSON.h"

// Tienda de Steam: URLs y lectura de las respuestas (JSON) a estructuras simples.

typedef struct {
	u32 appid;
	char name[96];
	int disc;               // % de descuento (0 si no hay)
	char price[20];         // precio final ya formateado por Steam ("17,99€")
	char orig[20];          // precio original si hay descuento
	bool free;
	char review[40];        // "Muy positivas"
	int review_pct;
	int type;               // 0 juego, 1 demo, 4 DLC… (como lo da Steam)
	bool header;            // fila de título de apartado (Inicio), no es un juego
	bool coming_soon;
	bool early;             // sigue en acceso anticipado (dato de Steam, no la etiqueta de los usuarios)
	int minutes;            // biblioteca: minutos jugados
	u32 last_played;        // biblioteca: última vez (hora Unix)
	char icon[44];          // icono del juego (el hash de 32×32 de la comunidad de Steam)
} StoreRow;

typedef struct {
	StoreRow *rows;
	int count, cap;
	int total;              // total que dice Steam
	bool end;
	Req *req;
	char err[112];
} RowList;

enum {
	FEAT_SINGLE    = 1 << 0,
	FEAT_MULTI     = 1 << 1,
	FEAT_COOP      = 1 << 2,
	FEAT_PVP       = 1 << 3,
	FEAT_ACHIEVE   = 1 << 4,
	FEAT_CARDS     = 1 << 5,
	FEAT_CLOUD     = 1 << 6,
	FEAT_FAMILY    = 1 << 7,
	FEAT_PAD_FULL  = 1 << 8,
	FEAT_PAD_PART  = 1 << 9,
	FEAT_WORKSHOP  = 1 << 10,
	FEAT_INAPP     = 1 << 11,
};

typedef struct { char key[24]; char val[96]; } ReqLine;

typedef struct {
	bool voted_up;
	int hours;
	char *text;
} Review;

typedef struct {
	u32 appid;
	bool ok;
	char name[128];
	char *desc;                 // descripción corta
	char genres[3][32];
	int ngenres;
	char release[32];
	char developer[64];
	bool coming_soon;

	char rating_sys[8];         // "PEGI", "ESRB", "USK"…
	char rating[8];             // "18", "M"…
	int rating_age;
	char descriptors[4][32];    // claves en español para _()
	int ndesc;

	int metacritic;
	int achievements;
	u32 feats;
	int nscreens;
	char *shots[24];          // capturas (miniatura de 600 px)
	int nshots;
	char header[256];

	bool is_free, has_price;
	int disc;
	char price[20], orig[20];

	ReqLine rmin[7], rrec[7];
	int nmin, nrec;
	ReqLine mmin[7], mrec[7];   // macOS
	int nmmin, nmrec;
	char *about;                // «Acerca de este juego» en texto: \x01 delante de los títulos, «• » en las listas
	u32 demo;                   // demo gratuita del juego (0 si no tiene)
	bool mac, lnx;

	char review_label[40];
	int review_pct, review_count;

	u32 dlc[64];
	int ndlc;
	bool is_dlc, is_demo;
	u32 fullgame;               // si es un DLC: el juego base que necesita
	char fullgame_name[96];
} AppInfo;

void steam_set_context(const char *lang_steam, const char *country);
const char *steam_country(void);
const char *steam_lang(void);

// Listados de la tienda (IStoreQueryService/Query)
typedef enum {
	Q_OFFERS,   // juegos con descuento, por relevancia (como la tienda)
	Q_TOP,      // lo más vendido
	Q_NEW,      // novedades populares (últimos 30 días)
	Q_SOON,     // próximamente
	Q_F2P,      // gratis para jugar
	Q_DEMOS,    // demos más populares
	Q_EARLY,    // acceso anticipado
} StoreQuery;
Req *steam_req_query(StoreQuery q, int start, int count);
Req *steam_req_offers(int start, int count);
Req *steam_req_search(const char *term);
bool steam_parse_rows(const char *json, RowList *list, bool append);

// Ficha
Req *steam_req_appdetails(u32 appid);   // se lee en el hilo de red: recógela con steam_take_appdetails
bool steam_take_appdetails(Req *r, AppInfo *out);
bool steam_parse_appdetails(const char *json, u32 appid, AppInfo *out);
Req *steam_req_items(const u32 *ids, int n, bool assets);   // assets: también el icono
// URL del icono de 32×32 de una fila (vacía si Steam no lo da)
void steam_icon_url(const StoreRow *r, char *out, size_t n);      // nombres, precios y reseñas de varios a la vez
bool steam_parse_review_summary(const char *json, AppInfo *a);
Req *steam_req_reviews(u32 appid, const char *cursor);
int steam_parse_reviews(const char *json, Review *out, int max, char *cursor, size_t cursor_sz);
Req *steam_req_morelike(u32 appid);

// País según Steam (cabecera steamCountry)
Req *steam_req_country(void);

// Con sesión (token de acceso)
Req *steam_req_owned(u64 steamid, const char *token);
bool steam_parse_owned(const char *json, RowList *list);     // ordenada por nombre, como en Steam
void steam_sort_by_name(RowList *list);
Req *steam_req_wishlist(u64 steamid, const char *token);
int steam_parse_wishlist(const char *json, u32 **ids);       // appids por prioridad (malloc); -1 si falla
// store.steampowered.com/dynamicstore/userdata/ (con las cookies de la tienda): juegos que tienes
int steam_parse_userdata_owned(const char *json, u32 **ids); // appids (malloc); -1 si falla

void appinfo_free(AppInfo *a);
void rowlist_free(RowList *l);

// ---------------------------------------------------------------------------- Compras (fase 4)

// Una forma de comprar un juego (edición normal, edición completa, pack…)
typedef struct {
	u32 packageid, bundleid;    // uno de los dos
	char name[96];
	char price[20], orig[20];
	int disc;
	bool free;                  // licencia gratuita
	u32 disc_end;               // cuándo acaba la oferta (hora Unix, UTC; 0 si no hay o no se sabe)
} BuyOpt;

Req *steam_req_buyopts(u32 appid);
int steam_parse_buyopts(const char *json, u32 appid, BuyOpt *out, int max);

// Carrito de la cuenta (IAccountCartService): el mismo que ves en el móvil y en el PC
typedef struct {
	u64 line_id;
	u32 packageid, bundleid;
	u32 appid;                  // juego principal (para la ficha), cuando se conoce
	char name[96];
	char price[20], orig[20];   // precio de ahora (o el de cuando se añadió)
	int disc;
	bool valid;
} CartLine;

#define CART_MAX 40
typedef struct {
	CartLine line[CART_MAX];
	int n;
	char subtotal[24];
	bool valid;
} CartInfo;

// POST a un servicio de la Web API con sesión (input_json en el cuerpo)
Req *steam_post_service(const char *iface_method, cJSON *input, const char *token);
Req *steam_get_service(const char *iface_method, cJSON *input, const char *token);   // los que solo van por GET
bool steam_parse_cart(const char *json, CartInfo *c);           // respuesta con «cart»
Req *steam_req_cart_names(const CartInfo *c);
void steam_parse_cart_names(const char *json, CartInfo *c);     // nombres, precios y juego de cada línea

// ---------------------------------------------------------------------------- Pago (fase 4b)

// Lo que la página de pago de Steam (checkout) dice de ti. Solo se lee: los métodos de pago
// se cambian en la app oficial o en la web.
typedef struct {
	bool ok;                    // la página es la del pago (no la de iniciar sesión)
	char sessionid[32];         // g_sessionID de la página
	bool wallet;                // ofrece pagar con la cartera
	bool card;                  // hay una tarjeta guardada
	char card_type[16];         // visa, mastercard…
	char card_gid[32];          // id de la tarjeta guardada (gidPaymentID)
	char card_last4[8];         // los cuatro últimos números
	char country[4];            // país de facturación
} CheckoutPage;
bool steam_parse_checkout_page(const char *html, CheckoutPage *out);

typedef struct {
	bool has_wallet;
	long long balance;          // céntimos
	char formatted[24];
	char country[4];
} WalletInfo;
bool steam_parse_wallet(const char *json, WalletInfo *w);
