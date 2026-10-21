// Pruebas de steam.c con respuestas reales guardadas en data/
#include "steam.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern char last_url[];
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FALLO %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)
static char *slurp(const char *p) { FILE *f = fopen(p, "rb"); if (!f) { printf("falta %s\n", p); exit(2); }
	fseek(f, 0, SEEK_END); long n = ftell(f); rewind(f); char *b = malloc(n + 1); fread(b, 1, n, f); b[n] = 0; fclose(f); return b; }
int main(void) {
	steam_set_context("spanish", "ES");
	// Ofertas
	RowList l = {0};
	CHECK(steam_parse_rows(slurp("data/offers.json"), &l, false));
	printf("ofertas: %d de %d\n", l.count, l.total);
	CHECK(l.count == 30 && l.total > 1000);
	for (int i = 0; i < 5; i++) printf("  %u %s -%d%% %s (%s) %s %d%%\n", l.rows[i].appid, l.rows[i].name, l.rows[i].disc, l.rows[i].price, l.rows[i].orig, l.rows[i].review, l.rows[i].review_pct);
	CHECK(l.rows[0].appid && l.rows[0].name[0] && l.rows[0].disc > 0 && l.rows[0].price[0] && l.rows[0].orig[0]);
	CHECK(steam_parse_rows(slurp("data/offers2.json"), &l, true));
	CHECK(l.count == 60);
	// Ficha
	AppInfo a;
	CHECK(steam_parse_appdetails(slurp("data/app_1091500.json"), 1091500, &a));
	printf("ficha: %s | %s | %s | %s %s (%d) desc=%d metacritic=%d logros=%d shots=%d dlc=%d feats=%x\n",
		a.name, a.genres[0], a.release, a.rating_sys, a.rating, a.rating_age, a.ndesc, a.metacritic, a.achievements, a.nshots, a.ndlc, a.feats);
	for (int i = 0; i < a.ndesc; i++) printf("  desc %s\n", a.descriptors[i]);
	for (int i = 0; i < a.nmin; i++) printf("  min %s = %s\n", a.rmin[i].key, a.rmin[i].val);
	for (int i = 0; i < a.nrec; i++) printf("  rec %s = %s\n", a.rrec[i].key, a.rrec[i].val);
	printf("  precio %s (%s) -%d%%\n", a.price, a.orig, a.disc);
	CHECK(!strcmp(a.rating_sys, "PEGI") && !strcmp(a.rating, "18"));
	CHECK(a.feats & FEAT_FAMILY);
	CHECK(a.feats & FEAT_CLOUD);
	CHECK(a.nmin >= 5 && a.nrec >= 5);
	CHECK(a.ndlc == 3);
	CHECK(a.nshots > 10);
	CHECK(a.desc && strstr(a.desc, "Night City"));
	CHECK(!a.is_dlc && a.fullgame == 0);
	// Un DLC sabe cuál es su juego base
	AppInfo dl;
	CHECK(steam_parse_appdetails(slurp("data/app_2138330.json"), 2138330, &dl));
	printf("DLC: %s -> necesita %u %s\n", dl.name, dl.fullgame, dl.fullgame_name);
	CHECK(dl.is_dlc && dl.fullgame == 1091500 && !strcmp(dl.fullgame_name, "Cyberpunk 2077"));
	// Con país de EE. UU. sale ESRB
	steam_set_context("english", "US");
	AppInfo b;
	CHECK(steam_parse_appdetails(slurp("data/app_1091500.json"), 1091500, &b));
	printf("EE. UU.: %s %s\n", b.rating_sys, b.rating);
	CHECK(!strcmp(b.rating_sys, "ESRB"));
	steam_set_context("spanish", "ES");
	// Resumen de reseñas
	CHECK(steam_parse_review_summary(slurp("data/items_self.json"), &a));
	printf("reseñas: %s %d%% de %d\n", a.review_label, a.review_pct, a.review_count);
	CHECK(a.review_count > 100000 && a.review_pct > 50);
	// Reseñas
	Review rv[8]; char cur[128];
	int n = steam_parse_reviews(slurp("data/reviews.json"), rv, 8, cur, sizeof cur);
	printf("reseñas: %d, cursor=%s\n", n, cur);
	CHECK(n > 0 && cur[0]);
	for (int i = 0; i < n && i < 3; i++) printf("  %s %dh: %.90s\n", rv[i].voted_up ? "+" : "-", rv[i].hours, rv[i].text);
	for (int i = 0; i < n; i++) { CHECK(!strchr(rv[i].text, '\n')); CHECK(!strstr(rv[i].text, "[h1]")); }
	// Similares
	RowList s = {0};
	CHECK(steam_parse_rows(slurp("data/morelike.json"), &s, false));
	printf("similares: %d, primero %s %s\n", s.count, s.count ? s.rows[0].name : "", s.count ? s.rows[0].review : "");
	CHECK(s.count >= 5);
	// URLs
	steam_req_offers(30, 30);
	printf("url ofertas: %.140s…\n", last_url);
	CHECK(strstr(last_url, "IStoreQueryService/Query/v1/?input_json=%7B"));
	steam_req_appdetails(10);
	CHECK(!strcmp(last_url, "https://store.steampowered.com/api/appdetails?appids=10&cc=ES&l=spanish"));
	printf(fails ? "\n%d FALLOS\n" : "\nTODO OK\n", fails);
	return fails != 0;
}
