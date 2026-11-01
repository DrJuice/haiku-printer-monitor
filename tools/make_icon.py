# ============================================================================
# File:        make_icon.py
# Project:     haiku-printer-monitor
# Component:   tools
# Description: Builds the Printer Monitor app icon in HVIF (Haiku Vector Icon
#              Format, as read by Haiku's src/libs/icon/flat_icon/
#              FlatIconImporter.cpp): a printer with four toner bars. Prints
#              the hex for the "vector_icon" resource in
#              src/app/PrinterMonitor.rdef, checks it with an independent
#              decoder, and (if Pillow is installed) writes PNG previews.
#              Usage: python3 tools/make_icon.py
# License:     MIT (see LICENSE)
# Author:      Tony (DrJuice), with Claude (Anthropic)
# ----------------------------------------------------------------------------
# Revision History
#   Rev  Date        Author        Changes
#   0.1  2026-11-01  Tony/Claude   Initial creation (milestone 6b).
# ============================================================================
import struct

styles, paths, shapes = [], [], []

def style(rgb):
    if rgb not in styles: styles.append(rgb)
    return styles.index(rgb)

def rect(x0, y0, x1, y1, rgb):
    paths.append([(x0,y0),(x1,y0),(x1,y1),(x0,y1)])
    shapes.append((style(rgb), len(paths)-1))

def coord(v):
    v = float(v)
    if v == int(v) and -32 <= v <= 95:
        return bytes([int(v) + 32])
    raw = int(round((v + 128.0) * 102.0))
    return bytes([0x80 | (raw >> 8), raw & 0xFF])

# --- design, 64x64 canvas, drawn back to front ---
rect(15, 6, 49, 27, (0x70,0x70,0x70))        # paper outline
rect(16, 7, 48, 27, (0xFF,0xFF,0xFF))        # paper
rect(20, 12, 44, 14, (0xB0,0xB0,0xB0))       # text line
rect(20, 17, 38, 19, (0xB0,0xB0,0xB0))       # text line
rect(4, 23, 60, 55, (0x3C,0x3C,0x3C))        # body outline
rect(5, 24, 59, 54, (0xD6,0xD6,0xD6))        # body
rect(5, 24, 59, 27, (0xEC,0xEC,0xEC))        # top highlight
rect(11, 48, 53, 51, (0x50,0x50,0x50))       # output slot
levels = [(0x10,0x10,0x10,0.30), (0x00,0xAE,0xEF,0.75),
          (0xEC,0x00,0x8C,0.55), (0xFF,0xD8,0x00,0.90)]
x = 13
top, bottom = 29, 45
for r,g,b,f in levels:
    rect(x-1, top-1, x+8, bottom+1, (0x60,0x60,0x60))   # bar frame
    rect(x, top, x+7, bottom, (0xF4,0xF4,0xF4))         # bar track
    h = round((bottom-top)*f)
    rect(x, bottom-h, x+7, bottom, (r,g,b))              # level
    x += 10

# --- encode ---
out = bytearray(b'ncif')
out.append(len(styles))
for r,g,b in styles: out += bytes([3, r, g, b])          # SOLID_COLOR_NO_ALPHA
out.append(len(paths))
for pts in paths:
    out += bytes([0x02 | 0x08, len(pts)])                 # CLOSED | NO_CURVES
    for px,py in pts: out += coord(px) + coord(py)
out.append(len(shapes))
for s,p in shapes:
    out += bytes([10, s, 1, p, 0x04])                     # PATH_SOURCE, HINTING
assert len(styles) < 256 and len(paths) < 256 and len(shapes) < 256

# --- independent decode check (mirrors the importer) ---
def decode(buf):
    i = 4; assert buf[:4] == b'ncif'
    n = buf[i]; i += 1; st = []
    for _ in range(n):
        assert buf[i] == 3; st.append(tuple(buf[i+1:i+4])); i += 4
    n = buf[i]; i += 1; pa = []
    for _ in range(n):
        flags, cnt = buf[i], buf[i+1]; i += 2; pts = []
        assert flags & 0x08
        for _ in range(cnt*2):
            v = buf[i]; i += 1
            if v & 0x80: v = ((v & 0x7F) << 8) | buf[i]; i += 1; v = v/102 - 128
            else: v = v - 32
            pts.append(v)
        pa.append(list(zip(pts[0::2], pts[1::2])))
    n = buf[i]; i += 1; sh = []
    for _ in range(n):
        assert buf[i] == 10; sh.append((buf[i+1], buf[i+3])); i += 5
    assert i == len(buf)
    return st, pa, sh

st, pa, sh = decode(bytes(out))

try:
    from PIL import Image, ImageDraw
    for size in (64, 32, 16):
        img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        k = size / 64
        for s, p in sh:
            xs = [q[0] * k for q in pa[p]]; ys = [q[1] * k for q in pa[p]]
            draw.rectangle([min(xs), min(ys), max(xs) - 1e-6, max(ys) - 1e-6],
                fill=st[s] + (255,))
        img.save('icon_preview_%d.png' % size)
    print("Wrote icon_preview_64/32/16.png")
except ImportError:
    print("Pillow not installed; skipping PNG previews")

hexs = out.hex().upper()
lines = [hexs[j:j+64] for j in range(0, len(hexs), 64)]
print("%d bytes. Paste into PrinterMonitor.rdef:" % len(out))
print("resource vector_icon {")
for line in lines:
    print('\t$"%s"' % line)
print("};")
