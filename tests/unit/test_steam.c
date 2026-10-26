// Pruebas de steam.c con respuestas reales guardadas en data/
#include "steam.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern char last_url[];
extern char last_body[];
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
	steam_req_query(Q_EARLY, 0, 30);
	CHECK(strstr(last_url, "493") && strstr(last_url, "sort%22%3A12"));
	steam_req_query(Q_DEMOS, 0, 30);
	CHECK(strstr(last_url, "include_demos"));
	steam_req_query(Q_NEW, 0, 10);
	CHECK(strstr(last_url, "release_date_type%22%3A1"));
	steam_req_search("hollow knight");
	CHECK(strstr(last_url, "SearchSuggestions") && strstr(last_url, "hollow%20knight"));
	// La búsqueda devuelve filas con tipo (juego/DLC/demo) y si siguen en acceso anticipado
	RowList sr = {0};
	CHECK(steam_parse_rows(slurp("data/search.json"), &sr, false));
	printf("búsqueda: %d, %s tipo=%d\n", sr.count, sr.count ? sr.rows[0].name : "", sr.count ? sr.rows[0].type : -1);
	CHECK(sr.count > 0);
	// Biblioteca: ordenada por nombre, y con el icono de cada juego
	RowList ow = {0};
	CHECK(steam_parse_owned(slurp("data/owned.json"), &ow));
	printf("biblioteca: %d, primero %s (%d min)\n", ow.count, ow.rows[0].name, ow.rows[0].minutes);
	CHECK(ow.count == 3 && ow.rows[0].appid == 730 && ow.rows[1].appid == 1091500 && ow.rows[2].appid == 570);
	char icu[160];
	steam_icon_url(&ow.rows[0], icu, sizeof icu);
	CHECK(!strcmp(icu, "https://shared.akamai.steamstatic.com/community_assets/images/apps/730/8dbc71957312bbd3baea65848b545be9eae2a355.jpg"));
	// Los nombres por GetItems traen el icono si se pide (include_assets)
	RowList ia = {0};
	CHECK(steam_parse_rows("{\"response\":{\"store_items\":[{\"appid\":367520,\"name\":\"Hollow Knight\",\"assets\":{\"community_icon\":\"f6ab055c2366237200b1a31cccbd6cf81e436d72\"}}]}}", &ia, false));
	CHECK(ia.count == 1 && !strcmp(ia.rows[0].icon, "f6ab055c2366237200b1a31cccbd6cf81e436d72"));
	// El precio en céntimos (para la barra de precio de Buscar); -1 si no se vende
	RowList pc = {0};
	CHECK(steam_parse_rows(slurp("data/items_self.json"), &pc, false));
	CHECK(pc.count == 1 && pc.rows[0].cents == 1799 && !pc.rows[0].free);
	rowlist_free(&pc);
	CHECK(steam_parse_rows("{\"response\":{\"store_items\":[{\"appid\":4115450,\"name\":\"Phantom Blade Zero\"}]}}", &pc, false));
	CHECK(pc.count == 1 && pc.rows[0].cents == -1);
	rowlist_free(&pc);
	steam_req_query(Q_BROWSE_FREE, 0, 30);
	CHECK(strstr(last_url, "only_free_items") != NULL);
	// Lo que no está en la tienda llega sin nombre y con appid 0 (respuesta real de GetItems para 228980)
	RowList inv = {0};
	CHECK(steam_parse_rows("{\"response\":{\"store_items\":[{\"item_type\":0,\"id\":228980,\"success\":15,\"visible\":false,\"name\":\"\",\"appid\":0},{\"appid\":730,\"name\":\"Counter-Strike 2\"}]}}", &inv, false));
	CHECK(inv.count == 1 && inv.rows[0].appid == 730);
	rowlist_free(&inv);
	steam_req_items((u32[]){367520}, 1, true);
	CHECK(strstr(last_url, "include_assets") != NULL);
	// Deseados: por prioridad, y la 0 («sin ordenar») al final
	u32 *ids;
	int nw = steam_parse_wishlist(slurp("data/wishlist.json"), &ids);
	printf("deseados: %d (%u %u %u %u)\n", nw, ids[0], ids[1], ids[2], ids[3]);
	CHECK(nw == 4 && ids[0] == 2138330 && ids[1] == 1174180 && ids[2] == 1716740 && ids[3] == 668580);
	CHECK(steam_parse_wishlist("{\"response\":{}}", &ids) == 0);
	CHECK(steam_parse_wishlist("basura", &ids) == -1);
	steam_req_appdetails(10);
	CHECK(!strcmp(last_url, "https://store.steampowered.com/api/appdetails?appids=10&cc=ES&l=spanish"));
	// Formas de comprar: Cyberpunk tiene el juego (paquete) y la Ultimate (lote)
	BuyOpt bo[8];
	int nb = steam_parse_buyopts(slurp("data/purchase_options.json"), 1091500, bo, 8);
	printf("ediciones: %d (%s %s, %s %s)\n", nb, bo[0].name, bo[0].price, bo[1].name, bo[1].price);
	CHECK(nb == 2 && bo[0].packageid == 367653 && !bo[0].bundleid && bo[0].disc == 70 && bo[0].orig[0]);
	CHECK(bo[1].bundleid == 32470 && !bo[1].packageid);
	CHECK(steam_parse_buyopts(slurp("data/purchase_options.json"), 2507950, bo, 8) == 0);   // gratis, sin opciones
	CHECK(steam_parse_buyopts("basura", 1, bo, 8) == -1);
	// Carrito: líneas con id de 64 bits en texto, y el total
	CartInfo cart = {0};
	CHECK(steam_parse_cart(slurp("data/cart.json"), &cart));
	printf("carrito: %d líneas, total %s\n", cart.n, cart.subtotal);
	CHECK(cart.n == 2 && cart.line[0].line_id == 9000000000000001ULL && cart.line[1].bundleid == 32470);
	CHECK(!strcmp(cart.subtotal, "51,09€") && !strcmp(cart.line[1].price, "33,10€"));
	steam_parse_cart_names(slurp("data/items_pkg.json"), &cart);
	CHECK(!strcmp(cart.line[0].name, "Cyberpunk 2077") && cart.line[0].appid == 1091500);
	CHECK(!strcmp(cart.line[1].name, "Cyberpunk 2077: Ultimate Edition") && cart.line[1].appid == 1091500 && cart.line[1].disc == 60);
	// Al volver a leer el carrito no se pierden los nombres ya conocidos
	CHECK(steam_parse_cart(slurp("data/cart.json"), &cart) && !strcmp(cart.line[1].name, "Cyberpunk 2077: Ultimate Edition"));
	CHECK(steam_parse_cart("{\"response\":{\"cart\":{}}}", &cart) && cart.n == 0);
	// POST con sesión: el token va en la URL y la entrada en el cuerpo
	cJSON *in = cJSON_CreateObject();
	cJSON_AddNumberToObject(in, "appid", 570);
	steam_post_service("IWishlistService/AddToWishlist", in, "TOKEN");
	CHECK(!strcmp(last_url, "https://api.steampowered.com/IWishlistService/AddToWishlist/v1/?access_token=TOKEN"));
	CHECK(!strcmp(last_body, "input_json=%7B%22appid%22%3A570%7D"));
	// Página de pago: cartera en la lista, tarjeta guardada (solo los 4 últimos) y país
	CheckoutPage cp;
	const char *html = "<script>var g_sessionID = \"abc123\";</script><input type=\"hidden\" id=\"payment_method\" value=\"\">"
		"<ul id=\"payment_method_droplist\"><li><a id=\"steamaccount\" href=\"#\">Cartera</a></li>"
		"<li><a id=\"storedcreditcard\">Visa</a></li></ul>"
		"<input type=\"hidden\" name=\"stored_payment_method\" id=\"stored_payment_method\" value=\"visa\">"
		"<input value=\"776655\" type=\"hidden\" id=\"stored_card_id\">"
		"<input type=\"hidden\" id=\"stored_card_number\" value=\"************4821\">"
		"<input type=\"hidden\" id=\"billing_country\" value=\"ES\">";
	CHECK(steam_parse_checkout_page(html, &cp));
	printf("pago: sesion %s cartera %d tarjeta %s ...%s gid %s pais %s\n", cp.sessionid, cp.wallet, cp.card_type, cp.card_last4, cp.card_gid, cp.country);
	CHECK(!strcmp(cp.sessionid, "abc123") && cp.wallet && cp.card && !strcmp(cp.card_type, "visa"));
	CHECK(!strcmp(cp.card_last4, "4821") && !strcmp(cp.card_gid, "776655") && !strcmp(cp.country, "ES"));
	// Sin tarjeta guardada ni cartera, y la página de iniciar sesión (no es la de pago)
	CHECK(steam_parse_checkout_page("<input id=\"payment_method\"><ul id=\"payment_method_droplist\"><li><a id=\"paypal\">P</a></li></ul>"
		"<input id=\"stored_card_id\" value=\"\">", &cp) && !cp.card && !cp.wallet);
	CHECK(!steam_parse_checkout_page("<html>Iniciar sesión</html>", &cp));
	WalletInfo wi;
	CHECK(steam_parse_wallet("{\"response\":{\"has_wallet\":true,\"balance\":\"3210\",\"formatted_balance\":\"32,10€\",\"wallet_country_code\":\"ES\"}}", &wi));
	CHECK(wi.has_wallet && wi.balance == 3210 && !strcmp(wi.formatted, "32,10€") && !strcmp(wi.country, "ES"));
	// Página de pago: las casillas que hay que marcar, con el texto que da Steam
	CheckoutPage cb;
	CHECK(steam_parse_checkout_page("<input id=\"payment_method\"><input type=\"checkbox\" id=\"accept_ssa\">"
		"<label for=\"accept_ssa\">Acepto las condiciones del <a href=\"#\">Acuerdo de Suscriptor a Steam</a>\n (última actualización: 26 SEP 2024).</label>", &cb));
	CHECK(!strcmp(cb.ssa_text, "Acepto las condiciones del Acuerdo de Suscriptor a Steam (última actualización: 26 SEP 2024).") && !cb.withdrawal);
	CHECK(steam_parse_checkout_page("<input id=\"payment_method\"><input id=\"accept_right_withdrawal\"><label for=\"accept_right_withdrawal\">Renuncio&nbsp;al desistimiento.</label>", &cb));
	CHECK(cb.withdrawal && !strcmp(cb.withdrawal_text, "Renuncio al desistimiento.") && !cb.ssa_text[0]);
	printf(fails ? "\n%d FALLOS\n" : "\nTODO OK\n", fails);
	return fails != 0;
}
