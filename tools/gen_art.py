#!/usr/bin/env python3
"""Arte de Steam3DS: icono del HOME, banner y sprites de la pantalla de carga.

El emblema es una rueda de válvula con vapor: recuerda a Steam/Valve sin
copiar su logo. Todo se dibuja a 8× y se reduce, para bordes suaves.

Salida:
  meta/icon.png     48×48   icono del menú HOME
  meta/banner.png   256×128 banner del menú HOME
  gfx/wheel.png     128×128 rueda (pantalla de carga, gira)
  gfx/puff.png      64×64   nube de vapor
  gfx/glow.png      128×128 resplandor
"""
import math
import os

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
K = 8

NAVY_TOP = (0x2a, 0x47, 0x5e)
NAVY_BOT = (0x17, 0x1a, 0x21)
ACC = (0x66, 0xc0, 0xf4)
LIGHT = (0xe8, 0xf4, 0xfc)


def font(px, bold=True):
    names = ["DejaVuSans-Bold.ttf", "LiberationSans-Bold.ttf"] if bold else ["DejaVuSans.ttf", "LiberationSans-Regular.ttf"]
    for d in ("/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/truetype/liberation"):
        for n in names:
            p = os.path.join(d, n)
            if os.path.exists(p):
                return ImageFont.truetype(p, px)
    return ImageFont.load_default()


def vgrad(w, h, top, bot):
    im = Image.new("RGBA", (w, h))
    d = ImageDraw.Draw(im)
    for y in range(h):
        t = y / max(1, h - 1)
        d.line([(0, y), (w, y)], fill=tuple(int(top[i] + (bot[i] - top[i]) * t) for i in range(3)) + (255,))
    return im


def wheel(size, angle=18.0, spokes=5, color_out=LIGHT, color_in=ACC):
    """Rueda de válvula: aro, radios y cubo, con un degradado claro→azul."""
    s = size * K
    c = s / 2
    R = s * 0.40
    mask = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(mask)
    th = R * 0.20
    d.ellipse([c - R, c - R, c + R, c + R], fill=255)
    d.ellipse([c - R + th, c - R + th, c + R - th, c + R - th], fill=0)
    # Agarres en el aro, entre radio y radio
    for i in range(spokes):
        a = math.radians(angle + (i + 0.5) * 360 / spokes)
        x, y = c + math.cos(a) * (R - th / 2), c + math.sin(a) * (R - th / 2)
        r = th * 0.78
        d.ellipse([x - r, y - r, x + r, y + r], fill=255)
    # Radios
    sw = R * 0.15
    for i in range(spokes):
        a = math.radians(angle + i * 360 / spokes)
        x2, y2 = c + math.cos(a) * (R - th / 2), c + math.sin(a) * (R - th / 2)
        d.line([(c, c), (x2, y2)], fill=255, width=int(sw))
    # Cubo
    hub = R * 0.30
    d.ellipse([c - hub, c - hub, c + hub, c + hub], fill=255)
    hole = R * 0.11
    d.ellipse([c - hole, c - hole, c + hole, c + hole], fill=0)
    grad = Image.new("RGBA", (s, s))
    gd = ImageDraw.Draw(grad)
    for y in range(s):
        t = y / (s - 1)
        gd.line([(0, y), (s, y)], fill=tuple(int(color_out[i] + (color_in[i] - color_out[i]) * t) for i in range(3)) + (255,))
    out = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    out.paste(grad, (0, 0), mask)
    return out


def puff(size, alpha=230):
    """Nube de vapor: tres círculos suaves."""
    s = size * K
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for cx, cy, r in ((0.40, 0.58, 0.26), (0.60, 0.50, 0.30), (0.52, 0.36, 0.22)):
        d.ellipse([(cx - r) * s, (cy - r) * s, (cx + r) * s, (cy + r) * s], fill=(255, 255, 255, alpha))
    return im.filter(ImageFilter.GaussianBlur(s * 0.03))


def glow(size):
    """Resplandor radial con caída suave y tramado en el alfa (sin escalones)."""
    import random
    rnd = random.Random(7)
    im = Image.new("RGBA", (size, size))
    px = im.load()
    c = (size - 1) / 2
    for y in range(size):
        for x in range(size):
            d = min(1.0, math.hypot(x - c, y - c) / c)
            a = (1 - d) ** 1.6 * 255 + rnd.uniform(-0.5, 0.5)
            px[x, y] = (ACC[0], ACC[1], ACC[2], max(0, min(255, int(a))))
    return im


def down(im, size):
    return im.resize(size, Image.LANCZOS)


def icon():
    s = 48 * K
    bg = vgrad(s, s, NAVY_TOP, NAVY_BOT)
    hl = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(hl).ellipse([-s * 0.2, -s * 0.5, s * 1.2, s * 0.55], fill=(255, 255, 255, 22))
    bg = Image.alpha_composite(bg, hl)
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([1 * K, 1 * K, s - 1 * K, s - 1 * K], radius=9 * K, fill=255)
    im = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    im.paste(bg, (0, 0), mask)
    w = wheel(40)
    im.alpha_composite(w, (int(2 * K), int(7 * K)))
    p = puff(20, 215)
    im.alpha_composite(p, (int(27 * K), int(1 * K)))
    p2 = down(puff(20, 150), (12 * K, 12 * K))
    im.alpha_composite(p2, (int(33 * K), int(-2 * K)))
    return down(im, (48, 48))


def banner():
    W, H = 256 * K, 128 * K
    im = vgrad(W, H, (0x24, 0x3b, 0x50), NAVY_BOT)
    g = down(glow(128), (int(150 * K), int(150 * K)))
    im.alpha_composite(g, (int(-12 * K), int(-11 * K)))
    w = wheel(84)
    im.alpha_composite(w, (int(12 * K), int(22 * K)))
    for (x, y, sz, a) in ((66, 6, 34, 200), (88, 0, 22, 140), (100, -4, 14, 90)):
        im.alpha_composite(down(puff(sz, a), (sz * K, sz * K)), (int(x * K), int(y * K)))
    d = ImageDraw.Draw(im)
    x0, maxw = 102 * K, 144 * K

    def fit(text, px, bold=True):
        # Letra más pequeña hasta que el texto quepa en el hueco
        while px > 6 and d.textlength(text, font=font(px * K, bold)) > maxw:
            px -= 0.5
        return font(px * K, bold)

    f = fit("Steam3DS", 30)
    y = 50 * K
    d.text((x0, y), "Steam", font=f, fill=(255, 255, 255, 255), anchor="ls")
    x = x0 + d.textlength("Steam", font=f) - 0.6 * K
    d.text((x, y), "3DS", font=f, fill=ACC + (255,), anchor="ls")
    d.line([(x0 + K, 62 * K), (x0 + maxw, 62 * K)], fill=(0x66, 0xc0, 0xf4, 110), width=int(1.2 * K))
    f2 = fit("Tienda · Ofertas · Biblioteca", 12, bold=False)
    d.text((x0 + K, 80 * K), "Tienda · Ofertas · Biblioteca", font=f2, fill=(0xc7, 0xd5, 0xe0, 255), anchor="ls")
    f3 = font(9 * K, bold=False)
    d.text((x0 + K, 98 * K), "hecho por VENOMSHiNE", font=f3, fill=(0x8f, 0x98, 0xa0, 255), anchor="ls")
    return down(im, (256, 128)).convert("RGB")


if __name__ == "__main__":
    os.makedirs(os.path.join(ROOT, "meta"), exist_ok=True)
    os.makedirs(os.path.join(ROOT, "gfx"), exist_ok=True)
    icon().save(os.path.join(ROOT, "meta", "icon.png"))
    banner().save(os.path.join(ROOT, "meta", "banner.png"))
    down(wheel(128, angle=0), (128, 128)).save(os.path.join(ROOT, "gfx", "wheel.png"))
    down(puff(64), (64, 64)).save(os.path.join(ROOT, "gfx", "puff.png"))
    glow(128).save(os.path.join(ROOT, "gfx", "glow.png"))
    print("meta/icon.png meta/banner.png gfx/wheel.png gfx/puff.png gfx/glow.png")
