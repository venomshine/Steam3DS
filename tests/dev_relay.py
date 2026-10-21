#!/usr/bin/env python3
"""Relé para probar Steam3DS en el emulador desde la nube.

La build de pruebas (make DEV=1) cambia https://HOST/RUTA por
http://127.0.0.1:8099/HOST/RUTA. Este relé hace la petición real a Steam
y devuelve la respuesta tal cual (cuerpo, Content-Type y Set-Cookie).
La build que se publica nunca usa esto.

Uso: python3 tests/dev_relay.py [--offline CARPETA]
  --offline guarda/sirve las respuestas desde CARPETA (pruebas repetibles).
"""
import hashlib
import json
import os
import sys
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

PORT = 8099
ALLOWED = ("store.steampowered.com", "api.steampowered.com", "shared.akamai.steamstatic.com",
           "shared.fastly.steamstatic.com", "cdn.akamai.steamstatic.com", "steamcdn-a.akamaihd.net")
CACHE = None


def fetch(method, url):
    key = hashlib.sha1((method + " " + url).encode()).hexdigest()
    if CACHE:
        path = os.path.join(CACHE, key)
        if os.path.exists(path + ".json"):
            meta = json.load(open(path + ".json"))
            return meta["status"], meta["headers"], open(path + ".body", "rb").read()
    req = urllib.request.Request(url, method=method, headers={
        "User-Agent": "Steam3DS-dev-relay", "Accept-Encoding": "identity"})
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


class Relay(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def handle_any(self, method):
        host, _, rest = self.path.lstrip("/").partition("/")
        if host not in ALLOWED:
            self.send_error(403, "host no permitido")
            return
        url = "https://%s/%s" % (host, rest)
        try:
            status, headers, body = fetch(method, url)
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
        self.handle_any("GET")

    def do_HEAD(self):
        self.handle_any("HEAD")

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    if "--offline" in sys.argv:
        CACHE = sys.argv[sys.argv.index("--offline") + 1]
        os.makedirs(CACHE, exist_ok=True)
    ThreadingHTTPServer(("127.0.0.1", PORT), Relay).serve_forever()
