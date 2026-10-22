#!/usr/bin/env python3
"""Sonidos de la pantalla de carga.

La válvula de hierro se clava en la pared de un sótano: golpe sordo con el metal, el yeso que se
raja y los cascotes que caen; un bombillo flojo que zumba y chisporrotea cuando parpadea; la
válvula oxidada que cruje al soltarse y chirría al girar; y el vapor que sale de golpe y sigue
silbando.

El golpe, los cascotes y el óxido se montan con grabaciones reales de Kenney (www.kenney.nl,
CC0: dominio público), en tools/sfx_src: se bajan de tono para que pesen más, se mezclan y se les
pone la sala. El vapor, el zumbido y el chisporroteo se sintetizan. Necesita ffmpeg para leer
los .ogg.

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
import subprocess
import sys
import wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SR = 22050
SRC = os.path.join(ROOT, "tools", "sfx_src")
rng = np.random.default_rng(1977)


def rec(name, pitch=1.0, start=0.0, end=None):
    """Una grabación de tools/sfx_src (mono, a SR), bajada o subida de tono (cambia también lo
    que dura, como al girar un disco más despacio) y recortada [start, end) s del original."""
    raw = subprocess.run(["ffmpeg", "-loglevel", "error", "-i", os.path.join(SRC, name + ".ogg"),
                          "-ac", "1", "-ar", str(SR), "-f", "f32le", "-"], capture_output=True, check=True).stdout
    x = np.frombuffer(raw, dtype='<f4').astype(float)
    x = x[int(start * SR):int(end * SR) if end else None]
    if pitch != 1.0:
        n = int(x.size / pitch)
        x = np.interp(np.arange(n) * pitch, np.arange(x.size), x)
    return x / (np.abs(x).max() + 1e-9)


def place(out, x, at, gain=1.0):
    i = int(at * SR)
    if i >= out.size:
        return
    m = min(x.size, out.size - i)
    out[i:i + m] += gain * x[:m]


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
    n = secs(2.4)
    t = tt(n)
    out = np.zeros(n)
    # El cuerpo del golpe: un porrazo sordo, más grave (pesa más)
    place(out, filt(rec("impactPunch_heavy_002", 0.72), 25, 2500), 0.0, 1.0)
    place(out, filt(rec("impactPunch_heavy_000", 0.6), 25, 900), 0.004, 0.55)
    # El hierro de la válvula: un golpe de metal pesado bajado de tono y apagado (hierro grueso)
    place(out, filt(rec("impactMetal_heavy_001", 0.62), 60, 3800), 0.0, 0.42)
    place(out, filt(rec("impactMetal_heavy_003", 0.55), 60, 2500), 0.012, 0.22)
    # El yeso se raja: picazos en roca (con su desmoronamiento), uno encima del otro
    place(out, filt(rec("impactMining_004", 0.85), 120, 9000), 0.006, 0.55)
    place(out, filt(rec("impactMining_000", 0.95, start=0.03), 150, 9000), 0.05, 0.3)
    # Cascotes y polvo: las colas de desmoronamiento de las grabaciones, sueltas, cada vez menos
    tails = [("impactMining_001", 0.10), ("impactMining_003", 0.12), ("impactMining_000", 0.15), ("impactMining_004", 0.14)]
    for k in range(9):
        name, st = tails[k % len(tails)]
        x = rec(name, 1.1 + rng.random() * 0.45, start=st)
        x[:secs(0.03)] *= np.linspace(0, 1, secs(0.03))           # sin el golpe del pico
        tk = 0.16 + rng.gamma(1.6, 0.22)
        place(out, filt(x, 400, 8000), tk, 0.22 * np.exp(-tk / 0.9) * (0.6 + 0.4 * rng.random()))
    for k in range(6):
        x = rec("footstep_concrete_00%d" % (1 + k % 2), 1.6 + rng.random() * 0.6)
        tk = 0.25 + rng.gamma(2.0, 0.25)
        place(out, filt(x, 700, 9000), tk, 0.08 * (0.5 + rng.random()))
    # Arenilla que sigue cayendo un poco
    sand = band_noise(n, 1500, 7000)
    grain = np.clip(rng.normal(size=n // 64 + 1), 0, None).repeat(64)[:n]
    sand_env = np.minimum(1, np.maximum(t - 0.1, 0) / 0.2) * np.exp(-np.maximum(t - 0.3, 0) / 0.5)
    out += 0.05 * sand * filt(grain, 1, 60) / 2.0 * sand_env
    out = room(out, wet=0.28, length=0.9, tone=3000)
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
# La válvula oxidada: crujidos reales de bisagra, más graves (hierro grande) y con el roce del eje

CREAKS = [("creak1", 0.16, 0.27), ("creak1", 0.36, 0.53), ("creak2", 0.01, 0.14), ("creak2", 0.31, 0.42),
          ("creak2", 0.51, 0.65), ("creak3", 0.06, 0.24), ("doorOpen_1", 0.10, 0.32), ("doorOpen_2", 0.15, 0.34)]


def creak_piece(k, pitch):
    name, a, b = CREAKS[k % len(CREAKS)]
    x = rec(name, pitch, start=a, end=b)
    f = secs(0.012)
    x[:f] *= np.linspace(0, 1, f)
    x[-f:] *= np.linspace(1, 0, f)
    return filt(x, 120, 6000)


def grind(n, level):
    """El roce continuo del eje oxidado (periódico: vale para el bucle)."""
    g = band_noise(n, 120, 1800, tilt=-3)
    t = tt(n)
    period = n / SR
    mod = 0.6 + 0.4 * np.sin(2 * np.pi * (3 / period) * t) * np.sin(2 * np.pi * (7 / period) * t + 1)
    return level * g * mod


def creak0():
    n = secs(1.4)
    out = np.zeros(n)
    # Se suelta: un chasquido seco del metal y un crujido largo y grave
    place(out, filt(rec("impactMetal_heavy_003", 0.5), 60, 2000), 0.0, 0.35)
    place(out, creak_piece(6, 0.62), 0.05, 0.9)
    place(out, creak_piece(1, 0.7), 0.55, 0.75)
    out += grind(n, 0.05) * np.minimum(1, tt(n) / 0.3)
    out = room(out, wet=0.22, length=0.6, tone=3000)[:n]
    return fade_edges(norm(out, 0.9), 0.0005, 0.08)


def creak():
    n = secs(4.0)
    out = np.zeros(n)
    # Crujidos de distintas alturas con huecos entre ellos (los del final no pasan del bucle)
    for at, k, p, g in ((0.10, 7, 0.66, 0.8), (0.95, 2, 0.72, 0.6), (1.55, 5, 0.6, 0.75),
                        (2.45, 0, 0.7, 0.55), (3.05, 4, 0.64, 0.7)):
        place(out, creak_piece(k, p), at, g)
    out = room(out, wet=0.2, length=0.5, tone=3000)
    # La cola de la reverberación se suma al principio (bucle sin corte)
    tail = out[n:]
    out = out[:n]
    out[:tail.size] += tail
    out += grind(n, 0.05 * np.abs(out).max())
    return norm(out, 0.85)


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
