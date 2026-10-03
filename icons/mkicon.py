#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2026 Rigby Foundation
"""mkicon.py: the app icons, as the `.zicon` section a program carries.

    mkicon.py <name> <out.zicon>            one of the icons drawn below
    mkicon.py --png <file.png> <out.zicon>  from a PNG (8-bit RGB/RGBA)
    mkicon.py --game <dir> <out.zicon>      a Ren'Py game's gui/window_icon.png (plain or in an .rpa)
    mkicon.py --list

A zicon is "ZICN", width, height (little-endian u32) and RGBA bytes, 96x96
here (drawn in 48-unit coordinates, 2x oversampled on top of that, boxed
down: soft edges, and enough pixels for a HiDPI dock)."""
import sys, struct, zlib, pickle, os, glob

SIZE = 96
SS = 2
U = SIZE // 48            # icon units (48 per icon) to pixels
N = SIZE * SS

class Canvas:
    def __init__(self):
        self.px = [[(0, 0, 0, 0)] * N for _ in range(N)]
    def blend(self, x, y, c):
        if 0 <= x < N and 0 <= y < N:
            r, g, b, a = c
            if a >= 255: self.px[y][x] = (r, g, b, 255); return
            R, G, B, A = self.px[y][x]
            na = a + A * (255 - a) // 255
            if na == 0: return
            self.px[y][x] = ((r * a + R * A * (255 - a) // 255) // na, (g * a + G * A * (255 - a) // 255) // na,
                             (b * a + B * A * (255 - a) // 255) // na, na)
    def rrect(self, x, y, w, h, r, c, grad=None):
        """rounded rectangle in icon units; grad = colour at the bottom for a vertical gradient"""
        x, y, w, h, r = x * SS * U, y * SS * U, w * SS * U, h * SS * U, r * SS * U
        for j in range(int(y), int(y + h)):
            t = (j - y) / max(h - 1, 1)
            col = c if not grad else tuple(int(c[k] + (grad[k] - c[k]) * t) for k in range(4))
            for i in range(int(x), int(x + w)):
                dx = max(x + r - i - 0.5, i + 0.5 - (x + w - r), 0)
                dy = max(y + r - j - 0.5, j + 0.5 - (y + h - r), 0)
                if dx * dx + dy * dy <= r * r: self.blend(i, j, col)
    def disc(self, cx, cy, r, c): self.rrect(cx - r, cy - r, 2 * r, 2 * r, r, c)
    def ring(self, cx, cy, r, t, c):
        cx, cy, r, t = cx * SS * U, cy * SS * U, r * SS * U, t * SS * U
        for j in range(int(cy - r - 1), int(cy + r + 2)):
            for i in range(int(cx - r - 1), int(cx + r + 2)):
                d = ((i + 0.5 - cx) ** 2 + (j + 0.5 - cy) ** 2) ** 0.5
                if r - t <= d <= r: self.blend(i, j, c)
    def line(self, x0, y0, x1, y1, t, c):
        x0, y0, x1, y1, t = x0 * SS * U, y0 * SS * U, x1 * SS * U, y1 * SS * U, t * SS * U
        L = max(((x1 - x0) ** 2 + (y1 - y0) ** 2) ** 0.5, 1e-6)
        ux, uy = (x1 - x0) / L, (y1 - y0) / L
        for j in range(int(min(y0, y1) - t), int(max(y0, y1) + t + 2)):
            for i in range(int(min(x0, x1) - t), int(max(x0, x1) + t + 2)):
                px, py = i + 0.5 - x0, j + 0.5 - y0
                s = max(0, min(L, px * ux + py * uy))
                d = ((px - s * ux) ** 2 + (py - s * uy) ** 2) ** 0.5
                if d <= t / 2: self.blend(i, j, c)
    def poly(self, pts, c):
        pts = [(x * SS * U, y * SS * U) for x, y in pts]
        ys = [p[1] for p in pts]
        for j in range(int(min(ys)), int(max(ys)) + 1):
            xs = []
            for k in range(len(pts)):
                (xa, ya), (xb, yb) = pts[k], pts[(k + 1) % len(pts)]
                if (ya <= j + 0.5) != (yb <= j + 0.5):
                    xs.append(xa + (j + 0.5 - ya) * (xb - xa) / (yb - ya))
            xs.sort()
            for a, b in zip(xs[::2], xs[1::2]):
                for i in range(int(a), int(b)): self.blend(i, j, c)
    def down(self):
        out = bytearray()
        for y in range(SIZE):
            for x in range(SIZE):
                r = g = b = a = 0
                for j in range(SS):
                    for i in range(SS):
                        pr, pg, pb, pa = self.px[y * SS + j][x * SS + i]
                        r += pr * pa; g += pg * pa; b += pb * pa; a += pa
                if a: out += bytes((r // a, g // a, b // a, a // (SS * SS)))
                else: out += b"\0\0\0\0"
        return bytes(out)

def rgba(h, a=255): return (h >> 16 & 255, h >> 8 & 255, h & 255, a)
WHITE = rgba(0xfafafa); DIM = rgba(0xa1a1aa); DARK = rgba(0x18181b); INK = rgba(0x27272a)

def tile(c, top, bottom):
    c.rrect(4, 4, 40, 40, 10, rgba(top), rgba(bottom))
    c.rrect(4, 4, 40, 40, 10, (255, 255, 255, 0))

def icon_zterm(c):
    tile(c, 0x3f3f46, 0x18181b)
    c.line(13, 17, 20, 23, 3, WHITE); c.line(20, 23, 13, 29, 3, WHITE)
    c.line(24, 31, 34, 31, 3, WHITE)

def icon_zfiles(c):
    c.rrect(5, 12, 22, 12, 4, rgba(0xd9a83a))
    c.rrect(5, 17, 38, 24, 5, rgba(0xf5c451), rgba(0xe0a832))
    c.rrect(5, 17, 38, 4, 2, rgba(0xfbd570))

def icon_zclock(c):
    c.disc(24, 24, 20, WHITE)
    c.disc(24, 24, 17, DARK)
    for k in range(12):
        import math
        a = k * math.pi / 6
        c.line(24 + 13 * math.sin(a), 24 - 13 * math.cos(a), 24 + 15 * math.sin(a), 24 - 15 * math.cos(a), 1.5 if k % 3 else 2.5, WHITE)
    c.line(24, 24, 24, 12, 2.5, WHITE); c.line(24, 24, 32, 28, 2.5, WHITE)
    c.disc(24, 24, 2, rgba(0xef4444))

def icon_zabout(c):
    c.disc(24, 24, 20, rgba(0x3b82f6))
    c.disc(24, 15, 2.5, WHITE)
    c.rrect(21.5, 20, 5, 14, 2, WHITE)

def icon_zview(c):
    c.rrect(10, 4, 28, 40, 4, rgba(0xfafafa), rgba(0xd4d4d8))
    c.poly([(30, 4), (38, 12), (30, 12)], rgba(0xa1a1aa))
    for y in (16, 22, 28, 34): c.rrect(15, y, 18 if y != 34 else 10, 2.5, 1, rgba(0x71717a))

def icon_zlaunch(c):
    tile(c, 0x52525b, 0x27272a)
    for i in range(3):
        for j in range(3):
            c.rrect(11 + i * 10, 11 + j * 10, 6, 6, 2, WHITE if (i + j) % 2 == 0 else DIM)

def icon_zwifi(c):
    import math
    tile(c, 0x2563eb, 0x1e3a8a)
    for r in (21, 14, 7):
        pts = [(24 + r * math.sin(math.radians(a)), 33 - r * math.cos(math.radians(a))) for a in range(-45, 46, 5)]
        for (x0, y0), (x1, y1) in zip(pts, pts[1:]): c.line(x0, y0, x1, y1, 3, WHITE)
    c.disc(24, 34, 2.5, WHITE)

def icon_gldemo(c):
    tile(c, 0x1e3a5f, 0x0f172a)
    c.poly([(24, 9), (38, 16), (24, 23), (10, 16)], rgba(0x93c5fd))
    c.poly([(10, 16), (24, 23), (24, 39), (10, 32)], rgba(0x3b82f6))
    c.poly([(38, 16), (24, 23), (24, 39), (38, 32)], rgba(0x1d4ed8))

def icon_sdldemo(c):
    tile(c, 0x7c3aed, 0x3b0764)
    c.disc(19, 21, 8, rgba(0xf472b6)); c.disc(29, 27, 8, rgba(0xfbbf24, 220)); c.disc(24, 18, 5, rgba(0x67e8f9, 230))

def icon_blitbench(c):
    tile(c, 0x065f46, 0x022c22)
    for i, h in enumerate((10, 18, 14, 24)):
        c.rrect(11 + i * 7.5, 36 - h, 5, h, 1.5, rgba(0x6ee7b7))

def icon_sdltone(c):
    tile(c, 0xb45309, 0x78350f)
    c.line(17, 33, 17, 15, 3, WHITE); c.line(17, 15, 31, 12, 3, WHITE); c.line(31, 12, 31, 30, 3, WHITE)
    c.disc(14, 33, 4, WHITE); c.disc(28, 30, 4, WHITE)

def icon_sdlrecreate(c):
    tile(c, 0x475569, 0x1e293b)
    c.rrect(10, 14, 20, 16, 3, rgba(0xcbd5e1)); c.rrect(18, 20, 20, 16, 3, rgba(0x94a3b8)); c.rrect(18, 20, 20, 4, 2, rgba(0x64748b))

def icon_renpy(c):
    tile(c, 0xbe185d, 0x500724)
    c.rrect(12, 13, 24, 22, 4, WHITE)
    c.poly([(20, 35), (28, 35), (18, 41)], WHITE)
    for y in (18, 23, 28): c.rrect(16, y, 16 if y != 28 else 9, 2.5, 1, rgba(0xbe185d))

def icon_image(c):
    c.rrect(6, 10, 36, 28, 4, rgba(0x60a5fa), rgba(0x2563eb))
    c.poly([(9, 35), (20, 22), (28, 31), (33, 26), (39, 35)], rgba(0x166534))
    c.disc(33, 17, 3.5, rgba(0xfde047))

def icon_folder(c): icon_zfiles(c)      # what Files shows for a directory
def icon_doc(c): icon_zview(c)          # ... and for a file it can only view

def icon_app(c):          # the generic executable
    tile(c, 0x52525b, 0x27272a)
    c.rrect(14, 15, 20, 18, 3, WHITE)
    c.rrect(14, 15, 20, 5, 2, DIM)

ICONS = {n[5:]: f for n, f in globals().items() if n.startswith("icon_")}

# ---- PNG in ---------------------------------------------------------------------
def png_decode(data):
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    pos, idat, w = 8, b"", None
    while pos < len(data):
        ln, typ = struct.unpack(">I4s", data[pos:pos + 8]); body = data[pos + 8:pos + 8 + ln]; pos += 12 + ln
        if typ == b"IHDR": w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif typ == b"IDAT": idat += body
    assert depth == 8 and interlace == 0 and ctype in (2, 6), "only 8-bit RGB/RGBA, non-interlaced"
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat); stride = w * bpp
    out, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]; line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0; b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out.append(bytes(line)); prev = line
    px = []
    for y in range(h):
        row = out[y]
        px.append([(row[x * bpp], row[x * bpp + 1], row[x * bpp + 2], row[x * bpp + 3] if bpp == 4 else 255) for x in range(w)])
    return w, h, px

def scale_to(px, w, h):
    """box-filter to SIZE x SIZE, premultiplied"""
    out = bytearray()
    for y in range(SIZE):
        y0, y1 = y * h // SIZE, max((y + 1) * h // SIZE, y * h // SIZE + 1)
        for x in range(SIZE):
            x0, x1 = x * w // SIZE, max((x + 1) * w // SIZE, x * w // SIZE + 1)
            r = g = b = a = n = 0
            for j in range(y0, y1):
                for i in range(x0, x1):
                    pr, pg, pb, pa = px[j][i]; r += pr * pa; g += pg * pa; b += pb * pa; a += pa; n += 1
            out += bytes((r // a, g // a, b // a, a // n)) if a else b"\0\0\0\0"
    return bytes(out)

def rpa_read(path, want):
    with open(path, "rb") as f:
        hdr = f.readline().split()
        if not hdr or not hdr[0].startswith(b"RPA-3"): return None
        off, key = int(hdr[1], 16), int(hdr[2], 16)
        f.seek(off); idx = pickle.loads(zlib.decompress(f.read()), encoding="bytes")
        for k, v in idx.items():
            name = k if isinstance(k, str) else k.decode()
            if name == want:
                o, l, pre = v[0]; o ^= key; l ^= key
                pre = pre if isinstance(pre, bytes) else pre.encode("latin-1")
                f.seek(o); return pre + f.read(l - len(pre))
    return None

def game_icon(d):
    plain = os.path.join(d, "game", "gui", "window_icon.png")
    if os.path.exists(plain): return open(plain, "rb").read()
    for rpa in sorted(glob.glob(os.path.join(d, "game", "*.rpa"))):
        data = rpa_read(rpa, "gui/window_icon.png")
        if data: return data
    return None

def write(out, rgba_bytes):
    with open(out, "wb") as f: f.write(b"ZICN" + struct.pack("<II", SIZE, SIZE) + rgba_bytes)

def main():
    a = sys.argv[1:]
    if a and a[0] == "--list": print(" ".join(sorted(ICONS))); return
    if len(a) == 3 and a[0] == "--png":
        w, h, px = png_decode(open(a[1], "rb").read()); write(a[2], scale_to(px, w, h)); return
    if len(a) == 3 and a[0] == "--game":
        data = game_icon(a[1])
        if not data: c = Canvas(); icon_renpy(c); write(a[2], c.down()); print("mkicon: no window_icon.png in %s, using the Ren'Py icon" % a[1]); return
        w, h, px = png_decode(data); write(a[2], scale_to(px, w, h)); return
    if len(a) != 2 or a[0] not in ICONS: sys.exit(__doc__)
    c = Canvas(); ICONS[a[0]](c); write(a[1], c.down())

if __name__ == "__main__":
    main()
