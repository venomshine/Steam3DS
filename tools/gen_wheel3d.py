#!/usr/bin/env python3
"""Válvula de la pantalla de carga, en 3D y con la luz de un bombillo que se balancea.

La válvula es un mapa de alturas (aro como tubo, radios cilíndricos, cubo abombado con su
agujero y agarres esféricos), sucia de mugre negra pegada a ella, e iluminada por un bombillo
flojo y cálido arriba a la derecha. Tiene cinco radios, así que la forma se repite cada 72°:
basta con 24 fotogramas de 3°. La mugre no se repite (si se horneara, saltaría 72° atrás al
volver al primer fotograma): va en una capa aparte que la consola gira entera. Se hornean con la luz en los dos extremos del balanceo y la
consola los mezcla según dónde esté el bombillo (y gira la imagen lo que falte entre dos
fotogramas, como mucho 1,5°).

Salida:
  gfx/w3d/aNN.png   24 fotogramas, luz en un extremo del balanceo
  gfx/w3d/bNN.png   24 fotogramas, luz en el otro
  gfx/wheel_dirt.png     la mugre (negra, con arañazos claros), para dibujar encima girada
  gfx/wheel_shadow.png   la silueta difuminada (la sombra en la pared; la consola la gira)

Uso: python3 tools/gen_wheel3d.py   (solo necesita numpy y Pillow)
"""
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SIZE = 112                      # cada fotograma (en pantalla se ve a ~100 px)
FRAMES = 24                     # 72° / 3°
SWING = (22.0, 68.0)            # extremos del balanceo: ángulo de la luz sobre la horizontal (arriba a la derecha)
# Bombillo flojo y cálido, válvula muy sucia (mugre negra)
# Pintada de rojo como la de la intro de Valve, pero de pintura de válvula de verdad (algo apagada,
# con brillo moderado), sucia y con el cubo de hierro oscuro
LOOK = dict(dirt=1.0, amb=0.24, kd=1.05, tint=(1.0, 0.92, 0.80), ks=0.70, paint=True)


def light_dir(th):
    t = math.radians(th)
    return (math.cos(t) * 0.8, -math.sin(t) * 0.8, 0.42)


def blur(a, r):
    """Desenfoque gaussiano separable (en float)."""
    k = int(r * 3) + 1
    g = np.exp(-(np.arange(-k, k + 1) ** 2) / (2 * r * r))
    g /= g.sum()
    p = np.pad(a, k, mode='edge')
    p = np.apply_along_axis(lambda v: np.convolve(v, g, 'valid'), 1, p)
    p = np.apply_along_axis(lambda v: np.convolve(v, g, 'valid'), 0, p)
    return p


def grime_map(s, seed=7):
    """Suciedad en el espacio de la válvula (gira con ella): manchas, mugre y arañazos. s×s, 0..1."""
    rng = np.random.default_rng(seed)
    def noise(octaves):
        out = np.zeros((s, s)); amp = 1.0; tot = 0
        for o in octaves:
            g = rng.random((o + 1, o + 1))
            out += amp * np.array(Image.fromarray((g * 255).astype('uint8')).resize((s, s), Image.BICUBIC), float) / 255
            tot += amp; amp *= 0.55
        return out / tot
    blot = noise((3, 6, 12, 24))              # manchas grandes
    fine = noise((48, 96))                    # grano fino
    spots = np.clip((noise((4, 8, 16)) - 0.58) * 4, 0, 1)  # salpicaduras
    # arañazos: rayitas cortas en direcciones al azar
    sc = Image.new('L', (s, s), 0); d = ImageDraw.Draw(sc)
    for _ in range(140):
        x, y = rng.random() * s, rng.random() * s
        a = rng.random() * math.pi; l = s * (0.02 + rng.random() * 0.07)
        d.line([(x, y), (x + math.cos(a) * l, y + math.sin(a) * l)], fill=int(90 + rng.random() * 120), width=max(1, s // 400))
    scratches = np.array(sc, float) / 255
    return blot, fine, spots, scratches


def wheel3d(size, angle=0.0, spokes=5, light=(0.62, -0.62, 0.48), relief=0.75, ss=4, dirt=0.0, amb=0.42, kd=0.78, tint=(1, 1, 1), ks=0.75, split=False, paint=False):
    """Válvula con relieve: mapa de alturas (aro como tubo, radios cilíndricos, cubo abombado y
    agarres esféricos) iluminado desde arriba a la derecha. Devuelve RGBA size×size.
    split: la mugre que no se repite cada 72° (manchas, grano, salpicaduras y arañazos) no se
    hornea; se devuelve aparte como capa (la consola la gira entera, sin saltos)."""
    s = size * ss
    c = (s - 1) / 2
    y, x = np.mgrid[0:s, 0:s].astype(float)
    x = (x - c) / (s * 0.5); y = (y - c) / (s * 0.5)        # -1..1
    R = 0.92                                                  # radio exterior del aro (con los agarres, cabe justo)
    th = R * 0.24                                             # algo más grueso que el plano: el sombreado lo afina
    rm = R - th / 2; tr = th / 2
    r = np.hypot(x, y)
    z = np.full_like(x, -1.0)
    def sph(d2, rad):  # casquete
        return np.where(d2 < rad * rad, np.sqrt(np.clip(rad * rad - d2, 0, None)), -1.0)
    # aro (toro)
    z = np.maximum(z, sph((r - rm) ** 2, tr))
    # agarres
    for i in range(spokes):
        a = math.radians(angle + (i + 0.5) * 360 / spokes)
        gx, gy = math.cos(a) * rm, math.sin(a) * rm
        z = np.maximum(z, sph((x - gx) ** 2 + (y - gy) ** 2, th * 0.78) * 0.95)
    # radios (cilindros del cubo al aro)
    sr = R * 0.17 / 2
    for i in range(spokes):
        a = math.radians(angle + i * 360 / spokes)
        ux, uy = math.cos(a), math.sin(a)
        along = x * ux + y * uy
        perp = -x * uy + y * ux
        m = (along > 0) & (along < rm)
        z = np.maximum(z, np.where(m, sph(perp ** 2, sr) * 0.85, -1.0))
    # cubo: disco abombado con borde redondeado y agujero
    hub = R * 0.30; hole = R * 0.11
    hz = np.where(r < hub, 0.10 + 0.08 * np.sqrt(np.clip(1 - (r / hub) ** 2, 0, None)), -1.0)
    edge = np.clip((hub - r) / (hub * 0.25), 0, 1)
    hz = np.where(r < hub, hz * (0.55 + 0.45 * np.sqrt(edge)), hz)
    hz = np.where(r < hole, 0.02, hz)                      # el agujero, hundido
    z = np.maximum(z, hz)
    mask = z > -0.5
    z = np.where(mask, z, 0) * relief
    # normales
    px = 2.0 / s
    dzdx = np.gradient(z, axis=1) / px
    dzdy = np.gradient(z, axis=0) / px
    n = np.stack([-dzdx, -dzdy, np.ones_like(z)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    L = np.array(light); L = L / np.linalg.norm(L)
    V = np.array([0, 0, 1.0]); H = (L + V); H /= np.linalg.norm(H)
    diff = np.clip(n @ L, 0, 1)
    spec = np.clip(n @ H, 0, 1) ** 48
    if paint:
        # la pintura: un brillo no muy duro y un lustre ancho y suave
        spec = np.clip(n @ H, 0, 1) ** 60 + 0.12 * np.clip(n @ H, 0, 1) ** 10
    # sombra propia: hacia la luz, ¿algo más alto tapa?
    shadow = np.ones_like(z)
    lx, ly = L[0], L[1]; lz = L[2] / math.hypot(lx, ly)
    step = 0.012
    for k in range(1, 28):
        d = k * step
        sx = int(round(lx / math.hypot(lx, ly) * d / px)); sy = int(round(ly / math.hypot(lx, ly) * d / px))
        zz = np.roll(np.roll(z, -sy, 0), -sx, 1)
        occ = zz - (z + lz * d * relief * 0.9)
        shadow = np.minimum(shadow, np.clip(1 - occ * 18, 0, 1))
    shadow = blur(shadow, ss * 0.8)
    shadow = 0.45 + 0.55 * shadow
    # oclusión ambiental: lo hundido junto a lo alto, más oscuro
    ao = np.clip(1 - (blur(z, ss * 3) - z) * 3.2, 0.55, 1)
    # material: acero claro azulado (el mismo tono que la válvula de antes), más claro arriba
    t = (y + 1) / 2
    albedo = np.stack([0xdc + (0x5a - 0xdc) * t, 0xf0 + (0xb8 - 0xf0) * t, 0xff + (0xf6 - 0xff) * t], -1) / 255
    if paint:
        # pintura roja de válvula, más clara arriba; el cubo (y el vástago que asoma) de hierro oscuro
        albedo = np.stack([0xd2 + (0x7a - 0xd2) * t, 0x36 + (0x14 - 0x36) * t, 0x26 + (0x0e - 0x26) * t], -1) / 255
        iron = np.array([0x30, 0x32, 0x36]) / 255
        hubm = np.clip((hub * 0.82 - r) / (hub * 0.12), 0, 1)
        albedo = albedo * (1 - hubm[..., None]) + iron * hubm[..., None]
    if dirt > 0:
        # Suciedad negra pegada a la válvula (gira con ella): manchas, mugre en lo hundido y en
        # las uniones, y salpicaduras; el azul sigue debajo
        blot, fine, spots, scr = grime_map(s, seed=11)
        def rot(m):
            return np.array(Image.fromarray(m.astype('float32'), 'F').rotate(-angle, resample=Image.BILINEAR)) if angle else m
        blot, fine, spots, scr = rot(blot), rot(fine), rot(spots), rot(scr)
        crev = np.clip((blur(z, ss * 2.5) - z) * 9, 0, 1)            # rincones y uniones
        low = np.clip(1 - z / (0.12 * relief + 1e-6), 0, 1) * mask    # lo más bajo, junto al canto
        black = np.clip(0.55 * np.clip((blot - 0.35) * 1.8, 0, 1) + 0.25 * fine + 0.9 * crev + 0.35 * low + 0.25 * spots, 0, 0.72)
        if split:
            # En los fotogramas solo la mugre de la forma (rincones y canto), que gira con los radios;
            # el resto oscurece lo mismo desde la capa: (1 - 0.82·total) = (1 - 0.82·forma)·(1 - a)
            shape = np.clip(0.9 * crev + 0.35 * low, 0, 0.72)
            a = 1 - (1 - 0.82 * dirt * black) / (1 - 0.82 * dirt * shape)
            e = scr * 0.15 * dirt * (1 - black)                  # arañazos: un velo claro y cálido
            A = np.clip(a + e, 0, 1) * mask
            g = np.where(A > 1e-4, e / np.maximum(A, 1e-4), 0)
            layer = np.zeros((s, s, 4))
            layer[..., :3] = (g[..., None] * np.array(tint) * 0.85) * A[..., None]   # premultiplicado
            layer[..., 3] = A
            black = shape
        albedo = albedo * (1 - 0.82 * dirt * black)[..., None]
        if not split:
            albedo = np.clip(albedo + (scr * 0.18 * dirt * (1 - black))[..., None], 0, 1)   # arañazos claros donde no hay mugre
        spec_mask = 1 - 0.8 * dirt * black
    else:
        spec_mask = 1
    lit = amb + kd * diff * shadow
    col = albedo * (lit * ao)[..., None] * np.array(tint) + (spec * shadow * ks * spec_mask)[..., None] * np.array(tint)
    # el agujero del cubo: oscuro
    col = np.where((r < hole)[..., None], col * 0.35, col)
    rgba = np.zeros((s, s, 4))
    rgba[..., :3] = np.clip(col, 0, 1) * 255
    rgba[..., 3] = mask * 255
    im = Image.fromarray(rgba.astype('uint8'), 'RGBA')
    im = im.resize((size, size), Image.LANCZOS)
    m = Image.fromarray((mask * 255).astype('uint8')).resize((size, size), Image.LANCZOS)
    if split and dirt > 0:
        # Se reduce premultiplicada (sin halos en los bordes) y se vuelve a dividir por el alfa
        ch = [np.array(Image.fromarray(layer[..., i].astype('float32'), 'F').resize((size, size), Image.BOX)) for i in range(4)]
        al = np.clip(ch[3], 0, 1)
        out = np.zeros((size, size, 4))
        for i in range(3):
            out[..., i] = np.where(al > 1e-3, ch[i] / np.maximum(al, 1e-3), 0)
        out[..., 3] = al
        return im, m, Image.fromarray((np.clip(out, 0, 1) * 255 + 0.5).astype('uint8'), 'RGBA')
    return im, m


if __name__ == "__main__":
    out = os.path.join(ROOT, "gfx", "w3d")
    os.makedirs(out, exist_ok=True)
    for tag, th in zip("ab", SWING):
        for k in range(FRAMES):
            fr, _, _ = wheel3d(SIZE, k * 3.0, light=light_dir(th), split=True, **LOOK)
            fr.save(os.path.join(out, "%s%02d.png" % (tag, k)))
    # La mugre que no se repite cada 72°, en su capa (la misma con cualquier luz)
    _, _, layer = wheel3d(SIZE, 0.0, light=light_dir(SWING[0]), split=True, **LOOK)
    layer.save(os.path.join(ROOT, "gfx", "wheel_dirt.png"))
    # Sombra: la silueta, con margen para el desenfoque
    _, m = wheel3d(SIZE, 0.0, ss=2)
    pad = 16
    sh = Image.new("L", (SIZE + 2 * pad, SIZE + 2 * pad), 0)
    sh.paste(m, (pad, pad))
    sh = sh.filter(ImageFilter.GaussianBlur(4.0))
    rgba = Image.new("RGBA", sh.size, (3, 5, 9, 0))
    rgba.putalpha(sh)
    rgba.save(os.path.join(ROOT, "gfx", "wheel_shadow.png"))
    print("gfx/w3d/a00..b%02d.png gfx/wheel_dirt.png gfx/wheel_shadow.png" % (FRAMES - 1))
