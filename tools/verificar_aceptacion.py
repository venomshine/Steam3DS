#!/usr/bin/env python3
"""Comprueba si un registro de aceptación de las condiciones (aceptacion.txt) es auténtico.

El registro va firmado por Steam3DS (HMAC-SHA256 con la clave de la compilación oficial y la consola
donde se aceptó). Si alguien lo ha escrito a mano, lo ha cambiado o lo ha copiado de otra consola, la
firma no cuadra.

Uso:
  python3 tools/verificar_aceptacion.py aceptacion.txt [--clave secret/terms.key]

La clave es la de secret/terms.key (no está en el repositorio: guárdala tú).
"""
import argparse
import hashlib
import hmac
import os
import re
import sys

SIG = "Firma: "


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("registro")
    ap.add_argument("--clave", default=os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "secret", "terms.key"))
    a = ap.parse_args()
    data = open(a.registro, "rb").read()
    key = open(a.clave).read().strip()
    i = data.rfind(b"\n" + SIG.encode())
    if i < 0:
        print("NO VÁLIDO: el registro no tiene firma")
        return 1
    body, sig = data[: i + 1], data[i + 1 + len(SIG):].strip().decode("ascii", "replace")
    text = body.decode("utf-8", "replace")
    m = re.search(r"^Consola: ([0-9a-f]{16})$", text, re.M)
    if not m:
        print("NO VÁLIDO: falta la consola")
        return 1
    want = hmac.new(("%s|%s" % (key, m.group(1))).encode(), body, hashlib.sha256).hexdigest()
    if not hmac.compare_digest(want, sig):
        print("NO VÁLIDO: la firma no cuadra (cambiado a mano, de otra compilación o con otra clave)")
        return 1
    print("AUTÉNTICO: generado por Steam3DS y sin cambios\n")
    for k in ("Versión de las condiciones", "Versión de la aplicación", "Fecha y hora (reloj de la consola)", "Consola",
              "Idioma en que se leyeron", "Huella del texto aceptado (SHA-256)"):
        mm = re.search(r"^%s: (.*)$" % re.escape(k), text, re.M)
        print("  %s: %s" % (k, mm.group(1) if mm else "?"))
    acc = text.split("Cuentas de Steam con las que se ha iniciado sesión después de aceptar:\n", 1)
    print("\n  Cuentas:")
    print("".join("    " + l + "\n" for l in (acc[1].splitlines() if len(acc) > 1 else []) if l.strip()) or "    (ninguna)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
