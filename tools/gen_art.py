#!/usr/bin/env python3
"""Arte de Steam3DS: icono del HOME, banner y sprites de la pantalla de carga.

El emblema es el logo de Steam redibujado con nuestra rueda de válvula en el aro
grande: de reojo es el mismo, al mirarlo bien es otro. Todo se dibuja a 8× y se
reduce, para bordes suaves.

Salida:
  meta/icon.png     48×48   icono del menú HOME
  meta/banner.png   256×128 banner del menú HOME
  gfx/logo.png      96×96   el logo dentro de la app (Acerca de)
  gfx/wheel.png     128×128 rueda (pantalla de carga, gira)
  gfx/steam.png     128×128 nube de vapor (textura de ruido, para las partículas)
  gfx/glow.png      128×128 resplandor
  gfx/bg_top.png    400×240 fondo sucio de la pantalla de carga (arriba)
  gfx/bg_bot.png    320×240 el de abajo
"""
import math
import os

import numpy as np

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


def valve_logo(size, angle=-90.0, spokes=5, light=False, square=False):
    """El logo de Steam redibujado con nuestras válvulas: de reojo es el mismo (el círculo con el
    degradado, la biela, el brazo y los dos aros), pero cada aro blanco es una rueda de válvula
    entera (aro, radios, cubo y agarres) y los círculos de dentro desaparecen. La biela y el brazo
    quedan detrás y la válvula delante, separada por una raya fina del color del fondo.

    La geometría está medida sobre el logo oficial en un lienzo de 1500. Devuelve RGBA de
    size×size (a 8×). light: el círculo en el azul clarito de la válvula de antes (para el banner).
    square: en vez del círculo, el cuadrado de bordes redondos de los iconos de la 3DS, con el
    mismo encuadre que el oficial y algo más grande (el brazo sigue saliendo por la izquierda)."""
    s = size * K
    f = s / 1500.0
    # Encuadre: en el cuadrado, el mismo que en el círculo oficial (centrado en su centro, no en la
    # caja de las válvulas: el brazo que sale por la izquierda equilibra que vayan algo a la derecha)
    z, ox, oy = (1.16, 749.5, 749.0) if square else (1.0, 750.0, 750.0)

    def P(x, y):
        return (((x - ox) * z + 750) * f, ((y - oy) * z + 750) * f)

    def disc(d, cx, cy, r, fill):
        (x, y), r = P(cx, cy), r * z * f
        d.ellipse([x - r, y - r, x + r, y + r], fill=fill)

    def layer():
        im = Image.new("L", (s, s), 0)
        return im, ImageDraw.Draw(im)

    def valve(cx, cy, R, rot):
        """Rueda de válvula blanca de radio exterior R (con los agarres un poco por fuera)."""
        im, d = layer()
        th = R * 0.19
        rr = R - th * 0.35                   # los agarres sobresalen un poco del aro
        disc(d, cx, cy, rr, 255)
        disc(d, cx, cy, rr - th, 0)
        for i in range(spokes):
            a = math.radians(rot + (i + 0.5) * 360 / spokes)
            disc(d, cx + math.cos(a) * (rr - th / 2), cy + math.sin(a) * (rr - th / 2), th * 0.85, 255)
        mid = rr - th / 2
        for i in range(spokes):
            a = math.radians(rot + i * 360 / spokes)
            d.line([P(cx, cy), P(cx + math.cos(a) * mid, cy + math.sin(a) * mid)], fill=255, width=max(1, int(R * 0.16 * z * f)))
        disc(d, cx, cy, R * 0.30, 255)
        disc(d, cx, cy, R * 0.11, 0)
        return im

    circle, cd = layer()
    if square:
        cd.rounded_rectangle([31 * f, 31 * f, 1469 * f, 1469 * f], radius=281 * f, fill=255)
    else:
        disc(cd, 749.5, 749.0, 688.0, 255)

    # Detrás: la biela y el brazo (entra por la izquierda, cortado por el círculo)
    back, bd = layer()
    BIG, SMALL = (975.5, 577.0), (538.0, 1015.0)
    # Los lados de la biela son los del oficial, alargados hasta dentro de cada válvula para que no
    # asome ninguna esquina por fuera del aro
    bd.polygon([P(797.5, 456), P(455, 958.4), P(600.5, 1102), P(1096.8, 746.7)], fill=255)
    ux, uy = math.cos(math.radians(22.4)), math.sin(math.radians(22.4))
    nx, ny = -uy, ux
    hw, ex, ey, far = 108.5, 530.0, 1013.0, 900.0
    bd.polygon([P(ex + nx * hw, ey + ny * hw), P(ex - nx * hw, ey - ny * hw),
                P(ex - nx * hw - ux * far, ey - ny * hw - uy * far), P(ex + nx * hw - ux * far, ey + ny * hw - uy * far)], fill=255)
    disc(bd, ex, ey, hw, 255)
    white = np.array(back)

    # Delante: las dos válvulas, cada una con una raya del color del fondo alrededor
    gap = max(2, int(16 * z * f)) | 1
    for (cx, cy), R, rot in ((BIG, 261.0, angle), (SMALL, 190.5, angle + 36)):
        v = valve(cx, cy, R, rot)
        # Lo de detrás no asoma entre los radios: la válvula lo tapa entera
        inner, idr = layer()
        disc(idr, cx, cy, R * (1 - 0.19 * 0.35), 255)
        halo = np.maximum(np.array(v.filter(ImageFilter.MaxFilter(gap))), np.array(inner))
        white = np.minimum(white, 255 - halo)
        white = np.maximum(white, np.array(v))
    white = np.minimum(white, np.array(circle))

    # Degradado del círculo, medido en el oficial: casi negro azulado arriba, azul Steam abajo
    stops = [(0.0, (0x10, 0x1d, 0x2d)), (0.17, (0x0a, 0x1b, 0x33)), (0.35, (0x07, 0x19, 0x38)),
             (0.6, (0x12, 0x2c, 0x60)), (0.75, (0x14, 0x4e, 0x81)), (0.86, (0x13, 0x64, 0x96)),
             (1.0, (0x14, 0x86, 0xbb))]
    if light:
        stops = [(0.0, (0x8f, 0xd2, 0xf8)), (0.5, ACC), (1.0, (0x3a, 0x9c, 0xe0))]
    t0, t1 = (31, 1469) if square else (60, 1438)
    t = np.clip((np.arange(s) / f - t0) / (t1 - t0), 0, 1)
    col = np.zeros((s, 3))
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        m = (t >= t0) & (t <= t1)
        k = ((t[m] - t0) / (t1 - t0))[:, None]
        col[m] = np.array(c0) * (1 - k) + np.array(c1) * k
    rgb = np.repeat(col[:, None, :], s, axis=1)
    w = white.astype(float)[..., None] / 255
    rgb = rgb * (1 - w) + 255 * w
    out = np.zeros((s, s, 4), dtype="uint8")
    out[..., :3] = np.clip(rgb, 0, 255).astype("uint8")
    out[..., 3] = np.array(circle)
    return Image.fromarray(out, "RGBA")


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


def steam(size, seed):
    """Vapor: ruido fractal dentro de una caída radial suave. Blanco con alfa."""
    import numpy as np
    rng = np.random.default_rng(seed)
    n = np.zeros((size, size))
    amp, total = 1.0, 0.0
    for octave in (4, 8, 16, 32):
        g = rng.random((octave + 1, octave + 1))
        layer = np.array(Image.fromarray((g * 255).astype("uint8")).resize((size, size), Image.BICUBIC), dtype=float) / 255
        n += layer * amp
        total += amp
        amp *= 0.55
    n /= total
    y, x = np.mgrid[0:size, 0:size]
    c = (size - 1) / 2
    d = np.clip(np.hypot(x - c, y - c) / c, 0, 1)
    fall = (1 - d) ** 1.8
    a = np.clip((n - 0.25) * 1.9, 0, 1) * fall
    a = a / a.max()
    a = a + rng.uniform(-0.6, 0.6, a.shape) / 255      # tramado: sin escalones al ampliarla
    rgba = np.zeros((size, size, 4), dtype="uint8")
    rgba[..., :3] = 255
    rgba[..., 3] = np.clip(a * 255, 0, 255).astype("uint8")
    return Image.fromarray(rgba, "RGBA").filter(ImageFilter.GaussianBlur(1.2))


def centered(im, size):
    """Coloca la forma centrada por su caja real (píxeles visibles), no por el lienzo."""
    box = im.getchannel("A").point(lambda v: 255 if v > 24 else 0).getbbox()
    shape = im.crop(box)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.alpha_composite(shape, ((size - shape.width) // 2, (size - shape.height) // 2))
    return out


def down(im, size):
    return im.resize(size, Image.LANCZOS)


def icon():
    s = 48 * K
    # El logo en la forma de los iconos de la 3DS: cuadrado de bordes redondos, todo centrado
    return down(valve_logo(48, square=True), (48, 48))


def inter(px, weight="Black"):
    """Inter Display (si está): más contundente que DejaVu para el nombre del banner"""
    p = "/usr/share/fonts/opentype/inter/InterDisplay-%s.otf" % weight
    return ImageFont.truetype(p, px) if os.path.exists(p) else font(px, bold=weight != "Medium")


def _paste_clip(dst, src, x, y):
    """alpha_composite que admite posiciones negativas (recorta lo que se sale)"""
    sx, sy = max(0, -x), max(0, -y)
    dst.alpha_composite(src.crop((sx, sy, src.width, src.height)), (max(0, x), max(0, y)))


def banner():
    """Banner del menú HOME (256×128): fondo azul noche con luz por la izquierda y vapor suave,
    el emblema grande con su halo y el nombre en grande; poco texto, para que se lea en la consola."""
    W, H = 256 * K, 128 * K
    y, x = np.mgrid[0:H, 0:W].astype(np.float32)
    # Fondo: degradado en diagonal y un foco de luz detrás del emblema
    t = np.clip((x / W) * 0.65 + (y / H) * 0.35, 0, 1)[..., None]
    top, bot = np.array((0x1f, 0x3a, 0x55), np.float32), np.array((0x0b, 0x10, 0x18), np.float32)
    img = top * (1 - t) + bot * t
    cx, cy = 58 * K, 64 * K
    r = np.sqrt((x - cx) ** 2 + (y - cy) ** 2) / (95 * K)
    img += np.array((0x30, 0x6a, 0x9a), np.float32) * np.clip(1 - r, 0, 1)[..., None] ** 2 * 0.9
    # Vapor: ruido suave en bandas que suben en diagonal
    rng = np.random.default_rng(7)
    n = Image.fromarray((rng.random((16, 32)) * 255).astype("uint8")).resize((W, H), Image.BICUBIC)
    n = np.asarray(n.filter(ImageFilter.GaussianBlur(10 * K)), np.float32) / 255
    band = 0.5 + 0.5 * np.sin((x * 0.6 - y) / (22 * K))
    img += (n * band)[..., None] * np.array((0x26, 0x40, 0x58), np.float32) * 0.6
    # Viñeta
    vx, vy = x / W - 0.5, y / H - 0.5
    img *= np.clip(1 - (vx * vx * 0.9 + vy * vy * 1.6) * 1.1, 0.35, 1)[..., None]
    im = Image.fromarray(np.clip(img, 0, 255).astype("uint8"), "RGB").convert("RGBA")
    # Emblema con halo
    logo = valve_logo(100)
    pad = 24 * K   # el halo necesita sitio alrededor para difuminarse sin cortes
    halo = Image.new("RGBA", (logo.width + 2 * pad, logo.height + 2 * pad), (0x66, 0xc0, 0xf4, 0))
    a = Image.new("L", halo.size, 0)
    a.paste(logo.getchannel("A").point(lambda v: v * 0.6), (pad, pad))
    halo.putalpha(a.filter(ImageFilter.GaussianBlur(8 * K)))
    lx, ly = int(8 * K), int(14 * K)
    im.alpha_composite(halo, (lx - pad, ly - pad)) if lx >= pad and ly >= pad else _paste_clip(im, halo, lx - pad, ly - pad)
    im.alpha_composite(logo, (lx, ly))
    d = ImageDraw.Draw(im)
    # El nombre: «Steam» en blanco y «3DS» en el azul de Steam, en grande
    x0, maxw = 116 * K, 132 * K
    px = 34
    while px > 12 and d.textlength("Steam3DS", font=inter(px * K)) > maxw:
        px -= 0.5
    f = inter(px * K)
    base = 64 * K
    sw = d.textlength("Steam", font=f)
    tw = d.textlength("Steam3DS", font=f)
    # Sombra suave bajo el nombre
    sh = Image.new("RGBA", im.size, (0, 0, 0, 0))
    ds = ImageDraw.Draw(sh)
    ds.text((x0, base + 2 * K), "Steam3DS", font=f, fill=(0, 0, 0, 170), anchor="ls")
    im.alpha_composite(sh.filter(ImageFilter.GaussianBlur(2.2 * K)))
    d.text((x0, base), "Steam", font=f, fill=(255, 255, 255, 255), anchor="ls")
    # «3DS» con degradado vertical (claro arriba)
    m = Image.new("L", im.size, 0)
    ImageDraw.Draw(m).text((x0 + sw, base), "3DS", font=f, fill=255, anchor="ls")
    g = vgrad(W, H, (0xa8, 0xe0, 0xff), (0x1a, 0x9f, 0xff))
    im.paste(g, (0, 0), m)
    # Raya fina y lema en mayúsculas espaciadas
    ly2 = base + 9 * K
    d.rounded_rectangle([x0 + K, ly2, x0 + tw, ly2 + int(1.6 * K)], radius=K, fill=(0x66, 0xc0, 0xf4, 150))
    tag = "TU STEAM EN LA 3DS"
    ft = inter(int(9.5 * K), "SemiBold")
    sp = 1.35 * K
    tx = x0 + K
    for ch in tag:
        d.text((tx, ly2 + 15 * K), ch, font=ft, fill=(0xd6, 0xe4, 0xef, 255), anchor="ls")
        tx += d.textlength(ch, font=ft) + sp
    fb = inter(int(8 * K), "Medium")
    d.text((x0 + K, ly2 + 31 * K), "por VENOMSHiNE", font=fb, fill=(0x8a, 0xa4, 0xb8, 255), anchor="ls")
    return down(im, (256, 128)).convert("RGB")


def grimy_bg(w, h, seed, light=(0.5, 0.42), pipe_cols=(), pipe_rows=()):
    """Fondo de sala de máquinas: degradado oscuro, manchas de humedad, grano y viñeta sucia.
    light: dónde da la luz (en fracción de la pantalla), un poco más claro alrededor."""
    import numpy as np
    rng = np.random.default_rng(seed)

    def noise(octaves, sz):
        out = np.zeros((h, w))
        amp = 1.0
        for o in octaves:
            g = rng.random((o + 1, int(o * w / h) + 1))
            out += amp * np.array(Image.fromarray((g * 255).astype("uint8")).resize((w, h), Image.BICUBIC), float) / 255
            amp *= 0.55
        return (out - out.min()) / (out.max() - out.min())

    y, x = np.mgrid[0:h, 0:w].astype(float)
    t = y / (h - 1)
    # Los tonos de la intro de Valve: sepia y bronce oscuros que caen a negro, con las sombras
    # algo azuladas
    top, bot = np.array([0x3a, 0x30, 0x28]), np.array([0x0a, 0x0c, 0x10])
    base = top[None, None, :] * (1 - t[..., None]) + bot[None, None, :] * t[..., None]
    # Manchas de humedad y mugre fina
    stains = noise((3, 6, 12, 24), 0)
    grime = noise((40, 80), 0)
    shade = 0.55 + 0.75 * stains * (0.8 + 0.4 * grime)
    # Churretes que caen desde arriba
    cols = noise((14,), 0)[0]
    drip_x = np.clip((cols - 0.62) * 6, 0, 1)
    drips = drip_x[None, :] * np.clip(1 - t[:, :1] * 1.4, 0, 1) * (0.6 + 0.4 * noise((30,), 0))
    shade -= 0.25 * drips
    # Luz de la válvula: centro algo más claro
    lx, ly = light[0] * w, light[1] * h
    d = np.hypot((x - lx) / w, (y - ly) / h * 0.8)
    glow = np.exp(-(d / 0.30) ** 2) * 0.45
    img = base * shade[..., None] + glow[..., None] * np.array([0x9a, 0x7a, 0x58])[None, None, :]
    # Sombras hacia el azul frío (lo oscuro azulado, lo claro cálido)
    lum = img.mean(-1, keepdims=True) / 255
    img = img + (1 - np.clip(lum * 4, 0, 1)) * np.array([-3, 1, 9])
    # Tuberías desenfocadas (fuera de foco, detrás de la válvula): dan profundidad y ambiente
    pipes = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    pd = ImageDraw.Draw(pipes)
    for px, pw in pipe_cols:
        X, W = px * w, pw * w
        pd.rectangle([X, 0, X + W, h], fill=(6, 9, 13, 235))
        pd.rectangle([X + W * 0.18, 0, X + W * 0.30, h], fill=(110, 92, 70, 110))    # brillo del metal
        for yy_ in range(int(h * 0.15), h, int(h * 0.38)):                            # abrazaderas
            pd.rectangle([X - W * 0.12, yy_, X + W * 1.12, yy_ + h * 0.035], fill=(4, 6, 9, 240))
    for py, ph in pipe_rows:
        Y, H = py * h, ph * h
        pd.rectangle([0, Y, w, Y + H], fill=(6, 9, 13, 225))
        pd.rectangle([0, Y + H * 0.15, w, Y + H * 0.28], fill=(110, 92, 70, 90))
    pipes = pipes.filter(ImageFilter.GaussianBlur(w * 0.012))
    pa = np.array(pipes, float)
    a = pa[..., 3:4] / 255
    img = img * (1 - a) + pa[..., :3] * a
    # Viñeta sucia: esquinas oscuras con el borde ahumado e irregular
    vx, vy = (x / w - 0.5) * 2, (y / h - 0.5) * 2
    vr = np.sqrt(vx * vx * 0.85 + vy * vy) + (noise((4, 8, 16), 0) - 0.5) * 0.35
    vign = np.clip(1 - np.clip((vr - 0.40) / 0.6, 0, 1) ** 1.3 * 0.97, 0, 1)
    img = img * vign[..., None]
    # Grano de película
    img += rng.normal(0, 4.0, img.shape)
    return Image.fromarray(np.clip(img, 0, 255).astype("uint8"), "RGB")


if __name__ == "__main__":
    os.makedirs(os.path.join(ROOT, "meta"), exist_ok=True)
    os.makedirs(os.path.join(ROOT, "gfx"), exist_ok=True)
    icon().save(os.path.join(ROOT, "meta", "icon.png"))
    banner().save(os.path.join(ROOT, "meta", "banner.png"))
    down(valve_logo(96), (96, 96)).save(os.path.join(ROOT, "gfx", "logo.png"))   # dentro de la app (Acerca de)
    down(wheel(128, angle=0), (128, 128)).save(os.path.join(ROOT, "gfx", "wheel.png"))
    steam(128, 2026).save(os.path.join(ROOT, "gfx", "steam.png"))
    glow(128).save(os.path.join(ROOT, "gfx", "glow.png"))
    grimy_bg(400, 240, 11, pipe_cols=((0.035, 0.07), (0.885, 0.085)), pipe_rows=((0.05, 0.09),)).save(os.path.join(ROOT, "gfx", "bg_top.png"))
    grimy_bg(320, 240, 12, light=(0.5, -0.4), pipe_cols=((0.02, 0.07), (0.9, 0.08))).save(os.path.join(ROOT, "gfx", "bg_bot.png"))
    print("meta/icon.png meta/banner.png gfx/wheel.png gfx/steam.png gfx/glow.png gfx/bg_top.png gfx/bg_bot.png")
