#!/usr/bin/env python3
"""Sonido del banner del menú HOME (meta/banner.wav).

Original, con aire «industrial» de los juegos clásicos de Valve: golpe grave,
siseo de vapor que cruza de izquierda a derecha, eco metálico de válvula y un
acorde que se abre al final. Estéreo PCM16 a 32728 Hz (la frecuencia del DSP:
bannertool no remuestrea) y menos de 3 s.
"""
import math
import os
import random
import struct
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SR = 32728
DUR = 2.85
N = int(SR * DUR)


def env(t, a, d, peak=1.0):
    """Ataque lineal a, luego caída exponencial con constante d."""
    if t < 0:
        return 0.0
    if t < a:
        return peak * t / a
    return peak * math.exp(-(t - a) / d)


def main():
    rnd = random.Random(1998)
    L = [0.0] * N
    R = [0.0] * N

    # 1) Golpe grave con caída de tono (el «whoomp» de la válvula al abrirse)
    ph = 0.0
    for i in range(N):
        t = i / SR
        f = 46 + 26 * math.exp(-t / 0.18)
        ph += 2 * math.pi * f / SR
        v = math.sin(ph) * env(t, 0.012, 0.55, 0.55)
        L[i] += v
        R[i] += v

    # 2) Siseo de vapor: ruido por un filtro de banda que sube y cruza de lado
    y1 = y2 = 0.0
    for i in range(N):
        t = i / SR
        fc = 1800 + 3200 * min(1.0, t / 1.1)
        q = 2.2
        w0 = 2 * math.pi * fc / SR
        alpha = math.sin(w0) / (2 * q)
        b0, a0, a1, a2 = alpha, 1 + alpha, -2 * math.cos(w0), 1 - alpha
        x = rnd.uniform(-1, 1)
        y = (b0 * x - a1 * y1 - a2 * y2) / a0
        y2, y1 = y1, y
        e = env(t - 0.03, 0.10, 0.42, 0.55)
        pan = min(1.0, max(0.0, (t - 0.03) / 0.9))
        L[i] += y * e * math.cos(pan * math.pi / 2) * 1.4
        R[i] += y * e * math.sin(pan * math.pi / 2) * 1.4

    # 3) Eco metálico: campana FM con parciales inarmónicos (como un tubo golpeado)
    for (f, amp, dec, delay, side) in ((196.0, 0.16, 0.9, 0.05, 0.4), (196.0 * 2.76, 0.07, 0.55, 0.05, 0.6),
                                        (196.0 * 5.4, 0.035, 0.3, 0.05, 0.5), (146.8, 0.10, 1.1, 0.42, 0.65)):
        for i in range(N):
            t = i / SR - delay
            if t < 0:
                continue
            mod = math.sin(2 * math.pi * f * 1.41 * t) * 1.6 * math.exp(-t / 0.25)
            v = math.sin(2 * math.pi * f * t + mod) * env(t, 0.004, dec, amp)
            L[i] += v * (1 - side)
            R[i] += v * side

    # 4) Acorde que se abre (Re menor con novena): sierras suaves algo desafinadas
    notes = (146.83, 220.0, 293.66, 349.23, 329.63)
    lp_l = lp_r = 0.0
    for i in range(N):
        t = i / SR
        if t < 0.55:
            continue
        tt = t - 0.55
        a = min(1.0, tt / 0.7) * (1.0 if t < DUR - 0.9 else max(0.0, (DUR - t) / 0.9))
        sl = sr = 0.0
        for k, f in enumerate(notes):
            for det, side in ((-0.18, 0.75), (0.18, 0.25)):
                p = (t * (f + det)) % 1.0
                s = (2 * p - 1) * (0.12 if k < 3 else 0.07)
                sl += s * side
                sr += s * (1 - side)
        cut = 0.04 + 0.10 * min(1.0, tt / 1.2)    # el filtro se abre poco a poco
        lp_l += (sl - lp_l) * cut
        lp_r += (sr - lp_r) * cut
        L[i] += lp_l * a * 0.9
        R[i] += lp_r * a * 0.9

    # Eco cruzado (un rebote en una sala grande)
    d = int(0.19 * SR)
    for i in range(d, N):
        L[i] += R[i - d] * 0.22
        R[i] += L[i - d] * 0.22

    # Fundido final y normalizado sin saturar
    fade = int(0.25 * SR)
    for i in range(N - fade, N):
        g = (N - i) / fade
        L[i] *= g
        R[i] *= g
    peak = max(max(abs(v) for v in L), max(abs(v) for v in R))
    g = 0.89 / peak
    out = os.path.join(ROOT, "meta", "banner.wav")
    with wave.open(out, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<hh", int(L[i] * g * 32767), int(R[i] * g * 32767)) for i in range(N)))
    print(out, "%.2f s" % DUR)


if __name__ == "__main__":
    main()
