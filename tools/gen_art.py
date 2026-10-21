#!/usr/bin/env python3
"""Genera el icono del menú HOME (meta/icon.png, 48×48).

Se dibuja a 8× y se reduce, para que los bordes salgan suaves.
Provisional: el arte definitivo se hará en la fase final.
"""
import os
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
K = 8


def icon():
    s = 48 * K
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    # Fondo: degradado azul oscuro de la tienda
    top, bot = (0x2a, 0x47, 0x5e), (0x17, 0x1a, 0x21)
    grad = Image.new("RGBA", (s, s))
    gd = ImageDraw.Draw(grad)
    for y in range(s):
        t = y / (s - 1)
        gd.line([(0, y), (s, y)], fill=tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3)) + (255,))
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([2 * K, 2 * K, s - 2 * K, s - 2 * K], radius=9 * K, fill=255)
    im.paste(grad, (0, 0), mask)
    # Bolsa de la tienda con el 3
    acc = (0x66, 0xc0, 0xf4, 255)
    d.rounded_rectangle([12 * K, 17 * K, 36 * K, 38 * K], radius=3 * K, fill=acc)
    d.arc([17 * K, 9 * K, 31 * K, 23 * K], 180, 360, fill=acc, width=int(2.5 * K))
    d.text((24 * K, 28 * K), "3", fill=(0x17, 0x1a, 0x21, 255), anchor="mm",
           font=_font(15 * K))
    return im.resize((48, 48), Image.LANCZOS)


def _font(px):
    from PIL import ImageFont
    for f in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
              "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf"):
        if os.path.exists(f):
            return ImageFont.truetype(f, px)
    return ImageFont.load_default()


if __name__ == "__main__":
    os.makedirs(os.path.join(ROOT, "meta"), exist_ok=True)
    icon().save(os.path.join(ROOT, "meta", "icon.png"))
    print("meta/icon.png")
