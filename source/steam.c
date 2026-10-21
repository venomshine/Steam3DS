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

static void copy(char *dst, size_t n, const char *src)
{
	snprintf(dst, n, "%s", src ? src : "");
}

// Recorta en n bytes sin partir un carácter UTF-8
static void copy_utf8(char *dst, size_t n, const char *src)
{
	if (!src) { dst[0] = 0; return; }
	size_t len = strlen(src);
	if (len < n) { memcpy(dst, src, len + 1); return; }
	size_t cut = n - 1;
	while (cut > 0 && ((unsigned char)src[cut] & 0xC0) == 0x80) cut--;
	memcpy(dst, src, cut);
	dst[cut] = 0;
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
	row->appid = (u32)jnum(it, "appid", jnum(it, "id", 0));
	copy_utf8(row->name, sizeof row->name, jstr(it, "name"));
	const cJSON *bp = cJSON_GetObjectItemCaseSensitive(it, "best_purchase_option");
	if (bp) {
		copy(row->price, sizeof row->price, jstr(bp, "formatted_final_price"));
		row->disc = (int)jnum(bp, "discount_pct", 0);
		if (row->disc > 0) copy(row->orig, sizeof row->orig, jstr(bp, "formatted_original_price"));
		if (jnum(bp, "final_price_in_cents", -1) == 0) row->free = true;
	}
	if (jbool(it, "is_free")) row->free = true;
	row->type = (int)jnum(it, "type", 0);
	if (row->type == 1) row->free = true;          // las demos son gratis
	const cJSON *rel = cJSON_GetObjectItemCaseSensitive(it, "release");
	if (rel && jbool(rel, "is_coming_soon")) row->coming_soon = true;
	if (rel && jbool(rel, "is_early_access")) row->early = true;
	const cJSON *rv = cJSON_GetObjectItemCaseSensitive(it, "reviews");
	const cJSON *sf = rv ? cJSON_GetObjectItemCaseSensitive(rv, "summary_filtered") : NULL;
	if (sf) {
		copy_utf8(row->review, sizeof row->review, jstr(sf, "review_score_label"));
		row->review_pct = (int)jnum(sf, "percent_positive", 0);
	}
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
	const cJSON *it;
	cJSON_ArrayForEach(it, items) {
		if (!jstr(it, "name")) continue;
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
	return service_call("IStoreQueryService/Query", in);
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
	return service_call("IStoreQueryService/SearchSuggestions", in);
}

Req *steam_req_offers(int start, int count)
{
	return steam_req_query(Q_OFFERS, start, count);
}

Req *steam_req_items(const u32 *ids, int n)
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
	return service_call("IStoreBrowseService/GetItems", in);
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
	return service_call("IStoreQueryService/MoreLikeThis", in);
}

Req *steam_req_country(void)
{
	return net_head("https://store.steampowered.com/about/");
}

// ----------------------------------------------------------------------------
// Ficha

Req *steam_req_appdetails(u32 appid)
{
	char url[160];
	snprintf(url, sizeof url, "https://store.steampowered.com/api/appdetails?appids=%lu&cc=%s&l=%s",
		(unsigned long)appid, g_country, g_lang);
	return net_get(url, LANE_API);
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
				{"&#39;", "'"}, {"&reg;", "®"}, {"&trade;", "™"}, {"&copy;", "©"},
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
		if (a->desc) html_to_lines(sd, a->desc, n);
	}
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

Req *steam_req_reviews(u32 appid, const char *cursor)
{
	char *cur = url_escape(cursor && *cursor ? cursor : "*");
	char url[320];
	snprintf(url, sizeof url,
		"https://store.steampowered.com/appreviews/%lu?json=1&language=%s&filter=all&review_type=all&purchase_type=all&num_per_page=8&cursor=%s",
		(unsigned long)appid, g_lang, cur);
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
	cJSON_ArrayForEach(r, cJSON_GetObjectItemCaseSensitive(root, "reviews")) {
		if (n >= max) break;
		const char *t = jstr(r, "review");
		if (!t) continue;
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

static int cmp_last_played(const void *a, const void *b)
{
	const StoreRow *x = a, *y = b;
	if (x->last_played != y->last_played) return x->last_played < y->last_played ? 1 : -1;
	return y->minutes - x->minutes;
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
		if (row->appid && row->name[0]) l->count++;
	}
	qsort(l->rows, l->count, sizeof(StoreRow), cmp_last_played);
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
	for (int i = 0; i < a->nshots; i++) free(a->shots[i]);
	a->nshots = 0;
}

void rowlist_free(RowList *l)
{
	free(l->rows);
	if (l->req) net_release(l->req);
	memset(l, 0, sizeof *l);
}
