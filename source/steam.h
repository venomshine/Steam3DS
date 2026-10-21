#pragma once
#include <3ds.h>
#include <stdbool.h>
#include "net.h"

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
Req *steam_req_appdetails(u32 appid);
bool steam_parse_appdetails(const char *json, u32 appid, AppInfo *out);
Req *steam_req_items(const u32 *ids, int n);      // nombres, precios y reseñas de varios a la vez
bool steam_parse_review_summary(const char *json, AppInfo *a);
Req *steam_req_reviews(u32 appid, const char *cursor);
int steam_parse_reviews(const char *json, Review *out, int max, char *cursor, size_t cursor_sz);
Req *steam_req_morelike(u32 appid);

// País según Steam (cabecera steamCountry)
Req *steam_req_country(void);

void appinfo_free(AppInfo *a);
void rowlist_free(RowList *l);
