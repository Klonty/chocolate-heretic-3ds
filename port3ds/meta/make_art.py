#!/usr/bin/env python3
#
# Draws the HOME menu icon and banner for the .cia (and the .3dsx icon).
# Original art: a green spell-tome under a glowing crystal; no artwork
# from the game data is used.  The outputs are committed, so building does
# not need this script; rerun it only to change the art:
#
#   python make_art.py
#
# Needs Pillow.  The banner text uses Georgia Bold from Windows; pass
# another TrueType file with --font elsewhere.

import argparse
import math
import os
import struct
import wave

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))

BG_TOP = (34, 18, 40)
BG_BOTTOM = (8, 6, 12)
LEATHER = (36, 112, 48)
LEATHER_DARK = (18, 64, 26)
LEATHER_LIGHT = (70, 160, 80)
PAGES = (226, 210, 168)
PAGES_SHADE = (176, 158, 116)
GOLD = (236, 184, 60)
GOLD_DARK = (150, 100, 24)
CRYSTAL = (150, 200, 255)
CRYSTAL_CORE = (240, 250, 255)


def gradient(w, h):
    im = Image.new("RGB", (w, h))
    px = im.load()
    for y in range(h):
        t = y / max(1, h - 1)
        c = tuple(int(a + (b - a) * t) for a, b in zip(BG_TOP, BG_BOTTOM))
        for x in range(w):
            px[x, y] = c
    return im


def glow(size, cx, cy, radius, colour, strength):
    """An RGBA layer with a soft round glow."""
    layer = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.ellipse((cx - radius, cy - radius, cx + radius, cy + radius),
              fill=colour + (strength,))
    return layer.filter(ImageFilter.GaussianBlur(radius * 0.6))


def star(d, cx, cy, r_out, r_in, points, fill):
    pts = []
    for i in range(points * 2):
        r = r_out if i % 2 == 0 else r_in
        a = math.pi * i / points - math.pi / 2
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    d.polygon(pts, fill=fill)


def tome(d, x, y, w, h):
    """A closed book seen slightly from the front: pages at the right and
    bottom edge, a gold clasp and corner caps, and an embossed sigil."""
    page = max(2, w // 10)
    # Pages (behind the cover, showing at the right and bottom).
    d.rectangle((x + page, y + page, x + w, y + h), fill=PAGES_SHADE)
    d.rectangle((x + page, y + page, x + w - 1, y + h - 2), fill=PAGES)
    for i in range(1, 4):
        yy = y + h - 1 - i * max(1, page // 3)
        d.line((x + page + 2, yy, x + w - 2, yy), fill=PAGES_SHADE)
    # Cover.
    cw, ch = w - page, h - page
    d.rectangle((x, y, x + cw, y + ch), fill=LEATHER_DARK)
    d.rectangle((x + 1, y + 1, x + cw - 1, y + ch - 1), fill=LEATHER)
    d.line((x + 1, y + 1, x + cw - 2, y + 1), fill=LEATHER_LIGHT)
    # Spine.
    sp = max(2, cw // 7)
    d.rectangle((x, y, x + sp, y + ch), fill=LEATHER_DARK)
    # Corner caps.
    cap = max(2, cw // 6)
    for cx, cy in ((x + cw - cap, y), (x + cw - cap, y + ch - cap)):
        d.rectangle((cx, cy, cx + cap, cy + cap), fill=GOLD)
        d.rectangle((cx + 1, cy + 1, cx + cap, cy + cap), fill=GOLD_DARK)
        d.rectangle((cx, cy, cx + cap - 1, cy + cap - 1), fill=GOLD)
    # Clasp.
    cl_y = y + ch // 2
    d.rectangle((x + cw - cap // 2, cl_y - cap // 2, x + cw + page,
                 cl_y + cap // 2), fill=GOLD)
    # Sigil: a diamond with a dot.
    mx, my = x + sp + (cw - sp) // 2, y + ch // 2
    r = max(3, (cw - sp) // 4)
    d.polygon(((mx, my - r), (mx + r, my), (mx, my + r), (mx - r, my)),
              outline=GOLD, fill=LEATHER_DARK)
    d.ellipse((mx - r // 3, my - r // 3, mx + r // 3, my + r // 3),
              fill=GOLD)


def crystal(base, cx, cy, r):
    base.alpha_composite(glow(base.size, cx, cy, r * 2.2, CRYSTAL, 170))
    d = ImageDraw.Draw(base)
    star(d, cx, cy, r, r * 0.38, 4, CRYSTAL + (255,))
    star(d, cx, cy, r * 0.55, r * 0.2, 4, CRYSTAL_CORE + (255,))


def make_icon(path):
    w = h = 48
    im = gradient(w, h).convert("RGBA")
    im.alpha_composite(glow((w, h), 24, 30, 18, (60, 160, 70), 90))
    tome(ImageDraw.Draw(im), 9, 20, 30, 24)
    crystal(im, 24, 11, 9)
    im.convert("RGB").save(path)


def make_banner(path, font_path):
    w, h = 256, 128
    im = gradient(w, h).convert("RGBA")
    im.alpha_composite(glow((w, h), 52, 80, 40, (60, 160, 70), 80))
    tome(ImageDraw.Draw(im), 16, 52, 72, 58)
    crystal(im, 52, 28, 20)

    d = ImageDraw.Draw(im)
    tx, ty = 100, 34
    size = 34
    while True:
        title = ImageFont.truetype(font_path, size)
        if d.textlength("HERETIC", font=title) <= w - tx - 8 or size <= 12:
            break
        size -= 1
    sub = ImageFont.truetype(font_path, 13)
    d.text((tx + 2, ty + 2), "HERETIC", font=title, fill=(0, 0, 0, 200))
    d.text((tx, ty), "HERETIC", font=title, fill=GOLD + (255,))
    d.text((tx + 2, ty + 46), "Chocolate Heretic", font=sub,
           fill=(200, 220, 200, 255))
    d.text((tx + 2, ty + 63), "3DS port", font=sub,
           fill=(160, 180, 160, 255))
    im.convert("RGB").save(path)


def make_silence(path, seconds=1.0, rate=22050):
    with wave.open(path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(struct.pack("<h", 0) * int(seconds * rate))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default=r"C:\Windows\Fonts\georgiab.ttf")
    args = ap.parse_args()

    make_icon(os.path.join(HERE, "icon.png"))
    make_banner(os.path.join(HERE, "banner.png"), args.font)
    make_silence(os.path.join(HERE, "banner.wav"))


if __name__ == "__main__":
    main()
