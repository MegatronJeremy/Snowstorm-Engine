#!/usr/bin/env python3
"""Generates Projects/Sandbox/assets/editor-icons/icons.png: a 4x1 atlas of 64px editor icons
(point light, spot light, directional light, camera). White glyph with a dark outline on
transparent, so ImDrawList::AddImage can tint it with the light color. Stdlib only."""
import math, struct, zlib, pathlib

CELL, N, SS = 64, 4, 4  # cell px, icon count, supersampling per axis


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def in_poly(px, py, pts):
    inside = False
    for i in range(len(pts)):
        (x1, y1), (x2, y2) = pts[i], pts[(i + 1) % len(pts)]
        if (y1 > py) != (y2 > py) and px < (x2 - x1) * (py - y1) / (y2 - y1) + x1:
            inside = not inside
    return inside


def poly_dist(px, py, pts):
    d = min(seg_dist(px, py, *pts[i], *pts[(i + 1) % len(pts)]) for i in range(len(pts)))
    return -d if in_poly(px, py, pts) else d


# Each glyph is a signed distance function in 0..64 cell space (negative inside).
def point(x, y):  # bulb: disc + base
    bulb = math.hypot(x - 32, y - 28) - 16
    base = max(abs(x - 32) - 8, abs(y - 50) - 5)
    return min(bulb, base)


def spot(x, y):  # downward cone from an apex
    return poly_dist(x, y, [(32, 8), (50, 52), (14, 52)])


def directional(x, y):  # sun: disc + 8 rays
    d = math.hypot(x - 32, y - 32) - 11
    for k in range(8):
        a = k * math.pi / 4
        d = min(d, seg_dist(x, y, 32 + math.cos(a) * 17, 32 + math.sin(a) * 17,
                            32 + math.cos(a) * 26, 32 + math.sin(a) * 26) - 2.5)
    return d


def camera(x, y):  # body + lens + viewfinder bump
    body = max(abs(x - 30) - 22, abs(y - 34) - 14)
    bump = max(abs(x - 22) - 8, abs(y - 18) - 4)
    lens = poly_dist(x, y, [(52, 28), (60, 20), (60, 48), (52, 40)])
    return min(body, bump, lens)


GLYPHS = [point, spot, directional, camera]
OUT = 2.0  # outline width px

W, H = CELL * N, CELL
rows = []
for py in range(H):
    row = bytearray([0])
    for px in range(W):
        fn = GLYPHS[px // CELL]
        lx0 = px % CELL
        fill = ring = 0
        for sy in range(SS):
            for sx in range(SS):
                d = fn(lx0 + (sx + .5) / SS, py + (sy + .5) / SS)
                if d <= 0: fill += 1
                elif d <= OUT: ring += 1
        n = SS * SS
        a = (fill + ring) / n
        # premultiplied-free straight alpha: color blends white (fill) and dark (ring)
        c = 255 * fill / max(fill + ring, 1) + 25 * ring / max(fill + ring, 1)
        row += bytes([int(c)] * 3 + [int(255 * a)])
    rows.append(bytes(row))


def chunk(t, d):
    c = struct.pack(">I", len(d)) + t + d
    return c + struct.pack(">I", zlib.crc32(t + d))


png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 6, 0, 0, 0)) \
    + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
out = pathlib.Path(__file__).resolve().parent.parent / "Projects/Sandbox/assets/editor-icons/icons.png"
out.write_bytes(png)
print(out, len(png), "bytes")
