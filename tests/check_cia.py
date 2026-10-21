#!/usr/bin/env python3
"""Comprueba Steam3DS.cia y el banner antes de entregarlos.

- Versión del título en la TMD del .cia.
- Sonido del banner (CWAV dentro del CBMD): PCM16, estéreo, 32728 Hz,
  y las muestras del WAV metidas tal cual (bannertool no remuestrea).
Uso: python3 tests/check_cia.py Steam3DS.cia build/banner.bnr meta/banner.wav
"""
import struct
import sys
import wave


def align(x, a=64):
    return (x + a - 1) // a * a


def tmd_version(cia):
    d = open(cia, "rb").read()
    hdr, _, cert, tik, tmd = struct.unpack_from("<IHHIII", d, 0)[0], 0, *struct.unpack_from("<III", d, 8)
    off = align(hdr) + align(cert) + align(tik)
    sig = struct.unpack_from(">I", d, off)[0]
    sig_len = {0x10003: 0x240, 0x10004: 0x140, 0x10005: 0x80}[sig]
    return struct.unpack_from(">H", d, off + sig_len + 0x9C)[0]


def check_banner(bnr, wav_path):
    d = open(bnr, "rb").read()
    assert d[:4] == b"CBMD", "no es un CBMD"
    cwav_off = struct.unpack_from("<I", d, 0x84)[0]
    c = d[cwav_off:]
    assert c[:4] == b"CWAV", "no hay CWAV en 0x%x" % cwav_off
    # Bloques: referencias en la cabecera (tipo u16, pad, offset u32, tamaño u32)
    nblocks = struct.unpack_from("<H", c, 0x10)[0]
    blocks = {}
    for i in range(nblocks):
        t, _, o, s = struct.unpack_from("<HHII", c, 0x14 + i * 12)
        blocks[t] = (o, s)
    info = blocks[0x7000][0]
    enc = c[info + 8]
    rate = struct.unpack_from("<I", c, info + 12)[0]
    chans = struct.unpack_from("<I", c, info + 0x1C)[0]
    w = wave.open(wav_path)
    raw = w.readframes(w.getnframes())
    left = b"".join(raw[i:i + 2] for i in range(0, len(raw), 4))
    probe = left[2000:2400]
    ok_samples = probe in c
    print("banner: codificación=%d (1=PCM16) frecuencia=%d canales=%d muestras_iguales=%s" % (enc, rate, chans, ok_samples))
    return enc == 1 and rate == 32728 and chans == 2 and ok_samples


if __name__ == "__main__":
    cia, bnr, wav = sys.argv[1:4]
    v = tmd_version(cia)
    print("TMD: %d (%d.%d.%d)" % (v, v >> 10, (v >> 4) & 0x3F, v & 0xF))
    ok = check_banner(bnr, wav)
    print("OK" if ok else "FALLO")
    sys.exit(0 if ok else 1)
