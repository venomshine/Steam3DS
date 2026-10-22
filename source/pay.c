#include "pay.h"
#include "app.h"
#include "auth.h"
#include "cJSON.h"
#include "net.h"
#include "steam.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECKOUT "https://checkout.steampowered.com/checkout/"
#define POLL_MS 4000
#define POLL_MAX (15 * 60 * 1000)   // el banco puede tardar: se espera hasta 15 min

static PayInfo P;
static CheckoutPage page;
static WalletInfo wallet;
static Req *page_req, *wallet_req, *req, *auth_req;
static char transid[32];
static char cvv[8];
static char cookies[1400];
static int calls;
static u64 next_poll, poll_start;
static enum { ST_NONE, ST_INIT, ST_PRICE, ST_FINAL, ST_STATUS } step;

static void set_state(PayState s)
{
	P.state = s;
	P.since = osGetTime();
	netlog("pago: estado %d", s);
}

static void wipe_cvv(void)
{
	memset(cvv, 0, sizeof cvv);
	P.cvv_set = false;
}

static void release_all(void)
{
	Req **all[] = {&page_req, &wallet_req, &req, &auth_req};
	for (size_t i = 0; i < sizeof all / sizeof *all; i++)
		if (*all[i]) net_release(*all[i]);
	page_req = wallet_req = req = auth_req = NULL;
}

static void fail(int detail, const char *err)
{
	P.detail = detail;
	snprintf(P.err, sizeof P.err, "%s", err ? err : "");
	wipe_cvv();
	set_state(PAY_FAILED);
	netlog("pago: fallo (detalle %d, %s)", detail, P.err);
}

const PayInfo *pay_info(void) { return &P; }
bool pay_busy(void) { return P.state == PAY_PAYING || P.state == PAY_BANK; }

// Cabeceras de las peticiones al checkout: cookies de la sesión, y de dónde viene (como la web)
static void build_cookies(void)
{
	char c[1200];
	cookies[0] = 0;
	if (!auth_web_cookies("checkout.steampowered.com", c, sizeof c)) return;
	snprintf(cookies, sizeof cookies, "%s\nOrigin: https://checkout.steampowered.com\nReferer: " CHECKOUT "?accountcart=1", c);
	memset(c, 0, sizeof c);
}

static Req *co_get(const char *path_query)
{
	char url[256];
	snprintf(url, sizeof url, CHECKOUT "%s", path_query);
	return net_request(url, NULL, cookies);
}

static Req *co_post(const char *path, const char *body)
{
	char url[160];
	snprintf(url, sizeof url, CHECKOUT "%s", path);
	return net_request(url, body, cookies);
}

void pay_open(void)
{
	release_all();
	wipe_cvv();
	memset(&P, 0, sizeof P);
	transid[0] = 0;
	step = ST_NONE;
	calls = 0;
	build_cookies();
	if (!cookies[0]) {
		fail(0, "sin cookies del checkout");
		return;
	}
	set_state(PAY_LOADING);
	page_req = co_get("?accountcart=1");
	char url[1300];
	// Solo por POST (por GET Steam responde 405)
	snprintf(url, sizeof url, "https://api.steampowered.com/IUserAccountService/GetClientWalletDetails/v1/?access_token=%s", auth_token());
	wallet_req = net_post(url, "include_formatted_balance=1");
	memset(url, 0, sizeof url);
}

static void cancel_transaction(void)
{
	if (!transid[0]) return;
	char body[96];
	snprintf(body, sizeof body, "transid=%s&sessionid=%s", transid, auth_sessionid());
	net_release(co_post("canceltransaction/", body));
	netlog("pago: compra a medias cancelada");
	transid[0] = 0;
}

void pay_close(void)
{
	if (pay_busy()) return;   // ya se mandó el pago: se deja terminar
	if (P.state == PAY_PREPARING || P.state == PAY_REVIEW) cancel_transaction();
	release_all();
	wipe_cvv();
	memset(&P, 0, sizeof P);
}

void pay_choose(PayMethod m)
{
	if (P.state != PAY_CHOOSE) return;
	if ((m == PM_WALLET && !P.wallet) || (m == PM_CARD && !P.card)) return;
	P.method = m;
	char *country = url_escape(page.country[0] ? page.country : wallet.country[0] ? wallet.country : steam_country());
	char body[1400];
	// Igual que la web con un método guardado: sin dirección ni datos de tarjeta
	snprintf(body, sizeof body,
		"gidShoppingCart=-1&gidReplayOfTransID=-1&bUseAccountCart=1&PaymentMethod=%s&abortPendingTransactions=0"
		"&bHasCardInfo=0&CardNumber=&CardExpirationYear=&CardExpirationMonth="
		"&FirstName=&LastName=&Address=&AddressTwo=&Country=%s&City=&State=&PostalCode=&Phone="
		"&ShippingFirstName=&ShippingLastName=&ShippingAddress=&ShippingAddressTwo=&ShippingCountry=&ShippingCity="
		"&ShippingState=&ShippingPostalCode=&ShippingPhone="
		"&bIsGift=0&GifteeAccountID=0&GifteeEmail=&GifteeName=&GiftMessage=&Sentiment=&Signature=&ScheduledSendOnDate=0"
		"&BankAccount=&BankCode=&BankIBAN=&BankBIC=&TPBankID=&BankAccountID="
		"&bSaveBillingAddress=0&gidPaymentID=%s&bUseRemainingSteamAccount=0&bPreAuthOnly=0&sessionid=%s",
		m == PM_WALLET ? "steamaccount" : page.card_type, country, m == PM_CARD ? page.card_gid : "", auth_sessionid());
	free(country);
	req = co_post("inittransaction/", body);
	memset(body, 0, sizeof body);
	step = ST_INIT;
	set_state(PAY_PREPARING);
}

void pay_set_cvv(const char *s)
{
	wipe_cvv();
	size_t n = 0;
	for (; s && s[n] && n < sizeof cvv - 1; n++) {
		if (s[n] < '0' || s[n] > '9') { wipe_cvv(); return; }
		cvv[n] = s[n];
	}
	P.cvv_set = n >= 3;
	if (!P.cvv_set) wipe_cvv();
}

void pay_confirm(void)
{
	if (P.state != PAY_REVIEW || P.short_wallet || (P.need_cvv && !P.cvv_set)) return;
	char body[256];
	snprintf(body, sizeof body, "transid=%s&CardCVV2=%s&browserInfo=%s&sessionid=%s", transid, P.need_cvv ? cvv : "",
		"%7B%22language%22%3A%22es-ES%22%2C%22javaEnabled%22%3A%22false%22%2C%22colorDepth%22%3A24%2C%22screenHeight%22%3A240%2C%22screenWidth%22%3A400%7D",
		auth_sessionid());
	req = co_post("finalizetransaction/", body);
	memset(body, 0, sizeof body);
	wipe_cvv();
	step = ST_FINAL;
	poll_start = osGetTime();
	set_state(PAY_PAYING);
}

// ----------------------------------------------------------------------------

static int jint(const cJSON *o, const char *k)
{
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	if (cJSON_IsNumber(v)) return v->valueint;
	if (cJSON_IsString(v)) return atoi(v->valuestring);
	return 0;
}

static long long jll(const cJSON *o, const char *k)
{
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	if (cJSON_IsNumber(v)) return (long long)v->valuedouble;
	if (cJSON_IsString(v)) return strtoll(v->valuestring, NULL, 10);
	return 0;
}

static void jcopy(char *dst, size_t n, const cJSON *o, const char *k)
{
	const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, k);
	if (cJSON_IsString(v)) snprintf(dst, n, "%s", v->valuestring);
	else if (cJSON_IsNumber(v)) snprintf(dst, n, "%.0f", v->valuedouble);
}

static void done_ok(const cJSON *r)
{
	const cJSON *rc = cJSON_GetObjectItemCaseSensitive(r, "purchasereceipt");
	jcopy(P.receipt_total, sizeof P.receipt_total, rc, "formattedTotal");
	jcopy(P.receipt_id, sizeof P.receipt_id, rc, "transactionid");
	if (!P.receipt_total[0]) snprintf(P.receipt_total, sizeof P.receipt_total, "%s", P.total_fmt);
	transid[0] = 0;
	set_state(PAY_DONE);
	netlog("pago: hecho%s", P.approval ? " (pendiente de aprobacion)" : "");
}

// Lo que dice Steam del pago (finalizetransaction o transactionstatus)
static void on_status(const cJSON *r, u64 now)
{
	int success = jint(r, "success"), detail = jint(r, "purchaseresultdetail");
	if (success == 1 || (success == 22 && detail == 29)) {
		P.approval = success == 22;
		done_ok(r);
	} else if (success == 22 && detail == 66) {
		P.pending = true;
		done_ok(r);
	} else if (success == 22 && detail == 86) {
		// El banco quiere verificar: Steam tiene una página para hacerlo en otra ventana
		const cJSON *ad = cJSON_GetObjectItemCaseSensitive(r, "authenticationdetails");
		int s3 = ad ? jint(ad, "threed_secure_step") : 0;
		if (!ad && !auth_req) {
			char q[96];
			snprintf(q, sizeof q, "authenticationdetails/?transid=%s", transid);
			auth_req = co_get(q);
		}
		if (s3 == 3 || s3 == 4) {
			fail(0, "verificacion rechazada");
			return;
		}
		if (s3 == 2 && P.state != PAY_BANK) {
			snprintf(P.bank_url, sizeof P.bank_url, CHECKOUT "beginauthentication/?transid=%s", transid);
			set_state(PAY_BANK);
		}
		next_poll = now + POLL_MS;
	} else if (success == 22) {
		next_poll = now + (P.state == PAY_BANK ? POLL_MS : 2000);
	} else {
		// Steam a veces explica el motivo (es lo que enseña su web)
		const cJSON *d = cJSON_GetObjectItemCaseSensitive(r, "errorDescription");
		P.err_desc[0] = 0;
		if (cJSON_IsString(d)) {
			size_t n = 0;
			bool tag = false;
			for (const char *c = d->valuestring; *c && n < sizeof P.err_desc - 1; c++) {
				if (*c == '<') { tag = true; if (n && P.err_desc[n - 1] != ' ') P.err_desc[n++] = ' '; continue; }
				if (*c == '>') { tag = false; continue; }
				if (!tag) P.err_desc[n++] = *c;
			}
			while (n && P.err_desc[n - 1] == ' ') n--;
			P.err_desc[n] = 0;
		}
		fail(detail, "");
	}
}

void pay_update(void)
{
	u64 now = osGetTime();
	cJSON *root = NULL;

	// 1) Página de pago y cartera
	if (P.state == PAY_LOADING && page_req && page_req->state >= REQ_DONE && wallet_req && wallet_req->state >= REQ_DONE) {
		bool okp = page_req->state == REQ_DONE && steam_parse_checkout_page(page_req->data, &page);
		bool okw = wallet_req->state == REQ_DONE && steam_parse_wallet(wallet_req->data, &wallet);
		netlog("pago: pagina %s (cartera en lista %d, tarjeta %d), cartera %s", okp ? "ok" : "mal", page.wallet, page.card,
			okw ? "ok" : wallet_req->err);
		if (okp) {
			P.wallet = page.wallet || (okw && wallet.has_wallet && wallet.balance > 0);
			P.wallet_cents = wallet.balance;
			snprintf(P.wallet_fmt, sizeof P.wallet_fmt, "%s", wallet.formatted);
			P.card = page.card;
			snprintf(P.card_type, sizeof P.card_type, "%s", page.card_type);
			snprintf(P.card_last4, sizeof P.card_last4, "%s", page.card_last4);
			set_state(PAY_CHOOSE);
		} else {
			fail(0, page_req->state == REQ_DONE ? "pagina de pago no reconocida" : page_req->err);
		}
		net_release(page_req);
		net_release(wallet_req);
		page_req = wallet_req = NULL;
	}

	// 2) Pasos de la compra
	if (req && req->state >= REQ_DONE) {
		root = req->state == REQ_DONE && req->data ? cJSON_Parse(req->data) : NULL;
		bool net_fail = !root;
		// Lo que responde Steam en cada paso, para entender un pago que falla (no lleva datos de la
		// tarjeta: solo el resultado, el código de detalle y el recibo)
		if (req->data && step != ST_PRICE) netlog("pago: respuesta %.400s", req->data);
		if (step == ST_INIT) {
			const char *t = NULL;
			const cJSON *tv = cJSON_GetObjectItemCaseSensitive(root, "transid");
			if (cJSON_IsString(tv)) t = tv->valuestring;
			if (root && jint(root, "success") == 1 && t && strcmp(t, "-1") && strcmp(t, "18446744073709551615")) {
				snprintf(transid, sizeof transid, "%s", t);
				char q[160];
				snprintf(q, sizeof q, "getfinalprice/?count=%d&transid=%s&purchasetype=self&microtxnid=-1&cart=-1&gidReplayOfTransID=-1", ++calls, transid);
				net_release(req);
				req = co_get(q);
				step = ST_PRICE;
				cJSON_Delete(root);
				return;
			}
			fail(root ? jint(root, "purchaseresultdetail") : 0, net_fail ? req->err : "");
		} else if (step == ST_PRICE) {
			if (root && jint(root, "success") == 1) {
				P.total_cents = jll(root, "total");
				jcopy(P.total_fmt, sizeof P.total_fmt, root, "formattedTotal");
				P.need_cvv = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "requirecvv")) || !strcmp(page.card_type, "cartebleue");
				if (P.method != PM_CARD) P.need_cvv = false;
				long long bal = jll(root, "steamAccountBalance");
				if (bal) P.wallet_cents = bal;
				P.short_wallet = P.method == PM_WALLET && P.total_cents > P.wallet_cents;
				netlog("pago: total %lld, metodo %s, CVV %s", P.total_cents, P.method == PM_WALLET ? "cartera" : page.card_type,
					P.need_cvv ? "pedido" : "no pedido");
				set_state(PAY_REVIEW);
			} else {
				fail(root ? jint(root, "purchaseresultdetail") : 0, net_fail ? req->err : "");
				cancel_transaction();
			}
		} else if (step == ST_FINAL || step == ST_STATUS) {
			if (root) {
				on_status(root, now);
			} else if (now - poll_start > POLL_MAX) {
				P.maybe_paid = true;
				fail(0, req->err);
			} else {
				next_poll = now + POLL_MS;   // sin red un momento: se sigue preguntando
			}
			step = ST_STATUS;
		}
		cJSON_Delete(root);
		root = NULL;
		if (req) net_release(req);
		req = NULL;
	}

	if (auth_req && auth_req->state >= REQ_DONE) {
		root = auth_req->state == REQ_DONE && auth_req->data ? cJSON_Parse(auth_req->data) : NULL;
		const cJSON *d = cJSON_GetObjectItemCaseSensitive(root, "data");
		if (root && jint(root, "eresult") == 1 && d) {
			cJSON *fake = cJSON_CreateObject();
			cJSON_AddNumberToObject(fake, "success", 22);
			cJSON_AddNumberToObject(fake, "purchaseresultdetail", 86);
			cJSON_AddItemToObject(fake, "authenticationdetails", cJSON_Duplicate(d, 1));
			on_status(fake, now);
			cJSON_Delete(fake);
		}
		cJSON_Delete(root);
		net_release(auth_req);
		auth_req = NULL;
	}

	// 3) Preguntar cómo va el pago
	if ((P.state == PAY_PAYING || P.state == PAY_BANK) && step == ST_STATUS && !req && now >= next_poll) {
		if (now - poll_start > POLL_MAX) {
			P.maybe_paid = true;
			fail(0, "sin respuesta del pago");
			return;
		}
		char q[96];
		snprintf(q, sizeof q, "transactionstatus/?count=%d&transid=%s", ++calls, transid);
		req = co_get(q);
	}
}
