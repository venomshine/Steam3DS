#define _GNU_SOURCE
#include "steam.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <strings.h>
#include <time.h>

static char g_lang[16] = "spanish";
static char g_country[4] = "US";

void steam_set_context(const char *lang_steam, const char *country)
{
	if (lang_steam && *lang_steam) snprintf(g_lang, sizeof g_lang, "%s", lang_steam);
	if (country && strlen(country) == 2) snprintf(g_country, sizeof g_country, "%s", country);
}

const char *steam_country(void) { return g_country; }
const char *steam_lang(void) { return g_lang; }

// ----------------------------------------------------------------------------
// Utilidades

static const char *jstr(const cJSON *o, const char *k)
{
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	return cJSON_IsString(v) ? v->valuestring : NULL;
}

static double jnum(const cJSON *o, const char *k, double def)
{
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	if (cJSON_IsNumber(v)) return v->valuedouble;
	if (cJSON_IsString(v)) return atof(v->valuestring);
	return def;
}

static bool jbool(const cJSON *o, const char *k)
{
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	if (cJSON_IsBool(v)) return cJSON_IsTrue(v);
	if (cJSON_IsString(v)) return strcmp(v->valuestring, "true") == 0;
	return false;
}

static u64 jid(const cJSON *o, const char *k);

static void copy(char *dst, size_t n, const char *src)
{
	snprintf(dst, n, "%s", src ? src : "");
}

// Espacios raros (sin salto, finos, de ancho cero…) que la fuente de la consola pinta como un
// cuadrado: los anchos pasan a ser un espacio normal y los invisibles se quitan
static void tidy_utf8(char *s)
{
	unsigned char *r = (unsigned char *)s, *w = (unsigned char *)s;
	while (*r) {
		u32 cp = 0;
		int len = 1;
		if (r[0] == 0xC2 && r[1]) { cp = 0x80 | (r[1] & 0x3F); len = 2; }
		else if ((r[0] & 0xF0) == 0xE0 && r[1] && r[2]) { cp = ((r[0] & 0x0F) << 12) | ((r[1] & 0x3F) << 6) | (r[2] & 0x3F); len = 3; }
		bool space = cp == 0xA0 || (cp >= 0x2000 && cp <= 0x200A) || cp == 0x202F || cp == 0x205F || cp == 0x3000;
		bool gone = cp == 0xAD || (cp >= 0x200B && cp <= 0x200D) || cp == 0x2060 || cp == 0xFEFF;
		// Guiones que no están en la fuente de la consola (Counter‑Strike con el guion que no parte)
		bool dash = cp == 0x2010 || cp == 0x2011;
		if (space) { *w++ = ' '; r += len; continue; }
		if (dash) { *w++ = '-'; r += len; continue; }
		if (gone) { r += len; continue; }
		*w++ = *r++;
	}
	*w = 0;
}

// Recorta en n bytes sin partir un carácter UTF-8
static void copy_utf8(char *dst, size_t n, const char *src)
{
	if (!src) { dst[0] = 0; return; }
	size_t len = strlen(src);
	if (len < n) {
		memcpy(dst, src, len + 1);
	} else {
		size_t cut = n - 1;
		while (cut > 0 && ((unsigned char)src[cut] & 0xC0) == 0x80) cut--;
		memcpy(dst, src, cut);
		dst[cut] = 0;
	}
	tidy_utf8(dst);
}

static Req *service_call(const char *iface_method, cJSON *input)
{
	char *js = cJSON_PrintUnformatted(input);
	char *esc = url_escape(js);
	size_t n = strlen(esc) + 128;
	char *url = malloc(n);
	snprintf(url, n, "https://api.steampowered.com/%s/v1/?input_json=%s", iface_method, esc);
	Req *r = net_get(url, LANE_API);
	free(url);
	free(esc);
	free(js);
	cJSON_Delete(input);
	return r;
}

static cJSON *context(void)
{
	cJSON *c = cJSON_CreateObject();
	cJSON_AddStringToObject(c, "language", g_lang);
	cJSON_AddStringToObject(c, "country_code", g_country);
	return c;
}

// ----------------------------------------------------------------------------
// Listas (Query, GetItems, MoreLikeThis devuelven store_items con el mismo formato)

static void row_from_item(const cJSON *it, StoreRow *row)
{
	memset(row, 0, sizeof *row);
	row->cents = -1;
	row->appid = (u32)jnum(it, "appid", jnum(it, "id", 0));
	copy_utf8(row->name, sizeof row->name, jstr(it, "name"));
	const cJSON *bp = cJSON_GetObjectItemCaseSensitive(it, "best_purchase_option");
	if (bp) {
		copy(row->price, sizeof row->price, jstr(bp, "formatted_final_price"));
		row->disc = (int)jnum(bp, "discount_pct", 0);
		if (row->disc > 0) copy(row->orig, sizeof row->orig, jstr(bp, "formatted_original_price"));
		row->cents = (int)jnum(bp, "final_price_in_cents", -1);
		if (row->cents == 0) row->free = true;
	}
	if (jbool(it, "is_free")) row->free = true;
	row->type = (int)jnum(it, "type", 0);
	if (row->type == 1) row->free = true;          // las demos son gratis
	const cJSON *rel = cJSON_GetObjectItemCaseSensitive(it, "release");
	if (rel && jbool(rel, "is_coming_soon")) row->coming_soon = true;
	if (rel && jbool(rel, "is_early_access")) row->early = true;
	const cJSON *as = cJSON_GetObjectItemCaseSensitive(it, "assets");
	if (as) copy(row->icon, sizeof row->icon, jstr(as, "community_icon"));
	const cJSON *rv = cJSON_GetObjectItemCaseSensitive(it, "reviews");
	const cJSON *sf = rv ? cJSON_GetObjectItemCaseSensitive(rv, "summary_filtered") : NULL;
	if (sf) {
		copy_utf8(row->review, sizeof row->review, jstr(sf, "review_score_label"));
		row->review_pct = (int)jnum(sf, "percent_positive", 0);
	}
}

// Las listas también se leen en el hilo de red: una página de la biblioteca con iconos pesa
// unos 75 KB y leerla en el hilo de la interfaz era el bajón de imagen al abrir START al empezar
static bool rows_job(Req *r)
{
	RowList *l = r->parse_out;
	return steam_parse_rows(r->data, l, false);
}

static void rows_free(void *p)
{
	rowlist_free(p);
	free(p);
}

bool steam_take_rows(Req *r, RowList *list, bool append)
{
	if (!r || !r->parsed || !r->parse_out) return false;
	RowList *got = r->parse_out;
	if (!append) list->count = 0;
	if (list->count + got->count > list->cap) {
		int cap = list->cap ? list->cap : 32;
		while (cap < list->count + got->count) cap *= 2;
		StoreRow *n = realloc(list->rows, cap * sizeof(StoreRow));
		if (!n) return false;   // se queda en parse_out y la lee steam_parse_rows
		list->rows = n;
		list->cap = cap;
	}
	if (got->count) memcpy(list->rows + list->count, got->rows, got->count * sizeof(StoreRow));
	list->count += got->count;
	list->total = got->has_total ? got->total : list->count;
	if (got->count == 0 || list->count >= list->total) list->end = true;
	list->err[0] = 0;
	rows_free(got);
	r->parse_out = NULL;
	return true;
}

static Req *service_rows(const char *iface_method, cJSON *input)
{
	RowList *l = calloc(1, sizeof *l);
	if (!l) {
		cJSON_Delete(input);
		return NULL;
	}
	char *js = cJSON_PrintUnformatted(input);
	char *esc = url_escape(js);
	size_t n = strlen(esc) + 128;
	char *url = malloc(n);
	snprintf(url, n, "https://api.steampowered.com/%s/v1/?input_json=%s", iface_method, esc);
	Req *r = net_get_parsed(url, LANE_API, rows_job, l, rows_free);
	free(url);
	free(esc);
	free(js);
	cJSON_Delete(input);
	return r;
}

bool steam_parse_rows(const char *json, RowList *list, bool append)
{
	cJSON *root = cJSON_Parse(json);
	if (!root) {
		snprintf(list->err, sizeof list->err, "respuesta no válida");
		return false;
	}
	const cJSON *resp = cJSON_GetObjectItemCaseSensitive(root, "response");
	const cJSON *items = resp ? cJSON_GetObjectItemCaseSensitive(resp, "store_items") : NULL;
	const cJSON *meta = resp ? cJSON_GetObjectItemCaseSensitive(resp, "metadata") : NULL;
	if (!append) list->count = 0;
	int added = 0;
	// Lo más visitado (IUserStoreVisitService) llega en «items»
	if (!items && resp) items = cJSON_GetObjectItemCaseSensitive(resp, "items");
	const cJSON *it;
	cJSON_ArrayForEach(it, items) {
		// Lo que no está en la tienda (herramientas, servidores, apps ocultas) llega sin nombre y
		// con appid 0: en la biblioteca salía como una fila vacía que no abría nada
		const char *name = jstr(it, "name");
		if (!name || !*name || jnum(it, "appid", 0) <= 0) continue;
		if (list->count == list->cap) {
			int cap = list->cap ? list->cap * 2 : 32;
			StoreRow *r = realloc(list->rows, cap * sizeof(StoreRow));
			if (!r) break;
			list->rows = r;
			list->cap = cap;
		}
		row_from_item(it, &list->rows[list->count++]);
		added++;
	}
	list->has_total = meta != NULL;
	if (meta) list->total = (int)jnum(meta, "total_matching_records", list->count);
	else list->total = list->count;
	if (added == 0 || list->count >= list->total) list->end = true;
	list->err[0] = 0;
	cJSON_Delete(root);
	return true;
}

Req *steam_req_query(StoreQuery kind, int start, int count)
{
	static const int sorts[] = {
		[Q_OFFERS] = 10, [Q_TOP] = 12, [Q_NEW] = 12, [Q_SOON] = 10, [Q_F2P] = 12, [Q_DEMOS] = 13, [Q_EARLY] = 12,
	};
	cJSON *in = cJSON_CreateObject();
	cJSON *q = cJSON_AddObjectToObject(in, "query");
	cJSON_AddNumberToObject(q, "start", start);
	cJSON_AddNumberToObject(q, "count", count);
	cJSON_AddNumberToObject(q, "sort", sorts[kind]);
	cJSON *f = cJSON_AddObjectToObject(q, "filters");
	if (kind == Q_SOON) cJSON_AddBoolToObject(f, "coming_soon_only", true);
	else cJSON_AddBoolToObject(f, "released_only", true);
	cJSON *tf = cJSON_AddObjectToObject(f, "type_filters");
	cJSON_AddBoolToObject(tf, kind == Q_DEMOS ? "include_demos" : "include_games", true);
	if (kind == Q_OFFERS) {
		cJSON *pf = cJSON_AddObjectToObject(f, "price_filters");
		cJSON_AddNumberToObject(pf, "min_discount_percent", 1);
	} else if (kind == Q_F2P) {
		cJSON *pf = cJSON_AddObjectToObject(f, "price_filters");
		cJSON_AddBoolToObject(pf, "only_free_items", true);
	} else if (kind == Q_NEW) {
		// release_date_type 1: por fecha de lanzamiento en Steam (probado: 0 y 4 no filtran)
		time_t now = time(NULL);
		cJSON *rd = cJSON_AddObjectToObject(f, "release_date_filter");
		cJSON_AddNumberToObject(rd, "release_date_type", 1);
		cJSON_AddNumberToObject(rd, "start_date", (double)(now - 30 * 86400));
		cJSON_AddNumberToObject(rd, "end_date", (double)now);
	} else if (kind == Q_EARLY) {
		// Etiqueta 493: Acceso anticipado
		cJSON *tags = cJSON_AddArrayToObject(f, "tagids_must_match");
		cJSON *tm = cJSON_CreateObject();
		cJSON *ids = cJSON_AddArrayToObject(tm, "tagids");
		cJSON_AddItemToArray(ids, cJSON_CreateNumber(493));
		cJSON_AddItemToArray(tags, tm);
	}
	cJSON_AddItemToObject(in, "context", context());
	cJSON *dr = cJSON_AddObjectToObject(in, "data_request");
	cJSON_AddBoolToObject(dr, "include_reviews", true);
	cJSON_AddBoolToObject(dr, "include_release", true);
	return service_rows("IStoreQueryService/Query", in);
}

Req *steam_req_browse(int start, int count, bool free_only, u32 tagid)
{
	cJSON *in = cJSON_CreateObject();
	cJSON *q = cJSON_AddObjectToObject(in, "query");
	cJSON_AddNumberToObject(q, "start", start);
	cJSON_AddNumberToObject(q, "count", count);
	cJSON_AddNumberToObject(q, "sort", 10);   // relevancia, como la web
	cJSON *f = cJSON_AddObjectToObject(q, "filters");
	cJSON_AddBoolToObject(f, "released_only", true);
	cJSON *tf = cJSON_AddObjectToObject(f, "type_filters");
	cJSON_AddBoolToObject(tf, "include_games", true);
	if (free_only) {
		cJSON *pf = cJSON_AddObjectToObject(f, "price_filters");
		cJSON_AddBoolToObject(pf, "only_free_items", true);
	}
	if (tagid) {
		cJSON *tags = cJSON_AddArrayToObject(f, "tagids_must_match");
		cJSON *tm = cJSON_CreateObject();
		cJSON *ids = cJSON_AddArrayToObject(tm, "tagids");
		cJSON_AddItemToArray(ids, cJSON_CreateNumber(tagid));
		cJSON_AddItemToArray(tags, tm);
	}
	cJSON_AddItemToObject(in, "context", context());
	cJSON *dr = cJSON_AddObjectToObject(in, "data_request");
	cJSON_AddBoolToObject(dr, "include_reviews", true);
	cJSON_AddBoolToObject(dr, "include_release", true);
	return service_rows("IStoreQueryService/Query", in);
}

// Lo más visitado de la tienda ahora mismo: lo que la web de Steam enseña como «Búsquedas
// populares» al abrir la lupa sin escribir nada (con el precio de tu país)
Req *steam_req_most_visited(int count)
{
	cJSON *in = cJSON_CreateObject();
	cJSON_AddNumberToObject(in, "count", count);
	cJSON_AddItemToObject(in, "context", context());
	cJSON *dr = cJSON_AddObjectToObject(in, "data_request");
	cJSON_AddBoolToObject(dr, "include_reviews", true);
	cJSON_AddBoolToObject(dr, "include_release", true);
	return service_rows("IUserStoreVisitService/GetMostVisitedItemsOnStore", in);
}

Req *steam_req_popular_tags(void)
{
	char url[160];
	snprintf(url, sizeof url, "https://api.steampowered.com/IStoreService/GetMostPopularTags/v1/?language=%s", g_lang);
	return net_get(url, LANE_API);
}

int steam_parse_tags(const char *json, StoreTag *out, int max)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	const cJSON *tags = resp ? cJSON_GetObjectItemCaseSensitive(resp, "tags") : NULL;
	const cJSON *t;
	int n = 0;
	cJSON_ArrayForEach(t, tags) {
		if (n >= max) break;
		const char *name = jstr(t, "name");
		u32 id = (u32)jnum(t, "tagid", 0);
		if (!name || !*name || !id) continue;
		out[n].id = id;
		copy_utf8(out[n].name, sizeof out[n].name, name);
		n++;
	}
	bool ok = root != NULL;
	cJSON_Delete(root);
	return ok ? n : -1;
}

Req *steam_req_search(const char *term)
{
	cJSON *in = cJSON_CreateObject();
	cJSON_AddStringToObject(in, "search_term", term);
	cJSON_AddNumberToObject(in, "max_results", 30);
	cJSON_AddBoolToObject(in, "use_spellcheck", true);
	cJSON_AddItemToObject(in, "context", context());
	cJSON *f = cJSON_AddObjectToObject(in, "filters");
	cJSON *tf = cJSON_AddObjectToObject(f, "type_filters");
	cJSON_AddBoolToObject(tf, "include_games", true);
	cJSON_AddBoolToObject(tf, "include_dlc", true);
	cJSON_AddBoolToObject(tf, "include_demos", true);
	cJSON *dr = cJSON_AddObjectToObject(in, "data_request");
	cJSON_AddBoolToObject(dr, "include_reviews", true);
	cJSON_AddBoolToObject(dr, "include_release", true);
	return service_rows("IStoreQueryService/SearchSuggestions", in);
}

Req *steam_req_offers(int start, int count)
{
	return steam_req_query(Q_OFFERS, start, count);
}

Req *steam_req_items(const u32 *ids, int n, bool assets)
{
	cJSON *in = cJSON_CreateObject();
	cJSON *arr = cJSON_AddArrayToObject(in, "ids");
	for (int i = 0; i < n; i++) {
		cJSON *o = cJSON_CreateObject();
		cJSON_AddNumberToObject(o, "appid", ids[i]);
		cJSON_AddItemToArray(arr, o);
	}
	cJSON_AddItemToObject(in, "context", context());
	cJSON *dr = cJSON_AddObjectToObject(in, "data_request");
	cJSON_AddBoolToObject(dr, "include_reviews", true);
	if (assets) cJSON_AddBoolToObject(dr, "include_assets", true);
	return service_rows("IStoreBrowseService/GetItems", in);
}

void steam_icon_url(const StoreRow *r, char *out, size_t n)
{
	if (r->icon[0]) snprintf(out, n, "https://shared.akamai.steamstatic.com/community_assets/images/apps/%lu/%s.jpg", (unsigned long)r->appid, r->icon);
	else if (n) out[0] = 0;
}

Req *steam_req_morelike(u32 appid)
{
	cJSON *in = cJSON_CreateObject();
	cJSON *id = cJSON_AddObjectToObject(in, "item_id");
	cJSON_AddNumberToObject(id, "appid", appid);
	cJSON_AddNumberToObject(in, "count", 20);
	cJSON_AddItemToObject(in, "context", context());
	cJSON *dr = cJSON_AddObjectToObject(in, "data_request");
	cJSON_AddBoolToObject(dr, "include_reviews", true);
	return service_rows("IStoreQueryService/MoreLikeThis", in);
}

Req *steam_req_country(void)
{
	return net_head("https://store.steampowered.com/about/");
}

// ----------------------------------------------------------------------------
// Ficha

// La ficha llega leída del hilo de red: un appdetails pesa 30-50 KB y leerlo (con el «acerca de»)
// en el hilo de la interfaz se notaba como un tirón cada vez que cambiabas de juego
static bool details_job(Req *r)
{
	AppInfo *a = r->parse_out;
	return steam_parse_appdetails(r->data, a->appid, a);
}

static void details_free(void *p)
{
	appinfo_free(p);
	free(p);
}

Req *steam_req_appdetails(u32 appid)
{
	char url[160];
	snprintf(url, sizeof url, "https://store.steampowered.com/api/appdetails?appids=%lu&cc=%s&l=%s",
		(unsigned long)appid, g_country, g_lang);
	AppInfo *a = calloc(1, sizeof *a);
	if (!a) return NULL;
	a->appid = appid;
	return net_get_parsed(url, LANE_API, details_job, a, details_free);
}

bool steam_take_appdetails(Req *r, AppInfo *out)
{
	if (!r || !r->parsed || !r->parse_out) return false;
	*out = *(AppInfo *)r->parse_out;   // los punteros de dentro pasan a out
	free(r->parse_out);
	r->parse_out = NULL;
	return true;
}

// Quita etiquetas HTML y entidades, y deja un salto de línea por <br> o </li>
static void html_to_lines(const char *in, char *out, size_t n)
{
	size_t o = 0;
	while (*in && o + 4 < n) {
		if (*in == '<') {
			if (!strncasecmp(in, "<br", 3) || !strncasecmp(in, "</li", 4) || !strncasecmp(in, "</p", 3)) out[o++] = '\n';
			const char *e = strchr(in, '>');
			if (!e) break;
			in = e + 1;
		} else if (*in == '&') {
			static const struct { const char *e; const char *r; } ents[] = {
				{"&amp;", "&"}, {"&quot;", "\""}, {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "},
				{"&#39;", "'"}, {"&reg;", "®"}, {"&trade;", "™"}, {"&copy;", "©"}, {"&euro;", "€"}, {"&pound;", "£"},
			};
			bool done = false;
			for (size_t i = 0; i < sizeof ents / sizeof *ents; i++) {
				size_t l = strlen(ents[i].e);
				if (!strncmp(in, ents[i].e, l)) {
					size_t rl = strlen(ents[i].r);
					if (o + rl < n) { memcpy(out + o, ents[i].r, rl); o += rl; }
					in += l;
					done = true;
					break;
				}
			}
			if (!done) out[o++] = *in++;
		} else if (*in == '\r' || *in == '\t') {
			in++;
		} else {
			out[o++] = *in++;
		}
	}
	out[o] = 0;
}

static void replace_all(char *s, size_t n, const char *from, const char *to)
{
	size_t fl = strlen(from), tl = strlen(to);
	char *p = s;
	while ((p = strstr(p, from))) {
		size_t rest = strlen(p + fl);
		if ((size_t)(p - s) + tl + rest + 1 > n) break;
		memmove(p + tl, p + fl, rest + 1);
		memcpy(p, to, tl);
		p += tl;
	}
}

// «Acerca de este juego» a texto: fuera imágenes y vídeos; títulos marcados con \x01 al principio
// de su línea, listas con «• », párrafos separados por una línea en blanco
static char *html_to_about(const char *in)
{
	if (!in) return NULL;
	size_t cap = strlen(in) + 64;
	char *out = malloc(cap);
	if (!out) return NULL;
	size_t o = 0;
	int skip = 0;   // dentro de <video> o <script>
	while (*in && o + 8 < cap) {
		if (*in == '<') {
			const char *e = strchr(in, '>');
			if (!e) break;
			bool close = in[1] == '/';
			const char *tag = in + (close ? 2 : 1);
			if (!strncasecmp(tag, "video", 5) || !strncasecmp(tag, "script", 6)) skip += close ? -1 : 1;
			else if (!skip) {
				if ((tag[0] == 'h' || tag[0] == 'H') && tag[1] >= '1' && tag[1] <= '6') {
					out[o++] = '\n';
					if (!close) out[o++] = '\x01';
					else out[o++] = '\n';
				} else if (!strncasecmp(tag, "br", 2)) {
					out[o++] = '\n';
				} else if (!strncasecmp(tag, "p", 1) && (tag[1] == '>' || tag[1] == ' ') && close) {
					out[o++] = '\n';
					out[o++] = '\n';
				} else if (!strncasecmp(tag, "li", 2) && !close) {
					out[o++] = '\n';
					memcpy(out + o, "• ", 4);
					o += 4;
				} else if (!strncasecmp(tag, "ul", 2) && close) {
					out[o++] = '\n';
				}
			}
			if (skip < 0) skip = 0;
			in = e + 1;
		} else if (skip) {
			in++;
		} else if (*in == '&') {
			static const struct { const char *e; const char *r; } ents[] = {
				{"&amp;", "&"}, {"&quot;", "\""}, {"&lt;", "<"}, {"&gt;", ">"}, {"&nbsp;", " "},
				{"&#39;", "'"}, {"&reg;", "®"}, {"&trade;", "™"}, {"&copy;", "©"},
			};
			bool done = false;
			for (size_t i = 0; i < sizeof ents / sizeof *ents; i++) {
				size_t l = strlen(ents[i].e);
				if (!strncmp(in, ents[i].e, l)) {
					size_t rl = strlen(ents[i].r);
					memcpy(out + o, ents[i].r, rl);
					o += rl;
					in += l;
					done = true;
					break;
				}
			}
			if (!done) out[o++] = *in++;
		} else if (*in == '\r' || *in == '\t' || *in == '\n') {
			if (o && out[o - 1] != ' ' && out[o - 1] != '\n') out[o++] = ' ';
			in++;
		} else {
			out[o++] = *in++;
		}
	}
	out[o] = 0;
	// Limpieza: espacios al empezar línea, títulos vacíos y como mucho una línea en blanco seguida
	size_t w = 0;
	int nl = 2;
	for (size_t i = 0; i < o; i++) {
		char c = out[i];
		if (c == ' ' && (nl || (w && out[w - 1] == ' '))) continue;
		if (c == '\n') {
			while (w && out[w - 1] == ' ') w--;
			if (w && out[w - 1] == '\x01') { w--; continue; }   // título vacío
			if (nl >= 2) continue;
			nl++;
			out[w++] = c;
			continue;
		}
		if (c != '\x01') nl = 0;
		out[w++] = c;
	}
	while (w && (out[w - 1] == '\n' || out[w - 1] == ' ')) w--;
	out[w] = 0;
	// Títulos sin texto debajo (antes de otro título o del final): fuera. Pasa cuando lo que
	// seguía eran solo imágenes o vídeos
	char *dst = out;
	bool text = false;
	for (char *line = out; *line;) {
		char *end = strchr(line, '\n');
		size_t len = end ? (size_t)(end - line) + 1 : strlen(line);
		bool heading = line[0] == '\x01';
		bool keep = true;
		if (heading) {
			keep = false;
			for (char *q = line + len; *q;) {
				char *qe = strchr(q, '\n');
				size_t ql = qe ? (size_t)(qe - q) : strlen(q);
				if (ql && q[0] != '\x01') { keep = true; break; }
				if (ql && q[0] == '\x01') break;
				q += ql + (qe ? 1 : 0);
			}
		} else if (len > 1) {
			text = true;
		}
		if (keep) {
			memmove(dst, line, len);
			dst += len;
		}
		line += len;
	}
	*dst = 0;
	w = dst - out;
	while (w && (out[w - 1] == '\n' || out[w - 1] == ' ')) out[--w] = 0;
	tidy_utf8(out);
	if (!w || !text) {
		free(out);
		return NULL;
	}
	return out;
}

static int parse_requirements(const char *html, ReqLine *out, int max)
{
	if (!html) return 0;
	size_t n = strlen(html) + 1;
	char *buf = malloc(n);
	if (!buf) return 0;
	html_to_lines(html, buf, n);
	int count = 0;
	char *save = NULL;
	for (char *line = strtok_r(buf, "\n", &save); line && count < max; line = strtok_r(NULL, "\n", &save)) {
		while (*line == ' ') line++;
		char *colon = strchr(line, ':');
		if (!colon || colon == line || colon - line > 22) continue;
		*colon = 0;
		char *val = colon + 1;
		while (*val == ' ') val++;
		if (!*val) continue;   // "Mínimo:" / "Recomendado:"
		// Las notas son largas y van aparte
		if (!strncasecmp(line, "Notas", 5) || !strncasecmp(line, "Additional", 10) || !strncmp(line, "追加", 6)) continue;
		copy_utf8(out[count].key, sizeof out[count].key, line);
		copy_utf8(out[count].val, sizeof out[count].val, val);
		replace_all(out[count].val, sizeof out[count].val, " or ", " / ");
		replace_all(out[count].val, sizeof out[count].val, " de espacio disponible", "");
		replace_all(out[count].val, sizeof out[count].val, " available space", "");
		count++;
	}
	free(buf);
	return count;
}

static const char *pick_rating(const cJSON *ratings, const char *country, const cJSON **out)
{
	static const char *esrb_c[] = {"US", "CA", "MX", "AR", "BR", "CL", "CO", "PE", NULL};
	const char *pref = "pegi";
	for (int i = 0; esrb_c[i]; i++) if (!strcmp(country, esrb_c[i])) pref = "esrb";
	if (!strcmp(country, "DE")) pref = "usk";
	const char *order[] = {pref, "pegi", "esrb", "usk", NULL};
	for (int i = 0; order[i]; i++) {
		const cJSON *r = cJSON_GetObjectItemCaseSensitive(ratings, order[i]);
		if (r && jstr(r, "rating")) { *out = r; return order[i]; }
	}
	*out = NULL;
	return NULL;
}

static void add_desc(AppInfo *a, const char *key)
{
	for (int i = 0; i < a->ndesc; i++) if (!strcmp(a->descriptors[i], key)) return;
	if (a->ndesc < 4) copy(a->descriptors[a->ndesc++], sizeof a->descriptors[0], key);
}

static void parse_descriptors(AppInfo *a, const char *d)
{
	if (!d) return;
	static const struct { const char *en; const char *key; } map[] = {
		{"Violence", "Violencia"}, {"Blood", "Sangre"}, {"Sex", "Sexo"}, {"Nudity", "Desnudos"},
		{"Language", "Lenguaje soez"}, {"Drug", "Drogas"}, {"Alcohol", "Drogas"}, {"Fear", "Miedo"},
		{"Horror", "Miedo"}, {"Gambling", "Juegos de azar"}, {"Discrimination", "Discriminación"},
		{"purchase", "Compras en el juego"}, {"Humor", "Humor adulto"},
	};
	for (size_t i = 0; i < sizeof map / sizeof *map; i++)
		if (strcasestr(d, map[i].en)) add_desc(a, map[i].key);
}

bool steam_parse_appdetails(const char *json, u32 appid, AppInfo *a)
{
	memset(a, 0, sizeof *a);
	a->appid = appid;
	cJSON *root = cJSON_Parse(json);
	if (!root) return false;
	char key[16];
	snprintf(key, sizeof key, "%lu", (unsigned long)appid);
	const cJSON *e = cJSON_GetObjectItemCaseSensitive(root, key);
	const cJSON *d = e ? cJSON_GetObjectItemCaseSensitive(e, "data") : NULL;
	if (!d || !jbool(e, "success")) {
		cJSON_Delete(root);
		return false;
	}
	copy_utf8(a->name, sizeof a->name, jstr(d, "name"));
	const char *sd = jstr(d, "short_description");
	if (sd) {
		size_t n = strlen(sd) + 1;
		a->desc = malloc(n);
		if (a->desc) {
			html_to_lines(sd, a->desc, n);
			tidy_utf8(a->desc);
		}
	}
	a->about = html_to_about(jstr(d, "about_the_game"));
	if (!a->about) a->about = html_to_about(jstr(d, "detailed_description"));
	// Sin descripción corta (pasa en algunas demos): el principio del «acerca de», sin títulos
	if ((!a->desc || !a->desc[0]) && a->about) {
		free(a->desc);
		a->desc = strdup(a->about);
		if (a->desc) {
			for (char *q = a->desc; *q; q++) if (*q == '\n' || *q == '\x01') *q = ' ';
			if (strlen(a->desc) > 400) a->desc[400] = 0;
		}
	}
	if (a->desc) {
		// Sin espacios al principio ni al final
		char *q = a->desc;
		while (*q == ' ' || *q == '\n') q++;
		memmove(a->desc, q, strlen(q) + 1);
		size_t dl = strlen(a->desc);
		while (dl && (a->desc[dl - 1] == ' ' || a->desc[dl - 1] == '\n')) a->desc[--dl] = 0;
		if (!dl) {
			free(a->desc);
			a->desc = NULL;
		}
	}
	const cJSON *dm = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(d, "demos"), 0);
	if (dm) a->demo = (u32)jnum(dm, "appid", 0);
	copy(a->header, sizeof a->header, jstr(d, "header_image"));
	a->is_free = jbool(d, "is_free");

	const cJSON *g;
	cJSON_ArrayForEach(g, cJSON_GetObjectItemCaseSensitive(d, "genres")) {
		if (a->ngenres < 3) copy_utf8(a->genres[a->ngenres++], sizeof a->genres[0], jstr(g, "description"));
	}
	const cJSON *rd = cJSON_GetObjectItemCaseSensitive(d, "release_date");
	if (rd) {
		copy_utf8(a->release, sizeof a->release, jstr(rd, "date"));
		a->coming_soon = jbool(rd, "coming_soon");
	}
	const cJSON *dev = cJSON_GetObjectItemCaseSensitive(d, "developers");
	if (cJSON_GetArraySize(dev) > 0) copy_utf8(a->developer, sizeof a->developer, cJSON_GetArrayItem(dev, 0)->valuestring);

	const cJSON *po = cJSON_GetObjectItemCaseSensitive(d, "price_overview");
	if (po) {
		a->has_price = true;
		a->disc = (int)jnum(po, "discount_percent", 0);
		copy(a->price, sizeof a->price, jstr(po, "final_formatted"));
		if (a->disc > 0) copy(a->orig, sizeof a->orig, jstr(po, "initial_formatted"));
	}

	const cJSON *ratings = cJSON_GetObjectItemCaseSensitive(d, "ratings"), *r = NULL;
	const char *sys = ratings ? pick_rating(ratings, g_country, &r) : NULL;
	if (sys && r) {
		for (int i = 0; sys[i] && i < 7; i++) a->rating_sys[i] = toupper((unsigned char)sys[i]);
		const char *rv = jstr(r, "rating");
		for (int i = 0; rv[i] && i < 7; i++) a->rating[i] = toupper((unsigned char)rv[i]);
		a->rating_age = (int)jnum(r, "required_age", atoi(rv));
		parse_descriptors(a, jstr(r, "descriptors"));
		parse_descriptors(a, jstr(r, "interactive_elements"));
	}

	const cJSON *mc = cJSON_GetObjectItemCaseSensitive(d, "metacritic");
	if (mc) a->metacritic = (int)jnum(mc, "score", 0);
	const cJSON *ach = cJSON_GetObjectItemCaseSensitive(d, "achievements");
	if (ach) a->achievements = (int)jnum(ach, "total", 0);
	const cJSON *sc;
	cJSON_ArrayForEach(sc, cJSON_GetObjectItemCaseSensitive(d, "screenshots")) {
		a->nscreens++;
		const char *u = jstr(sc, "path_thumbnail");
		if (u && a->nshots < 24) a->shots[a->nshots++] = strdup(u);
	}

	const cJSON *c;
	cJSON_ArrayForEach(c, cJSON_GetObjectItemCaseSensitive(d, "categories")) {
		switch ((int)jnum(c, "id", 0)) {
		case 2:  a->feats |= FEAT_SINGLE; break;
		case 1:  a->feats |= FEAT_MULTI; break;
		case 9: case 38: case 39: case 24: a->feats |= FEAT_COOP; break;
		case 49: case 36: case 37: case 47: a->feats |= FEAT_PVP; break;
		case 22: a->feats |= FEAT_ACHIEVE; break;
		case 29: a->feats |= FEAT_CARDS; break;
		case 23: a->feats |= FEAT_CLOUD; break;
		case 62: a->feats |= FEAT_FAMILY; break;
		case 28: a->feats |= FEAT_PAD_FULL; break;
		case 18: a->feats |= FEAT_PAD_PART; break;
		case 30: a->feats |= FEAT_WORKSHOP; break;
		case 35: a->feats |= FEAT_INAPP; break;
		}
	}
	if (a->feats & FEAT_PAD_FULL) a->feats &= ~FEAT_PAD_PART;

	const cJSON *pc = cJSON_GetObjectItemCaseSensitive(d, "pc_requirements");
	if (cJSON_IsObject(pc)) {
		a->nmin = parse_requirements(jstr(pc, "minimum"), a->rmin, 7);
		a->nrec = parse_requirements(jstr(pc, "recommended"), a->rrec, 7);
	}
	const cJSON *macr = cJSON_GetObjectItemCaseSensitive(d, "mac_requirements");
	if (cJSON_IsObject(macr)) {
		a->nmmin = parse_requirements(jstr(macr, "minimum"), a->mmin, 7);
		a->nmrec = parse_requirements(jstr(macr, "recommended"), a->mrec, 7);
	}
	const cJSON *pl = cJSON_GetObjectItemCaseSensitive(d, "platforms");
	if (pl) {
		a->mac = jbool(pl, "mac");
		a->lnx = jbool(pl, "linux");
	}

	const char *type = jstr(d, "type");
	a->is_dlc = type && !strcmp(type, "dlc");
	a->is_demo = type && !strcmp(type, "demo");
	const cJSON *fg = cJSON_GetObjectItemCaseSensitive(d, "fullgame");
	if (fg) {
		a->fullgame = (u32)jnum(fg, "appid", 0);
		copy_utf8(a->fullgame_name, sizeof a->fullgame_name, jstr(fg, "name"));
	}

	cJSON_ArrayForEach(c, cJSON_GetObjectItemCaseSensitive(d, "dlc")) {
		if (a->ndlc < 64 && cJSON_IsNumber(c)) a->dlc[a->ndlc++] = (u32)c->valuedouble;
	}
	a->ok = true;
	cJSON_Delete(root);
	return true;
}

bool steam_parse_review_summary(const char *json, AppInfo *a)
{
	RowList l = {0};
	if (!steam_parse_rows(json, &l, false) || l.count == 0) {
		rowlist_free(&l);
		return false;
	}
	copy(a->review_label, sizeof a->review_label, l.rows[0].review);
	a->review_pct = l.rows[0].review_pct;
	// review_count no está en StoreRow: se lee aparte
	cJSON *root = cJSON_Parse(json);
	const cJSON *it = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "response"), "store_items"), 0);
	const cJSON *rv = it ? cJSON_GetObjectItemCaseSensitive(it, "reviews") : NULL;
	const cJSON *sf = rv ? cJSON_GetObjectItemCaseSensitive(rv, "summary_filtered") : NULL;
	if (sf) a->review_count = (int)jnum(sf, "review_count", 0);
	cJSON_Delete(root);
	rowlist_free(&l);
	return true;
}

// ----------------------------------------------------------------------------
// Reseñas

static int rv_type;
static bool rv_recent, rv_all, rv_other;
static int rv_raw;

void steam_reviews_other(bool on) { rv_other = on; }
int steam_reviews_last_raw(void) { return rv_raw; }

void steam_reviews_filter(int type, bool recent, bool all_langs)
{
	rv_type = type;
	rv_recent = recent;
	rv_all = all_langs;
}

Req *steam_req_reviews(u32 appid, const char *cursor)
{
	static const char *types[] = {"all", "positive", "negative"};
	char *cur = url_escape(cursor && *cursor ? cursor : "*");
	char url[320];
	snprintf(url, sizeof url,
		"https://store.steampowered.com/appreviews/%lu?json=1&language=%s&filter=%s&review_type=%s&purchase_type=all&num_per_page=8&cursor=%s",
		(unsigned long)appid, rv_all || rv_other ? "all" : g_lang, rv_recent ? "recent" : "all", types[rv_type % 3], cur);
	free(cur);
	return net_get(url, LANE_API);
}

// Limpia BBCode ([b], [h1], [url=…]) y saltos de línea; colapsa espacios
static char *clean_review(const char *s)
{
	size_t n = strlen(s);
	char *o = malloc(n + 1), *p = o;
	if (!o) return NULL;
	bool space = true;
	while (*s) {
		if (*s == '[') {
			const char *e = strchr(s, ']');
			if (e && e - s < 40) { s = e + 1; continue; }
		}
		unsigned char ch = *s;
		if (ch == '\n' || ch == '\r' || ch == '\t' || ch == ' ') {
			if (!space) *p++ = ' ';
			space = true;
			s++;
			continue;
		}
		// Fuera emojis y símbolos que la fuente de la consola no tiene
		if (ch >= 0xF0) {
			s++;
			while (((unsigned char)*s & 0xC0) == 0x80) s++;
			continue;
		}
		if (ch == 0xE2 || ch == 0xEF) {
			unsigned cp = ((ch & 0x0F) << 12) | (((unsigned char)s[1] & 0x3F) << 6) | ((unsigned char)s[2] & 0x3F);
			// ☀…➿ (símbolos y dingbats), ⬀…⯿, selectores de variante y unión de emojis
			if ((cp >= 0x2600 && cp <= 0x27BF) || (cp >= 0x2B00 && cp <= 0x2BFF) || (cp >= 0xFE00 && cp <= 0xFE0F) ||
			    cp == 0x200D || (cp >= 0x2300 && cp <= 0x23FF)) {
				s += 3;
				continue;
			}
		}
		*p++ = *s++;
		space = false;
	}
	while (p > o && p[-1] == ' ') p--;
	*p = 0;
	return o;
}

int steam_parse_reviews(const char *json, Review *out, int max, char *cursor, size_t cursor_sz)
{
	cJSON *root = cJSON_Parse(json);
	if (!root) return -1;
	int n = 0;
	const cJSON *r;
	rv_raw = 0;
	cJSON_ArrayForEach(r, cJSON_GetObjectItemCaseSensitive(root, "reviews")) {
		rv_raw++;
		if (n >= max) break;
		const char *t = jstr(r, "review");
		if (!t) continue;
		// Las de otros idiomas: las del tuyo ya salieron antes
		const char *lang = jstr(r, "language");
		bool other = lang && strcmp(lang, g_lang);
		if (rv_other && !rv_all && !other) continue;
		char *c = clean_review(t);
		if (!c || !*c) { free(c); continue; }
		// En pantalla caben tres líneas: no hace falta guardar reseñas de miles de caracteres
		if (strlen(c) > 360) {
			size_t cut = 360;
			while (cut > 0 && ((unsigned char)c[cut] & 0xC0) == 0x80) cut--;
			c[cut] = 0;
		}
		out[n].text = c;
		out[n].voted_up = jbool(r, "voted_up");
		out[n].id = jid(r, "recommendationid");
		out[n].other_lang = !rv_all && other;
		const cJSON *au = cJSON_GetObjectItemCaseSensitive(r, "author");
		out[n].hours = au ? (int)(jnum(au, "playtime_forever", 0) / 60) : 0;
		n++;
	}
	if (cursor) copy(cursor, cursor_sz, jstr(root, "cursor"));
	cJSON_Delete(root);
	return n;
}

// ----------------------------------------------------------------------------
// Con sesión

Req *steam_req_owned(u64 steamid, const char *token)
{
	char url[1400];
	snprintf(url, sizeof url,
		"https://api.steampowered.com/IPlayerService/GetOwnedGames/v1/?access_token=%s&steamid=%llu"
		"&include_appinfo=1&include_played_free_games=1&skip_unvetted_apps=0&language=%s",
		token, (unsigned long long)steamid, g_lang);
	return net_get(url, LANE_API);
}

Req *steam_req_family_group(u64 steamid, const char *token)
{
	char url[1400];
	snprintf(url, sizeof url, "https://api.steampowered.com/IFamilyGroupsService/GetFamilyGroupForUser/v1/?access_token=%s&steamid=%llu",
		token, (unsigned long long)steamid);
	return net_get(url, LANE_API);
}

int steam_parse_family_group(const char *json, u64 *gid)
{
	*gid = 0;
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return -1;
	}
	const char *g = jstr(resp, "family_groupid");
	if (g) *gid = strtoull(g, NULL, 10);
	else *gid = (u64)jnum(resp, "family_groupid", 0);
	int r = (*gid && !jbool(resp, "is_not_member_of_any_group")) ? 1 : 0;
	cJSON_Delete(root);
	return r;
}

Req *steam_req_family_apps(u64 gid, u64 steamid, const char *token)
{
	char url[1500];
	snprintf(url, sizeof url, "https://api.steampowered.com/IFamilyGroupsService/GetSharedLibraryApps/v1/?access_token=%s"
		"&family_groupid=%llu&steamid=%llu&include_own=false&include_excluded=false&include_free=false&include_non_games=false&language=%s",
		token, (unsigned long long)gid, (unsigned long long)steamid, g_lang);
	return net_get(url, LANE_API);
}

int steam_parse_family_apps(const char *json, u64 me, RowList *out)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return -1;
	}
	char mine[24];
	snprintf(mine, sizeof mine, "%llu", (unsigned long long)me);
	int n = 0;
	const cJSON *a;
	cJSON_ArrayForEach(a, cJSON_GetObjectItemCaseSensitive(resp, "apps")) {
		u32 appid = (u32)jnum(a, "appid", 0);
		const char *name = jstr(a, "name");
		if (!appid || !name || !*name || jnum(a, "exclude_reason", 0) != 0) continue;
		// Lo que ya es tuyo va por la biblioteca normal
		bool own = false;
		const cJSON *o;
		cJSON_ArrayForEach(o, cJSON_GetObjectItemCaseSensitive(a, "owner_steamids")) {
			if ((cJSON_IsString(o) && !strcmp(o->valuestring, mine)) || (cJSON_IsNumber(o) && (u64)o->valuedouble == me)) own = true;
		}
		if (own) continue;
		if (out->count == out->cap) {
			int cap = out->cap ? out->cap * 2 : 16;
			StoreRow *r = realloc(out->rows, cap * sizeof(StoreRow));
			if (!r) break;
			out->rows = r;
			out->cap = cap;
		}
		StoreRow *row = &out->rows[out->count++];
		memset(row, 0, sizeof *row);
		row->cents = -1;
		row->appid = appid;
		row->family = true;
		copy_utf8(row->name, sizeof row->name, name);
		copy(row->icon, sizeof row->icon, jstr(a, "img_icon_hash"));
		n++;
	}
	cJSON_Delete(root);
	return n;
}

static int cmp_name(const void *a, const void *b)
{
	const StoreRow *x = a, *y = b;
	return strcasecmp(x->name, y->name);
}

void steam_sort_by_name(RowList *l)
{
	qsort(l->rows, l->count, sizeof(StoreRow), cmp_name);
}

bool steam_parse_owned(const char *json, RowList *l)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		snprintf(l->err, sizeof l->err, "respuesta no válida");
		cJSON_Delete(root);
		return false;
	}
	l->count = 0;
	const cJSON *g;
	cJSON_ArrayForEach(g, cJSON_GetObjectItemCaseSensitive(resp, "games")) {
		if (l->count == l->cap) {
			int cap = l->cap ? l->cap * 2 : 64;
			StoreRow *r = realloc(l->rows, cap * sizeof(StoreRow));
			if (!r) break;
			l->rows = r;
			l->cap = cap;
		}
		StoreRow *row = &l->rows[l->count];
		memset(row, 0, sizeof *row);
		row->appid = (u32)jnum(g, "appid", 0);
		copy_utf8(row->name, sizeof row->name, jstr(g, "name"));
		row->minutes = (int)jnum(g, "playtime_forever", 0);
		row->last_played = (u32)jnum(g, "rtime_last_played", 0);
		copy(row->icon, sizeof row->icon, jstr(g, "img_icon_url"));
		if (row->appid && row->name[0]) l->count++;
	}
	steam_sort_by_name(l);
	l->total = l->count;
	l->end = true;
	l->err[0] = 0;
	cJSON_Delete(root);
	return true;
}

Req *steam_req_wishlist(u64 steamid, const char *token)
{
	char url[1400];
	snprintf(url, sizeof url, "https://api.steampowered.com/IWishlistService/GetWishlist/v1/?access_token=%s&steamid=%llu",
		token, (unsigned long long)steamid);
	return net_get(url, LANE_API);
}

typedef struct { u32 appid; int prio; } WishItem;

static int cmp_prio(const void *a, const void *b)
{
	return ((const WishItem *)a)->prio - ((const WishItem *)b)->prio;
}

int steam_parse_wishlist(const char *json, u32 **ids)
{
	*ids = NULL;
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return -1;
	}
	const cJSON *items = cJSON_GetObjectItemCaseSensitive(resp, "items");
	int n = cJSON_GetArraySize(items), k = 0;
	WishItem *w = n ? malloc(n * sizeof(WishItem)) : NULL;
	const cJSON *it;
	cJSON_ArrayForEach(it, items) {
		if (!w) break;
		w[k].appid = (u32)jnum(it, "appid", 0);
		// La prioridad 0 es «sin ordenar»: al final, como en Steam
		int p = (int)jnum(it, "priority", 0);
		w[k].prio = p > 0 ? p : 1 << 20;
		if (w[k].appid) k++;
	}
	if (k) qsort(w, k, sizeof(WishItem), cmp_prio);
	if (k) {
		*ids = malloc(k * sizeof(u32));
		for (int i = 0; *ids && i < k; i++) (*ids)[i] = w[i].appid;
	}
	free(w);
	cJSON_Delete(root);
	return k;
}

void appinfo_free(AppInfo *a)
{
	free(a->desc);
	a->desc = NULL;
	free(a->about);
	a->about = NULL;
	for (int i = 0; i < a->nshots; i++) free(a->shots[i]);
	a->nshots = 0;
}

void rowlist_free(RowList *l)
{
	free(l->rows);
	if (l->req) net_release(l->req);
	memset(l, 0, sizeof *l);
}

// ----------------------------------------------------------------------------
// Compras

Req *steam_req_buyopts(u32 appid)
{
	cJSON *in = cJSON_CreateObject();
	cJSON *arr = cJSON_AddArrayToObject(in, "ids");
	cJSON *o = cJSON_CreateObject();
	cJSON_AddNumberToObject(o, "appid", appid);
	cJSON_AddItemToArray(arr, o);
	cJSON_AddItemToObject(in, "context", context());
	cJSON *dr = cJSON_AddObjectToObject(in, "data_request");
	cJSON_AddBoolToObject(dr, "include_all_purchase_options", true);
	return service_call("IStoreBrowseService/GetItems", in);
}

static void opt_prices(const cJSON *p, char *price, char *orig, int *disc)
{
	copy(price, 20, jstr(p, "formatted_final_price"));
	copy(orig, 20, jstr(p, "formatted_original_price"));
	*disc = (int)jnum(p, "discount_pct", 0);
	if (jbool(p, "hide_discount_pct_for_compliance")) *disc = 0;
	if (*disc <= 0) orig[0] = 0;
}

int steam_parse_buyopts(const char *json, u32 appid, BuyOpt *out, int max)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *items = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "response"), "store_items");
	if (!cJSON_IsArray(items)) {
		cJSON_Delete(root);
		return -1;
	}
	int n = 0;
	const cJSON *it;
	cJSON_ArrayForEach(it, items) {
		if ((u32)jnum(it, "appid", jnum(it, "id", 0)) != appid) continue;
		const cJSON *opts = cJSON_GetObjectItemCaseSensitive(it, "purchase_options");
		const cJSON *p;
		cJSON_ArrayForEach(p, opts) {
			if (n >= max) break;
			BuyOpt *b = &out[n];
			memset(b, 0, sizeof *b);
			b->packageid = (u32)jnum(p, "packageid", 0);
			b->bundleid = (u32)jnum(p, "bundleid", 0);
			if (!b->packageid && !b->bundleid) continue;
			copy_utf8(b->name, sizeof b->name, jstr(p, "purchase_option_name"));
			opt_prices(p, b->price, b->orig, &b->disc);
			b->free = jbool(p, "is_free_license");
			// La oferta acaba cuando acaba el primero de sus descuentos
			const cJSON *ad;
			cJSON_ArrayForEach(ad, cJSON_GetObjectItemCaseSensitive(p, "active_discounts")) {
				u32 end = (u32)jnum(ad, "discount_end_date", 0);
				if (end && (!b->disc_end || end < b->disc_end)) b->disc_end = end;
			}
			n++;
		}
	}
	cJSON_Delete(root);
	return n;
}

// Lo mismo por GET: hay métodos que Steam solo acepta así (GetCart responde 405 a un POST)
Req *steam_get_service(const char *iface_method, cJSON *input, const char *token)
{
	char *js = cJSON_PrintUnformatted(input);
	char *esc = url_escape(js);
	size_t n = strlen(esc) + strlen(token) + 160;
	char *url = malloc(n);
	snprintf(url, n, "https://api.steampowered.com/%s/v1/?access_token=%s&input_json=%s", iface_method, token, esc);
	Req *r = net_get(url, LANE_API);
	memset(url, 0, n);
	free(url);
	free(esc);
	free(js);
	cJSON_Delete(input);
	return r;
}

Req *steam_req_notifications(const char *token)
{
	char url[1400];
	snprintf(url, sizeof url, "https://api.steampowered.com/ISteamNotificationService/GetSteamNotifications/v1/?access_token=%s"
		"&include_hidden=false&include_read=true&count_only=false&language=%s", token, g_lang);
	return net_get(url, LANE_API);
}

// body_data es un JSON dentro del JSON (una cadena): de ahí sale el juego y, a veces, un título
static int cmp_notif(const void *a, const void *b)
{
	const SteamNotif *x = a, *y = b;
	if (x->time != y->time) return x->time < y->time ? 1 : -1;
	return x->id < y->id ? 1 : x->id > y->id ? -1 : 0;
}

int steam_parse_notifications(const char *json, SteamNotif *out, int max, int *unread)
{
	*unread = 0;
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return -1;
	}
	int n = 0;
	const cJSON *it;
	cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(resp, "notifications")) {
		if (n >= max) break;
		if (jbool(it, "hidden")) continue;
		SteamNotif *o = &out[n];
		memset(o, 0, sizeof *o);
		const char *id = jstr(it, "notification_id");
		o->id = id ? strtoull(id, NULL, 10) : (u64)jnum(it, "notification_id", 0);
		o->type = (int)jnum(it, "notification_type", 0);
		o->time = (u32)jnum(it, "timestamp", 0);
		o->read = jbool(it, "read");
		const char *bd = jstr(it, "body_data");
		cJSON *b = bd ? cJSON_Parse(bd) : NULL;
		if (b) {
			const char *as = jstr(b, "appid");
			o->appid = as ? (u32)strtoul(as, NULL, 10) : (u32)jnum(b, "appid", 0);
			copy_utf8(o->title, sizeof o->title, jstr(b, "title"));
			cJSON_Delete(b);
		}
		if (!o->read) (*unread)++;
		n++;
	}
	cJSON_Delete(root);
	// Steam las manda de la más antigua a la más nueva: la más reciente, arriba
	qsort(out, n, sizeof *out, cmp_notif);
	return n;
}

Req *steam_req_notifications_read(const char *token)
{
	cJSON *in = cJSON_CreateObject();
	cJSON_AddBoolToObject(in, "mark_all_read", true);
	return steam_post_service("ISteamNotificationService/MarkNotificationsRead", in, token);
}

Req *steam_req_friends(u64 steamid, const char *token, bool fallback)
{
	char url[1400];
	if (fallback)
		snprintf(url, sizeof url, "https://api.steampowered.com/ISteamUser/GetFriendList/v1/?access_token=%s&steamid=%llu&relationship=all",
			token, (unsigned long long)steamid);
	else
		snprintf(url, sizeof url, "https://api.steampowered.com/IFriendsListService/GetFriendsList/v1/?access_token=%s", token);
	return net_get(url, LANE_API);
}

// Las dos respuestas: {"response":{"friendslist":{"friends":[{"ulfriendid","efriendrelationship"}]}}}
// o {"friendslist":{"friends":[{"steamid","relationship":"friend"}]}}. 3/friend es amigo; 2 o
// requestrecipient, una solicitud que te han mandado
int steam_parse_friends(const char *json, u64 *ids, int max, int *pending)
{
	*pending = 0;
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	const cJSON *fl = cJSON_GetObjectItemCaseSensitive(resp ? resp : root, "friendslist");
	const cJSON *list = fl ? cJSON_GetObjectItemCaseSensitive(fl, "friends") : NULL;
	if (!list) {
		cJSON_Delete(root);
		return -1;
	}
	int n = 0;
	const cJSON *f;
	cJSON_ArrayForEach(f, list) {
		const char *sid = jstr(f, "ulfriendid");
		if (!sid) sid = jstr(f, "steamid");
		u64 id = sid ? strtoull(sid, NULL, 10) : 0;
		const char *rel_s = jstr(f, "relationship");
		int rel = rel_s ? (!strcmp(rel_s, "friend") ? 3 : !strcmp(rel_s, "requestrecipient") ? 2 : 0) : (int)jnum(f, "efriendrelationship", 0);
		if (rel == 2) (*pending)++;
		if (rel != 3 || !id || n >= max) continue;
		ids[n++] = id;
	}
	cJSON_Delete(root);
	return n;
}

Req *steam_req_people(const u64 *ids, int n, const char *token)
{
	cJSON *in = cJSON_CreateObject();
	cJSON *arr = cJSON_AddArrayToObject(in, "steamids");
	for (int i = 0; i < n; i++) {
		char s[24];
		snprintf(s, sizeof s, "%llu", (unsigned long long)ids[i]);
		cJSON_AddItemToArray(arr, cJSON_CreateString(s));
	}
	char *js = cJSON_PrintUnformatted(in);
	char *esc = url_escape(js);
	size_t len = strlen(esc) + strlen(token) + 160;
	char *url = malloc(len);
	snprintf(url, len, "https://api.steampowered.com/IPlayerService/GetPlayerLinkDetails/v1/?access_token=%s&input_json=%s", token, esc);
	Req *r = net_get(url, LANE_API);
	free(url);
	free(esc);
	free(js);
	cJSON_Delete(in);
	return r;
}

int steam_parse_people(const char *json, SteamPerson *out, int max)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return -1;
	}
	int n = 0;
	const cJSON *a;
	cJSON_ArrayForEach(a, cJSON_GetObjectItemCaseSensitive(resp, "accounts")) {
		if (n >= max) break;
		const cJSON *pd = cJSON_GetObjectItemCaseSensitive(a, "public_data");
		const cJSON *pv = cJSON_GetObjectItemCaseSensitive(a, "private_data");
		const char *sid = jstr(pd, "steamid");
		if (!sid) continue;
		SteamPerson *p = &out[n++];
		memset(p, 0, sizeof *p);
		p->id = strtoull(sid, NULL, 10);
		copy_utf8(p->name, sizeof p->name, jstr(pd, "persona_name"));
		copy(p->avatar_sha, sizeof p->avatar_sha, jstr(pd, "sha_digest_avatar"));
		if (pv) {
			p->state = (int)jnum(pv, "persona_state", 0);
			const char *g = jstr(pv, "game_id");
			p->game = g ? (u32)strtoull(g, NULL, 10) : (u32)jnum(pv, "game_id", 0);
			copy_utf8(p->game_name, sizeof p->game_name, jstr(pv, "game_extra_info"));
			p->last_off = (u32)jnum(pv, "last_logoff_time", 0);
		}
	}
	cJSON_Delete(root);
	return n;
}

Req *steam_req_achievements(u32 appid)
{
	char url[200];
	snprintf(url, sizeof url, "https://api.steampowered.com/IPlayerService/GetGameAchievements/v1/?appid=%lu&language=%s",
		(unsigned long)appid, g_lang);
	return net_get(url, LANE_API);
}

int steam_parse_achievements(const char *json, Achievement **out)
{
	*out = NULL;
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return -1;
	}
	const cJSON *list = cJSON_GetObjectItemCaseSensitive(resp, "achievements");
	int cap = cJSON_GetArraySize(list), n = 0;
	*out = calloc(cap ? cap : 1, sizeof **out);
	const cJSON *it;
	cJSON_ArrayForEach(it, list) {
		if (!*out || n >= cap) break;
		Achievement *a = &(*out)[n];
		copy_utf8(a->name, sizeof a->name, jstr(it, "localized_name"));
		if (!a->name[0]) continue;
		copy_utf8(a->desc, sizeof a->desc, jstr(it, "localized_desc"));
		copy(a->icon, sizeof a->icon, jstr(it, "icon"));
		copy(a->icon_gray, sizeof a->icon_gray, jstr(it, "icon_gray"));
		const char *p = jstr(it, "player_percent_unlocked");
		a->pct = p ? strtof(p, NULL) : (float)jnum(it, "player_percent_unlocked", 0);
		a->hidden = jbool(it, "hidden");
		n++;
	}
	cJSON_Delete(root);
	return n;
}

static cJSON *achv_input(u32 appid, u64 steamid)
{
	cJSON *in = cJSON_CreateObject();
	char sid[24];
	snprintf(sid, sizeof sid, "%llu", (unsigned long long)steamid);
	cJSON_AddStringToObject(in, "steamid", sid);
	cJSON_AddStringToObject(in, "language", g_lang);
	cJSON *ids = cJSON_AddArrayToObject(in, "appids");
	cJSON_AddItemToArray(ids, cJSON_CreateNumber(appid));
	return in;
}

// Los que tienes (el mismo servicio con el que la biblioteca de Steam enseña tus logros)
Req *steam_req_my_achievements(u32 appid, u64 steamid, const char *token)
{
	cJSON *in = achv_input(appid, steamid);
	cJSON_AddNumberToObject(in, "max_achievements", 10000);
	return steam_post_service("IPlayerService/GetTopAchievementsForGames", in, token);
}

// Se casan por el icono, que es único en cada juego
int steam_mark_unlocked(const char *json, Achievement *a, int n)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	const cJSON *games = resp ? cJSON_GetObjectItemCaseSensitive(resp, "games") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return -1;
	}
	int marked = 0;
	const cJSON *g, *it;
	cJSON_ArrayForEach(g, games) {
		cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(g, "achievements")) {
			const char *icon = jstr(it, "icon"), *name = jstr(it, "name");
			for (int i = 0; i < n; i++) {
				if (a[i].done) continue;
				if ((icon && a[i].icon[0] && !strcmp(icon, a[i].icon)) || (!icon && name && !strcmp(name, a[i].name))) {
					a[i].done = true;
					marked++;
					break;
				}
			}
		}
	}
	cJSON_Delete(root);
	return marked;
}

Req *steam_req_achievement_progress(u32 appid, u64 steamid, const char *token)
{
	return steam_post_service("IPlayerService/GetAchievementsProgress", achv_input(appid, steamid), token);
}

bool steam_parse_achievement_progress(const char *json, int *unlocked, int *total)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	const cJSON *p = resp ? cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(resp, "achievement_progress"), 0) : NULL;
	bool ok = p != NULL;
	if (ok) {
		*unlocked = (int)jnum(p, "unlocked", 0);
		*total = (int)jnum(p, "total", 0);
	}
	cJSON_Delete(root);
	return ok;
}

Req *steam_post_service(const char *iface_method, cJSON *input, const char *token)
{
	char *js = cJSON_PrintUnformatted(input);
	char *esc = url_escape(js);
	size_t n = strlen(esc) + 16;
	char *body = malloc(n);
	snprintf(body, n, "input_json=%s", esc);
	size_t un = strlen(token) + 128;
	char *url = malloc(un);
	snprintf(url, un, "https://api.steampowered.com/%s/v1/?access_token=%s", iface_method, token);
	Req *r = net_post(url, body);
	memset(url, 0, un);
	free(url);
	free(body);
	free(esc);
	free(js);
	cJSON_Delete(input);
	return r;
}

Req *steam_req_hidden(const char *token)
{
	cJSON *in = cJSON_CreateObject();
	cJSON *vs = cJSON_AddArrayToObject(in, "versions");
	cJSON *v = cJSON_CreateObject();
	cJSON_AddNumberToObject(v, "enamespace", 1);
	cJSON_AddNumberToObject(v, "version", 0);
	cJSON_AddItemToArray(vs, v);
	return steam_post_service("ICloudConfigStoreService/Download", in, token);   // suelta in
}

// {"response":{"data":[{"enamespace":1,"entries":[{"key":"user-collections.hidden",
//  "value":"{\"id\":\"hidden\",\"added\":[730,…],\"removed\":[…]}"}]}]}}
int steam_parse_hidden(const char *json, u32 **ids)
{
	*ids = NULL;
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return -1;
	}
	int n = 0, cap = 0;
	const cJSON *d, *e;
	cJSON_ArrayForEach(d, cJSON_GetObjectItemCaseSensitive(resp, "data")) {
		cJSON_ArrayForEach(e, cJSON_GetObjectItemCaseSensitive(d, "entries")) {
			const char *k = jstr(e, "key");
			const char *v = jstr(e, "value");
			if (!k || strcmp(k, "user-collections.hidden") || !v) continue;
			cJSON *c = cJSON_Parse(v);
			const cJSON *it;
			cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(c, "added")) {
				if (!cJSON_IsNumber(it)) continue;
				if (n == cap) {
					cap = cap ? cap * 2 : 32;
					u32 *nn = realloc(*ids, cap * sizeof(u32));
					if (!nn) break;
					*ids = nn;
				}
				(*ids)[n++] = (u32)it->valuedouble;
			}
			cJSON_Delete(c);
		}
	}
	cJSON_Delete(root);
	return n;
}

static void amount(const cJSON *a, char *out, size_t n)
{
	copy(out, n, jstr(a, "formatted_amount"));
}

bool steam_parse_cart(const char *json, CartInfo *c)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = cJSON_GetObjectItemCaseSensitive(root, "response");
	const cJSON *cart = cJSON_GetObjectItemCaseSensitive(resp, "cart");
	if (!resp) {
		cJSON_Delete(root);
		return false;
	}
	CartInfo old = *c;
	memset(c, 0, sizeof *c);
	// Un carrito vacío llega como «cart: {}» (o sin «cart»)
	const cJSON *lines = cJSON_GetObjectItemCaseSensitive(cart, "line_items");
	const cJSON *li;
	cJSON_ArrayForEach(li, lines) {
		if (c->n >= CART_MAX) break;
		CartLine *l = &c->line[c->n];
		const cJSON *id = cJSON_GetObjectItemCaseSensitive(li, "line_item_id");
		l->line_id = cJSON_IsString(id) ? strtoull(id->valuestring, NULL, 10) : (u64)jnum(li, "line_item_id", 0);
		l->packageid = (u32)jnum(li, "packageid", 0);
		l->bundleid = (u32)jnum(li, "bundleid", 0);
		const cJSON *v = cJSON_GetObjectItemCaseSensitive(li, "is_valid");
		l->valid = !v || cJSON_IsTrue(v);
		amount(cJSON_GetObjectItemCaseSensitive(li, "price_when_added"), l->price, sizeof l->price);
		// ¿Es un regalo? (flags.is_gift y gift_info con la cuenta y el nombre del amigo)
		const cJSON *fl = cJSON_GetObjectItemCaseSensitive(li, "flags");
		const cJSON *gi = cJSON_GetObjectItemCaseSensitive(li, "gift_info");
		if (jbool(fl, "is_gift") && gi) {
			l->giftee = (u32)jnum(gi, "accountid_giftee", 0);
			copy_utf8(l->giftee_name, sizeof l->giftee_name, jstr(cJSON_GetObjectItemCaseSensitive(gi, "gift_message"), "gifteename"));
		}
		// Lo que ya se sabía de esta línea (nombre, juego) se conserva hasta que lleguen los datos nuevos
		for (int i = 0; i < old.n; i++)
			if (old.line[i].line_id == l->line_id) {
				memcpy(l->name, old.line[i].name, sizeof l->name);
				l->appid = old.line[i].appid;
				if (old.line[i].price[0]) {
					memcpy(l->price, old.line[i].price, sizeof l->price);
					memcpy(l->orig, old.line[i].orig, sizeof l->orig);
					l->disc = old.line[i].disc;
				}
			}
		c->n++;
	}
	amount(cJSON_GetObjectItemCaseSensitive(cart, "subtotal"), c->subtotal, sizeof c->subtotal);
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(cart, "is_valid");
	c->valid = !v || cJSON_IsTrue(v);
	cJSON_Delete(root);
	return true;
}

Req *steam_req_cart_names(const CartInfo *c)
{
	cJSON *in = cJSON_CreateObject();
	cJSON *arr = cJSON_AddArrayToObject(in, "ids");
	for (int i = 0; i < c->n; i++) {
		cJSON *o = cJSON_CreateObject();
		if (c->line[i].bundleid) cJSON_AddNumberToObject(o, "bundleid", c->line[i].bundleid);
		else cJSON_AddNumberToObject(o, "packageid", c->line[i].packageid);
		cJSON_AddItemToArray(arr, o);
	}
	cJSON_AddItemToObject(in, "context", context());
	return service_call("IStoreBrowseService/GetItems", in);
}

void steam_parse_cart_names(const char *json, CartInfo *c)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *items = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root, "response"), "store_items");
	const cJSON *it;
	cJSON_ArrayForEach(it, items) {
		int type = (int)jnum(it, "item_type", -1);   // 1 paquete, 2 lote
		u32 id = (u32)jnum(it, "id", 0);
		for (int i = 0; i < c->n; i++) {
			CartLine *l = &c->line[i];
			if (!(type == 2 ? l->bundleid == id : type == 1 && !l->bundleid && l->packageid == id)) continue;
			const char *name = jstr(it, "name");
			if (name && *name) copy_utf8(l->name, sizeof l->name, name);
			const cJSON *apps = cJSON_GetObjectItemCaseSensitive(it, "included_appids");
			const cJSON *a0 = cJSON_GetArrayItem(apps, 0);
			if (cJSON_IsNumber(a0)) l->appid = (u32)a0->valuedouble;
			const cJSON *bp = cJSON_GetObjectItemCaseSensitive(it, "best_purchase_option");
			if (bp && jstr(bp, "formatted_final_price")) opt_prices(bp, l->price, l->orig, &l->disc);
		}
	}
	cJSON_Delete(root);
}

// ----------------------------------------------------------------------------
// Pago: la página del checkout es HTML; se buscan los elementos por su id

// El valor de un atributo dentro de la etiqueta que tiene id="<id>"
// El texto de la etiqueta <label for="id"> de una casilla, sin etiquetas HTML y con los espacios juntos
static void label_text(const char *html, const char *id, char *out, size_t n)
{
	char key[64];
	snprintf(key, sizeof key, "for=\"%s\"", id);
	out[0] = 0;
	const char *s = strstr(html, key);
	if (!s || !(s = strchr(s, '>'))) return;
	const char *end = strstr(++s, "</label>");
	if (!end) return;
	size_t k = 0;
	bool tag = false;
	for (; s < end && k < n - 1; s++) {
		if (*s == '<') { tag = true; continue; }
		if (*s == '>') { tag = false; continue; }
		if (tag) continue;
		char ch = *s == '\n' || *s == '\t' || *s == '\r' ? ' ' : *s;
		if (!strncmp(s, "&nbsp;", 6)) { ch = ' '; s += 5; }
		else if (!strncmp(s, "&amp;", 5)) { ch = '&'; s += 4; }
		if (ch == ' ' && (k == 0 || out[k - 1] == ' ')) continue;
		out[k++] = ch;
	}
	while (k && out[k - 1] == ' ') k--;
	out[k] = 0;
	tidy_utf8(out);
}

static bool html_attr(const char *html, const char *id, const char *attr, char *out, size_t n)
{
	char key[48];
	snprintf(key, sizeof key, "id=\"%s\"", id);
	const char *p = strstr(html, key);
	if (!p) return false;
	const char *start = p, *end = strchr(p, '>');
	while (start > html && *start != '<') start--;
	if (!end) return false;
	char a[24];
	snprintf(a, sizeof a, " %s=\"", attr);
	for (const char *q = start; q < end; q++) {
		if (strncmp(q, a, strlen(a))) continue;
		q += strlen(a);
		const char *e = memchr(q, '"', end - q);
		if (!e) return false;
		size_t len = e - q;
		if (len >= n) len = n - 1;
		memcpy(out, q, len);
		out[len] = 0;
		return true;
	}
	return false;
}

bool steam_parse_checkout_page(const char *html, CheckoutPage *o)
{
	memset(o, 0, sizeof *o);
	if (!html || !strstr(html, "id=\"payment_method\"")) return false;
	o->ok = true;
	const char *s = strstr(html, "g_sessionID = \"");
	if (s) {
		s += 15;
		size_t n = strcspn(s, "\"");
		if (n < sizeof o->sessionid) memcpy(o->sessionid, s, n);
	}
	// Las opciones del desplegable de Steam son <a id="metodo"> dentro de payment_method_droplist
	const char *dl = strstr(html, "id=\"payment_method_droplist\"");
	if (dl) {
		const char *end = strstr(dl, "</ul>");
		const char *w = strstr(dl, "id=\"steamaccount\"");
		o->wallet = w && (!end || w < end);
	}
	char v[48] = "";
	if (html_attr(html, "stored_card_id", "value", v, sizeof v) && v[0] && strcmp(v, "0") && strcmp(v, "-1")) {
		snprintf(o->card_gid, sizeof o->card_gid, "%.*s", (int)sizeof o->card_gid - 1, v);
		html_attr(html, "stored_payment_method", "value", o->card_type, sizeof o->card_type);
		char num[48] = "";
		html_attr(html, "stored_card_number", "value", num, sizeof num);
		// Solo se guardan los cuatro últimos dígitos, venga como venga («************4821», «4821»…)
		int nd = 0;
		char digits[8];
		for (int i = (int)strlen(num) - 1; i >= 0 && nd < 4; i--)
			if (num[i] >= '0' && num[i] <= '9') digits[nd++] = num[i];
		for (int i = 0; i < nd; i++) o->card_last4[i] = digits[nd - 1 - i];
		o->card = o->card_type[0] != 0;
	}
	char c[8] = "";
	if (html_attr(html, "billing_country", "value", c, sizeof c) && strlen(c) == 2) memcpy(o->country, c, 3);
	// Las casillas que la web obliga a marcar antes de pagar: se copian sus textos tal cual
	label_text(html, "accept_ssa", o->ssa_text, sizeof o->ssa_text);
	o->withdrawal = strstr(html, "accept_right_withdrawal") != NULL;
	if (o->withdrawal) label_text(html, "accept_right_withdrawal", o->withdrawal_text, sizeof o->withdrawal_text);
	return true;
}

bool steam_parse_wallet(const char *json, WalletInfo *w)
{
	memset(w, 0, sizeof *w);
	cJSON *root = cJSON_Parse(json);
	const cJSON *r = cJSON_GetObjectItemCaseSensitive(root, "response");
	if (!r) {
		cJSON_Delete(root);
		return false;
	}
	w->has_wallet = jbool(r, "has_wallet");
	w->balance = (long long)jnum(r, "balance", 0);
	copy(w->formatted, sizeof w->formatted, jstr(r, "formatted_balance"));
	copy(w->country, sizeof w->country, jstr(r, "wallet_country_code"));
	cJSON_Delete(root);
	return true;
}

// rgIgnoredApps es un objeto {"appid": motivo} (o una lista vacía si no hay ninguno)
int steam_parse_userdata_ignored(const char *json, u32 **ids)
{
	*ids = NULL;
	cJSON *root = cJSON_Parse(json);
	const cJSON *ig = cJSON_GetObjectItemCaseSensitive(root, "rgIgnoredApps");
	if (!ig || !(cJSON_IsObject(ig) || cJSON_IsArray(ig))) {
		cJSON_Delete(root);
		return -1;
	}
	int n = cJSON_GetArraySize(ig), k = 0;
	*ids = malloc(sizeof(u32) * (n ? n : 1));
	const cJSON *it;
	cJSON_ArrayForEach(it, ig) {
		u32 a = it->string ? (u32)strtoul(it->string, NULL, 10) : cJSON_IsNumber(it) ? (u32)it->valuedouble : 0;
		if (a && *ids) (*ids)[k++] = a;
	}
	cJSON_Delete(root);
	return k;
}

int steam_parse_userdata_followed(const char *json, u32 **ids)
{
	*ids = NULL;
	cJSON *root = cJSON_Parse(json);
	const cJSON *fa = cJSON_GetObjectItemCaseSensitive(root, "rgFollowedApps");
	if (!cJSON_IsArray(fa)) {
		cJSON_Delete(root);
		return -1;
	}
	int n = cJSON_GetArraySize(fa), k = 0;
	*ids = malloc(sizeof(u32) * (n ? n : 1));
	const cJSON *it;
	cJSON_ArrayForEach(it, fa)
		if (cJSON_IsNumber(it) && *ids) (*ids)[k++] = (u32)it->valuedouble;
	cJSON_Delete(root);
	return k;
}

int steam_parse_userdata_owned(const char *json, u32 **ids)
{
	*ids = NULL;
	cJSON *root = cJSON_Parse(json);
	const cJSON *apps = cJSON_GetObjectItemCaseSensitive(root, "rgOwnedApps");
	if (!cJSON_IsArray(apps)) {
		cJSON_Delete(root);
		return -1;
	}
	int n = cJSON_GetArraySize(apps), k = 0;
	*ids = malloc(sizeof(u32) * (n ? n : 1));
	const cJSON *it;
	cJSON_ArrayForEach(it, apps) if (cJSON_IsNumber(it) && *ids) (*ids)[k++] = (u32)it->valuedouble;
	cJSON_Delete(root);
	return k;
}

// ----------------------------------------------------------------------------
// Perfiles

Req *steam_req_profile_items(u64 steamid)
{
	char url[200];
	snprintf(url, sizeof url, "https://api.steampowered.com/IPlayerService/GetProfileItemsEquipped/v1/?steamid=%llu&language=%s",
		(unsigned long long)steamid, g_lang);
	return net_get(url, LANE_API);
}

bool steam_parse_profile_items(const char *json, SteamProfile *p)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (resp) {
		copy(p->bg, sizeof p->bg, jstr(cJSON_GetObjectItemCaseSensitive(resp, "profile_background"), "image_large"));
		const cJSON *f = cJSON_GetObjectItemCaseSensitive(resp, "avatar_frame");
		// image_large es la imagen fija; image_small suele ser la animada (APNG de varios MB)
		const char *fr = jstr(f, "image_large");
		copy(p->frame, sizeof p->frame, fr ? fr : jstr(f, "image_small"));
		p->items_ok = true;
	}
	cJSON_Delete(root);
	return resp != NULL;
}

Req *steam_req_profile_showcases(u64 steamid)
{
	char url[200];
	snprintf(url, sizeof url, "https://api.steampowered.com/IPlayerService/GetProfileCustomization/v1/?steamid=%llu&include_inactive_customizations=false",
		(unsigned long long)steamid);
	return net_get(url, LANE_API);
}

static u64 jid(const cJSON *o, const char *k)
{
	const char *s = jstr(o, k);
	return s ? strtoull(s, NULL, 10) : (u64)jnum(o, k, 0);
}

// Texto escrito por la gente (sobre mí, vitrina de texto): fuera emoticonos (ːnombreː), etiquetas
// BBCode, espacios repetidos y líneas en blanco de más
static void tidy_text(char *t)
{
	if (!t) return;
	char *o = t;
	const char *i = t;
	int nl = 0;
	bool sp = false, line_start = true;
	while (*i) {
		if (!strncmp(i, "ː", 2)) {   // ːcritterbirdː
			const char *e = strstr(i + 2, "ː");
			if (e && e - i < 40) {
				i = e + 2;
				continue;
			}
		}
		if (*i == '[') {
			const char *e = strchr(i, ']');
			if (e && e - i < 24 && (isalpha((unsigned char)i[1]) || i[1] == '/')) {
				i = e + 1;
				continue;
			}
		}
		char c = *i++;
		if (c == '\r' || c == '\t') c = ' ';
		if (c == '\n') {
			while (o > t && o[-1] == ' ') o--;
			if (++nl <= 2 && o > t) *o++ = '\n';
			sp = false;
			line_start = true;
			continue;
		}
		if (c == ' ') {
			sp = !line_start;
			continue;
		}
		if (sp) *o++ = ' ';
		sp = false;
		line_start = false;
		nl = 0;
		*o++ = c;
	}
	while (o > t && (o[-1] == '\n' || o[-1] == ' ')) o--;
	*o = 0;
}

bool steam_parse_profile_showcases(const char *json, SteamProfile *p)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return false;
	}
	const cJSON *c, *sl;
	cJSON_ArrayForEach(c, cJSON_GetObjectItemCaseSensitive(resp, "customizations")) {
		if (cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(c, "active"))) continue;
		int type = (int)jnum(c, "customization_type", 0);
		bool known = type == SHOW_COLLECTOR || type == SHOW_SHOTS || type == SHOW_TEXT || type == SHOW_FAV_GAME ||
			type == SHOW_ARTWORK || type == SHOW_ONE_ARTWORK || type == SHOW_COMPLETIONIST || type == SHOW_ACHIEVEMENTS;
		if (!known || p->nshow >= PROFILE_MAX_SHOW) {
			p->nother++;
			continue;
		}
		Showcase *sh = &p->show[p->nshow];
		memset(sh, 0, sizeof *sh);
		sh->type = type;
		cJSON_ArrayForEach(sl, cJSON_GetObjectItemCaseSensitive(c, "slots")) {
			if (type == SHOW_TEXT) {
				const char *t = jstr(sl, "title"), *n = jstr(sl, "notes");
				if (t) tidy_text(sh->title = strdup(t));
				if (n) tidy_text(sh->text = strdup(n));
				sh->n = (sh->title && sh->title[0]) || (sh->text && sh->text[0]);   // vacía: no se enseña
				if (!sh->n) {
					free(sh->title);
					free(sh->text);
					sh->title = sh->text = NULL;
				}
				break;
			}
			if (sh->n >= SHOW_MAX_SLOTS) break;
			u32 app = (u32)jnum(sl, "appid", 0);
			u64 file = jid(sl, "publishedfileid");
			if (!app && !file) continue;   // hueco vacío
			if (type == SHOW_ACHIEVEMENTS && !jstr(sl, "title")) continue;
			sh->appid[sh->n] = app;
			sh->file[sh->n] = file;
			copy(sh->ach[sh->n], sizeof sh->ach[0], jstr(sl, "title"));
			sh->n++;
		}
		if (sh->n) p->nshow++;
	}
	copy(p->theme, sizeof p->theme, jstr(cJSON_GetObjectItemCaseSensitive(resp, "profile_theme"), "theme_id"));
	p->shows_ok = true;
	cJSON_Delete(root);
	return true;
}

Req *steam_req_profile_xml(u64 steamid)
{
	char url[160];
	snprintf(url, sizeof url, "https://steamcommunity.com/profiles/%llu/?xml=1", (unsigned long long)steamid);
	return net_get(url, LANE_API);
}

// El texto de <tag>…</tag> o <tag><![CDATA[…]]></tag>, solo el del perfil (no el de sus grupos)
static bool xml_tag(const char *xml, const char *end, const char *tag, char *out, size_t n)
{
	char open[40];
	snprintf(open, sizeof open, "<%s>", tag);
	const char *a = strstr(xml, open);
	if (!a || (end && a > end)) return false;
	a += strlen(open);
	const char *b;
	if (!strncmp(a, "<![CDATA[", 9)) {
		a += 9;
		b = strstr(a, "]]>");
	} else {
		snprintf(open, sizeof open, "</%s>", tag);
		b = strstr(a, open);
	}
	if (!b) return false;
	size_t l = (size_t)(b - a);
	if (out) {
		if (l >= n) l = n - 1;
		memcpy(out, a, l);
		out[l] = 0;
	}
	return true;
}

static u32 parse_since(const char *s)
{
	// «November 13, 2015» (la comunidad siempre lo da en inglés)
	static const char *months[] = {"January", "February", "March", "April", "May", "June", "July", "August",
		"September", "October", "November", "December"};
	int mon = -1, d = 0, y = 0;
	for (int i = 0; i < 12; i++)
		if (!strncmp(s, months[i], strlen(months[i]))) mon = i;
	const char *sp = strchr(s, ' ');
	if (mon < 0 || !sp || sscanf(sp + 1, "%d, %d", &d, &y) != 2) return 0;
	struct tm tm = {0};
	tm.tm_year = y - 1900;
	tm.tm_mon = mon;
	tm.tm_mday = d;
	tm.tm_hour = 12;
	return (u32)mktime(&tm);
}

bool steam_parse_profile_xml(const char *xml, SteamProfile *p)
{
	if (!xml || !strstr(xml, "<profile>")) return false;
	const char *end = strstr(xml, "<groups>");
	if (!end) end = strstr(xml, "<mostPlayedGames>");
	char tmp[200];
	if (xml_tag(xml, end, "steamID", tmp, sizeof tmp)) {
		html_to_lines(tmp, p->name, sizeof p->name);
	}
	xml_tag(xml, end, "realname", p->realname, sizeof p->realname);
	xml_tag(xml, end, "location", p->location, sizeof p->location);
	xml_tag(xml, end, "avatarFull", p->avatar, sizeof p->avatar);
	// Mismo archivo en el CDN de Akamai (el que se usa en el resto de la app)
	char *fa = strstr(p->avatar, "avatars.fastly.");
	if (fa) memcpy(fa, "avatars.akamai.", 15);
	xml_tag(xml, end, "onlineState", p->state, sizeof p->state);
	if (xml_tag(xml, end, "stateMessage", tmp, sizeof tmp)) html_to_lines(tmp, p->state_msg, sizeof p->state_msg);
	if (xml_tag(xml, end, "memberSince", tmp, sizeof tmp)) p->since = parse_since(tmp);
	if (xml_tag(xml, end, "privacyState", tmp, sizeof tmp)) p->is_private = strcmp(tmp, "public") != 0;
	char *sum = malloc(4096);
	if (sum && xml_tag(xml, end, "summary", sum, 4096)) {
		size_t n = strlen(sum) * 2 + 16;
		free(p->summary);
		p->summary = malloc(n);
		if (p->summary) {
			html_to_lines(sum, p->summary, n);
			tidy_text(p->summary);
			if (!strcmp(p->summary, "No information given.") || !p->summary[0]) {
				free(p->summary);
				p->summary = NULL;
			}
		}
	}
	free(sum);
	// Sus grupos (el principal primero, como en su perfil)
	p->ngroups = 0;
	for (const char *g = end ? strstr(end, "<group ") : NULL; g && p->ngroups < 6; g = strstr(g + 7, "<group ")) {
		const char *ge = strstr(g, "</group>");
		if (!ge) break;
		typeof(p->group[0]) *gr = &p->group[p->ngroups];
		memset(gr, 0, sizeof *gr);
		if (!xml_tag(g, ge, "groupName", tmp, sizeof tmp)) continue;
		html_to_lines(tmp, gr->name, sizeof gr->name);
		xml_tag(g, ge, "avatarMedium", gr->avatar, sizeof gr->avatar);
		char *fa = strstr(gr->avatar, "avatars.fastly.");
		if (fa) memcpy(fa, "avatars.akamai.", 15);
		if (xml_tag(g, ge, "memberCount", tmp, sizeof tmp)) gr->members = atoi(tmp);
		gr->primary = !strncmp(g, "<group isPrimary=\"1\"", 20);
		p->ngroups++;
	}
	p->xml_ok = true;
	return true;
}

void steam_profile_free(SteamProfile *p)
{
	for (int i = 0; i < p->nshow; i++) {
		free(p->show[i].title);
		free(p->show[i].text);
	}
	free(p->summary);
	memset(p, 0, sizeof *p);
}

Req *steam_req_level(u64 steamid, const char *token)
{
	char url[1300];
	snprintf(url, sizeof url, "https://api.steampowered.com/IPlayerService/GetSteamLevel/v1/?access_token=%s&steamid=%llu",
		token, (unsigned long long)steamid);
	return net_get(url, LANE_API);
}

int steam_parse_level(const char *json)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	int lv = resp && cJSON_GetObjectItemCaseSensitive(resp, "player_level") ? (int)jnum(resp, "player_level", 0) : -1;
	cJSON_Delete(root);
	return lv;
}

Req *steam_req_files(const u64 *ids, int n, const char *token)
{
	cJSON *in = cJSON_CreateObject();
	cJSON *arr = cJSON_AddArrayToObject(in, "publishedfileids");
	for (int i = 0; i < n; i++) {
		char t[24];
		snprintf(t, sizeof t, "%llu", (unsigned long long)ids[i]);
		cJSON_AddItemToArray(arr, cJSON_CreateString(t));
	}
	cJSON_AddBoolToObject(in, "short_description", true);
	return steam_get_service("IPublishedFileService/GetDetails", in, token);
}

// La imagen de steamusercontent, ya reducida por Steam al ancho de la pantalla de arriba
static void ugc_sized(const char *src, char *out, size_t n)
{
	char base[200];
	// Las direcciones antiguas (steamuserimages-a.akamaihd.net/ugc/…) son los mismos archivos
	const char *old = strstr(src, "steamuserimages-a.akamaihd.net/");
	if (old) snprintf(base, sizeof base, "https://images.steamusercontent.com/%s", old + strlen("steamuserimages-a.akamaihd.net/"));
	else snprintf(base, sizeof base, "%s", src);
	char *q = strchr(base, '?');
	if (q) *q = 0;
	if (strstr(base, "steamusercontent.com")) snprintf(out, n, "%s?imw=400&imh=1000&ima=fit&impolicy=Letterbox&letterbox=false", base);
	else snprintf(out, n, "%s", base);
}

int steam_parse_files(const char *json, const u64 *ids, int n, char (*urls)[200])
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	int found = 0;
	const cJSON *it, *list = resp ? cJSON_GetObjectItemCaseSensitive(resp, "publishedfiledetails") : NULL;
	cJSON_ArrayForEach(it, list) {
		u64 id = jid(it, "publishedfileid");
		const char *u = jstr(it, "preview_url");
		if (!u || !*u) continue;
		for (int i = 0; i < n; i++) {
			if (ids[i] == id && !urls[i][0]) {
				ugc_sized(u, urls[i], sizeof urls[i]);
				found++;
			}
		}
	}
	cJSON_Delete(root);
	return found;
}

Req *steam_req_file_page(u64 id)
{
	char url[120];
	snprintf(url, sizeof url, "https://steamcommunity.com/sharedfiles/filedetails/?id=%llu", (unsigned long long)id);
	return net_get(url, LANE_API);
}

bool steam_parse_file_page(const char *html, char *url, size_t n)
{
	// La imagen que enseña la página (ActualMedia); si no está, la de la vista previa (og:image).
	// La imagen genérica del Workshop no vale: es la que sale cuando la página no tiene la suya
	const char *a = html ? strstr(html, "id=\"ActualMedia\"") : NULL;
	if (a) a = strstr(a, "src=\"");
	if (a) a += 5;
	else {
		a = html ? strstr(html, "<meta property=\"og:image\" content=\"") : NULL;
		if (!a) return false;
		a += strlen("<meta property=\"og:image\" content=\"");
	}
	const char *b = strchr(a, '"');
	if (!b || b - a >= 200) return false;
	char raw[200];
	memcpy(raw, a, b - a);
	raw[b - a] = 0;
	if (strstr(raw, "steam_workshop_default")) return false;
	ugc_sized(raw, url, n);
	return url[0] != 0;
}

// ----------------------------------------------------------------------------
// Tus capturas

Req *steam_req_my_shots(u64 steamid, u32 appid, const char *token)
{
	cJSON *in = cJSON_CreateObject();
	char sid[24];
	snprintf(sid, sizeof sid, "%llu", (unsigned long long)steamid);
	cJSON_AddStringToObject(in, "steamid", sid);
	cJSON_AddNumberToObject(in, "appid", appid);
	cJSON_AddNumberToObject(in, "filetype", 4);          // k_EPublishedFileInfoMatchingFileType_Screenshots
	cJSON_AddNumberToObject(in, "numperpage", 50);
	cJSON_AddNumberToObject(in, "page", 1);
	cJSON_AddStringToObject(in, "sortmethod", "created");
	cJSON_AddBoolToObject(in, "return_short_description", true);
	cJSON_AddBoolToObject(in, "strip_description_bbcode", true);
	return steam_get_service("IPublishedFileService/GetUserFiles", in, token);
}

int steam_parse_my_shots(const char *json, u32 appid, MyShot *out, int max, int *total)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return -1;
	}
	int n = 0;
	const cJSON *it, *list = cJSON_GetObjectItemCaseSensitive(resp, "publishedfiledetails");
	cJSON_ArrayForEach(it, list) {
		if (n >= max) break;
		u32 app = (u32)jnum(it, "consumer_appid", 0);
		if (app && appid && app != appid) continue;   // de otro juego
		const char *u = jstr(it, "file_url");
		if (!u || !*u) u = jstr(it, "preview_url");
		if (!u || !*u) continue;
		MyShot *m = &out[n];
		memset(m, 0, sizeof *m);
		ugc_sized(u, m->url, sizeof m->url);
		const char *c = jstr(it, "short_description");
		if (!c || !*c) c = jstr(it, "title");
		copy_utf8(m->caption, sizeof m->caption, c);
		m->when = (u32)jnum(it, "time_created", 0);
		n++;
	}
	*total = (int)jnum(resp, "total", n);
	if (*total < n) *total = n;
	cJSON_Delete(root);
	return n;
}

// ----------------------------------------------------------------------------
// Tu reseña (el servicio con el que el cliente de Steam enseña la tuya)

Req *steam_req_my_review(u64 steamid, u32 appid, const char *token)
{
	cJSON *in = cJSON_CreateObject();
	cJSON *reqs = cJSON_AddArrayToObject(in, "requests");
	cJSON *r = cJSON_CreateObject();
	char sid[24];
	snprintf(sid, sizeof sid, "%llu", (unsigned long long)steamid);
	cJSON_AddStringToObject(r, "steamid", sid);
	cJSON_AddNumberToObject(r, "appid", appid);
	cJSON_AddItemToArray(reqs, r);
	return steam_get_service("IUserReviewsService/GetIndividualRecommendations", in, token);
}

bool steam_parse_my_review(const char *json, u32 appid, MyReview *out)
{
	free(out->text);
	memset(out, 0, sizeof *out);
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return false;
	}
	const cJSON *it;
	cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(resp, "recommendations")) {
		u32 app = (u32)jnum(it, "appid", 0);
		if ((app && app != appid) || !jid(it, "recommendationid")) continue;
		out->has = true;
		out->up = jbool(it, "voted_up");
		const char *t = jstr(it, "review");
		out->text = strdup(t ? t : "");
		out->created = (u32)jnum(it, "timestamp_created", 0);
		out->updated = (u32)jnum(it, "timestamp_updated", 0);
		out->votes_up = (int)jnum(it, "votes_up", 0);
		out->votes_funny = (int)jnum(it, "votes_funny", 0);
		out->comments = (int)jnum(it, "comment_count", 0);
		break;
	}
	cJSON_Delete(root);
	return true;
}

// ----------------------------------------------------------------------------
// Instalación remota (IClientCommService)

Req *steam_req_remote_pcs(const char *token)
{
	return steam_get_service("IClientCommService/GetAllClientLogonInfo", cJSON_CreateObject(), token);
}

int steam_parse_remote_pcs(const char *json, RemotePC *out, int max)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return -1;
	}
	int n = 0;
	const cJSON *it;
	cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(resp, "sessions")) {
		if (n >= max) break;
		u64 id = jid(it, "client_instanceid");
		if (!id) continue;
		RemotePC *pc = &out[n++];
		memset(pc, 0, sizeof *pc);
		pc->id = id;
		copy_utf8(pc->name, sizeof pc->name, jstr(it, "machine_name"));
		copy_utf8(pc->os, sizeof pc->os, jstr(it, "os_name"));
		pc->installed = -1;
	}
	cJSON_Delete(root);
	return n;
}

static cJSON *client_input(u64 client, u32 appid)
{
	cJSON *in = cJSON_CreateObject();
	char t[24];
	snprintf(t, sizeof t, "%llu", (unsigned long long)client);
	cJSON_AddStringToObject(in, "client_instanceid", t);
	cJSON_AddNumberToObject(in, "appid", appid);
	return in;
}

Req *steam_req_remote_apps(u64 client, u32 appid, const char *token)
{
	cJSON *in = client_input(client, appid);
	cJSON_DeleteItemFromObject(in, "appid");
	cJSON_AddStringToObject(in, "fields", "games");
	cJSON *f = cJSON_AddArrayToObject(in, "filter_appids");
	cJSON_AddItemToArray(f, cJSON_CreateNumber(appid));
	cJSON_AddBoolToObject(in, "include_client_info", false);
	return steam_get_service("IClientCommService/GetClientAppList", in, token);
}

int steam_parse_remote_app(const char *json, u32 appid)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	const cJSON *apps = resp ? cJSON_GetObjectItemCaseSensitive(resp, "apps") : NULL;
	int st = resp ? 0 : -1;
	if (resp && !apps && !cJSON_GetObjectItemCaseSensitive(resp, "bytes_available")) st = -1;   // respuesta vacía: no se sabe
	const cJSON *it;
	cJSON_ArrayForEach(it, apps) {
		if ((u32)jnum(it, "appid", 0) != appid) continue;
		st = jbool(it, "installed") ? 1 : 0;
		break;
	}
	cJSON_Delete(root);
	return st;
}

Req *steam_req_remote_install(u64 client, u32 appid, const char *token)
{
	return steam_post_service("IClientCommService/InstallClientApp", client_input(client, appid), token);
}

// ----------------------------------------------------------------------------
// Playtests

Req *steam_req_playtest(u32 appid)
{
	cJSON *in = cJSON_CreateObject();
	cJSON *arr = cJSON_AddArrayToObject(in, "ids");
	cJSON *o = cJSON_CreateObject();
	cJSON_AddNumberToObject(o, "appid", appid);
	cJSON_AddItemToArray(arr, o);
	cJSON_AddItemToObject(in, "context", context());
	cJSON *dr = cJSON_AddObjectToObject(in, "data_request");
	cJSON_AddBoolToObject(dr, "include_platforms", true);
	char *js = cJSON_PrintUnformatted(in);
	char *esc = url_escape(js);
	size_t n = strlen(esc) + 128;
	char *url = malloc(n);
	snprintf(url, n, "https://api.steampowered.com/IStoreBrowseService/GetItems/v1/?input_json=%s", esc);
	Req *r = net_get(url, LANE_API);
	free(url);
	free(esc);
	free(js);
	cJSON_Delete(in);
	return r;
}

u32 steam_parse_playtest(const char *json, u32 appid)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	u32 pt = 0;
	const cJSON *it, *items = resp ? cJSON_GetObjectItemCaseSensitive(resp, "store_items") : NULL;
	cJSON_ArrayForEach(it, items) {
		if ((u32)jnum(it, "appid", 0) != appid) continue;
		const cJSON *rel = cJSON_GetObjectItemCaseSensitive(it, "related_items");
		const cJSON *p = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(rel, "playtests"), 0);
		pt = p ? (u32)jnum(p, "appid", 0) : 0;
		break;
	}
	cJSON_Delete(root);
	return pt;
}

// ----------------------------------------------------------------------------
// Privacidad y tema del perfil

Req *steam_req_privacy(u64 steamid, const char *cookies)
{
	char url[120];
	snprintf(url, sizeof url, "https://steamcommunity.com/profiles/%llu/edit/settings", (unsigned long long)steamid);
	return net_request(url, NULL, cookies);
}

// &quot; y compañía, en el mismo sitio
static void html_unescape(char *s)
{
	static const struct { const char *e; char c; } ents[] = {{"&quot;", '"'}, {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&#39;", '\''}};
	char *o = s;
	while (*s) {
		bool done = false;
		if (*s == '&')
			for (size_t i = 0; i < sizeof ents / sizeof *ents; i++) {
				size_t l = strlen(ents[i].e);
				if (!strncmp(s, ents[i].e, l)) {
					*o++ = ents[i].c;
					s += l;
					done = true;
					break;
				}
			}
		if (!done) *o++ = *s++;
	}
	*o = 0;
}

static int privacy_val(const cJSON *o, const char *k)
{
	int v = (int)jnum(o, k, 0);
	return v >= 1 && v <= 3 ? v : 0;
}

bool steam_parse_privacy(const char *html, SteamPrivacy *out)
{
	steam_privacy_free(out);
	const char *a = html ? strstr(html, "data-profile-edit=\"") : NULL;
	if (!a) return false;
	a += strlen("data-profile-edit=\"");
	const char *b = strchr(a, '"');
	if (!b) return false;
	char *js = strndup(a, b - a);
	if (!js) return false;
	html_unescape(js);
	cJSON *root = cJSON_Parse(js);
	free(js);
	const cJSON *pv = cJSON_GetObjectItemCaseSensitive(root, "Privacy");
	const cJSON *ps = cJSON_GetObjectItemCaseSensitive(pv, "PrivacySettings");
	bool ok = cJSON_IsObject(ps);
	if (ok) {
		out->profile = privacy_val(ps, "PrivacyProfile");
		out->games = privacy_val(ps, "PrivacyOwnedGames");
		out->friends = privacy_val(ps, "PrivacyFriendsList");
		out->inventory = privacy_val(ps, "PrivacyInventory");
		out->playtime_private = privacy_val(ps, "PrivacyPlaytime") == 1;
		out->gifts_private = privacy_val(ps, "PrivacyInventoryGifts") == 1;
		out->comments = (int)jnum(pv, "eCommentPermission", 1);
		out->raw = cJSON_PrintUnformatted(ps);
		ok = out->profile && out->games && out->friends && out->inventory && out->raw;
	}
	cJSON_Delete(root);
	if (!ok) steam_privacy_free(out);
	return ok;
}

static void set_num(cJSON *o, const char *k, int v)
{
	cJSON_DeleteItemFromObjectCaseSensitive(o, k);
	cJSON_AddNumberToObject(o, k, v);
}

Req *steam_req_set_privacy(u64 steamid, const SteamPrivacy *p, const char *sessionid, const char *cookies)
{
	cJSON *ps = p->raw ? cJSON_Parse(p->raw) : NULL;
	if (!ps) return NULL;
	set_num(ps, "PrivacyProfile", p->profile);
	set_num(ps, "PrivacyOwnedGames", p->games);
	set_num(ps, "PrivacyFriendsList", p->friends);
	set_num(ps, "PrivacyInventory", p->inventory);
	set_num(ps, "PrivacyPlaytime", p->playtime_private ? 1 : p->games);
	set_num(ps, "PrivacyInventoryGifts", p->gifts_private ? 1 : p->inventory);
	char *js = cJSON_PrintUnformatted(ps);
	cJSON_Delete(ps);
	char *esc = url_escape(js);
	size_t n = strlen(esc) + 160;
	char *body = malloc(n), url[120];
	snprintf(body, n, "sessionid=%s&Privacy=%s&eCommentPermission=%d", sessionid, esc, p->comments);
	snprintf(url, sizeof url, "https://steamcommunity.com/profiles/%llu/ajaxsetprivacy/", (unsigned long long)steamid);
	Req *r = net_request(url, body, cookies);
	free(body);
	free(esc);
	free(js);
	return r;
}

void steam_privacy_free(SteamPrivacy *p)
{
	free(p->raw);
	memset(p, 0, sizeof *p);
}

Req *steam_req_themes(const char *token)
{
	return steam_get_service("IPlayerService/GetProfileThemesAvailable", cJSON_CreateObject(), token);
}

int steam_parse_themes(const char *json, char (*out)[24], int max)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return -1;
	}
	int n = 0;
	const cJSON *it;
	cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(resp, "profile_themes")) {
		const char *id = jstr(it, "theme_id");
		if (!id || !*id || n >= max) continue;
		copy(out[n++], 24, id);
	}
	cJSON_Delete(root);
	return n;
}

Req *steam_req_set_theme(const char *theme, const char *token)
{
	cJSON *in = cJSON_CreateObject();
	cJSON_AddStringToObject(in, "theme_id", theme);
	return steam_post_service("IPlayerService/SetProfileTheme", in, token);
}

int steam_parse_top_achievements(const char *json, TopAch *out, int max)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return -1;
	}
	int n = 0;
	const cJSON *g, *it;
	cJSON_ArrayForEach(g, cJSON_GetObjectItemCaseSensitive(resp, "games")) {
		u32 app = (u32)jnum(g, "appid", 0);
		cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(g, "achievements")) {
			if (n >= max) break;
			TopAch *a = &out[n++];
			memset(a, 0, sizeof *a);
			a->appid = app;
			snprintf(a->key, sizeof a->key, "%d_%d", (int)jnum(it, "statid", 0), (int)jnum(it, "bit", 0));
			copy_utf8(a->name, sizeof a->name, jstr(it, "name"));
			copy(a->icon, sizeof a->icon, jstr(it, "icon"));
			const char *p = jstr(it, "player_percent_unlocked");
			a->pct = p ? strtof(p, NULL) : 0;
		}
	}
	cJSON_Delete(root);
	return n;
}

// ----------------------------------------------------------------------------
// Descargas de tu PC (IClientCommService)

Req *steam_req_pc_apps(u64 client, const char *token)
{
	cJSON *in = client_input(client, 0);
	cJSON_DeleteItemFromObject(in, "appid");
	cJSON_AddStringToObject(in, "fields", "games");
	cJSON_AddStringToObject(in, "filters", "installed");
	cJSON_AddStringToObject(in, "language", g_lang);
	cJSON_AddBoolToObject(in, "include_client_info", true);
	return steam_get_service("IClientCommService/GetClientAppList", in, token);
}

static long long jbig(const cJSON *o, const char *k)
{
	const char *s = jstr(o, k);
	return s ? strtoll(s, NULL, 10) : (long long)jnum(o, k, 0);
}

// Primero lo que se está descargando (por su puesto en la cola), luego lo demás por nombre
static int cmp_pcapp(const void *x, const void *y)
{
	const PcApp *a = x, *b = y;
	bool da = a->changing || a->queue >= 0, db = b->changing || b->queue >= 0;
	if (da != db) return da ? -1 : 1;
	if (da && a->queue != b->queue) return (a->queue < 0 ? 999 : a->queue) - (b->queue < 0 ? 999 : b->queue);
	return strcasecmp(a->name, b->name);
}

int steam_parse_pc_apps(const char *json, PcApp **out, long long *free_bytes)
{
	*out = NULL;
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return -1;
	}
	*free_bytes = jbig(resp, "bytes_available");
	const cJSON *apps = cJSON_GetObjectItemCaseSensitive(resp, "apps"), *it;
	int cap = cJSON_GetArraySize(apps), n = 0;
	*out = calloc(cap ? cap : 1, sizeof **out);
	cJSON_ArrayForEach(it, apps) {
		if (!*out || n >= cap) break;
		PcApp *a = &(*out)[n];
		a->appid = (u32)jnum(it, "appid", 0);
		if (!a->appid) continue;
		copy_utf8(a->name, sizeof a->name, jstr(it, "app"));
		a->installed = jbool(it, "installed");
		a->changing = jbool(it, "changing");
		a->paused = jbool(it, "download_paused");
		a->running = jbool(it, "running");
		a->done = jbig(it, "bytes_downloaded");
		a->total = jbig(it, "bytes_to_download");
		const cJSON *q = cJSON_GetObjectItemCaseSensitive(it, "queue_position");
		a->queue = cJSON_IsNumber(q) ? q->valueint : -1;
		if (a->total > 0 && a->done < a->total) a->changing = true;
		n++;
	}
	if (*out) qsort(*out, n, sizeof **out, cmp_pcapp);
	cJSON_Delete(root);
	return n;
}

Req *steam_req_pc_downloads(u64 client, bool enable, const char *token)
{
	cJSON *in = client_input(client, 0);
	cJSON_DeleteItemFromObject(in, "appid");
	cJSON_AddBoolToObject(in, "enable", enable);
	return steam_post_service("IClientCommService/EnableOrDisableDownloads", in, token);
}

Req *steam_req_pc_uninstall(u64 client, u32 appid, const char *token)
{
	return steam_post_service("IClientCommService/UninstallClientApp", client_input(client, appid), token);
}

// ----------------------------------------------------------------------------
// Solicitudes de amistad

int steam_parse_friend_requests(const char *json, u64 *ids, int max)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	const cJSON *fl = cJSON_GetObjectItemCaseSensitive(resp ? resp : root, "friendslist");
	int n = 0;
	const cJSON *f;
	const cJSON *list = fl ? cJSON_GetObjectItemCaseSensitive(fl, "friends") : NULL;
	cJSON_ArrayForEach(f, list) {
		const char *sid = jstr(f, "ulfriendid");
		if (!sid) sid = jstr(f, "steamid");
		const char *rel_s = jstr(f, "relationship");
		int rel = rel_s ? (!strcmp(rel_s, "requestrecipient") ? 2 : 0) : (int)jnum(f, "efriendrelationship", 0);
		if (rel == 2 && sid && n < max) ids[n++] = strtoull(sid, NULL, 10);
	}
	cJSON_Delete(root);
	return n;
}

Req *steam_req_friend_action(int action, u64 steamid, const char *sessionid, const char *cookies)
{
	char body[160];
	const char *url = action == FRIEND_IGNORE ? "https://steamcommunity.com/actions/IgnoreFriendInviteAjax"
	                                          : "https://steamcommunity.com/actions/AddFriendAjax";
	snprintf(body, sizeof body, "sessionID=%s&steamid=%llu%s", sessionid, (unsigned long long)steamid,
		action == FRIEND_ACCEPT ? "&accept_invite=1" : action == FRIEND_ADD ? "&accept_invite=0" : "");
	return net_request(url, body, cookies);
}

// La comunidad contesta con un JSON con success (1 o true), o solo «true»
bool steam_friend_action_ok(const Req *r)
{
	if (r->state != REQ_DONE || r->http != 200 || !r->data) return false;
	cJSON *root = cJSON_Parse(r->data);
	bool ok;
	if (cJSON_IsObject(root)) {
		const cJSON *sc = cJSON_GetObjectItemCaseSensitive(root, "success");
		ok = cJSON_IsTrue(sc) || (cJSON_IsNumber(sc) && sc->valueint == 1);
	} else {
		ok = cJSON_IsTrue(root) || (cJSON_IsNumber(root) && root->valueint == 1);
	}
	cJSON_Delete(root);
	return ok;
}

// ----------------------------------------------------------------------------
// Cola de descubrimiento

Req *steam_req_discovery(bool rebuild, const char *token)
{
	cJSON *in = cJSON_CreateObject();
	cJSON_AddNumberToObject(in, "queue_type", 0);
	cJSON_AddStringToObject(in, "country_code", g_country);
	cJSON_AddBoolToObject(in, "rebuild_queue", rebuild);
	cJSON_AddBoolToObject(in, "rebuild_queue_if_stale", true);
	cJSON_AddBoolToObject(in, "no_experimental_results", true);
	return steam_get_service("IStoreService/GetDiscoveryQueue", in, token);
}

int steam_parse_discovery(const char *json, u32 *ids, int max)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return -1;
	}
	int n = 0;
	const cJSON *it;
	cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(resp, "appids"))
		if (n < max && cJSON_IsNumber(it)) ids[n++] = (u32)it->valuedouble;
	cJSON_Delete(root);
	return n;
}

int steam_parse_deck(const char *json, u32 appid)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	int deck = -1;
	const cJSON *it, *items = resp ? cJSON_GetObjectItemCaseSensitive(resp, "store_items") : NULL;
	cJSON_ArrayForEach(it, items) {
		if ((u32)jnum(it, "appid", 0) != appid) continue;
		const cJSON *pl = cJSON_GetObjectItemCaseSensitive(it, "platforms");
		const cJSON *d = pl ? cJSON_GetObjectItemCaseSensitive(pl, "steam_deck_compat_category") : NULL;
		if (cJSON_IsNumber(d)) deck = d->valueint;
		break;
	}
	cJSON_Delete(root);
	return deck;
}

Req *steam_req_news(u32 appid)
{
	char url[200];
	snprintf(url, sizeof url, "https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=%lu&count=4&maxlength=280"
		"&feeds=steam_community_announcements&format=json", (unsigned long)appid);
	return net_get(url, LANE_API);
}

int steam_parse_news(const char *json, SteamNews *out, int max)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *an = root ? cJSON_GetObjectItemCaseSensitive(root, "appnews") : NULL;
	if (!an) {
		cJSON_Delete(root);
		return -1;
	}
	int n = 0;
	const cJSON *it;
	cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(an, "newsitems")) {
		if (n >= max) break;
		SteamNews *nw = &out[n];
		memset(nw, 0, sizeof *nw);
		copy_utf8(nw->title, sizeof nw->title, jstr(it, "title"));
		if (!nw->title[0]) continue;
		char tmp[600];
		html_to_lines(jstr(it, "contents") ? jstr(it, "contents") : "", tmp, sizeof tmp);
		tidy_text(tmp);
		// Las noticias traen etiquetas propias de Steam ({STEAM_CLAN_IMAGE}/…): fuera
		for (char *p; (p = strstr(tmp, "{STEAM_CLAN_IMAGE}"));) {
			char *e = p + strcspn(p, " \n");
			memmove(p, e, strlen(e) + 1);
		}
		for (char *p = tmp; *p; p++)
			if (*p == '\n') *p = ' ';
		const char *t = tmp;
		while (*t == ' ') t++;
		copy_utf8(nw->text, sizeof nw->text, t);
		nw->date = (u32)jnum(it, "date", 0);
		n++;
	}
	cJSON_Delete(root);
	return n;
}

// ----------------------------------------------------------------------------
// Comentarios de los perfiles (comment/Profile, como la página del perfil)

Req *steam_req_comments(u64 steamid, int count)
{
	char url[160];
	snprintf(url, sizeof url, "https://steamcommunity.com/comment/Profile/render/%llu/-1/?start=0&count=%d", (unsigned long long)steamid, count);
	return net_get(url, LANE_API);
}

// El texto entre a y la primera «end» de después, sin etiquetas
static void html_between(const char *a, const char *end, char *out, size_t n)
{
	out[0] = 0;
	if (!a) return;
	const char *b = strstr(a, end);
	if (!b) return;
	size_t l = (size_t)(b - a);
	char *tmp = malloc(l + 1);
	if (!tmp) return;
	memcpy(tmp, a, l);
	tmp[l] = 0;
	char *txt = malloc(l + 16);
	if (txt) {
		html_to_lines(tmp, txt, l + 16);
		tidy_text(txt);
		copy_utf8(out, n, txt);
	}
	free(txt);
	free(tmp);
}

Req *steam_req_profile_page(u64 steamid)
{
	char url[160];
	snprintf(url, sizeof url, "https://steamcommunity.com/profiles/%llu/?l=%s", (unsigned long long)steamid, g_lang);
	return net_get(url, LANE_API);
}

// El valor de un atributo dentro de [a, lim): src="…" (las imágenes, de akamai como el resto)
static void attr_in(const char *a, const char *lim, const char *name, char *out, size_t n)
{
	out[0] = 0;
	const char *v = a ? strstr(a, name) : NULL;
	if (!v || (lim && v > lim)) return;
	v += strlen(name);
	size_t l = strcspn(v, "\"");
	if (l >= n) return;
	memcpy(out, v, l);
	out[l] = 0;
	char *fa = strstr(out, ".fastly.steamstatic.com");
	if (fa) memcpy(fa, ".akamai.", 8);
}

// El número de un contador de la columna derecha: el enlace que acaba en «/badges/», «/screenshots/»…
static int count_link(const char *html, const char *tail)
{
	char key[48];
	snprintf(key, sizeof key, "%s\">", tail);
	for (const char *p = strstr(html, key); p; p = strstr(p + 1, key)) {
		const char *lab = strstr(p, "count_link_label");
		const char *tot = strstr(p, "profile_count_link_total\">");
		if (!lab || !tot || tot - p > 400) continue;
		tot += 26;
		int n = 0;
		bool any = false;
		for (; *tot && *tot != '<'; tot++) {
			if (*tot >= '0' && *tot <= '9') {
				n = n * 10 + (*tot - '0');
				any = true;
			}
		}
		return any ? n : -1;
	}
	return -1;
}

bool steam_parse_profile_page(const char *html, ProfilePage *o)
{
	memset(o, 0, sizeof *o);
	o->level = -1;
	o->shots_total = o->reviews_total = -1;
	if (!html || !strstr(html, "profile_header")) return false;
	o->ok = true;
	const char *lv = strstr(html, "persona_level");
	lv = lv ? strstr(lv, "friendPlayerLevelNum\">") : NULL;
	if (lv) o->level = atoi(lv + 22);
	// La destacada, junto al nivel
	const char *fb = strstr(html, "class=\"favorite_badge\"");
	if (fb) {
		const char *end = strstr(fb, "</a>");
		attr_in(fb, end, "src=\"", o->fav_icon, sizeof o->fav_icon);
		const char *nm = strstr(fb, "class=\"name ellipsis\">");
		if (nm && (!end || nm < end)) html_between(nm + 22, "</div>", o->fav_name, sizeof o->fav_name);
		const char *xp = strstr(fb, "class=\"xp\">");
		if (xp && (!end || xp < end)) html_between(xp + 11, "</div>", o->fav_xp, sizeof o->fav_xp);
	}
	// Las insignias de la columna derecha (la sección «profile_badges», no la de premios)
	const char *pb = strstr(html, "class=\"profile_badges\"");
	if (pb) {
		const char *end = strstr(pb, "profile_count_link_preview_ctn");   // la siguiente sección
		end = end ? strstr(end + 10, "profile_count_link_preview_ctn") : NULL;
		for (const char *b = strstr(pb, "class=\"profile_badges_badge"); b && (!end || b < end) && o->nbadges < PROFILE_BADGES;
		     b = strstr(b + 10, "class=\"profile_badges_badge")) {
			const char *bend = strstr(b, "</div>");
			int k = o->nbadges;
			attr_in(b, bend, "src=\"", o->badge_icon[k], sizeof o->badge_icon[k]);
			char tip[200] = "";
			const char *tt = strstr(b, "data-tooltip-html=\"");
			if (tt && (!bend || tt < bend)) {
				html_between(tt + 19, "\"", tip, sizeof tip);
				char *nl = strchr(tip, '\n');
				if (nl) *nl = 0;
			}
			copy_utf8(o->badge_name[k], sizeof o->badge_name[k], tip);
			if (o->badge_icon[k][0]) o->nbadges++;
		}
	}
	o->badges_total = count_link(html, "/badges/");
	if (o->badges_total < 0) o->badges_total = o->nbadges;
	o->shots_total = count_link(html, "/screenshots/");
	o->reviews_total = count_link(html, "/recommended/");
	return true;
}

Req *steam_req_profile_shots(u64 steamid)
{
	char url[200];
	snprintf(url, sizeof url, "https://steamcommunity.com/profiles/%llu/screenshots/?l=%s&sort=newestfirst&view=imagewall",
		(unsigned long long)steamid, g_lang);
	return net_get(url, LANE_API);
}

int steam_parse_profile_shots(const char *html, ProfileShot *out, int max)
{
	if (!html || !strstr(html, "sharedfiles")) return -1;
	int n = 0;
	for (const char *a = strstr(html, "class=\"profile_media_item"); a && n < max; a = strstr(a + 10, "class=\"profile_media_item")) {
		const char *next = strstr(a + 10, "class=\"profile_media_item");
		ProfileShot *o = &out[n];
		memset(o, 0, sizeof *o);
		const char *ap = strstr(a, "data-appid=\"");
		if (ap && (!next || ap < next)) o->appid = (u32)strtoul(ap + 12, NULL, 10);
		const char *id = strstr(a, "data-publishedfileid=\"");
		if (id && (!next || id < next)) o->id = strtoull(id + 22, NULL, 10);
		const char *bg = strstr(a, "background-image: url('");
		if (bg && (!next || bg < next)) {
			bg += 23;
			char raw[200];
			size_t l = strcspn(bg, "'");
			if (l < sizeof raw) {
				memcpy(raw, bg, l);
				raw[l] = 0;
				ugc_sized(raw, o->url, sizeof o->url);
			}
		}
		const char *q = strstr(a, "<q class=\"ellipsis\">");
		if (q && (!next || q < next)) html_between(q + 20, "</q>", o->desc, sizeof o->desc);
		if (o->url[0]) n++;
	}
	return n;
}

Req *steam_req_profile_reviews(u64 steamid)
{
	char url[160];
	snprintf(url, sizeof url, "https://steamcommunity.com/profiles/%llu/recommended/?l=%s", (unsigned long long)steamid, g_lang);
	return net_get(url, LANE_API);
}

int steam_parse_profile_reviews(const char *html, ProfileReview *out, int max)
{
	if (!html || (!strstr(html, "workshopBrowsePaging") && !strstr(html, "review_box"))) return -1;
	int n = 0;
	for (const char *b = strstr(html, "class=\"review_box_content\""); b && n < max; b = strstr(b + 10, "class=\"review_box_content\"")) {
		const char *next = strstr(b + 10, "class=\"review_box_content\"");
		ProfileReview *o = &out[n];
		memset(o, 0, sizeof *o);
		const char *ap = strstr(b, "steamcommunity.com/app/");
		if (ap && (!next || ap < next)) o->appid = (u32)strtoul(ap + 23, NULL, 10);
		const char *cap = strstr(b, "class=\"game_capsule\"");
		if (cap && (!next || cap < next)) attr_in(cap, next, "src=\"", o->capsule, sizeof o->capsule);
		const char *th = strstr(b, "icon_thumbs");
		o->up = th && (!next || th < next) && !strncmp(th + 11, "Up", 2);
		const char *h = strstr(b, "class=\"hours\">");
		if (h && (!next || h < next)) {
			html_between(h + 15, "</div>", o->hours, sizeof o->hours);
			// Solo las horas que lleva («… (223.8 h en el momento de la reseña)» sobra aquí)
			char *par = strchr(o->hours, '(');
			if (par) {
				*par = 0;
				size_t l = strlen(o->hours);
				while (l && o->hours[l - 1] == ' ') o->hours[--l] = 0;
			}
		}
		const char *c = strstr(b, "class=\"content");
		if (c && (!next || c < next) && (c = strchr(c, '>'))) html_between(c + 1, "</div>", o->text, sizeof o->text);
		const char *ps = strstr(b, "class=\"posted\">");
		if (ps && (!next || ps < next)) html_between(ps + 16, "</div>", o->posted, sizeof o->posted);
		if (o->appid) n++;
	}
	return n;
}

int steam_parse_comments(const char *json, ProfileComment *out, int max, int *total)
{
	cJSON *root = cJSON_Parse(json);
	const char *html = jstr(root, "comments_html");
	if (!root || !jbool(root, "success") || !html) {
		cJSON_Delete(root);
		return -1;
	}
	*total = (int)jnum(root, "total_count", 0);
	int n = 0;
	for (const char *c = strstr(html, "class=\"commentthread_comment "); c && n < max; c = strstr(c + 10, "class=\"commentthread_comment ")) {
		const char *next = strstr(c + 10, "class=\"commentthread_comment ");
		ProfileComment *pc = &out[n];
		memset(pc, 0, sizeof *pc);
		// El avatar: la imagen de dentro del enlace al perfil (no la del marco)
		const char *av = strstr(c, "commentthread_comment_avatar");
		const char *link = av ? strstr(av, "<a href=") : NULL;
		const char *img = link ? strstr(link, "<img src=\"") : NULL;
		if (img && (!next || img < next)) {
			img += 10;
			size_t l = strcspn(img, "\"");
			if (l < sizeof pc->avatar) {
				memcpy(pc->avatar, img, l);
				char *fa = strstr(pc->avatar, "avatars.fastly.");
				if (fa) memcpy(fa, "avatars.akamai.", 15);
			}
		}
		const char *mp = strstr(c, "data-miniprofile=\"");
		if (mp && (!next || mp < next)) {
			unsigned long acc = strtoul(mp + 18, NULL, 10);
			if (acc) pc->steamid = 76561197960265728ULL + acc;
		}
		const char *bd = strstr(c, "<bdi>");
		if (bd && (!next || bd < next)) html_between(bd + 5, "</bdi>", pc->author, sizeof pc->author);
		const char *ts = strstr(c, "data-timestamp=\"");
		if (ts && (!next || ts < next)) pc->when = (u32)strtoul(ts + 16, NULL, 10);
		const char *tx = strstr(c, "class=\"commentthread_comment_text\"");
		if (tx && (!next || tx < next)) {
			tx = strchr(tx, '>');
			if (tx) html_between(tx + 1, "</div>", pc->text, sizeof pc->text);
		}
		if (pc->author[0] || pc->steamid) n++;
	}
	cJSON_Delete(root);
	return n;
}

Req *steam_req_post_comment(u64 steamid, const char *text, const char *sessionid, const char *cookies)
{
	char url[160];
	snprintf(url, sizeof url, "https://steamcommunity.com/comment/Profile/post/%llu/-1/", (unsigned long long)steamid);
	char *esc = url_escape(text);
	size_t n = strlen(esc) + 160;
	char *body = malloc(n);
	Req *r = NULL;
	if (body) {
		snprintf(body, n, "comment=%s&count=6&sessionid=%s&feature2=-1", esc, sessionid);
		r = net_request(url, body, cookies);
	}
	free(body);
	free(esc);
	return r;
}

// ----------------------------------------------------------------------------
// Tienda de puntos (ILoyaltyRewardsService)

Req *steam_req_points(u64 steamid, const char *token)
{
	cJSON *in = cJSON_CreateObject();
	char sid[24];
	snprintf(sid, sizeof sid, "%llu", (unsigned long long)steamid);
	cJSON_AddStringToObject(in, "steamid", sid);
	return steam_get_service("ILoyaltyRewardsService/GetSummary", in, token);
}

long long steam_parse_points(const char *json)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	const cJSON *sum = resp ? cJSON_GetObjectItemCaseSensitive(resp, "summary") : NULL;
	long long p = sum ? jbig(sum, "points") : -1;
	cJSON_Delete(root);
	return p;
}

Req *steam_req_point_items(int cls, const char *cursor)
{
	cJSON *in = cJSON_CreateObject();
	cJSON_AddNumberToObject(in, "count", 30);
	if (cursor && *cursor) cJSON_AddStringToObject(in, "cursor", cursor);
	cJSON *c = cJSON_AddArrayToObject(in, "community_item_classes");
	cJSON_AddItemToArray(c, cJSON_CreateNumber(cls));
	cJSON_AddStringToObject(in, "language", g_lang);
	cJSON_AddNumberToObject(in, "sort", 2);   // las más nuevas primero, como al entrar en la tienda
	char *js = cJSON_PrintUnformatted(in);
	char *esc = url_escape(js);
	size_t n = strlen(esc) + 128;
	char *url = malloc(n);
	snprintf(url, n, "https://api.steampowered.com/ILoyaltyRewardsService/QueryRewardItems/v1/?input_json=%s", esc);
	Req *r = net_get(url, LANE_API);
	free(url);
	free(esc);
	free(js);
	cJSON_Delete(in);
	return r;
}

int steam_parse_point_items(const char *json, PointItem *out, int max, char *cursor, size_t cursor_sz, int *total)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return -1;
	}
	copy(cursor, cursor_sz, jstr(resp, "next_cursor"));
	*total = (int)jnum(resp, "total_count", 0);
	int n = 0;
	const cJSON *it;
	cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(resp, "definitions")) {
		if (n >= max) break;
		if (cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(it, "active"))) continue;
		const cJSON *d = cJSON_GetObjectItemCaseSensitive(it, "community_item_data");
		PointItem *p = &out[n];
		memset(p, 0, sizeof *p);
		p->appid = (u32)jnum(it, "appid", 0);
		p->defid = (u32)jnum(it, "defid", 0);
		p->cls = (int)jnum(it, "community_item_class", 0);
		p->cost = jbig(it, "point_cost");
		const char *t = jstr(d, "item_title");
		copy_utf8(p->name, sizeof p->name, t && *t ? t : jstr(d, "item_name"));
		copy_utf8(p->desc, sizeof p->desc, jstr(d, "item_description"));
		// La imagen fija: «large» (en marcos y avatares animados, «small» es la animación, de varios MB)
		const char *im = jstr(d, "item_image_large");
		p->animated = jbool(d, "animated");
		copy(p->img, sizeof p->img, im && *im ? im : jstr(d, "item_image_small"));
		if (p->defid && p->name[0]) n++;
	}
	cJSON_Delete(root);
	return n;
}

Req *steam_req_redeem(u32 defid, const char *token)
{
	cJSON *in = cJSON_CreateObject();
	cJSON_AddNumberToObject(in, "defid", defid);
	return steam_post_service("ILoyaltyRewardsService/RedeemPoints", in, token);
}

// ----------------------------------------------------------------------------
// Inventario (steamcommunity.com/inventory)

Req *steam_req_inventory(u64 steamid, const char *cookies)
{
	char url[160];
	snprintf(url, sizeof url, "https://steamcommunity.com/inventory/%llu/753/6?l=%s&count=500", (unsigned long long)steamid, g_lang);
	return net_request(url, NULL, cookies);
}

int steam_parse_inventory(const char *json, InvItem **out, int *total)
{
	*out = NULL;
	cJSON *root = cJSON_Parse(json);
	if (!root || jnum(root, "success", 0) != 1) {
		cJSON_Delete(root);
		return -1;
	}
	*total = (int)jnum(root, "total_inventory_count", 0);
	const cJSON *assets = cJSON_GetObjectItemCaseSensitive(root, "assets");
	const cJSON *descs = cJSON_GetObjectItemCaseSensitive(root, "descriptions");
	int cap = cJSON_GetArraySize(descs), n = 0;
	*out = calloc(cap ? cap : 1, sizeof **out);
	u64 *keys = calloc(cap ? cap : 1, sizeof *keys);
	const cJSON *d, *a;
	cJSON_ArrayForEach(d, descs) {
		if (!*out || !keys || n >= cap) break;
		InvItem *it = &(*out)[n];
		copy_utf8(it->name, sizeof it->name, jstr(d, "name"));
		copy_utf8(it->type, sizeof it->type, jstr(d, "type"));
		copy(it->icon, sizeof it->icon, jstr(d, "icon_url"));
		keys[n] = strtoull(jstr(d, "classid") ? jstr(d, "classid") : "0", NULL, 10) * 1000003ULL + strtoull(jstr(d, "instanceid") ? jstr(d, "instanceid") : "0", NULL, 10);
		if (it->name[0]) n++;
	}
	// Cuántos tienes de cada uno
	cJSON_ArrayForEach(a, assets) {
		u64 k = strtoull(jstr(a, "classid") ? jstr(a, "classid") : "0", NULL, 10) * 1000003ULL + strtoull(jstr(a, "instanceid") ? jstr(a, "instanceid") : "0", NULL, 10);
		for (int i = 0; i < n; i++)
			if (keys[i] == k) {
				(*out)[i].amount += jstr(a, "amount") ? atoi(jstr(a, "amount")) : 1;
				break;
			}
	}
	free(keys);
	cJSON_Delete(root);
	return n;
}

// ----------------------------------------------------------------------------
// Historial de compras

Req *steam_req_history(const char *cookies)
{
	return net_request("https://store.steampowered.com/account/history/", NULL, cookies);
}

// El texto de la celda <td class="wht_…"> (sin etiquetas), dentro de la fila
static void history_cell(const char *row, const char *row_end, const char *cls, char *out, size_t n)
{
	out[0] = 0;
	char key[48];
	snprintf(key, sizeof key, "class=\"%s", cls);
	const char *c = strstr(row, key);
	if (!c || c > row_end) return;
	c = strchr(c, '>');
	const char *e = c ? strstr(c, "</td>") : NULL;
	if (!e) return;
	// Cada <div> de la celda en su línea (el método de pago va en otro <div>)
	size_t l = (size_t)(e - c - 1);
	char *raw = malloc(l * 2 + 16);
	if (!raw) return;
	size_t o = 0;
	for (const char *q = c + 1; q < e; q++) {
		if (!strncmp(q, "</div>", 6)) raw[o++] = '\n';
		raw[o++] = *q;
	}
	memcpy(raw + o, "</td>", 6);
	html_between(raw, "</td>", out, n);
	free(raw);
}

int steam_parse_history(const char *html, Purchase *out, int max)
{
	if (!html || !strstr(html, "wallet_history_table")) return -1;
	int n = 0;
	for (const char *r = strstr(html, "wallet_table_row"); r && n < max; r = strstr(r + 16, "wallet_table_row")) {
		const char *re = strstr(r, "</tr>");
		if (!re) break;
		Purchase *p = &out[n];
		history_cell(r, re, "wht_date", p->date, sizeof p->date);
		if (!p->date[0]) continue;
		// Varios juegos en la misma compra: uno por línea → separados por comas
		char items[400], joined[400];
		history_cell(r, re, "wht_items", items, sizeof items);
		size_t o = 0;
		for (char *line = strtok(items, "\n"); line && o < sizeof joined - 4; line = strtok(NULL, "\n")) {
			if (!*line) continue;
			o += snprintf(joined + o, sizeof joined - o, "%s%s", o ? ", " : "", line);
		}
		joined[o < sizeof joined ? o : sizeof joined - 1] = 0;
		copy_utf8(p->items, sizeof p->items, joined);
		char type[120];
		history_cell(r, re, "wht_type", type, sizeof type);
		char *nl = strchr(type, '\n');
		if (nl) *nl = 0;
		copy_utf8(p->type, sizeof p->type, type);
		history_cell(r, re, "wht_total", p->total, sizeof p->total);
		nl = strchr(p->total, '\n');
		if (nl) *nl = 0;
		n++;
		r = re;
	}
	return n;
}

Req *steam_req_badges(u64 steamid, const char *token)
{
	char url[1300];
	snprintf(url, sizeof url, "https://api.steampowered.com/IPlayerService/GetBadges/v1/?access_token=%s&steamid=%llu", token, (unsigned long long)steamid);
	return net_get(url, LANE_API);
}

bool steam_parse_badges(const char *json, SteamXp *out)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	bool ok = resp && cJSON_GetObjectItemCaseSensitive(resp, "player_xp");
	if (ok) {
		out->level = (int)jnum(resp, "player_level", 0);
		out->xp = (int)jnum(resp, "player_xp", 0);
		out->xp_next = (int)jnum(resp, "player_xp_needed_to_level_up", 0);
		out->xp_cur = (int)jnum(resp, "player_xp_needed_current_level", 0);
		out->nbadges = cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(resp, "badges"));
	}
	cJSON_Delete(root);
	return ok;
}

// ----------------------------------------------------------------------------
// Premios (ILoyaltyRewardsService)

Req *steam_req_award_config(void)
{
	return net_get("https://api.steampowered.com/ILoyaltyRewardsService/GetReactionConfig/v1/", LANE_API);
}

int steam_parse_award_config(const char *json, SteamAward *out, int max)
{
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return -1;
	}
	int n = 0;
	const cJSON *it, *t;
	cJSON_ArrayForEach(it, cJSON_GetObjectItemCaseSensitive(resp, "reactions")) {
		if (n >= max || !jbool(it, "purchaseable")) continue;
		bool review = false;
		cJSON_ArrayForEach(t, cJSON_GetObjectItemCaseSensitive(it, "valid_target_types"))
			review |= cJSON_IsNumber(t) && t->valueint == 1;
		if (!review) continue;
		out[n].id = (u32)jnum(it, "reactionid", 0);
		out[n].cost = (int)jnum(it, "points_cost", 0);
		copy_utf8(out[n].title, sizeof out[n].title, jstr(it, "localized_title"));
		n++;
	}
	cJSON_Delete(root);
	return n;
}

Req *steam_req_award(int target_type, u64 targetid, u32 reactionid, const char *token)
{
	cJSON *in = cJSON_CreateObject();
	char t[24];
	snprintf(t, sizeof t, "%llu", (unsigned long long)targetid);
	cJSON_AddNumberToObject(in, "target_type", target_type);
	cJSON_AddStringToObject(in, "targetid", t);
	cJSON_AddNumberToObject(in, "reactionid", reactionid);
	return steam_post_service("ILoyaltyRewardsService/AddReaction", in, token);
}

Req *steam_req_family(u64 gid, const char *token)
{
	cJSON *in = cJSON_CreateObject();
	char t[24];
	snprintf(t, sizeof t, "%llu", (unsigned long long)gid);
	cJSON_AddStringToObject(in, "family_groupid", t);
	return steam_get_service("IFamilyGroupsService/GetFamilyGroup", in, token);
}

bool steam_parse_family(const char *json, SteamFamily *out)
{
	memset(out, 0, sizeof *out);
	cJSON *root = cJSON_Parse(json);
	const cJSON *resp = root ? cJSON_GetObjectItemCaseSensitive(root, "response") : NULL;
	if (!resp) {
		cJSON_Delete(root);
		return false;
	}
	copy_utf8(out->name, sizeof out->name, jstr(resp, "name"));
	const cJSON *m;
	cJSON_ArrayForEach(m, cJSON_GetObjectItemCaseSensitive(resp, "members")) {
		if (out->n >= 8) break;
		u64 id = jid(m, "steamid");
		if (!id) continue;
		out->member[out->n] = id;
		out->role[out->n] = (int)jnum(m, "role", 1);
		out->n++;
	}
	cJSON_Delete(root);
	return out->n > 0;
}
