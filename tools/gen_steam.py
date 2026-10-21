#!/usr/bin/env python3
"""Flipbook de vapor para la pantalla de carga (gfx/steam/fNN.png).

Como en los juegos actuales: no es una nube dibujada, es una simulación de
fluido de verdad (stable fluids de Jos Stam: advección semilagrangiana,
proyección de presión y confinamiento de vorticidad para los rizos) de una
bocanada que se expande, se enrosca y se deshace. Cada fotograma se ilumina
al hornearlo:
  - luz que entra desde arriba y atrás, con sombra propia (transmitancia a
    través de la densidad que tiene encima);
  - borde brillante (contraluz) donde el vapor es fino;
  - sombras algo azuladas, para que case con el fondo de la tienda;
  - erosión: al final se deshace en hilachas en vez de apagarse entero.
En la consola cada partícula reproduce estos fotogramas mientras vive.

Uso: python3 tools/gen_steam.py   (solo necesita numpy y Pillow)
"""
import os

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
N = 160            # rejilla de la simulación
OUT = 128          # tamaño de cada fotograma
FRAMES = 24
STEPS_PER_FRAME = 3
rng = np.random.default_rng(1998)

yy, xx = np.mgrid[0:N, 0:N].astype(np.float64)


def sample(f, x, y):
    """Interpolación bilineal de f en (x, y), con los bordes sujetos."""
    x = np.clip(x, 0, N - 1.001)
    y = np.clip(y, 0, N - 1.001)
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - x0, y - y0
    return (f[y0, x0] * (1 - fx) * (1 - fy) + f[y0, x0 + 1] * fx * (1 - fy) +
            f[y0 + 1, x0] * (1 - fx) * fy + f[y0 + 1, x0 + 1] * fx * fy)


def advect(f, u, v, dt):
    return sample(f, xx - u * dt, yy - v * dt)


def project(u, v, iters=40):
    """Quita la divergencia: el vapor se mueve como un fluido, sin «inflarse»."""
    div = 0.5 * (np.roll(u, -1, 1) - np.roll(u, 1, 1) + np.roll(v, -1, 0) - np.roll(v, 1, 0))
    p = np.zeros_like(u)
    for _ in range(iters):
        p = (np.roll(p, 1, 0) + np.roll(p, -1, 0) + np.roll(p, 1, 1) + np.roll(p, -1, 1) - div) / 4
    u -= 0.5 * (np.roll(p, -1, 1) - np.roll(p, 1, 1))
    v -= 0.5 * (np.roll(p, -1, 0) - np.roll(p, 1, 0))
    return u, v


def vorticity(u, v, eps, dt):
    """Confinamiento de vorticidad: devuelve los remolinos pequeños que la rejilla pierde."""
    w = 0.5 * (np.roll(v, -1, 1) - np.roll(v, 1, 1) - np.roll(u, -1, 0) + np.roll(u, 1, 0))
    aw = np.abs(w)
    gx = 0.5 * (np.roll(aw, -1, 1) - np.roll(aw, 1, 1))
    gy = 0.5 * (np.roll(aw, -1, 0) - np.roll(aw, 1, 0))
    mag = np.sqrt(gx * gx + gy * gy) + 1e-6
    nx, ny = gx / mag, gy / mag
    u += eps * ny * w * dt
    v -= eps * nx * w * dt
    return u, v


def fbm(size, octaves=(6, 12, 24, 48)):
    out = np.zeros((size, size))
    amp = 1.0
    for o in octaves:
        g = rng.random((o + 1, o + 1))
        out += amp * np.array(Image.fromarray((g * 255).astype(np.uint8)).resize((size, size), Image.BICUBIC), float) / 255
        amp *= 0.5
    return (out - out.min()) / (out.max() - out.min())


def simulate():
    c = N / 2
    r = np.hypot(xx - c, yy - c)
    # Bocanada inicial: un núcleo denso con el borde roto por ruido
    d = np.clip(1 - r / 22, 0, 1) ** 1.1 * (0.6 + 0.8 * fbm(N))
    # Velocidad: empuje hacia fuera (la bocanada se abre) más remolinos pequeños (ruido de rizo)
    u = (xx - c) / (r + 1) * 1.1 * np.exp(-r / 24)
    v = (yy - c) / (r + 1) * 1.1 * np.exp(-r / 24)
    psi = (fbm(N, (10, 20, 40)) - 0.5) * 34
    u += 0.5 * (np.roll(psi, -1, 0) - np.roll(psi, 1, 0))
    v -= 0.5 * (np.roll(psi, -1, 1) - np.roll(psi, 1, 1))
    u, v = project(u, v)
    frames = []
    dt = 1.0
    for step in range(FRAMES * STEPS_PER_FRAME):
        u, v = vorticity(u, v, 0.8, dt)
        # Flotación: el vapor caliente sube y se enrosca como un champiñón
        v -= 0.045 * d * dt
        u, v = advect(u, u, v, dt), advect(v, u, v, dt)
        u *= 0.992
        v *= 0.992
        u, v = project(u, v)
        d = advect(d, u, v, dt) * 0.995
        if step % STEPS_PER_FRAME == STEPS_PER_FRAME - 1:
            frames.append(d.copy())
    return frames


def render(d, k):
    """Ilumina un fotograma de densidad y devuelve RGBA."""
    t = k / (FRAMES - 1)
    # Centrado por el centro de masa (la bocanada sube; el movimiento lo pone la partícula)
    tot = d.sum() + 1e-9
    cy = int(round((d * yy).sum() / tot)) - OUT // 2
    cx = int(round((d * xx).sum() / tot)) - OUT // 2
    cy = min(max(cy, 0), N - OUT)
    cx = min(max(cx, 0), N - OUT)
    d = d[cy:cy + OUT, cx:cx + OUT]
    alpha = 1 - np.exp(-d * 4.0)
    # Erosión: el umbral sube con la edad y el vapor se deshace en hilachas
    e = 0.03 + 0.30 * t ** 1.7
    alpha = np.clip((alpha - e) / (1 - e), 0, 1)
    # Luz desde arriba y un poco por detrás: lo que tiene densidad encima queda en sombra
    above = np.cumsum(d, axis=0) - d
    shade = np.exp(-above * 0.10)
    # Contraluz: el vapor fino de los bordes se ilumina
    rim = 4 * alpha * (1 - alpha)
    light = np.clip(0.30 + 0.62 * shade + 0.35 * rim, 0, 1)
    lit = np.array([1.0, 1.0, 1.0])
    dark = np.array([0.50, 0.60, 0.72])       # sombras azuladas, como el fondo
    rgb = dark[None, None, :] + (lit - dark)[None, None, :] * light[..., None]
    # Los bordes del fotograma, siempre transparentes (la partícula no se «corta»)
    yy2, xx2 = np.mgrid[0:OUT, 0:OUT]
    edge = np.clip((OUT / 2 - np.hypot(xx2 - OUT / 2 + 0.5, yy2 - OUT / 2 + 0.5)) / 10, 0, 1)
    alpha = alpha * edge
    # Tramado: sin escalones al ampliarlo
    alpha = alpha + rng.uniform(-0.5, 0.5, alpha.shape) / 255
    out = np.zeros((OUT, OUT, 4), np.uint8)
    out[..., :3] = np.clip(rgb * 255, 0, 255).astype(np.uint8)
    out[..., 3] = np.clip(alpha * 255, 0, 255).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def main():
    frames = simulate()
    peak = max(f.max() for f in frames[:3])
    folder = os.path.join(ROOT, "gfx", "steam")
    os.makedirs(folder, exist_ok=True)
    for k, f in enumerate(frames):
        render(f / peak, k).save(os.path.join(folder, "f%02d.png" % k))
    # Hoja de prueba para mirarlos todos de un vistazo
    sheet = Image.new("RGBA", (OUT * 8, OUT * 3), (0x1b, 0x28, 0x38, 255))
    for k in range(FRAMES):
        im = Image.open(os.path.join(folder, "f%02d.png" % k))
        sheet.alpha_composite(im, ((k % 8) * OUT, (k // 8) * OUT))
    sheet.save(os.path.join(ROOT, "tools", "steam_preview.png"))
    print("gfx/steam/f00..f%02d.png" % (FRAMES - 1))


if __name__ == "__main__":
    main()
