#!/usr/bin/env python3
"""Grietas de la pared donde se clava la válvula de la pantalla de carga.

Salen del centro (lo que se clava es el cubo con su vástago) y corren hacia fuera por detrás de
los radios hasta pasar del aro, quebradas y con ramas; alrededor del cubo el yeso queda machacado
y desconchado. Cada grieta es oscura por dentro y tiene un filo claro del lado
del bombillo (arriba a la derecha), como un surco en la pared. Se abren en tres etapas (la consola
las cambia en el golpe, una tras otra, y se ven correr).

Salida:
  gfx/crack0.png, crack1.png, crack2.png   (CRACK×CRACK, el centro es el de la válvula)

Uso: python3 tools/gen_crack.py   (solo necesita numpy y Pillow)
"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CRACK = 200                     # en pantalla, a tamaño real (la válvula mide unos 100 px)
SS = 4                          # supermuestreo
HUB = 9.0                       # radio donde empiezan (el borde del vástago, bajo el cubo)
RIM = 46.0                      # el aro de la válvula
STAGES = (0.30, 0.62, 1.0)      # hasta dónde ha corrido cada grieta en cada etapa


def walk(rng, x, y, ang, length, width, t0, speed, out, depth=0):
    """Una grieta: pasos cortos que se tuercen al azar y a veces se parten en dos."""
    pts = [(x, y, width, t0)]
    d = 0.0
    while d < length:
        # tramos rectos con quiebros secos, como se raja el yeso
        step = 2.5 + rng.random() * 3.5
        ang += rng.normal(0, 0.42)
        # sin perder del todo el rumbo hacia fuera
        out_ang = math.atan2(y, x)
        diff = (out_ang - ang + math.pi) % (2 * math.pi) - math.pi
        ang += diff * 0.08
        x += math.cos(ang) * step
        y += math.sin(ang) * step
        d += step
        w = width * (1 - 0.75 * d / length)
        pts.append((x, y, w, t0 + d / speed))
        if depth < 2 and d > 5 and rng.random() < 0.13 * (1 if depth == 0 else 0.6):
            side = 1 if rng.random() < 0.5 else -1
            walk(rng, x, y, ang + side * (0.45 + rng.random() * 0.6), (length - d) * (0.35 + rng.random() * 0.4),
                 w * 0.7, t0 + d / speed, speed * 0.9, out, depth + 1)
        if math.hypot(x, y) > CRACK / 2 - 6:
            break
    out.append(pts)


def build(seed=5):
    rng = np.random.default_rng(seed)
    cracks = []
    n = 8
    for i in range(n):
        a = (i + rng.random() * 0.7) * 2 * math.pi / n
        r = HUB + rng.random() * 2
        length = 52 + rng.random() * 30
        if i % 3 == 0:
            length += 16                     # unas pocas llegan más lejos
        walk(rng, math.cos(a) * r, math.sin(a) * r, a + rng.normal(0, 0.25), length, 1.5 + rng.random() * 0.6,
             0.0, 90.0 + rng.random() * 30, cracks)
    # grietas finas entre las grandes
    for i in range(10):
        a = rng.random() * 2 * math.pi
        r = HUB + 4 + rng.random() * 8
        walk(rng, math.cos(a) * r, math.sin(a) * r, a + rng.normal(0, 0.5), 8 + rng.random() * 16, 0.8,
             0.05 + rng.random() * 0.15, 80.0, cracks, depth=2)
    return rng, cracks


def render(rng_seed, cracks, limit):
    S = CRACK * SS
    c = S / 2
    core = Image.new('L', (S, S), 0)
    lit = Image.new('L', (S, S), 0)
    dc, dl = ImageDraw.Draw(core), ImageDraw.Draw(lit)
    for pts in cracks:
        for (x0, y0, w0, t0), (x1, y1, w1, t1) in zip(pts, pts[1:]):
            if t1 > limit:
                break
            w = max(1, int(round((w0 + w1) / 2 * SS)))
            a = (c + x0 * SS, c + y0 * SS)
            b = (c + x1 * SS, c + y1 * SS)
            dc.line([a, b], fill=255, width=w)
            dc.ellipse([b[0] - w / 2, b[1] - w / 2, b[0] + w / 2, b[1] + w / 2], fill=255)
            # el filo que da a la luz: la misma grieta, corrida hacia arriba a la derecha
            o = 0.7 * SS
            dl.line([(a[0] + o, a[1] - o), (b[0] + o, b[1] - o)], fill=255, width=max(1, int(w * 0.6)))
    core_a = np.array(core, float) / 255
    lit_a = np.clip(np.array(lit, float) / 255 - core_a, 0, 1)

    # El yeso machacado alrededor del cubo: un anillo de manchas y desconchones
    rng = np.random.default_rng(rng_seed + 100)
    y, x = (np.mgrid[0:S, 0:S] - c) / SS
    r = np.hypot(x, y)
    ang = np.arctan2(y, x)
    wob = np.zeros_like(r)
    for k in (3, 5, 9, 14, 23):
        wob += np.sin(ang * k + rng.random() * 6.28) * (1.6 / math.sqrt(k))
    ring = np.clip(1 - np.abs(r - (HUB + 9 + wob)) / (6 + wob * 0.5), 0, 1)
    grain = np.array(Image.fromarray((rng.random((S // 6, S // 6)) * 255).astype('uint8')).resize((S, S), Image.BICUBIC), float) / 255
    crush = np.clip(ring * (0.25 + 0.55 * grain), 0, 1)
    chips = Image.new('L', (S, S), 0)
    dch = ImageDraw.Draw(chips)
    for _ in range(26):
        a = rng.random() * 2 * math.pi
        rr = (HUB + 5 + rng.random() * 12) * SS
        px, py = c + math.cos(a) * rr, c + math.sin(a) * rr
        sz = (1.2 + rng.random() * 2.6) * SS
        poly = [(px + math.cos(a + k * 2.1 + rng.random()) * sz, py + math.sin(a + k * 2.1 + rng.random()) * sz) for k in range(3)]
        dch.polygon(poly, fill=int(150 + rng.random() * 100))
    chips_a = np.array(chips.filter(ImageFilter.GaussianBlur(0.6 * SS)), float) / 255
    inner = np.clip((r - (HUB - 3)) / 3, 0, 1)                       # el agujero del vástago, tapado

    dark = np.clip(np.maximum(core_a * 0.92, np.maximum(crush * 0.55, chips_a * 0.75)), 0, 1) * inner
    light = np.clip(lit_a * 0.28, 0, 1) * inner * (1 - dark)
    # Premultiplicado: lo oscuro casi negro, el filo de color yeso con la luz cálida
    a = np.clip(dark + light, 0, 1)
    col = np.zeros((S, S, 3))
    col += light[..., None] * np.array([0.86, 0.80, 0.70])
    col += dark[..., None] * np.array([0.02, 0.025, 0.035])
    rgba = np.zeros((S, S, 4))
    rgba[..., :3] = col
    rgba[..., 3] = a
    # Se reduce premultiplicada y se divide por el alfa (sin halos)
    ch = [np.array(Image.fromarray(rgba[..., i].astype('float32'), 'F').resize((CRACK, CRACK), Image.BOX)) for i in range(4)]
    al = np.clip(ch[3], 0, 1)
    out = np.zeros((CRACK, CRACK, 4))
    for i in range(3):
        out[..., i] = np.where(al > 1e-3, ch[i] / np.maximum(al, 1e-3), 0)
    out[..., 3] = al
    return Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype('uint8'), 'RGBA')


if __name__ == "__main__":
    _, cracks = build()
    tmax = max(p[-1][3] for p in cracks)
    for i, st in enumerate(STAGES):
        render(5, cracks, st * tmax).save(os.path.join(ROOT, "gfx", "crack%d.png" % i))
    print("gfx/crack0..%d.png" % (len(STAGES) - 1))
