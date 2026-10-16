#!/usr/bin/env python3
"""Generate ICON0.PNG (XMB icon) for the Aurora Coverflow PKG.

Pure standard library - no dependencies. Draws a purple/blue aurora-style
gradient with glowing bands, 320x176 (standard PS3 icon size).
"""
import zlib
import struct
import math
import os

W, H = 320, 176


def clamp(v, lo=0, hi=255):
    return max(lo, min(hi, int(v)))


def pixel(x, y):
    t = y / H
    # vertical gradient deep purple -> navy
    r = 24 + 26 * t
    g = 14 + 18 * t
    b = 62 + 34 * t

    # aurora arcs
    for cx, ph, col in ((0.25, 0.0, (0, 255, 220)), (0.75, 1.7, (170, 90, 255)), (0.5, 3.6, (255, 90, 200))):
        cy = H * 0.45 + 26 * math.sin(x / W * math.pi * 1.6 + ph)
        d = abs(y - cy) / (H * 0.16)
        if d < 1:
            a = (1 - d) ** 2 * 0.55
            r = r * (1 - a) + col[0] * a / 4
            g = g * (1 - a) + col[1] * a / 4
            b = b * (1 - a) + col[2] * a / 4

    # soft glow top-left
    dx, dy = x - W * 0.18, y - H * 0.30
    d = math.sqrt(dx * dx + dy * dy) / (W * 0.55)
    if d < 1:
        a = (1 - d) ** 2 * 0.30
        r += 90 * a
        g += 70 * a
        b += 160 * a

    return clamp(r), clamp(g), clamp(b)


def chunk(tag, data):
    return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)


def write_png(path, w, h):
    rows = bytearray()
    for y in range(h):
        rows.append(0)  # filter: none
        for x in range(w):
            r, g, b = pixel(x, y)
            rows += bytes((r, g, b))
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)  # 8-bit truecolor RGB
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(bytes(rows), 9))
           + chunk(b"IEND", b""))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)
    print("wrote", path)


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    write_png(os.path.join(here, "..", "data", "ICON0.PNG"), W, H)