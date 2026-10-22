#!/usr/bin/env python3
"""Sonidos de la pantalla de carga, sintetizados (sin grabaciones de nadie).

La válvula de hierro se clava en la pared de un sótano: golpe sordo con el «clonc» del metal, el
yeso que se raja y los cascotes que caen; un bombillo flojo que zumba y chisporrotea cuando
parpadea; la válvula oxidada que cruje al soltarse y chirría al girar; y el vapor que sale de
golpe y sigue silbando.

Cada sonido va suelto para que la consola lo dispare en su momento exacto de la animación (y los
bucles se repiten sin corte: el ruido se hace en frecuencia, así que es periódico).

Salida: romfs/sfx/<nombre>.pcm   (PCM 16 bits con signo, mono, 22050 Hz)
  impact      golpe, grietas, cascotes y arenilla (con la reverberación de la sala)
  hum         zumbido del bombillo (bucle)
  flicker     chisporroteo del bombillo al parpadear
  creak0      la válvula oxidada se suelta (crujido entrecortado y un quejido)
  creak       chirridos y roce mientras gira (bucle)
  steam0      el vapor sale de golpe (y queda al nivel del bucle)
  steam       silbido del vapor (bucle)

Uso: python3 tools/gen_sfx.py [carpeta para .wav de muestra]   (solo necesita numpy)
"""
import os
import sys
import wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SR = 22050
rng = np.random.default_rng(1977)


def secs(d):
    return int(round(d * SR))


def tt(n):
    return np.arange(n) / SR


def band_noise(n, lo, hi, tilt=0.0, periodic=True):
    """Ruido con la energía entre lo y hi Hz (bordes suaves); tilt en dB/octava desde lo."""
    f = np.fft.rfftfreq(n, 1 / SR)
    spec = (rng.normal(size=f.size) + 1j * rng.normal(size=f.size))
    g = 1 / (1 + (lo / np.maximum(f, 1)) ** 4) / (1 + (f / hi) ** 4)
    if tilt:
        g *= (np.maximum(f, lo) / lo) ** (tilt / 6.02)
    x = np.fft.irfft(spec * g, n)
    return x / (np.abs(x).max() + 1e-9)


def filt(x, lo, hi):
    """Filtro paso banda sin fase (en frecuencia)."""
    f = np.fft.rfftfreq(x.size, 1 / SR)
    g = 1 / (1 + (lo / np.maximum(f, 1)) ** 4) / (1 + (f / hi) ** 4)
    return np.fft.irfft(np.fft.rfft(x) * g, x.size)


def conv(x, ir):
    n = x.size + ir.size - 1
    m = 1 << (n - 1).bit_length()
    return np.fft.irfft(np.fft.rfft(x, m) * np.fft.rfft(ir, m), m)[:n]


def modal(n, modes, t0=0.0):
    """Resonancias que se apagan: (frecuencia, amplitud, tau)."""
    t = tt(n) - t0
    out = np.zeros(n)
    on = t >= 0
    for fr, a, tau in modes:
        out[on] += a * np.exp(-t[on] / tau) * np.sin(2 * np.pi * fr * t[on] + rng.random() * 6.28)
    return out


def room(x, wet=0.22, length=0.7, tone=3500):
    """Sala de paredes duras: cola de ruido que se apaga."""
    n = secs(length)
    ir = filt(rng.normal(size=n), 80, tone) * np.exp(-tt(n) / (length / 4.5))
    ir[:secs(0.012)] = 0                                   # el primer rebote llega tarde
    ir /= np.sqrt((ir ** 2).sum())
    y = np.concatenate([x, np.zeros(n)])
    w = np.zeros(y.size)
    c = conv(x, ir)
    w[:c.size] = c[:y.size]
    return (1 - wet) * y + wet * 3.0 * w


def norm(x, peak):
    return x * (peak / (np.abs(x).max() + 1e-9))


def fade_edges(x, a=0.002, b=0.05):
    na, nb = secs(a), secs(b)
    x = x.copy()
    x[:na] *= np.linspace(0, 1, na)
    x[-nb:] *= np.linspace(1, 0, nb)
    return x


# --------------------------------------------------------------------------
# Golpe en la pared

def impact():
    n = secs(2.2)
    t = tt(n)
    out = np.zeros(n)
    # El porrazo: un golpe grave que cae de tono, más el aire empujado
    f = 38 + 34 * np.exp(-t / 0.05)
    ph = 2 * np.pi * np.cumsum(f) / SR
    thud = np.sin(ph) * np.exp(-t / 0.22) * np.minimum(1, t / 0.002)
    out += 1.0 * thud
    out += 0.55 * filt(rng.normal(size=n), 30, 260) * np.exp(-t / 0.07)
    # El hierro: un «clonc» apagado (hierro fundido, grueso), con parejas que baten
    clank = modal(n, [(196, 0.5, 0.30), (199.5, 0.35, 0.28), (512, 0.4, 0.18), (517, 0.25, 0.16),
                      (931, 0.28, 0.10), (1384, 0.2, 0.07), (2113, 0.12, 0.045), (2890, 0.07, 0.03)])
    out += 0.38 * clank * np.minimum(1, t / 0.001)
    # El yeso se raja: un chasquido seco y luego crujidos cada vez más espaciados
    snap = filt(rng.normal(size=secs(0.03)), 1400, 9000) * np.exp(-tt(secs(0.03)) / 0.006)
    s0 = secs(0.008)
    out[s0:s0 + snap.size] += 0.9 * snap / np.abs(snap).max()
    times = np.sort(0.01 + rng.exponential(0.11, 34))
    times = times[times < 0.75]
    for k, tk in enumerate(times):
        ln = secs(0.002 + rng.random() * 0.006)
        lo = 600 + rng.random() * 1500
        g = filt(rng.normal(size=ln + 64), lo, lo * (2.5 + rng.random() * 3))[:ln] * np.exp(-tt(ln) / (ln / SR / 3))
        a = 0.55 * np.exp(-tk / 0.3) * (0.4 + 0.6 * rng.random())
        i = secs(tk)
        out[i:i + ln] += a * g / (np.abs(g).max() + 1e-9)
    # Cascotes: piedrecitas que golpean el suelo (y rebotan alguna vez)
    for k in range(30):
        tk = 0.18 + rng.gamma(2.0, 0.22)
        if tk > 1.9:
            continue
        for b in range(1 + (rng.random() < 0.4)):
            m = secs(0.05)
            fr = 1800 + rng.random() * 4200
            tick = modal(m, [(fr, 1, 0.006 + rng.random() * 0.008), (fr * 1.63, 0.5, 0.004)])
            tick += 0.4 * filt(rng.normal(size=m), 2000, 9000) * np.exp(-tt(m) / 0.002)
            i = secs(tk + b * (0.07 + rng.random() * 0.05))
            if i + m < n:
                out[i:i + m] += 0.16 * (0.4 + rng.random()) * (0.5 if b else 1) * tick / (np.abs(tick).max() + 1e-9)
    # Arenilla: el polvo que cae, un siseo granulado que se apaga
    sand = band_noise(n, 1500, 7000)
    grain = np.clip(rng.normal(size=n // 64 + 1), 0, None).repeat(64)[:n]
    sand_env = np.minimum(1, np.maximum(t - 0.08, 0) / 0.15) * np.exp(-np.maximum(t - 0.2, 0) / 0.55)
    out += 0.09 * sand * filt(grain, 1, 60) / 2.0 * sand_env
    out = room(out, wet=0.25, length=0.8)
    return fade_edges(norm(out, 0.95), 0.0005, 0.3)


# --------------------------------------------------------------------------
# Bombillo

def hum():
    n = secs(2.0)                      # 100 ciclos de 50 Hz: el bucle no se nota
    t = tt(n)
    x = np.zeros(n)
    for h, a in ((2, 1.0), (4, 0.35), (6, 0.22), (8, 0.12), (10, 0.06)):
        x += a * np.sin(2 * np.pi * 50 * h * t + h)
    x *= 1 + 0.15 * np.sin(2 * np.pi * 0.5 * t)
    x += 0.25 * band_noise(n, 3000, 9000) * (0.6 + 0.4 * np.sin(2 * np.pi * 100 * t) ** 8)
    return norm(x, 0.5)


def flicker():
    n = secs(0.16)
    t = tt(n)
    # El arco: chispazos de ruido al ritmo de la red (100 Hz) y el zumbido que se ahoga
    gate = (np.sin(2 * np.pi * 100 * t) > 0.2).astype(float)
    arc = filt(rng.normal(size=n), 1800, 8000) * gate * (0.4 + 0.6 * rng.random(n) ** 3)
    buzz = np.sign(np.sin(2 * np.pi * 100 * t)) * 0.3
    buzz = filt(buzz, 90, 2500)
    env = np.minimum(1, t / 0.003) * np.exp(-t / 0.05)
    x = (arc + 0.8 * buzz) * env
    x[:secs(0.004)] += filt(rng.normal(size=secs(0.004) + 32), 2500, 9000)[:secs(0.004)] * 1.5   # el «tic»
    return fade_edges(norm(x, 0.8), 0.0005, 0.02)


# --------------------------------------------------------------------------
# La válvula oxidada (rozamiento a tirones: cada tirón hace sonar el hierro)

BODY = [(287, 1.0, 0.035), (291, 0.6, 0.03), (743, 0.7, 0.025), (1210, 0.5, 0.018), (1890, 0.35, 0.012), (2780, 0.2, 0.008)]


def slips(n, rate_fn, amp_fn, jitter=0.08):
    """Tren de tirones: rate_fn(t) tirones por segundo, amp_fn(t) su fuerza."""
    x = np.zeros(n)
    tk = 0.0
    while True:
        r = rate_fn(tk)
        if r <= 0:
            tk += 0.01
        else:
            tk += (1 / r) * (1 + rng.normal(0, jitter))
        if tk >= n / SR:
            break
        a = amp_fn(tk)
        if a > 0:
            x[int(tk * SR)] += a * (0.6 + 0.4 * rng.random())
    return x


def body(x):
    ir = modal(secs(0.15), BODY)
    y = conv(x, ir)[:x.size]
    y += 0.3 * filt(x, 1500, 7000) * 10      # el raspado del óxido
    return y


def groan(n, t0, dur, r0, r1, a):
    """Un quejido: la frecuencia de los tirones se desliza de r0 a r1."""
    def rate(t):
        u = (t - t0) / dur
        return r0 + (r1 - r0) * (3 * u * u - 2 * u ** 3) if 0 <= u <= 1 else 0
    def amp(t):
        u = (t - t0) / dur
        return a * np.sin(np.pi * np.clip(u, 0, 1)) ** 0.6 if 0 <= u <= 1 else 0
    return slips(n, rate, amp, jitter=0.05)


def grind(n, level):
    """El roce continuo del eje oxidado (periódico: vale para el bucle)."""
    g = band_noise(n, 200, 2400, tilt=-3)
    t = tt(n)
    period = n / SR
    mod = 0.6 + 0.4 * np.sin(2 * np.pi * (3 / period) * t) * np.sin(2 * np.pi * (7 / period) * t + 1)
    return level * g * mod


def creak0():
    n = secs(1.3)
    t = tt(n)
    # Se suelta: unos chasquidos sueltos y secos, cada vez más seguidos, y un quejido grave
    def rate(tk):
        return 9 + 50 * min(1, tk / 0.45) if tk < 0.5 else 0
    def amp(tk):
        return 1.0 if tk < 0.5 else 0
    x = slips(n, rate, amp, jitter=0.35)
    x += groan(n, 0.42, 0.75, 70, 165, 0.55)
    y = body(x)
    y += grind(n, 0.05) * np.minimum(1, t / 0.4)
    y = room(y, wet=0.2, length=0.6)[:n]
    return fade_edges(norm(y, 0.9), 0.0005, 0.08)


def creak():
    n = secs(4.0)
    x = np.zeros(n)
    # Quejidos de distintas alturas con huecos entre ellos (el último acaba antes del final)
    x += groan(n, 0.15, 0.55, 140, 230, 0.5)
    x += groan(n, 1.15, 0.40, 210, 150, 0.4)
    x += groan(n, 1.95, 0.65, 120, 260, 0.55)
    x += groan(n, 3.05, 0.45, 180, 240, 0.35)
    y = body(x)
    y = room(y, wet=0.18, length=0.5)
    # La cola de la reverberación se suma al principio (bucle sin corte)
    tail = y[n:]
    y = y[:n]
    y[:tail.size] += tail
    y += grind(n, 0.06 * np.abs(y).max())
    return norm(y, 0.85)


# --------------------------------------------------------------------------
# Vapor

STEAM_LEVEL = 0.42


def steam_bed(n):
    t = tt(n)
    period = n / SR
    x = 0.75 * band_noise(n, 1200, 8500, tilt=-1.5) + 0.25 * band_noise(n, 250, 1400)
    mod = 1 + 0.10 * np.sin(2 * np.pi * (1 / period) * t) + 0.06 * np.sin(2 * np.pi * (5 / period) * t + 2)
    return x * mod


def steam0():
    n = secs(1.4)
    t = tt(n)
    # «Pssshh»: sale de golpe con un soplido grave y se queda en el nivel del bucle
    x = steam_bed(n)
    env = np.minimum(1, t / 0.02) * (STEAM_LEVEL + (1 - STEAM_LEVEL) * np.exp(-t / 0.35))
    y = x * env
    y += 0.5 * filt(rng.normal(size=n), 60, 400) * np.exp(-t / 0.08) * np.minimum(1, t / 0.004)
    y = room(y, wet=0.15, length=0.5)[:n]
    return norm(y, 0.9) * 1.0


def steam():
    n = secs(3.0)
    x = steam_bed(n)
    # Mismo nivel que el final de steam0 (el bucle entra justo detrás)
    tail = steam0_cache[-secs(0.2):]
    return x * np.sqrt((tail ** 2).mean() / (x ** 2).mean())


steam0_cache = None


def steam0_keep():
    global steam0_cache
    steam0_cache = steam0()
    return steam0_cache


SOUNDS = {
    "impact": impact, "hum": hum, "flicker": flicker,
    "creak0": creak0, "creak": creak, "steam0": steam0_keep, "steam": steam,
}


def save_pcm(path, x):
    pcm = (np.clip(x, -1, 1) * 32767).astype('<i2')
    with open(path, 'wb') as f:
        f.write(pcm.tobytes())


def save_wav(path, x):
    pcm = (np.clip(x, -1, 1) * 32767).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


if __name__ == "__main__":
    out = os.path.join(ROOT, "romfs", "sfx")
    os.makedirs(out, exist_ok=True)
    wav_dir = sys.argv[1] if len(sys.argv) > 1 else None
    total = 0
    for name, fn in SOUNDS.items():
        x = fn()
        save_pcm(os.path.join(out, name + ".pcm"), x)
        total += x.size * 2
        if wav_dir:
            save_wav(os.path.join(wav_dir, name + ".wav"), x)
        print("%-8s %.2f s  pico %.2f" % (name, x.size / SR, np.abs(x).max()))
    print("romfs/sfx: %d KB" % (total // 1024))
