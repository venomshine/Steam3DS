#!/usr/bin/env python3
"""Genera source/i18n_table.h a partir de tools/i18n_tr.py.

Busca en source/*.c los textos de _("…") y avisa de los que no tienen traducción
y de las traducciones que ya no se usan. Uso: python3 tools/gen_i18n.py
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from i18n_tr import TR, INDIRECT  # noqa: E402

STR = r'"((?:[^"\\]|\\.)*)"'


def used_keys():
    keys = set()
    for name in sorted(os.listdir(os.path.join(ROOT, "source"))):
        if not name.endswith(".c"):
            continue
        src = open(os.path.join(ROOT, "source", name), encoding="utf-8").read()
        for m in re.finditer(r'\b(?:_|tr)\(\s*' + STR, src):
            keys.add(m.group(1))
    return keys


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def main():
    used = used_keys() | set(INDIRECT)
    missing = sorted(k for k in used if k not in TR)
    unused = sorted(k for k in TR if k not in used)
    for k in missing:
        print("SIN TRADUCIR:", k)
    for k in unused:
        print("SIN USO:", k)
    for k, (en, ja) in TR.items():
        for v in (en, ja):
            if k.count("%") != v.count("%"):
                print("FORMATO DISTINTO:", k, "->", v)
    rows = sorted(TR.items(), key=lambda kv: kv[0].encode("utf-8"))
    out = ["// Generado por tools/gen_i18n.py: no editar a mano.",
           "static const TrEntry tr_table[] = {"]
    for k, (en, ja) in rows:
        out.append("\t{%s, %s, %s}," % (c_str(k), c_str(en), c_str(ja)))
    out.append("};")
    open(os.path.join(ROOT, "source", "i18n_table.h"), "w", encoding="utf-8").write("\n".join(out) + "\n")
    print("%d textos, %d sin traducir, %d sin uso" % (len(rows), len(missing), len(unused)))
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main())
