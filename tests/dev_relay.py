#!/usr/bin/env python3
"""Relé para probar Steam3DS en el emulador desde la nube.

La build de pruebas (make DEV=1) cambia https://HOST/RUTA por
http://127.0.0.1:8099/HOST/RUTA. Este relé hace la petición real a Steam
y devuelve la respuesta tal cual (cuerpo, Content-Type y Set-Cookie).
La build que se publica nunca usa esto.

Uso: python3 tests/dev_relay.py [--offline CARPETA]
  --offline guarda/sirve las respuestas desde CARPETA (pruebas repetibles).

RELAY_FAKE_LOGIN=1: Steam simulado para la sesión (en la nube no hay cuenta
con la que aprobar el QR). El QR se pide a Steam de verdad, pero la
aprobación llega sola al tercer sondeo y las funciones con token
(perfil, nivel, biblioteca, deseados, amigos) responden con el formato real
de la API y datos de prueba. Nada de esto va en la app que se publica.
"""
import base64
import hashlib
import json
import time
import urllib.parse
import os
import sys
import re
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = 8099
ALLOWED = ("store.steampowered.com", "api.steampowered.com", "shared.akamai.steamstatic.com",
           "shared.fastly.steamstatic.com", "cdn.akamai.steamstatic.com", "steamcdn-a.akamaihd.net",
           "avatars.steamstatic.com", "avatars.akamai.steamstatic.com",
           "login.steampowered.com", "checkout.steampowered.com", "steamcommunity.com", "help.steampowered.com")
CACHE = None


def fetch(method, url, data=None):
    key = hashlib.sha1((method + " " + url + (data or "")).encode()).hexdigest()
    if CACHE:
        path = os.path.join(CACHE, key)
        if os.path.exists(path + ".json"):
            meta = json.load(open(path + ".json"))
            return meta["status"], meta["headers"], open(path + ".body", "rb").read()
    req = urllib.request.Request(url, method=method, data=data.encode() if data is not None else None, headers={
        "User-Agent": "Steam3DS-dev-relay", "Accept-Encoding": "identity",
        "Content-Type": "application/x-www-form-urlencoded"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            status, headers, body = r.status, r.headers.items(), r.read()
    except urllib.error.HTTPError as e:
        status, headers, body = e.code, e.headers.items(), e.read()
    headers = [(k, v) for k, v in headers if k.lower() in ("content-type", "set-cookie")]
    # La nube sale por EE. UU.: RELAY_COUNTRY=ES simula la cookie de otro país
    cc = os.environ.get("RELAY_COUNTRY")
    if cc:
        headers = [(k, "steamCountry=%s%%7C0; path=/" % cc if v.startswith("steamCountry=") else v) for k, v in headers]
    if CACHE:
        json.dump({"status": status, "headers": headers}, open(path + ".json", "w"))
        open(path + ".body", "wb").write(body)
    return status, headers, body


# --- Steam simulado (RELAY_FAKE_LOGIN=1) -------------------------------------
FAKE = os.environ.get("RELAY_FAKE_LOGIN") == "1"
FAKE_ID = 76561190000000042
polls = {}


def b64url(d):
    return base64.urlsafe_b64encode(json.dumps(d).encode()).decode().rstrip("=")


def fake_jwt(kind, aud=None):
    now = int(time.time())
    life = 86400 if kind == "access" else 200 * 86400
    payload = {"iss": "steam", "sub": str(FAKE_ID), "aud": aud or ["web", kind], "exp": now + life,
               "iat": now, "jti": "18E3_0000_FAKE" if kind == "refresh" else "18E3_ACCESS"}
    return "eyJhbGciOiJFZERTQSJ9." + b64url(payload) + ".firma"


FRIENDS = [76561190000000101, 76561190000000102, 76561190000000103]
NAMES = {FAKE_ID: "VENOMSHiNE (prueba)", 76561190000000101: "Kenji", 76561190000000102: "Lucía", 76561190000000103: "Marco"}
OWNED = [(1091500, "Cyberpunk 2077", 6120, 3), (381210, "Dead by Daylight", 2410, 1), (1145360, "Hades", 3900, 20),
         (292030, "The Witcher 3: Wild Hunt", 9300, 60), (413150, "Stardew Valley", 5200, 9), (730, "Counter-Strike 2", 12000, 2),
         (570, "Dota 2", 0, 400), (1203620, "Enshrouded", 1500, 12)]
WISH = [2138330, 1174180, 1716740, 668580]


def multipart(body):
    return dict(re.findall(r'name="([^"]+)"\r\n\r\n(.*?)\r\n', body, re.S))


WEB_HOSTS = ("store.steampowered.com", "checkout.steampowered.com", "steamcommunity.com", "help.steampowered.com")
settoken_calls = []


def fake_post(host, path, body):
    """Devuelve (json, cabeceras extra) o None."""
    if host == "login.steampowered.com" and path == "jwt/finalizelogin":
        f = multipart(body)
        if not f.get("nonce", "").startswith("eyJhbGciOiJFZERTQSJ9.") or not f.get("sessionid"):
            return {"error": 8}, []
        return {"steamID": str(FAKE_ID), "redir": f.get("redir", ""), "transfer_info": [
            {"url": "https://%s/login/settoken" % h, "params": {"nonce": "nonce-" + h, "auth": "auth-" + h}}
            for h in WEB_HOSTS]}, []
    if path == "login/settoken" and host in WEB_HOSTS:
        f = multipart(body)
        if f.get("steamID") != str(FAKE_ID) or f.get("nonce") != "nonce-" + host:
            return {"result": 8}, []
        settoken_calls.append(host)
        aud = {"store.steampowered.com": "web:store", "checkout.steampowered.com": "web:checkout",
               "steamcommunity.com": "web:community", "help.steampowered.com": "web:help"}[host]
        cookie = "steamLoginSecure=%d%%7C%%7C%s; Path=/; Secure; HttpOnly; SameSite=None" % (FAKE_ID, fake_jwt("access", [aud]))
        return {"result": 1}, [("Set-Cookie", cookie)]
    r = fake_post_api(path, body)
    if r is None:
        r = fake_shop(path, body)
    if isinstance(r, tuple):   # (json, eresult)
        return r[0], [("X-eresult", str(r[1]))]
    return (r, [("X-eresult", "1")]) if r is not None else None


# Carrito, deseados y licencias gratis simulados (con nombres y precios reales de la tienda)
CART = []
next_line = [9000000000000001]


def real_item(kind, ident):
    inp = {"ids": [{kind: ident}], "context": {"language": "spanish", "country_code": os.environ.get("RELAY_COUNTRY", "US")}}
    url = "https://api.steampowered.com/IStoreBrowseService/GetItems/v1/?input_json=" + urllib.parse.quote(json.dumps(inp))
    try:
        _, _, b = fetch("GET", url)
        return json.loads(b)["response"]["store_items"][0]
    except Exception:
        return {}


def cart_json():
    lines, total = [], 0
    for l in CART:
        it = real_item("bundleid" if "bundleid" in l else "packageid", l.get("bundleid") or l.get("packageid"))
        bp = it.get("best_purchase_option", {})
        cents = int(bp.get("final_price_in_cents", 0) or 0)
        total += cents
        d = dict(l)
        d["is_valid"] = True
        d["price_when_added"] = {"amount_in_cents": str(cents), "currency_code": 3, "formatted_amount": bp.get("formatted_final_price", "")}
        lines.append(d)
    sub = {"amount_in_cents": str(total), "currency_code": 3, "formatted_amount": ("%d,%02d€" % (total // 100, total % 100))}
    return {"line_items": lines, "subtotal": sub, "is_valid": True} if CART else {}


def fake_shop(path, body):
    form = dict(urllib.parse.parse_qsl(body))
    inp = json.loads(form.get("input_json", "{}") or "{}")
    if path.endswith("IAccountCartService/AddItemsToCart/v1/"):
        ids = []
        for it in inp.get("items", []):
            key = "bundleid" if "bundleid" in it else "packageid"
            if any(l.get(key) == it[key] for l in CART):
                continue
            l = {"line_item_id": str(next_line[0]), "type": 1, key: it[key], "time_added": int(time.time())}
            next_line[0] += 1
            CART.append(l)
            ids.append(l["line_item_id"])
        return {"response": {"line_item_ids": ids, "cart": cart_json()}}
    if path.endswith("IAccountCartService/RemoveItemFromCart/v1/"):
        CART[:] = [l for l in CART if l["line_item_id"] != str(inp.get("line_item_id"))]
        return {"response": {"cart": cart_json()}}
    if path.endswith("IWishlistService/AddToWishlist/v1/"):
        a = int(inp.get("appid", 0))
        if a in WISH:
            return {"response": {}}, 29
        WISH.append(a)
        return {"response": {"wishlist_count": len(WISH)}}
    if path.endswith("IWishlistService/RemoveFromWishlist/v1/"):
        a = int(inp.get("appid", 0))
        if a in WISH:
            WISH.remove(a)
        return {"response": {"wishlist_count": len(WISH)}}
    if path.endswith("ICheckoutService/AddFreeLicense/v1/"):
        a = int(inp.get("item_id", {}).get("appid", 0))
        if any(x[0] == a for x in OWNED):
            return {"response": {"purchase_result_detail": 9}}
        it = real_item("appid", a)
        if not it.get("is_free") and it.get("type") != 1:
            return {"response": {"purchase_result_detail": 5}}
        OWNED.append((a, it.get("name", "Juego %d" % a), 0, 0))
        return {"response": {"appids_added": [a], "packageids_added": [], "purchase_result_detail": 0}}
    return None


def wallet_json():
    c = WALLET_CENTS[0]
    return {"response": {"has_wallet": True, "user_country_code": "ES", "wallet_country_code": "ES", "balance": str(c),
                         "currency_code": 3, "formatted_balance": "%d,%02d€" % (c // 100, c % 100)}}


def fake_post_api(path, body):
    form = dict(urllib.parse.parse_qsl(body))
    if path.endswith("GetClientWalletDetails/v1/"):
        return wallet_json()
    if path.endswith("PollAuthSessionStatus/v1/"):
        n = polls.get(form.get("client_id"), 0) + 1
        polls[form.get("client_id")] = n
        if n < 2:
            return {"response": {"had_remote_interaction": False}}
        if n < 3:
            return {"response": {"had_remote_interaction": True}}
        return {"response": {"refresh_token": fake_jwt("refresh"), "access_token": fake_jwt("access"),
                             "had_remote_interaction": True, "account_name": "venomshine_prueba"}}
    if path.endswith("GenerateAccessTokenForApp/v1/"):
        # Como el Steam real desde 2025: las sesiones de navegador no pueden (AccessDenied)
        return {"response": {}}
    if path.endswith("RevokeRefreshToken/v1/"):
        return {"response": {}}
    return None


def fake_get(path, q):
    inp = json.loads(q.get("input_json", "{}") or "{}")
    if path.endswith("GetPlayerLinkDetails/v1/"):
        ids = [int(x) for x in inp.get("steamids", [])]
        # El avatar de «?» que Steam pone a las cuentas sin avatar
        sha = base64.b64encode(bytes.fromhex("fef49e7fa7e1997310d705b2a6158ff8dc1cdfeb")).decode()
        return {"response": {"accounts": [{"public_data": {"steamid": str(i), "persona_name": NAMES.get(i, "Amigo"),
                                                            "sha_digest_avatar": sha}} for i in ids]}}
    if path.endswith("GetSteamLevel/v1/"):
        return {"response": {"player_level": 23}}
    if path.endswith("GetOwnedGames/v1/"):
        # RELAY_EMPTY_OWNED=1: como le pasó en la consola, Steam no devuelve juegos (prueba el plan B)
        if os.environ.get("RELAY_EMPTY_OWNED") == "1":
            return {"response": {}}
        now = int(time.time())
        return {"response": {"game_count": len(OWNED), "games": [
            {"appid": a, "name": n, "playtime_forever": m, "rtime_last_played": now - d * 86400, "img_icon_url": ""}
            for a, n, m, d in OWNED]}}
    if path.endswith("GetWishlist/v1/"):
        return {"response": {"items": [{"appid": a, "priority": i + 1, "date_added": int(time.time()) - i * 86400 * 7}
                                       for i, a in enumerate(WISH)]}}
    if path.endswith("IAccountCartService/GetCart/v1/"):
        return {"response": {"cart": cart_json()}}
    if path.endswith("GetFriendsGameplayInfo/v1/"):
        appid = int(q.get("appid", inp.get("appid", 0)) or 0)
        owned = [a for a, *_ in OWNED]
        if appid in owned:
            return {"response": {"your_info": {"steamid": str(FAKE_ID), "owned": True},
                                 "owns": [{"steamid": str(f), "minutes_played_forever": 300} for f in FRIENDS],
                                 "in_game": [{"steamid": str(FRIENDS[0]), "minutes_played": 20}]}}
        return {"response": {"in_wishlist": [{"steamid": str(FRIENDS[1])}]}}
    return None


# Pago simulado (checkout.steampowered.com): cartera con 32,10 € y una Visa guardada que pide
# el CVV y la verificación del banco. Nunca se toca el checkout de verdad.
TRANS = {}
WALLET_CENTS = [3210]
FAKE_CVV = "123"


def cookies_of(headers):
    out = {}
    for part in headers.get("Cookie", "").split(";"):
        k, _, v = part.strip().partition("=")
        if k:
            out[k] = v
    return out


CHECKOUT_PAGE = """<html><body>
<script>var g_sessionID = "%(sid)s"; var g_bUseAccountCart = true;</script>
<input type="hidden" id="payment_method" value="">
<div id="payment_method_droplist_ctn"><ul id="payment_method_droplist">
<li><a id="steamaccount" href="#">Cartera de Steam</a></li>
<li><a id="storedcreditcard" href="#">Visa ****4821</a></li>
<li><a id="paypal" href="#">PayPal</a></li></ul></div>
<input type="hidden" name="stored_payment_method" id="stored_payment_method" value="visa">
<input type="hidden" id="stored_card_id" name="stored_card_id" value="776655">
<input type="hidden" id="stored_card_number" value="************4821">
<input type="hidden" id="billing_country" value="ES">
<div class="checkout_tab" id="purchase_confirm_ssa"><input type="checkbox" id="accept_ssa" name="accept_ssa">
<label for="accept_ssa">Acepto las condiciones del <a href="#">Acuerdo de Suscriptor a Steam</a> (última actualización: 26 SEP 2024).</label></div>
<div class="checkout_tab" id="purchase_confirm_right_withdrawal"><input type="checkbox" id="accept_right_withdrawal">
<label for="accept_right_withdrawal">Acepto que el contenido se ponga a mi disposición de inmediato y reconozco que, por ello,
pierdo mi derecho de desistimiento.</label></div>
</body></html>"""


def cart_cents():
    return sum(int(l["price_when_added"]["amount_in_cents"]) for l in cart_json().get("line_items", []))


def fake_checkout(method, path, q, body, headers):
    """(status, content-type, cuerpo) o None."""
    ck = cookies_of(headers)
    if not ck.get("steamLoginSecure", "").startswith(str(FAKE_ID)):
        return 302, "text/html", "<html>login</html>"
    form = dict(urllib.parse.parse_qsl(body)) if method == "POST" else {}
    if method == "POST" and form.get("sessionid") != ck.get("sessionid"):
        return 200, "application/json", json.dumps({"success": 2, "purchaseresultdetail": 7})
    js = lambda d: (200, "application/json", json.dumps(d))
    if path == "checkout/" and method == "GET":
        return 200, "text/html", CHECKOUT_PAGE % {"sid": ck.get("sessionid", "")}
    if path == "checkout/inittransaction/":
        pm = form.get("PaymentMethod")
        if pm == "visa" and form.get("gidPaymentID") != "776655":
            return js({"success": 2, "purchaseresultdetail": 6, "transid": "18446744073709551615"})
        if pm not in ("steamaccount", "visa") or not CART:
            return js({"success": 2, "purchaseresultdetail": 7, "transid": "18446744073709551615"})
        tid = str(7000000000000000 + len(TRANS) + 1)
        TRANS[tid] = {"method": pm, "total": cart_cents(), "polls": 0, "final": False}
        return js({"success": 1, "purchaseresultdetail": 0, "paymentmethod": 2, "transid": tid})
    if path == "checkout/getfinalprice/":
        t = TRANS.get(q.get("transid"))
        if not t:
            return js({"success": 2, "purchaseresultdetail": 7})
        c = t["total"]
        return js({"success": 1, "total": c, "formattedTotal": "%d,%02d€" % (c // 100, c % 100),
                   "steamAccountBalance": WALLET_CENTS[0], "providerpaymentmethod": 0 if t["method"] == "steamaccount" else 2,
                   "requirecvv": 1 if t["method"] == "visa" else 0})
    if path == "checkout/canceltransaction/":
        TRANS.pop(form.get("transid"), None)
        return js({"success": 1})
    if path == "checkout/finalizetransaction/":
        t = TRANS.get(form.get("transid"))
        if not t:
            return js({"success": 2, "purchaseresultdetail": 7})
        if t["method"] == "steamaccount" and t["total"] > WALLET_CENTS[0]:
            return js({"success": 2, "purchaseresultdetail": 2})
        if t["method"] == "visa" and form.get("CardCVV2") != FAKE_CVV:
            return js({"success": 2, "purchaseresultdetail": 11})
        t["final"] = True
        return js({"success": 22, "purchaseresultdetail": 0, "bShowBRSpecificCreditCardError": False})
    if path == "checkout/transactionstatus/":
        t = TRANS.get(q.get("transid"))
        if not t or not t["final"]:
            return js({"success": 2, "purchaseresultdetail": 7})
        t["polls"] += 1
        if t["method"] == "visa" and t["polls"] <= 3:
            # El banco pide verificar (3D Secure): tres sondeos «esperando al banco»
            return js({"success": 22, "purchaseresultdetail": 86,
                       "authenticationdetails": {"threed_secure_step": 2, "issuer_url": "https://banco.example/acs", "md": "x", "pa_request": "y", "term_url": "z"}})
        if t["method"] == "steamaccount":
            WALLET_CENTS[0] -= t["total"]
        for l in CART:
            it = real_item("bundleid" if "bundleid" in l else "packageid", l.get("bundleid") or l.get("packageid"))
            for a in it.get("included_appids", [])[:1]:
                if not any(x[0] == a for x in OWNED):
                    OWNED.insert(0, (a, it.get("name", "Juego"), 0, 0))
        CART.clear()
        c = t["total"]
        return js({"success": 1, "purchaseresultdetail": 0, "purchasereceipt": {
            "paymentmethod": 11 if t["method"] == "steamaccount" else 2, "purchasestatus": 1, "resultdetail": 0,
            "formattedTotal": "%d,%02d€" % (c // 100, c % 100), "transactionid": q.get("transid")}})
    return None


# Métodos que Steam solo acepta de una forma (comprobado contra la API real: el otro da 405)
METHOD_ONLY = {
    "IAccountCartService/GetCart/v1/": "GET",
    "IUserAccountService/GetClientWalletDetails/v1/": "POST",
}


class Relay(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def handle_any(self, method):
        host, _, rest = self.path.lstrip("/").partition("/")
        if host not in ALLOWED:
            self.send_error(403, "host no permitido")
            return
        url = "https://%s/%s" % (host, rest)
        path, _, query = rest.partition("?")
        q = dict(urllib.parse.parse_qsl(query))
        if FAKE and host == "store.steampowered.com" and path == "dynamicstore/userdata/":
            ck = cookies_of(self.headers)
            ok = ck.get("steamLoginSecure", "").startswith(str(FAKE_ID))
            body = json.dumps({"rgWishlist": WISH, "rgOwnedPackages": [], "rgOwnedApps": [a for a, *_ in OWNED]} if ok else {}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            sys.stderr.write("FAKE GET userdata\n")
            return
        if FAKE and host == "checkout.steampowered.com" and path.startswith("checkout/"):
            r = fake_checkout(method, path, q, self.body, self.headers)
            if r is not None:
                st, ct, body = r
                body = body.encode()
                self.send_response(st)
                self.send_header("Content-Type", ct)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                sys.stderr.write("FAKE %s %s -> %d\n" % (method, path, st))
                return
        # Como Steam: hay métodos que solo admiten GET o solo POST (lo demás, 405)
        only = METHOD_ONLY.get(path)
        if host == "api.steampowered.com" and only and method != only:
            self.send_response(405)
            self.send_header("Content-Length", "0")
            self.end_headers()
            sys.stderr.write("405 %s %s (solo %s)\n" % (method, path, only))
            return
        fake, extra = None, []
        if FAKE and method == "POST":
            fake = fake_post(host, path, self.body)
            if fake is not None:
                fake, extra = fake
        elif FAKE and q.get("access_token", "").startswith("eyJhbGciOiJFZERTQSJ9."):
            fake = fake_get(path, q)
        if fake is not None:
            body = json.dumps(fake).encode()
            self.send_response(200)
            for k, v in extra:
                self.send_header(k, v)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            sys.stderr.write("FAKE %s %s\n" % (method, path))
            return
        try:
            status, headers, body = fetch(method, url, self.body if method == "POST" else None)
        except Exception as e:  # red de la nube caída, etc.
            self.send_error(502, str(e)[:100])
            return
        self.send_response(status)
        for k, v in headers:
            self.send_header(k, v)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if method != "HEAD":
            self.wfile.write(body)
        sys.stderr.write("%s %d %dB %s\n" % (method, status, len(body), url[:110]))

    def do_GET(self):
        self.body = ""
        self.handle_any("GET")

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0) or 0)
        self.body = self.rfile.read(n).decode("utf-8", "replace")
        self.handle_any("POST")

    def do_HEAD(self):
        self.body = ""
        self.handle_any("HEAD")

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    if "--offline" in sys.argv:
        CACHE = sys.argv[sys.argv.index("--offline") + 1]
        os.makedirs(CACHE, exist_ok=True)
    ThreadingHTTPServer(("127.0.0.1", PORT), Relay).serve_forever()
