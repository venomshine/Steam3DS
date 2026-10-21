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
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = 8099
ALLOWED = ("store.steampowered.com", "api.steampowered.com", "shared.akamai.steamstatic.com",
           "shared.fastly.steamstatic.com", "cdn.akamai.steamstatic.com", "steamcdn-a.akamaihd.net",
           "avatars.steamstatic.com", "avatars.akamai.steamstatic.com")
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


def fake_jwt(kind):
    now = int(time.time())
    life = 86400 if kind == "access" else 200 * 86400
    payload = {"iss": "steam", "sub": str(FAKE_ID), "aud": ["web", kind], "exp": now + life,
               "iat": now, "jti": "18E3_0000_FAKE" if kind == "refresh" else "18E3_ACCESS"}
    return "eyJhbGciOiJFZERTQSJ9." + b64url(payload) + ".firma"


FRIENDS = [76561190000000101, 76561190000000102, 76561190000000103]
NAMES = {FAKE_ID: "VENOMSHiNE (prueba)", 76561190000000101: "Kenji", 76561190000000102: "Lucía", 76561190000000103: "Marco"}
OWNED = [(1091500, "Cyberpunk 2077", 6120, 3), (381210, "Dead by Daylight", 2410, 1), (1145360, "Hades", 3900, 20),
         (292030, "The Witcher 3: Wild Hunt", 9300, 60), (413150, "Stardew Valley", 5200, 9), (730, "Counter-Strike 2", 12000, 2),
         (570, "Dota 2", 0, 400), (1203620, "Enshrouded", 1500, 12)]
WISH = [2138330, 1174180, 1716740, 668580]


def fake_post(path, body):
    form = dict(urllib.parse.parse_qsl(body))
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
        return {"response": {"access_token": fake_jwt("access")}}
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
        now = int(time.time())
        return {"response": {"game_count": len(OWNED), "games": [
            {"appid": a, "name": n, "playtime_forever": m, "rtime_last_played": now - d * 86400, "img_icon_url": ""}
            for a, n, m, d in OWNED]}}
    if path.endswith("GetWishlist/v1/"):
        return {"response": {"items": [{"appid": a, "priority": i + 1, "date_added": int(time.time()) - i * 86400 * 7}
                                       for i, a in enumerate(WISH)]}}
    if path.endswith("GetFriendsGameplayInfo/v1/"):
        appid = int(q.get("appid", inp.get("appid", 0)) or 0)
        owned = [a for a, *_ in OWNED]
        if appid in owned:
            return {"response": {"your_info": {"steamid": str(FAKE_ID), "owned": True},
                                 "owns": [{"steamid": str(f), "minutes_played_forever": 300} for f in FRIENDS],
                                 "in_game": [{"steamid": str(FRIENDS[0]), "minutes_played": 20}]}}
        return {"response": {"in_wishlist": [{"steamid": str(FRIENDS[1])}]}}
    return None


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
        fake = None
        if FAKE and method == "POST":
            fake = fake_post(path, self.body)
        elif FAKE and q.get("access_token", "").startswith("eyJhbGciOiJFZERTQSJ9."):
            fake = fake_get(path, q)
        if fake is not None:
            body = json.dumps(fake).encode()
            self.send_response(200)
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
