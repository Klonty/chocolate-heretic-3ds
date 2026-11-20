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


# --- First-start screen (port3ds/p3ds_getwad.c) ------------------------------
#
# getwad_top.bin: the top-screen picture, both eyes, already in the
# framebuffer's layout (RGB565, 240-pixel columns from the bottom up, left
# to right; left eye first).  Layers sit at or behind the screen glass:
# the background furthest back, the tome and crystal a little behind, the
# text on the glass.  Nothing comes out of the screen.

TOP_W, TOP_H = 400, 240
DEPTH_BG = 6        # per-eye shift in pixels
DEPTH_TOME = 3


def top_layers(font_path, sub_font_path):
    margin = DEPTH_BG
    bg = gradient(TOP_W + 2 * margin, TOP_H).convert("RGBA")
    bg.alpha_composite(glow(bg.size, 120 + margin, 140, 110, (50, 140, 60), 70))
    bg.alpha_composite(glow(bg.size, 120 + margin, 60, 80, (60, 90, 160), 60))
    # Faint stone courses so the depth has something to show.
    d = ImageDraw.Draw(bg)
    for row, y in enumerate(range(10, TOP_H, 26)):
        d.line((0, y, bg.width, y), fill=(60, 40, 70, 60))
        for x in range((row % 2) * 30, bg.width, 60):
            d.line((x, y, x, y + 26), fill=(60, 40, 70, 45))

    mid = Image.new("RGBA", (TOP_W + 2 * DEPTH_TOME, TOP_H), (0, 0, 0, 0))
    tome(ImageDraw.Draw(mid), 50 + DEPTH_TOME, 112, 140, 104)
    crystal(mid, 116 + DEPTH_TOME, 62, 34)

    fg = Image.new("RGBA", (TOP_W, TOP_H), (0, 0, 0, 0))
    d = ImageDraw.Draw(fg)
    tx = 214
    size = 44
    while True:
        title = ImageFont.truetype(font_path, size)
        if d.textlength("HERETIC", font=title) <= TOP_W - tx - 10:
            break
        size -= 1
    d.text((tx + 2, 72 + 2), "HERETIC", font=title, fill=(0, 0, 0, 200))
    d.text((tx, 72), "HERETIC", font=title, fill=GOLD + (255,))
    sub = ImageFont.truetype(sub_font_path, 17)
    d.text((tx + 2, 130), "Chocolate Heretic", font=sub,
           fill=(210, 225, 210, 255))
    d.text((tx + 2, 152), "for Nintendo 3DS", font=sub,
           fill=(160, 180, 160, 255))
    return bg, mid, fg


def eye_image(layers, eye):
    bg, mid, fg = layers
    sign = -1 if eye == 0 else 1     # left eye: far things move left
    im = Image.new("RGBA", (TOP_W, TOP_H))
    im.alpha_composite(bg.crop((DEPTH_BG - sign * DEPTH_BG, 0,
                                DEPTH_BG - sign * DEPTH_BG + TOP_W, TOP_H)))
    im.alpha_composite(mid.crop((DEPTH_TOME - sign * DEPTH_TOME, 0,
                                 DEPTH_TOME - sign * DEPTH_TOME + TOP_W,
                                 TOP_H)))
    im.alpha_composite(fg)
    return im.convert("RGB")


def to_framebuffer(im):
    px = im.load()
    out = bytearray()
    w, h = im.size
    for x in range(w):
        for y in range(h - 1, -1, -1):
            r, g, b = px[x, y]
            out += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5)
                               | (b >> 3))
    return bytes(out)


def make_top(bin_path, preview_path, font_path, sub_font_path):
    layers = top_layers(font_path, sub_font_path)
    eyes = [eye_image(layers, e) for e in (0, 1)]
    with open(bin_path, "wb") as f:
        for im in eyes:
            f.write(to_framebuffer(im))
    if preview_path:
        eyes[0].save(preview_path)


# getwad_font.bin: anti-aliased fonts for the bottom-screen menu.
#
#   u32 font_count, then per font: u32 offset of its block.
#   block: u8 first_char, u8 char_count, u8 line_height, u8 reserved,
#          per char: u8 width, u8 advance, s8 x_offset, u8 reserved,
#                    u32 offset of width*line_height alpha bytes (from
#                    the start of the block),
#          then the alpha bytes.

FIRST_CHAR, CHAR_COUNT = 32, 95


def font_block(path, size):
    f = ImageFont.truetype(path, size)
    ascent, descent = f.getmetrics()
    height = ascent + descent
    glyphs = []
    for c in range(FIRST_CHAR, FIRST_CHAR + CHAR_COUNT):
        ch = chr(c)
        left, _, right, _ = f.getbbox(ch)
        width = max(1, right - left)
        im = Image.new("L", (width, height), 0)
        ImageDraw.Draw(im).text((-left, 0), ch, font=f, fill=255)
        glyphs.append((width, int(round(f.getlength(ch))), left,
                       im.tobytes()))
    header = struct.pack("<BBBB", FIRST_CHAR, CHAR_COUNT, height, 0)
    table_size = len(header) + 8 * CHAR_COUNT
    table = bytearray()
    data = bytearray()
    for width, advance, left, alpha in glyphs:
        table += struct.pack("<BBbBI", width, advance, left, 0,
                             table_size + len(data))
        data += alpha
    block = header + bytes(table) + bytes(data)
    return block + b"\0" * (-len(block) % 4)


def make_fonts(path, fonts):
    blocks = [font_block(p, s) for p, s in fonts]
    offset = 4 + 4 * len(blocks)
    out = struct.pack("<I", len(blocks))
    for b in blocks:
        out += struct.pack("<I", offset)
        offset += len(b)
    with open(path, "wb") as f:
        f.write(out + b"".join(blocks))


def make_silence(path, seconds=1.0, rate=22050):
    with wave.open(path, "wb") as f:
        f.setnchannels(1)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(struct.pack("<h", 0) * int(seconds * rate))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default=r"C:\Windows\Fonts\georgiab.ttf")
    ap.add_argument("--ui-font", default=r"C:\Windows\Fonts\segoeui.ttf")
    ap.add_argument("--ui-bold-font", default=r"C:\Windows\Fonts\seguisb.ttf")
    ap.add_argument("--preview", help="also save the top screen as a PNG")
    args = ap.parse_args()

    make_icon(os.path.join(HERE, "icon.png"))
    make_banner(os.path.join(HERE, "banner.png"), args.font)
    make_silence(os.path.join(HERE, "banner.wav"))
    make_top(os.path.join(HERE, "getwad_top.bin"), args.preview,
             args.font, args.ui_font)
    # Fonts 0-2 as used by p3ds_getwad.c: body, bold, title.
    make_fonts(os.path.join(HERE, "getwad_font.bin"),
               [(args.ui_font, 13), (args.ui_bold_font, 15),
                (args.font, 20)])


if __name__ == "__main__":
    main()
