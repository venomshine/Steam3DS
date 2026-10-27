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
# Pintada de rojo como la de la intro de Valve, pero de pintura vieja de válvula (oscura y apagada,
# con poco brillo), sucia y con el cubo de hierro oscuro
LOOK = dict(dirt=1.0, amb=0.24, kd=1.05, tint=(1.0, 0.92, 0.80), ks=0.45, paint=True, rust=1.0)


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


def polar_noise(s, x, y, angle, octaves, seed, period=72.0):
    """Ruido pegado a la válvula que se repite cada `period` grados (como su forma): así puede ir
    horneado en los fotogramas sin saltar al volver al primero. 0..1."""
    rng = np.random.default_rng(seed)
    u = ((np.degrees(np.arctan2(y, x)) - angle) % period) / period    # 0..1 a lo largo del giro
    v = np.clip(np.hypot(x, y) / 1.0, 0, 0.999)                        # 0..1 del centro al aro
    out = np.zeros_like(x); amp = 1.0; tot = 0
    for ou, ov in octaves:
        g = rng.random((ov + 1, ou))
        gx = u * ou; gy = v * ov
        x0 = np.floor(gx).astype(int); y0 = np.floor(gy).astype(int)
        fx = gx - x0; fy = gy - y0
        fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy)
        x1 = (x0 + 1) % ou; x0 = x0 % ou; y1 = np.minimum(y0 + 1, ov)
        a = g[y0, x0] * (1 - fx) + g[y0, x1] * fx
        b = g[y1, x0] * (1 - fx) + g[y1, x1] * fx
        out += amp * (a * (1 - fy) + b * fy)
        tot += amp; amp *= 0.55
    return out / tot


def wheel3d(size, angle=0.0, spokes=5, light=(0.62, -0.62, 0.48), relief=0.75, ss=4, dirt=0.0, amb=0.42, kd=0.78, tint=(1, 1, 1), ks=0.75, split=False, paint=False, rust=0.0):
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
    if rust > 0:
        # Manchas de óxido (se repiten con la forma): ahí el hierro está picado y con costras
        # (ya no es liso como goma); en el cubo, de hierro, poco
        hubf = np.clip((np.hypot(x, y) - R * 0.24) / (R * 0.12), 0.6, 1)
        rn = polar_noise(s, x, y, angle, ((6, 6), (14, 12), (34, 30)), seed=21)
        patch = np.clip((rn - 0.44) * 3.5, 0, 1) * hubf
        pits = polar_noise(s, x, y, angle, ((60, 50), (130, 110)), seed=3)
        crust = polar_noise(s, x, y, angle, ((18, 16), (40, 34)), seed=5)
        bump = (np.clip((0.45 - pits) * 3, 0, 1) * -0.010 + np.clip((crust - 0.5) * 3, 0, 1) * 0.010) * patch * rust
        bump += np.clip((0.40 - pits) * 3, 0, 1) * -0.004 * rust            # algún poro también en la pintura
        z = np.where(mask, z + bump, z)
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
        # la pintura vieja: un brillo blando y un lustre ancho y suave
        spec = np.clip(n @ H, 0, 1) ** 60 * 0.8
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
        albedo = np.stack([0xc8 + (0x70 - 0xc8) * t, 0x18 + (0x08 - 0x18) * t, 0x1c + (0x0a - 0x1c) * t], -1) / 255
        iron = np.array([0x58, 0x42, 0x1e]) / 255   # el cubo, de latón viejo
        hubm = np.clip((hub * 0.82 - r) / (hub * 0.12), 0, 1)
        albedo = albedo * (1 - hubm[..., None]) + iron * hubm[..., None]
    wear = np.zeros_like(z)
    rmask = np.zeros_like(z)
    if rust > 0:
        # La pintura saltada en lo más alto (donde se agarra y roza): hierro oscuro y algo brillante
        top = np.clip((z / relief - 0.10) / 0.10, 0, 1) * mask
        chip = polar_noise(s, x, y, angle, ((30, 26), (70, 60)), seed=9)
        wear = np.clip((chip - 0.62) * 6, 0, 1) * top * rust * np.clip((np.hypot(x, y) - R * 0.3) / (R * 0.1), 0, 1)
        steel = np.array([0x4a, 0x46, 0x44]) / 255
        albedo = albedo * (1 - wear[..., None]) + steel * wear[..., None]
        # Óxido (el que se repite con la forma): en las manchas, en los rincones y donde saltó la pintura
        crev0 = np.clip((blur(z, ss * 2.5) - z) * 9, 0, 1)
        rmask = np.clip(patch + crev0 * 0.7 * hubf + wear * 0.5, 0, 1) * mask * rust
        rfine = polar_noise(s, x, y, angle, ((40, 34), (100, 90)), seed=33)
        rc = (np.array([0x16, 0x12, 0x10]) / 255 * (1 - rfine[..., None]) + np.array([0x52, 0x26, 0x10]) / 255 * rfine[..., None])
        albedo = albedo * (1 - rmask[..., None]) + rc * rmask[..., None]
        # Grano de la pintura vieja: motas claras y oscuras por todas partes
        grain = polar_noise(s, x, y, angle, ((220, 180),), seed=41)
        albedo = albedo * (0.80 + 0.40 * grain[..., None])
    if dirt > 0:
        # Suciedad negra pegada a la válvula (gira con ella): manchas, mugre en lo hundido y en
        # las uniones, y salpicaduras; el azul sigue debajo
        blot, fine, spots, scr = grime_map(s, seed=11)
        def rot(m):
            return np.array(Image.fromarray(m.astype('float32'), 'F').rotate(-angle, resample=Image.BILINEAR)) if angle else m
        blot, fine, spots, scr = rot(blot), rot(fine), rot(spots), rot(scr)
        crev = np.clip((blur(z, ss * 2.5) - z) * 9, 0, 1)            # rincones y uniones
        low = np.clip(1 - z / (0.12 * relief + 1e-6), 0, 1) * mask    # lo más bajo, junto al canto
        black = np.clip(0.75 * np.clip((blot - 0.30) * 1.8, 0, 1) + 0.30 * fine + 1.1 * crev + 0.45 * low + 0.35 * spots, 0, 0.85)
        if split:
            # En los fotogramas solo la mugre de la forma (rincones y canto), que gira con los radios;
            # el resto oscurece lo mismo desde la capa: (1 - 0.82·total) = (1 - 0.82·forma)·(1 - a)
            shape = np.clip(1.1 * crev + 0.45 * low, 0, 0.85)
            a = 1 - (1 - 0.82 * dirt * black) / (1 - 0.82 * dirt * shape)
            e = scr * 0.15 * dirt * (1 - black)                  # arañazos: un velo claro y cálido
            A = np.clip(a + e, 0, 1) * mask
            g = np.where(A > 1e-4, e / np.maximum(A, 1e-4), 0)
            layer = np.zeros((s, s, 4))
            layer[..., :3] = (g[..., None] * np.array(tint) * 0.85) * A[..., None]   # premultiplicado
            layer[..., 3] = A
            if rust > 0:
                # Óxido que no se repite (rompe la simetría de los cinco radios): costras pardas encima
                rb, rf, _, _ = grime_map(s, seed=29)
                rb, rf = rot(rb), rot(rf)
                ra = np.clip((rb - 0.44) * 3.5, 0, 1) * (0.65 + 0.3 * rf) * mask * rust
                rcol = (np.array([0x12, 0x10, 0x0e]) / 255 * (1 - rf[..., None]) + np.array([0x4a, 0x22, 0x0e]) / 255 * rf[..., None]) * np.array(tint)
                layer[..., :3] = layer[..., :3] * (1 - ra[..., None]) + rcol * ra[..., None]
                layer[..., 3] = layer[..., 3] * (1 - ra) + ra
            black = shape
        albedo = albedo * (1 - 0.82 * dirt * black)[..., None]
        if not split:
            albedo = np.clip(albedo + (scr * 0.18 * dirt * (1 - black))[..., None], 0, 1)   # arañazos claros donde no hay mugre
        spec_mask = 1 - 0.8 * dirt * black
    else:
        spec_mask = 1
    if rust > 0:
        # El óxido no brilla; el hierro pelado da un brillo pequeño y duro
        spec = spec * (1 - 0.9 * rmask) * (1 - wear) + wear * np.clip(n @ H, 0, 1) ** 90 * 1.6
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
