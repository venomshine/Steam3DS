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
import html
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
           "login.steampowered.com", "checkout.steampowered.com", "steamcommunity.com", "help.steampowered.com",
           "images.steamusercontent.com", "avatars.fastly.steamstatic.com", "community.akamai.steamstatic.com",
           "community.fastly.steamstatic.com", "store.akamai.steamstatic.com")
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
    # La comunidad da 429 a urllib desde la nube (a curl no): se repite con curl
    if status == 429 and method == "GET" and "steamcommunity.com/" in url:
        import subprocess
        out = subprocess.run(["curl", "-s", "-L", "-w", "\n%{http_code}", url], capture_output=True, timeout=30).stdout
        body, _, code = out.rpartition(b"\n")
        status, headers = int(code or 0), [("Content-Type", "text/html; charset=UTF-8")]
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
# xPaw (fondo y marco), St4ck (muchas vitrinas), Robin Walker (juego favorito) y Tfue (ilustraciones)
PROFILE_AS = {FAKE_ID: 76561197972494985, 76561190000000101: 76561198023414915, 76561190000000102: 76561198059667390,
              76561190000000103: 76561198260200125}
# RELAY_KENJI=<steamid>: Kenji pasa a ser ese perfil público (para probar uno concreto)
if os.environ.get("RELAY_KENJI"):
    PROFILE_AS[76561190000000101] = int(os.environ["RELAY_KENJI"])
NAMES = {FAKE_ID: "VENOMSHiNE (prueba)", 76561190000000101: "Kenji", 76561190000000102: "Lucía", 76561190000000103: "Marco", 76561190000000109: "Sofía"}
OWNED = [(1091500, "Cyberpunk 2077", 6120, 3), (381210, "Dead by Daylight", 2410, 1), (1145360, "Hades", 3900, 20),
         (292030, "The Witcher 3: Wild Hunt", 9300, 60), (413150, "Stardew Valley", 5200, 9), (730, "Counter-Strike 2", 12000, 2),
         (570, "Dota 2", 0, 400), (1203620, "Enshrouded", 1500, 12), (251570, "7 Days to Die", 800, 30), (33440, "Driver: San Francisco", 1260, 90)]
# Los juegos ocultos de la biblioteca (colección «hidden»): RELAY_HIDDEN=id,id (por defecto, Dota 2)
HIDDEN = [int(x) for x in os.environ.get("RELAY_HIDDEN", "570").split(",") if x]
WISH = [2138330, 1174180, 1716740, 668580]
if os.environ.get("RELAY_WISH"):   # RELAY_WISH=id,id,…: otra lista de deseos (p. ej. con juegos en oferta)
    WISH = [int(x) for x in os.environ["RELAY_WISH"].split(",")]


def multipart(body):
    return dict(re.findall(r'name="([^"]+)"\r\n\r\n(.*?)\r\n', body, re.S))


WEB_HOSTS = ("store.steampowered.com", "checkout.steampowered.com", "steamcommunity.com", "help.steampowered.com")
settoken_calls = []


IGNORED = {}   # juegos ignorados (appid -> motivo), como rgIgnoredApps
FOLLOWED = {1145360}   # juegos que sigues (rgFollowedApps)
# Privacidad de la cuenta simulada (como la guarda la página de edición del perfil)
PRIVACY = {"PrivacySettings": {"PrivacyProfile": 3, "PrivacyInventory": 3, "PrivacyInventoryGifts": 3, "PrivacyOwnedGames": 3,
                               "PrivacyPlaytime": 3, "PrivacyFriendsList": 2}, "eCommentPermission": 1}
THEME = [""]
POINTS = [12345]   # puntos de Steam de la cuenta simulada
DQ_PAGE = [0]   # cola de descubrimiento simulada: cuántas veces se ha pedido una nueva
PC_STATE = {}   # descargas del PC simulado: lo desinstalado y si las descargas están en pausa
# Tus reseñas (appid -> datos), como las da IUserReviewsService; Cyberpunk ya tiene una
REVIEWS = {1091500: {"review": "Después de las actualizaciones es otro juego. Night City es increíble y la historia de V engancha muchísimo.",
                     "voted_up": True, "created": int(time.time()) - 40 * 86400, "votes_up": 12}}


def fake_post(host, path, body):
    """Devuelve (json, cabeceras extra) o None."""
    if host == "api.steampowered.com" and path.endswith("ICloudConfigStoreService/Download/v1/"):
        val = json.dumps({"id": "hidden", "name": "HIDDEN", "added": HIDDEN, "removed": []})
        return {"response": {"data": [{"enamespace": 1, "version": "12", "entries": [
            {"key": "user-collections.favorite", "value": json.dumps({"id": "favorite", "added": [730]})},
            {"key": "user-collections.hidden", "value": val}]}]}}, []
    # Activar un producto: AAAAA-BBBBB-CCCCC es buena (Hollow Knight), TAKEN-TAKEN-TAKEN ya la usó otro,
    # WALLETCODE12345 es un código del monedero de 20 €; lo demás no es válido
    if host == "store.steampowered.com" and path in ("account/ajaxregisterkey/", "account/ajaxredeemwalletcode/"):
        raw = body.decode() if isinstance(body, bytes) else (body or "")
        f = {k: v[0] for k, v in urllib.parse.parse_qs(raw).items()}
        if path.endswith("registerkey/"):
            key = f.get("product_key", "")
            if key == "AAAAA-BBBBB-CCCCC":
                return {"success": 1, "purchase_result_details": 0, "purchase_receipt_info": {"line_items": [
                    {"packageid": 52600, "line_item_description": "Hollow Knight"}]}}, []
            return {"success": 2, "purchase_result_details": 15 if key == "TAKEN-TAKEN-TAKEN" else 14,
                    "purchase_receipt_info": {}}, []
        if f.get("wallet_code") == "WALLETCODE12345":
            WALLET_CENTS[0] += 2000
            return {"success": 1, "detail": 0, "formattednewwalletbalance": "%d,%02d€" % divmod(WALLET_CENTS[0], 100)}, []
        return {"success": 2, "detail": 14}, []
    if host == "steamcommunity.com" and path == "profiles/%d/ajaxsetprivacy/" % FAKE_ID:
        raw = body.decode() if isinstance(body, bytes) else (body or "")
        f = {k: v[0] for k, v in urllib.parse.parse_qs(raw).items()}
        if not f.get("sessionid"):
            return {"success": 2}, []
        PRIVACY["PrivacySettings"] = json.loads(f.get("Privacy", "{}"))
        PRIVACY["eCommentPermission"] = int(f.get("eCommentPermission", "1"))
        sys.stderr.write("PRIVACIDAD %s\n" % json.dumps(PRIVACY))
        return {"success": 1, "Privacy": PRIVACY}, []
    if host == "steamcommunity.com" and path in ("actions/AddFriendAjax", "actions/IgnoreFriendInviteAjax"):
        raw = body.decode() if isinstance(body, bytes) else (body or "")
        f = {k: v[0] for k, v in urllib.parse.parse_qs(raw).items()}
        sys.stderr.write("AMIGOS %s %s\n" % (path, f))
        return ({"success": 1, "invited": [f.get("steamid")]} if f.get("sessionID") else {"success": 0}), []
    if host == "store.steampowered.com" and path == "explore/followgame/":
        raw = body.decode() if isinstance(body, bytes) else (body or "")
        f = {k: v[0] for k, v in urllib.parse.parse_qs(raw).items()}
        a = int(f.get("appid", "0") or 0)
        if not f.get("sessionid") or not a:
            return False, []
        (FOLLOWED.discard if f.get("unfollow") == "1" else FOLLOWED.add)(a)
        return True, []
    if host == "steamcommunity.com" and path.startswith("comment/Profile/post/"):
        raw = body.decode() if isinstance(body, bytes) else (body or "")
        f = {k: v[0] for k, v in urllib.parse.parse_qs(raw).items()}
        sys.stderr.write("COMENTARIO %s\n" % f.get("comment"))
        return ({"success": True} if f.get("sessionid") and f.get("comment") else {"success": False, "error": "Falta el texto"}), []
    if host == "store.steampowered.com" and path.startswith("ajaxrequestplaytestaccess/"):
        return {"success": 1, "granted": 0}, []
    if host == "store.steampowered.com" and path == "friends/recommendgame":
        raw = body.decode() if isinstance(body, bytes) else (body or "")
        f = {k: v[0] for k, v in urllib.parse.parse_qs(raw).items()}
        a = int(f.get("appid", "0") or 0)
        if not f.get("sessionid") or not a:
            return {"success": False}, []
        if a not in [x for x, *_ in OWNED]:
            return {"success": False, "strError": "Debes tener este producto para escribir una reseña."}, []
        old = REVIEWS.get(a, {})
        REVIEWS[a] = {"review": f.get("comment", ""), "voted_up": f.get("rated_up") == "true",
                      "created": old.get("created", int(time.time())), "updated": int(time.time()), "votes_up": old.get("votes_up", 0)}
        return {"success": True}, []
    if host == "store.steampowered.com" and path.startswith("recommended/ignorerecommendation"):
        raw = body.decode() if isinstance(body, bytes) else (body or "")
        f = {k: v[0] for k, v in urllib.parse.parse_qs(raw).items()}
        a = int(f.get("appid", "0") or 0)
        if not f.get("sessionid") or not a:
            return False, []
        if f.get("remove") == "1":
            IGNORED.pop(a, None)
        else:
            IGNORED[a] = int(f.get("ignore_reason", "0") or 0)
        return True, []
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
FREE_ADDED = set()
next_line = [9000000000000001]
# RELAY_CART=paquetes separados por comas: el carrito ya empieza con ellos (para probar sin añadir a mano)
for _pk in filter(None, os.environ.get("RELAY_CART", "").split(",")):
    CART.append({"line_item_id": str(next_line[0]), "type": 1, "packageid": int(_pk), "time_added": int(time.time())})
    next_line[0] += 1


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
    if path.endswith("IAccountCartService/ModifyLineItem/v1/"):
        for l in CART:
            if l["line_item_id"] == str(inp.get("line_item_id")):
                fl = inp.get("flags", {})
                l["flags"] = {"is_gift": bool(fl.get("is_gift")), "is_private": bool(fl.get("is_private"))}
                if fl.get("is_gift") and inp.get("gift_info"):
                    l["gift_info"] = inp["gift_info"]
                else:
                    l.pop("gift_info", None)
                return {"response": {"cart": cart_json()}}
        return {"response": {}}, 42
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
        FREE_ADDED.add(a)   # como en Steam: GetOwnedGames no trae las licencias gratis sin jugar
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
    if path.endswith("GetTopAchievementsForGames/v1/") or path.endswith("GetAchievementsProgress/v1/"):
        # Los logros del juego son los de verdad; de cada tres, la cuenta simulada tiene uno
        inp = json.loads(form.get("input_json", "{}"))
        appid = int((inp.get("appids") or [0])[0])
        st, _, b = fetch("GET", "https://api.steampowered.com/IPlayerService/GetGameAchievements/v1/?appid=%d&language=%s"
                         % (appid, inp.get("language", "english")))
        ach = json.loads(b).get("response", {}).get("achievements", []) if st == 200 else []
        mine = ach[::3] if appid in [a for a, *_ in OWNED] else []
        sid = int(inp.get("steamid", "0") or 0)
        if sid != FAKE_ID and sid in PROFILE_AS and path.endswith("GetTopAchievementsForGames/v1/"):
            # Un amigo simulado: los logros de la vitrina real de su perfil, con sus claves statid_bit
            st2, _, b2 = fetch("GET", "https://api.steampowered.com/IPlayerService/GetProfileCustomization/v1/?steamid=%d" % PROFILE_AS[sid])
            keys = []
            for c in json.loads(b2).get("response", {}).get("customizations", []) if st2 == 200 else []:
                if c.get("customization_type") == 17:
                    keys += [sl["title"] for sl in c.get("slots", []) if sl.get("appid") == appid and sl.get("title") and sl["title"] not in keys]
            ach.sort(key=lambda a: float(a["player_percent_unlocked"]))
            if not keys:   # sin vitrina de ese juego: la mitad de los logros (para comparar)
                return {"response": {"games": [{"appid": appid, "total_achievements": len(ach), "achievements": [
                    {"statid": i, "bit": 0, "name": a["localized_name"], "icon": a["icon"], "icon_gray": a["icon_gray"],
                     "player_percent_unlocked": a["player_percent_unlocked"]} for i, a in enumerate(ach[1::2])]}]}}
            return {"response": {"games": [{"appid": appid, "total_achievements": len(ach), "achievements": [
                {"statid": int(k.split("_")[0]), "bit": int(k.split("_")[1]), "name": a["localized_name"], "desc": a["localized_desc"],
                 "icon": a["icon"], "icon_gray": a["icon_gray"], "player_percent_unlocked": a["player_percent_unlocked"]}
                for k, a in zip(keys, ach)]}]}}
        if path.endswith("GetAchievementsProgress/v1/"):
            return {"response": {"achievement_progress": [{"appid": appid, "unlocked": len(mine), "total": len(ach),
                    "percentage": 100.0 * len(mine) / len(ach) if ach else 0, "all_unlocked": bool(ach) and len(mine) == len(ach)}]}}
        return {"response": {"games": [{"appid": appid, "total_achievements": len(ach), "achievements": [
            {"statid": i, "bit": 0, "name": a["localized_name"], "desc": a["localized_desc"], "icon": a["icon"],
             "icon_gray": a["icon_gray"], "hidden": a["hidden"], "player_percent_unlocked": a["player_percent_unlocked"]}
            for i, a in enumerate(mine)]}]}}
    if path.endswith("SetProfileTheme/v1/"):
        form = dict(urllib.parse.parse_qsl(body.decode() if isinstance(body, bytes) else (body or "")))
        THEME[0] = json.loads(form.get("input_json", "{}")).get("theme_id", "")
        return {"response": {}}, 1
    if path.endswith("EnableOrDisableDownloads/v1/") or path.endswith("UninstallClientApp/v1/"):
        inp = json.loads(form.get("input_json", "{}"))
        st = PC_STATE.setdefault(str(inp.get("client_instanceid", "")), {"gone": set(), "paused": False, "t0": time.time()})
        if path.endswith("UninstallClientApp/v1/"):
            st["gone"].add(int(inp.get("appid", 0)))
        else:
            st["paused"] = not inp.get("enable", True)
        return {"response": {}}, 1
    if path.endswith("AddReaction/v1/"):
        POINTS[0] -= 500
        return {"response": {}}, 1
    if path.endswith("RedeemPoints/v1/"):
        POINTS[0] -= 500
        return {"response": {"communityitemid": "123456789", "bundle_community_item_ids": []}}, 1
    if path.endswith("InstallClientApp/v1/"):
        return {"response": {}}, 1
    if path.endswith("MarkNotificationsRead/v1/"):
        for n in NOTIFS:
            n["read"] = True
        return {"response": {}}
    return None


# Notificaciones simuladas: una de cada tipo habitual
NOTIFS = [
    {"notification_id": "9001", "notification_type": 8, "body_data": json.dumps({"appid": 367520}), "read": False, "ago": 3600},
    {"notification_id": "9002", "notification_type": 2, "body_data": json.dumps({"gifter": str(76561190000000101)}), "read": False, "ago": 7200},
    {"notification_id": "9003", "notification_type": 5, "body_data": json.dumps({"requestor_id": str(76561190000000103)}), "read": False, "ago": 86400},
    {"notification_id": "9004", "notification_type": 3, "body_data": json.dumps({}), "read": True, "ago": 3 * 86400},
    {"notification_id": "9005", "notification_type": 27, "body_data": json.dumps({"appid": 1145360, "title": "Nueva actualización: El Olimpo"}), "read": True, "ago": 5 * 86400},
    {"notification_id": "9006", "notification_type": 7, "body_data": json.dumps({"appid": 1091500}), "read": True, "ago": 9 * 86400},
]


# RELAY_LIVE=segundos: pasado ese tiempo llega un comentario nuevo y Marco se conecta (tiempo real)
LIVE_AT = time.time() + float(os.environ.get("RELAY_LIVE", "0") or 0) if os.environ.get("RELAY_LIVE") else None


def live():
    return LIVE_AT is not None and time.time() > LIVE_AT


def fake_get(path, q):
    inp = json.loads(q.get("input_json", "{}") or "{}")
    if path.endswith("GetSteamNotifications/v1/"):
        now = int(time.time())
        ns = ([{"notification_id": "9100", "notification_type": 3, "body_data": json.dumps({}), "read": False, "ago": 5}] if live() else []) + NOTIFS
        out = [dict({k: v for k, v in n.items() if k != "ago"}, timestamp=now - n["ago"]) for n in ns]
        return {"response": {"notifications": out, "unread_count": sum(1 for n in ns if not n["read"])}}
    if path.endswith("GetPlayerLinkDetails/v1/"):
        ids = [int(x) for x in inp.get("steamids", [])]
        # El avatar de «?» que Steam pone a las cuentas sin avatar
        sha = base64.b64encode(bytes.fromhex("fef49e7fa7e1997310d705b2a6158ff8dc1cdfeb")).decode()
        # Estado: Kenji jugando a Hades, Lucía en línea y Marco desconectado desde hace dos días
        now = int(time.time())
        priv = {76561190000000101: {"persona_state": 1, "game_id": "1145360", "game_extra_info": "Hades"},
                76561190000000102: {"persona_state": 1}, 76561190000000103: {"persona_state": 0, "last_logoff_time": now - 2 * 86400}}
        if live():
            priv[76561190000000103] = {"persona_state": 1}
        return {"response": {"accounts": [{"public_data": {"steamid": str(i), "persona_name": NAMES.get(i, "Amigo"),
                                                            "sha_digest_avatar": sha}, "private_data": priv.get(i, {"persona_state": 1})}
                                          for i in ids]}}
    if path.endswith("IFriendsListService/GetFriendsList/v1/"):
        return {"response": {"friendslist": {"bincremental": False, "friends":
                [{"ulfriendid": str(f), "efriendrelationship": 3} for f in FRIENDS] +
                [{"ulfriendid": "76561190000000109", "efriendrelationship": 2}]}}}
    # Dos PCs con Steam abierto; en el del salón ya está instalado Cyberpunk
    if path.endswith("GetDiscoveryQueue/v1/"):
        # Tu cola: doce juegos de verdad; con rebuild_queue, los doce siguientes
        pool = [1245620, 1086940, 2379780, 1145350, 367520, 1593500, 814380, 1174180, 548430, 1817070, 105600, 2050650,
                242760, 646570, 1794680, 252490, 1966720, 2195250, 1888930, 990080, 1599340, 2215430, 1716740, 2138330]
        DQ_PAGE[0] = DQ_PAGE[0] + 1 if inp.get("rebuild_queue") else 0
        k = (DQ_PAGE[0] * 12) % len(pool)
        return {"response": {"appids": (pool[k:] + pool[:k])[:12], "country_code": "ES", "skipped": 0, "exhausted": False}}
    if path.endswith("ILoyaltyRewardsService/GetSummary/v1/"):
        return {"response": {"summary": {"points": str(POINTS[0]), "points_earned": "20000", "points_spent": str(20000 - POINTS[0])}}}
    if path.endswith("GetProfileThemesAvailable/v1/"):
        return {"response": {"profile_themes": [{"theme_id": t, "title": "#ProfileTheme_" + t} for t in ("Summer", "Midnight", "Cosmic")]}}
    if path.endswith("GetAllClientLogonInfo/v1/"):
        return {"response": {"sessions": [
            {"client_instanceid": "7001", "protocol_version": 2, "os_name": "Windows 11", "machine_name": "PC-ESCRITORIO", "os_type": 20},
            {"client_instanceid": "7002", "protocol_version": 2, "os_name": "SteamOS", "machine_name": "PC-SALON", "os_type": -203}],
            "refresh_interval": 60}}
    if path.endswith("GetClientAppList/v1/"):
        cid = str(inp.get("client_instanceid", ""))
        if inp.get("filter_appids"):   # la instalación remota pregunta por un juego
            apps = [{"appid": 1091500, "app": "Cyberpunk 2077", "installed": True}] if cid == "7002" else []
            return {"response": {"apps": apps, "bytes_available": "512000000000"}}
        # Descargas del PC: lo instalado en el escritorio, con Counter-Strike 2 actualizándose
        st = PC_STATE.setdefault(cid, {"gone": set(), "paused": False, "t0": time.time()})
        names = {a: n for a, n, *_ in OWNED}
        apps = []
        for a in [730, 1091500, 292030, 413150, 570] if cid == "7001" else [1091500]:
            if a in st["gone"]:
                continue
            e = {"appid": a, "app": names.get(a, str(a)), "installed": True, "queue_position": -1}
            if a == 730 and cid == "7001":
                tot = 6_400_000_000
                done = min(tot, int(1_100_000_000 + (0 if st["paused"] else (time.time() - st["t0"]) * 60_000_000)))
                e.update({"changing": done < tot, "bytes_downloaded": str(done), "bytes_to_download": str(tot), "download_paused": st["paused"], "queue_position": 0})
            if a == 292030 and cid == "7001":
                e.update({"changing": True, "bytes_downloaded": "0", "bytes_to_download": "2300000000", "queue_position": 1})
            if a == 413150:
                e["running"] = True
            apps.append(e)
        return {"response": {"apps": apps, "bytes_available": "187000000000", "client_info": {"machine_name": "PC"}}}
    if path.endswith("GetIndividualRecommendations/v1/"):
        out = []
        for r in inp.get("requests", []):
            a = int(r.get("appid", 0))
            if a in REVIEWS:
                v = REVIEWS[a]
                out.append({"recommendationid": str(5000 + a), "appid": a, "review": v["review"], "voted_up": v["voted_up"],
                            "timestamp_created": v["created"], "timestamp_updated": v.get("updated", v["created"]),
                            "votes_up": v["votes_up"], "votes_funny": 0, "comment_count": 0, "language": "spanish"})
        return {"response": {"recommendations": out}}
    if path.endswith("GetUserFiles/v1/"):
        # Tus capturas: cuatro imágenes públicas de verdad, en los juegos que tiene la cuenta simulada
        app = int(inp.get("appid", 0))
        if app not in [a for a, *_ in OWNED]:
            return {"response": {"total": 0}}
        now = int(time.time())
        ugc = ["1749057862502509881/D744FA2A1B2FF091CA773F5CDCE31E72CA339CAF", "941714934209988401/AAB1E89AA35E1E28F705AFF3DF7BE84399640DCA",
               "813307680284614858/D85825CF385135A2C07571F4CA0F36718D0F5FD0", "1749057862502418702/7739A308ADD910E2BE423AB8D4A5A4D8086CE659"]
        notes = ["Por fin el jefe final", "", "Atardecer", ""]
        return {"response": {"total": len(ugc), "publishedfiledetails": [
            {"publishedfileid": str(1000 + i), "consumer_appid": app, "file_type": 5, "time_created": now - (i + 1) * 5 * 86400,
             "file_url": "https://images.steamusercontent.com/ugc/%s/" % u, "short_description": notes[i]} for i, u in enumerate(ugc)]}}
    if path.endswith("GetBadges/v1/"):
        return {"response": {"badges": [{"badgeid": i, "level": 1, "xp": 100} for i in range(17)], "player_xp": 2366,
                             "player_level": 23, "player_xp_needed_to_level_up": 134, "player_xp_needed_current_level": 2300}}
    if path.endswith("GetSteamLevel/v1/"):
        return {"response": {"player_level": 23}}
    if path.endswith("GetOwnedGames/v1/"):
        # RELAY_EMPTY_OWNED=1: como le pasó en la consola, Steam no devuelve juegos (prueba el plan B)
        if os.environ.get("RELAY_EMPTY_OWNED") == "1":
            return {"response": {}}
        now = int(time.time())
        return {"response": {"game_count": len([a for a, *_ in OWNED if a not in FREE_ADDED]), "games": [
            {"appid": a, "name": n, "playtime_forever": m, "rtime_last_played": now - d * 86400, "img_icon_url": ""}
            for a, n, m, d in OWNED if a not in FREE_ADDED]}}
    # Familia de Steam simulada: Kenji y Lucía comparten dos juegos que la cuenta no tiene
    if path.endswith("IFamilyGroupsService/GetFamilyGroup/v1/"):
        return {"response": {"name": "Casa", "members": [{"steamid": str(FAKE_ID), "role": 1}, {"steamid": "76561190000000101", "role": 1},
                                                          {"steamid": "76561190000000102", "role": 2}], "free_spots": 3}}
    if path.endswith("GetFamilyGroupForUser/v1/"):
        return {"response": {"family_groupid": "9000001", "is_not_member_of_any_group": False,
                             "family_group": {"name": "Casa", "members": [
                                 {"steamid": str(FAKE_ID), "role": 1}, {"steamid": "76561190000000101", "role": 1},
                                 {"steamid": "76561190000000102", "role": 2}]}}}
    if path.endswith("GetSharedLibraryApps/v1/"):
        return {"response": {"owner_steamid": str(FAKE_ID), "apps": [
            {"appid": 367520, "name": "Hollow Knight", "owner_steamids": ["76561190000000101"], "exclude_reason": 0,
             "app_type": 1, "img_icon_hash": "f6ab055c2366237200b1a31cccbd6cf81e436d72", "rt_playtime": 0},
            {"appid": 504230, "name": "Celeste", "owner_steamids": ["76561190000000102"], "exclude_reason": 0,
             "app_type": 1, "img_icon_hash": "", "rt_playtime": 0},
            {"appid": 1145360, "name": "Hades", "owner_steamids": [str(FAKE_ID), "76561190000000101"], "exclude_reason": 0,
             "app_type": 1, "img_icon_hash": "", "rt_playtime": 0},
            {"appid": 271590, "name": "Grand Theft Auto V", "owner_steamids": ["76561190000000101"], "exclude_reason": 3,
             "app_type": 1, "img_icon_hash": "", "rt_playtime": 0}]}}
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


# RELAY_PAY_REFRESH=1: como pasa con Steam, el checkout no acepta el token que trae la consola y
# manda a renovarlo a login.steampowered.com/jwt/refresh, que solo lo renueva con la cookie
# steamRefresh_steam (sin ella devuelve al checkout y se entra en bucle)
PAY_REFRESH = os.environ.get("RELAY_PAY_REFRESH") == "1"
# RELAY_WEB_REFRESH=1: lo mismo en la tienda al seguir o ignorar un juego (el POST vuelve como GET
# tras renovar y Steam responde 404, como en la consola)
WEB_REFRESH = os.environ.get("RELAY_WEB_REFRESH") == "1"
WEB_REFRESH_PATHS = ("explore/followgame/", "recommended/ignorerecommendation")
RELAY_BASE = "http://127.0.0.1:8099/"


def fake_checkout(method, path, q, body, headers):
    """(status, content-type, cuerpo) o None; 302 -> (302, Location, set-cookie)."""
    ck = cookies_of(headers)
    if not ck.get("steamLoginSecure", "").startswith(str(FAKE_ID)):
        return 302, "text/html", "<html>login</html>"
    if PAY_REFRESH and "renewed" not in ck.get("steamLoginSecure", ""):
        back = RELAY_BASE + "checkout.steampowered.com/" + path + ("?" + urllib.parse.urlencode(q) if q else "")
        return "redirect", RELAY_BASE + "login.steampowered.com/jwt/refresh?redir=" + urllib.parse.quote(back, safe=""), None
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
            body = json.dumps({"rgWishlist": WISH, "rgOwnedPackages": [], "rgOwnedApps": [a for a, *_ in OWNED] + [228980, 1, 4406010],
                               "rgIgnoredApps": {str(a): r for a, r in IGNORED.items()}, "rgFollowedApps": sorted(FOLLOWED)} if ok else {}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            sys.stderr.write("FAKE GET userdata\n")
            return
        # Las reseñas de la tienda: desde la nube Steam las redirige a la portada, así que se simulan
        if FAKE and host == "store.steampowered.com" and path.startswith("appreviews/"):
            # RELAY_REV_ES=n: cuántas reseñas hay en español (las demás, en inglés y portugués)
            n_es = int(os.environ.get("RELAY_REV_ES", "3"))
            es = [("Una obra maestra. La historia y la música son increíbles.", True, 120),
                  ("Demasiado corto para lo que cuesta, pero bonito.", False, 8),
                  ("Lo jugué en la Steam Deck y va perfecto.", True, 45)][:n_es]
            other = [("english", "A masterpiece. Great story and amazing soundtrack.", True, 210),
                     ("english", "Runs great on the Steam Deck.", True, 33),
                     ("brazilian", "Muito bom, recomendo demais.", True, 12)]
            lang = q.get("language", "english")
            pool = [("spanish",) + r for r in es] + (other if lang == "all" else [])
            if lang not in ("all", "spanish"):
                pool = [r for r in other if r[0] == lang]
            if q.get("cursor", "*") != "*":
                pool = []
            pos = q.get("review_type") != "negative"
            revs = [{"recommendationid": str(9000 + i), "review": t, "voted_up": up, "language": lg, "author": {"playtime_forever": 60 * h}}
                    for i, (lg, t, up, h) in enumerate(pool) if q.get("review_type", "all") == "all" or up == pos]
            body = json.dumps({"success": 1, "query_summary": {"num_reviews": len(revs)}, "reviews": revs,
                               "cursor": "fin" if not revs else "AoJ"}).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if FAKE and host == "store.steampowered.com" and path == "account/history/":
            rows = [("18 OCT 2026", ["Hades II", "Hades II - Banda sonora"], "Compra", "Mastercard", "31,98&euro;"),
                    ("2 OCT 2026", ["Fondos del monedero de Steam"], "Compra", "PayPal", "20,00&euro;"),
                    ("14 SEP 2026", ["Stardew Valley"], "Regalo enviado", "Cartera", "13,99&euro;"),
                    ("30 AGO 2026", ["Cyberpunk 2077"], "Compra", "Cartera", "17,99&euro;")]
            trs = "".join('<tr class="wallet_table_row wallet_table_row_amt_change "><td class="wht_date">%s</td><td class="wht_items">%s'
                          '<div class="wth_payment"></div></td><td class="wht_type "><div>%s</div><div class="wth_payment">%s</div></td>'
                          '<td class="wht_total ">%s</td></tr>' % (d, "".join("<div>%s</div>" % i for i in it), t, pm, tot)
                          for d, it, t, pm, tot in rows)
            body = ('<html><table class="wallet_history_table"><tbody>%s</tbody></table></html>' % trs).encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=UTF-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if FAKE and WEB_REFRESH and host == "store.steampowered.com" and path in WEB_REFRESH_PATHS:
            ck = cookies_of(self.headers)
            if "renewed" not in ck.get("steamLoginSecure", ""):
                back = RELAY_BASE + host + "/" + path
                self.send_response(302)
                self.send_header("Location", RELAY_BASE + "login.steampowered.com/jwt/refresh?redir=" + urllib.parse.quote(back, safe=""))
                self.send_header("Content-Length", "0")
                self.end_headers()
                sys.stderr.write("FAKE %s %s -> 302 a jwt/refresh\n" % (method, path))
                return
            if method == "GET":
                self.send_response(404)
                self.send_header("Content-Length", "4")
                self.end_headers()
                self.wfile.write(b"null")
                sys.stderr.write("FAKE GET %s -> 404\n" % path)
                return
        if FAKE and (PAY_REFRESH or WEB_REFRESH) and host == "login.steampowered.com" and path == "jwt/refresh":
            ck = cookies_of(self.headers)
            self.send_response(302)
            if ck.get("steamRefresh_steam", "").startswith(str(FAKE_ID)):
                self.send_header("Set-Cookie", "steamLoginSecure=%d%%7C%%7Crenewed.fake.jwt0000000000; Path=/" % FAKE_ID)
                sys.stderr.write("FAKE jwt/refresh -> renovada\n")
            else:
                sys.stderr.write("FAKE jwt/refresh -> sin cookie de renovar, vuelve\n")
            self.send_header("Location", q.get("redir", ""))
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if FAKE and host == "checkout.steampowered.com" and path.startswith("checkout/"):
            r = fake_checkout(method, path, q, self.body, self.headers)
            if r is not None and r[0] == "redirect":
                self.send_response(302)
                self.send_header("Location", r[1])
                self.send_header("Content-Length", "0")
                self.end_headers()
                sys.stderr.write("FAKE %s %s -> 302 a jwt/refresh\n" % (method, path))
                return
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
        # RELAY_TALL_DIR: las dos ilustraciones destacadas de Kenji pasan a ser imágenes muy altas (tall.jpg y tall.gif)
        tall = os.environ.get("RELAY_TALL_DIR")
        if FAKE and tall and ((host == "steamcommunity.com" and path == "sharedfiles/filedetails/" and q.get("id") in ("2418659017", "2589121413"))
                              or (host == "images.steamusercontent.com" and path.startswith("ugc/TALL"))):
            if host == "steamcommunity.com":
                kind = "TALLGIF" if q.get("id") == "2418659017" else "TALLJPG"
                body = ('<meta property="og:image" content="https://images.steamusercontent.com/ugc/%s/X/?imw=512&amp;ima=fit">' % kind).encode()
                ct = "text/html; charset=UTF-8"
            else:
                gif = path.startswith("ugc/TALLGIF")
                body = open(os.path.join(tall, "tall.gif" if gif else "tall.jpg"), "rb").read()
                ct = "image/gif" if gif else "image/jpeg"
            self.send_response(200)
            self.send_header("Content-Type", ct)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            sys.stderr.write("FAKE TALL %s\n" % path)
            return
        if FAKE and host == "steamcommunity.com" and path == "profiles/%d/edit/settings" % FAKE_ID:
            ck = cookies_of(self.headers)
            ok = ck.get("steamLoginSecure", "").startswith(str(FAKE_ID))
            cfg = html.escape(json.dumps({"strPersonaName": "VENOMSHiNE (prueba)", "Privacy": PRIVACY}), quote=True)
            body = ('<html><div id="profile_edit_config" data-profile-edit="%s"></div></html>' % cfg if ok
                    else "<html>Iniciar sesión</html>").encode()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=UTF-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            sys.stderr.write("FAKE GET edit/settings\n")
            return
        # Los perfiles de las cuentas simuladas son perfiles públicos de verdad, muy personalizados
        if FAKE and ("GetProfileItemsEquipped" in path or "GetProfileCustomization" in path or path.startswith("profiles/")
                     or path.startswith("comment/Profile/render/")):
            for fid, real in PROFILE_AS.items():
                url = url.replace(str(fid), str(real))
        # El inventario de la cuenta simulada es uno público de verdad (el de Robin Walker)
        if FAKE and path.startswith("inventory/%d/" % FAKE_ID):
            url = url.replace(str(FAKE_ID), "76561197960435530")
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
