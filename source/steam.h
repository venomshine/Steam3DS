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
	int cents;              // precio final en céntimos de tu moneda (-1 si no se vende)
	bool free;
	char review[40];        // "Muy positivas"
	int review_pct;
	int type;               // 0 juego, 1 demo, 4 DLC… (como lo da Steam)
	bool header;            // fila de título de apartado (Inicio), no es un juego
	u32 tagid;              // Buscar: fila de búsqueda popular (una etiqueta), no es un juego
	bool family;            // biblioteca: te lo presta alguien de tu Familia de Steam
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
	bool has_total;         // Steam dio el total (si no, total es lo que hay)
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
// Buscar sin escribir nada: todo el catálogo por relevancia (o solo lo gratuito, o solo lo que tiene
// la etiqueta tagid si no es 0)
Req *steam_req_browse(int start, int count, bool free_only, u32 tagid);
// Las etiquetas más usadas de la tienda (las «búsquedas populares» de Buscar), en tu idioma
Req *steam_req_popular_tags(void);
typedef struct { u32 id; char name[40]; } StoreTag;
int steam_parse_tags(const char *json, StoreTag *out, int max);
Req *steam_req_offers(int start, int count);
Req *steam_req_search(const char *term);
bool steam_parse_rows(const char *json, RowList *list, bool append);
// Las búsquedas, Query, GetItems y «más como este» llegan ya leídas del hilo de red:
// steam_take_rows las une a list; si no se pudieron leer allí, queda steam_parse_rows
bool steam_take_rows(Req *r, RowList *list, bool append);

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
// Familia de Steam: a qué familia perteneces (1 sí y deja su id en gid, 0 no estás en ninguna,
// -1 respuesta no válida) y los juegos que comparten los demás (sin los tuyos ni los excluidos)
Req *steam_req_family_group(u64 steamid, const char *token);
int steam_parse_family_group(const char *json, u64 *gid);
Req *steam_req_family_apps(u64 gid, u64 steamid, const char *token);
int steam_parse_family_apps(const char *json, u64 me, RowList *out);
bool steam_parse_owned(const char *json, RowList *list);     // ordenada por nombre, como en Steam
void steam_sort_by_name(RowList *list);
Req *steam_req_wishlist(u64 steamid, const char *token);
int steam_parse_wishlist(const char *json, u32 **ids);       // appids por prioridad (malloc); -1 si falla
// store.steampowered.com/dynamicstore/userdata/ (con las cookies de la tienda): juegos que tienes
int steam_parse_userdata_owned(const char *json, u32 **ids); // appids (malloc); -1 si falla
int steam_parse_userdata_ignored(const char *json, u32 **ids); // los juegos ignorados (malloc); -1 si falla

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

// Notificaciones de Steam (la campana): regalos, ofertas de tu lista de deseos, amigos, comentarios…
typedef struct {
	u64 id;
	int type;               // ESteamNotificationType (1 regalo, 2 comentario, 4 amistad, 7 deseados en oferta…)
	u32 time;               // hora Unix
	bool read;
	u32 appid;              // el juego del que habla, si habla de uno
	char title[96];         // el título que trae (avisos generales y de los desarrolladores)
} SteamNotif;
Req *steam_req_notifications(const char *token);
int steam_parse_notifications(const char *json, SteamNotif *out, int max, int *unread);   // -1 si falla
Req *steam_req_notifications_read(const char *token);   // las marca todas como leídas

// Amigos: la lista (IFriendsListService; si no responde, ISteamUser/GetFriendList) y los datos de
// cada cuenta (GetPlayerLinkDetails: nombre, avatar, estado y el juego al que está jugando)
Req *steam_req_friends(u64 steamid, const char *token, bool fallback);
int steam_parse_friends(const char *json, u64 *ids, int max, int *pending);   // -1 si falla
typedef struct {
	u64 id;
	char name[40];
	char avatar_sha[32];    // sha_digest_avatar (base64); la URL la hace auth_avatar_url
	int state;              // 0 desconectado, 1 en línea, 2 ocupado, 3 ausente, 4 durmiendo…
	u32 game;               // a qué está jugando (0 nada)
	char game_name[64];
	u32 last_off;           // última vez que se desconectó
	bool request;           // te ha mandado una solicitud de amistad (aún no es tu amigo)
} SteamPerson;
Req *steam_req_people(const u64 *ids, int n, const char *token);

// Logros: los del juego (públicos, con el % de jugadores que los tienen) y cuáles tienes tú
typedef struct {
	char name[64];
	char desc[160];
	char icon[48], icon_gray[48];   // archivos en .../steamcommunity/public/images/apps/<appid>/
	float pct;                      // % de jugadores que lo tienen
	bool hidden;
	bool done;                      // lo tienes
} Achievement;
Req *steam_req_achievements(u32 appid);
int steam_parse_achievements(const char *json, Achievement **out);   // malloc; -1 si falla
Req *steam_req_my_achievements(u32 appid, u64 steamid, const char *token);
int steam_mark_unlocked(const char *json, Achievement *a, int n);   // cuántos marcó; -1 si falla
Req *steam_req_achievement_progress(u32 appid, u64 steamid, const char *token);
bool steam_parse_achievement_progress(const char *json, int *unlocked, int *total);
int steam_parse_people(const char *json, SteamPerson *out, int max);
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
	char ssa_text[200];         // la casilla del Acuerdo de Suscriptor, como la escribe Steam (con la fecha)
	bool withdrawal;            // Steam pide además renunciar al desistimiento (UE)
	char withdrawal_text[480];  // y su texto
} CheckoutPage;
bool steam_parse_checkout_page(const char *html, CheckoutPage *out);

typedef struct {
	bool has_wallet;
	long long balance;          // céntimos
	char formatted[24];
	char country[4];
} WalletInfo;
bool steam_parse_wallet(const char *json, WalletInfo *w);

// Perfil de Steam de cualquiera (también el tuyo): lo que lleva puesto (fondo y marco), sus vitrinas
// y su ficha pública (nombre, nombre real, país, «sobre mí», desde cuándo es miembro)
#define PROFILE_MAX_SHOW 10
#define SHOW_MAX_SLOTS 16
enum { SHOW_RARE_ACH = 1, SHOW_COLLECTOR = 2, SHOW_SHOTS = 7, SHOW_TEXT = 8, SHOW_FAV_GAME = 6, SHOW_ARTWORK = 13,
	SHOW_ONE_ARTWORK = 22, SHOW_COMPLETIONIST = 23, SHOW_ACHIEVEMENTS = 17 };
typedef struct {
	int type;                       // tipo de vitrina de Steam (SHOW_*)
	int n;                          // huecos con algo
	u32 appid[SHOW_MAX_SLOTS];
	u64 file[SHOW_MAX_SLOTS];       // capturas e ilustraciones (publishedfileid)
	char ach[SHOW_MAX_SLOTS][12];   // vitrina de logros: «statid_bit» de cada logro
	char *title, *text;             // vitrina de texto (malloc)
} Showcase;
typedef struct {
	char bg[112];                   // fondo (ruta bajo .../steamcommunity/public/images/)
	char frame[112];                // marco del avatar (PNG con transparencia)
	Showcase show[PROFILE_MAX_SHOW];
	int nshow, nother;              // nother: vitrinas que aquí no se pueden enseñar
	bool items_ok, shows_ok, xml_ok;
	char theme[24];                 // tema del perfil («» el predeterminado)
	char name[64], realname[64], location[64], avatar[160];
	char state[24];                 // online, offline, in-game
	char state_msg[96];
	u32 since;                      // miembro desde (hora Unix)
	bool is_private;
	char *summary;                  // «sobre mí» en texto (malloc)
} SteamProfile;
Req *steam_req_profile_items(u64 steamid);
bool steam_parse_profile_items(const char *json, SteamProfile *p);
Req *steam_req_profile_showcases(u64 steamid);
bool steam_parse_profile_showcases(const char *json, SteamProfile *p);
Req *steam_req_profile_xml(u64 steamid);
bool steam_parse_profile_xml(const char *xml, SteamProfile *p);
void steam_profile_free(SteamProfile *p);
Req *steam_req_level(u64 steamid, const char *token);
int steam_parse_level(const char *json);   // -1 si falla
// Imagen de una captura o ilustración: con la sesión (IPublishedFileService) y, si no, la de su página
Req *steam_req_files(const u64 *ids, int n, const char *token);
int steam_parse_files(const char *json, const u64 *ids, int n, char (*urls)[200]);   // cuántas encontró
Req *steam_req_file_page(u64 id);
bool steam_parse_file_page(const char *html, char *url, size_t n);

// Tus capturas de un juego (las que subiste a tu perfil de Steam)
typedef struct {
	char url[200];          // ya reducida al ancho de la pantalla de arriba
	char caption[96];
	u32 when;               // cuándo la hiciste (hora Unix)
} MyShot;
Req *steam_req_my_shots(u64 steamid, u32 appid, const char *token);
int steam_parse_my_shots(const char *json, u32 appid, MyShot *out, int max, int *total);   // -1 si falla

// Tu reseña de un juego (has = false si no has escrito ninguna)
typedef struct {
	bool has;
	bool up;                // la recomiendas
	char *text;             // malloc
	u32 created, updated;
	int votes_up, votes_funny, comments;
} MyReview;
Req *steam_req_my_review(u64 steamid, u32 appid, const char *token);
bool steam_parse_my_review(const char *json, u32 appid, MyReview *out);   // false si falla

// Instalar en tu PC de forma remota (lo mismo que el botón «Instalar» de la tienda con sesión): los
// equipos con Steam abierto con tu cuenta y la orden de instalar
typedef struct {
	u64 id;                 // client_instanceid
	char name[64], os[48];
	int installed;          // 1 ya está instalado, 0 no, -1 no se sabe (2: instalación pedida desde aquí)
} RemotePC;
Req *steam_req_remote_pcs(const char *token);
int steam_parse_remote_pcs(const char *json, RemotePC *out, int max);   // -1 si falla
Req *steam_req_remote_apps(u64 client, u32 appid, const char *token);
int steam_parse_remote_app(const char *json, u32 appid);   // 1 instalado, 0 no, -1 no se sabe
Req *steam_req_remote_install(u64 client, u32 appid, const char *token);

// Playtest del juego (el appid del playtest, 0 si no tiene): lo da la tienda en related_items
Req *steam_req_playtest(u32 appid);   // también trae la compatibilidad con Steam Deck
u32 steam_parse_playtest(const char *json, u32 appid);
int steam_parse_deck(const char *json, u32 appid);   // 3 verificado, 2 jugable, 1 no compatible, 0 sin probar, -1 no se sabe

// Privacidad de tu perfil: la de la página de edición del perfil en la comunidad (se lee de su
// configuración y se guarda con su mismo formulario, ajaxsetprivacy). Valores: 1 privado, 2 solo
// amigos, 3 público; comments: 1 cualquiera, 0 solo amigos, 2 solo tú
typedef struct {
	int profile, games, friends, inventory;
	bool playtime_private, gifts_private;
	int comments;
	char *raw;              // el objeto PrivacySettings tal cual (malloc), para no perder campos
} SteamPrivacy;
Req *steam_req_privacy(u64 steamid, const char *cookies);
bool steam_parse_privacy(const char *html, SteamPrivacy *out);
Req *steam_req_set_privacy(u64 steamid, const SteamPrivacy *p, const char *sessionid, const char *cookies);
void steam_privacy_free(SteamPrivacy *p);
// Temas del perfil que tienes (theme_id) y cambiar el tuyo
Req *steam_req_themes(const char *token);
int steam_parse_themes(const char *json, char (*out)[24], int max);
Req *steam_req_set_theme(const char *theme, const char *token);

// Logros desbloqueados de alguien en un juego, con su clave (statid_bit) para casarlos con su vitrina
typedef struct {
	u32 appid;
	char key[12];           // «statid_bit»
	char name[64];
	char icon[48];          // archivo en .../steamcommunity/public/images/apps/<appid>/
	float pct;
} TopAch;
int steam_parse_top_achievements(const char *json, TopAch *out, int max);   // -1 si falla

// Lo que hay en tu PC (como la sección de descargas de la app de Steam del móvil): juegos instalados,
// lo que se está descargando o actualizando, pausar o reanudar las descargas y desinstalar
typedef struct {
	u32 appid;
	char name[96];
	bool installed, changing, paused, running;
	long long done, total;      // bytes descargados y por descargar
	int queue;                  // puesto en la cola de descargas (-1 si no está en cola)
} PcApp;
Req *steam_req_pc_apps(u64 client, const char *token);
int steam_parse_pc_apps(const char *json, PcApp **out, long long *free_bytes);   // malloc; -1 si falla
Req *steam_req_pc_downloads(u64 client, bool enable, const char *token);
Req *steam_req_pc_uninstall(u64 client, u32 appid, const char *token);

// Solicitudes de amistad que te han mandado (de la misma lista de amigos) y responderlas o mandar
// una, con los mismos formularios de la comunidad (actions/AddFriendAjax e IgnoreFriendInviteAjax)
int steam_parse_friend_requests(const char *json, u64 *ids, int max);
enum { FRIEND_ADD, FRIEND_ACCEPT, FRIEND_IGNORE };
Req *steam_req_friend_action(int action, u64 steamid, const char *sessionid, const char *cookies);
bool steam_friend_action_ok(const Req *r);

// Tu cola de descubrimiento (los juegos que Steam te recomienda; rebuild: una cola nueva)
Req *steam_req_discovery(bool rebuild, const char *token);
int steam_parse_discovery(const char *json, u32 *ids, int max);   // -1 si falla

// Noticias oficiales del juego (los anuncios de su comunidad en Steam)
typedef struct {
	char title[128];
	char text[300];         // el principio, en texto
	u32 date;
} SteamNews;
Req *steam_req_news(u32 appid);
int steam_parse_news(const char *json, SteamNews *out, int max);   // -1 si falla
