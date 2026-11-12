"""Draws the opt-in widescreen margin art for the title screen.

The title screen is a 240x160 manga page (5 px light page border, grey-framed
panels). This script draws two 120x160 side strips that continue the page with
new panels in the same style: focus lines, speed lines and screentone in the
title's own grey palette, plus a Buu-pink energy panel on the right. No game
pixels are copied; only palette values sampled from the title are reused.

The USA cart has a different title (navy sky, giant Dragon Ball behind the
logo). Its strips only continue that background: flat navy plus the rest of
the ball's rim (centre and radius fitted to the native rim), dithered with
the ball's own shades. Nothing is invented over the sky.

Output: src/title_margin_art.h (RGB555, row-major, [160][120] per side).
Optional previews: pass native 240x160 screenshots of the Spanish and/or USA
title to write <path>_wide.png / _wide2x.png with the art composed around it.

Usage: python tools/make_title_margin_art.py [spanish_title.png [usa_title.png]]
"""
import math
import os
import random
import sys

from PIL import Image, ImageDraw

W, H = 120, 160
PAGE = (247, 247, 247)
FRAME = (57, 57, 57)
BLACK = (0, 0, 0)
WHITE = (255, 255, 255)
G = [(41, 41, 41), (82, 82, 82), (123, 123, 123), (165, 165, 165),
     (198, 198, 198), (222, 222, 222)]
PINK = [(255, 239, 222), (255, 222, 222), (255, 189, 173), (255, 165, 165),
        (255, 132, 132), (255, 107, 107), (198, 0, 0)]

TOP, BOTTOM = 5, 142          # panel rows, aligned with the native left column
GUTTER = 4


def frame(d, box):
    d.rectangle(box, outline=FRAME)


BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def focus_lines(img, box, focus, rng, count=90, clear=0.30, ink=BLACK,
                paper=WHITE):
    """Manga focus lines: wedges aimed at `focus`, leaving a clean centre."""
    x0, y0, x1, y1 = box
    layer = Image.new("1", img.size, 0)
    d = ImageDraw.Draw(layer)
    fx, fy = focus
    reach = math.hypot(x1 - x0, y1 - y0) * 0.9
    for i in range(count):
        a = (i + rng.random() * 0.7) * 2 * math.pi / count
        width = 0.006 + rng.random() * 0.016
        inner = reach * (clear + rng.random() * 0.22)
        pts = [(fx + math.cos(a - width) * reach, fy + math.sin(a - width) * reach),
               (fx + math.cos(a + width) * reach, fy + math.sin(a + width) * reach),
               (fx + math.cos(a) * inner, fy + math.sin(a) * inner)]
        d.polygon(pts, fill=1)
    px, lp = img.load(), layer.load()
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px[x, y] = ink if lp[x, y] else paper


def speed_lines(img, box, angle, rng, base, line, density=0.55):
    """Parallel action lines of random length over a flat tone."""
    x0, y0, x1, y1 = box
    px = img.load()
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            px[x, y] = base
    layer = Image.new("1", img.size, 0)
    d = ImageDraw.Draw(layer)
    dx, dy = math.cos(angle), math.sin(angle)
    nx, ny = -dy, dx
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    span = int(math.hypot(x1 - x0, y1 - y0))
    for off in range(-span, span, 2):
        if rng.random() > density:
            continue
        length = rng.uniform(0.25, 0.9) * span
        start = rng.uniform(-span, span - length)
        ax, ay = cx + nx * off + dx * start, cy + ny * off + dy * start
        d.line([(ax, ay), (ax + dx * length, ay + dy * length)], fill=1)
    lp = layer.load()
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if lp[x, y]:
                px[x, y] = line


def screentone(img, box, shade, light=PAGE, dot=BLACK, mask=None):
    """Ordered (Bayer 4x4) dither between two tones; shade(x, y) -> 0..1."""
    x0, y0, x1, y1 = box
    px = img.load()
    mp = mask.load() if mask else None
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            if mp and not mp[x, y]:
                continue
            v = max(0.0, min(1.0, shade(x, y))) * 16
            px[x, y] = dot if BAYER[y & 3][x & 3] < v else light


def burst(img, center, r_in, r_out, spikes, fill, edge, rng):
    cx, cy = center
    pts = []
    for i in range(spikes * 2):
        a = i * math.pi / spikes + rng.uniform(-0.08, 0.08)
        r = r_out * rng.uniform(0.8, 1.0) if i % 2 == 0 else r_in
        pts.append((cx + math.cos(a) * r, cy + math.sin(a) * r))
    ImageDraw.Draw(img).polygon(pts, fill=fill, outline=edge)


# ── Spanish manga page, v2 ────────────────────────────────────────────
# Everything is drawn at SS x resolution and box-filtered down, then snapped
# to the page's own grey (or Buu-pink) palette. Thin strokes therefore land as
# clean 1 px lines with the same one-step grey anti-aliasing the page uses.
SS = 4
FONTS = r"C:\Windows\Fonts"
GREYS = [BLACK, (41, 41, 41), (57, 57, 57), (82, 82, 82), (99, 99, 99),
         (123, 123, 123), (148, 148, 148), (165, 165, 165), (198, 198, 198),
         (222, 222, 222), PAGE, WHITE]
PINKS = [WHITE, PAGE] + PINK + [(156, 24, 90), (90, 0, 0), BLACK]


def font(name, size):
    from PIL import ImageFont
    return ImageFont.truetype(os.path.join(FONTS, name), size)


def big(w, color):
    return Image.new("RGB", (w * SS, H * SS), color)


def snap(img, palette):
    """Box-filter an SS canvas down and snap every pixel to `palette`."""
    small = img.resize((img.width // SS, H), Image.BOX)
    px = small.load()
    for y in range(H):
        for x in range(small.width):
            c = px[x, y]
            px[x, y] = min(palette, key=lambda p: (p[0] - c[0]) ** 2 * 3 +
                           (p[1] - c[1]) ** 2 * 4 + (p[2] - c[2]) ** 2 * 2)
    return small


def poly_mask(w, points):
    m = Image.new("1", (w, H), 0)
    ImageDraw.Draw(m).polygon(points, fill=1)
    return m


def ss_focus(d, centre, rng, count, inner, outer, ink, width=(0.006, 0.02)):
    fx, fy = centre[0] * SS, centre[1] * SS
    for i in range(count):
        a = (i + rng.random() * 0.8) * 2 * math.pi / count
        w = rng.uniform(*width)
        r0 = (inner + rng.random() * inner * 0.6) * SS
        r1 = outer * SS
        d.polygon([(fx + math.cos(a) * r0, fy + math.sin(a) * r0),
                   (fx + math.cos(a - w) * r1, fy + math.sin(a - w) * r1),
                   (fx + math.cos(a + w) * r1, fy + math.sin(a + w) * r1)],
                  fill=ink)


def ss_star(d, cx, cy, r, fill):
    pts = []
    for i in range(10):
        a = -math.pi / 2 + i * math.pi / 5
        rr = r if i % 2 == 0 else r * 0.42
        pts.append(((cx + math.cos(a) * rr) * SS, (cy + math.sin(a) * rr) * SS))
    d.polygon(pts, fill=fill)


def outlined_text(xy, text, fnt, fill, outline, stroke, canvas, angle=0):
    """Bold manga SFX on a temp layer so it can be rotated."""
    tmp = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    ImageDraw.Draw(tmp).text(xy, text, font=fnt, fill=fill, stroke_width=stroke,
                             stroke_fill=outline, anchor="mm")
    if angle:
        tmp = tmp.rotate(angle, resample=Image.BICUBIC, center=xy)
    canvas.paste(tmp, (0, 0), tmp)


def dragon_ball(canvas, bx, by, br):
    """Four-star ball: grey sphere, black stars, halftone shadow side."""
    d = ImageDraw.Draw(canvas)
    k = br / 27.0
    d.ellipse(((bx - br - 3) * SS, (by - br - 3) * SS,
               (bx + br + 3) * SS, (by + br + 3) * SS), fill=WHITE)
    d.ellipse(((bx - br) * SS, (by - br) * SS, (bx + br) * SS, (by + br) * SS),
              fill=GREYS[6], outline=BLACK, width=SS + 2)
    for i, tone in enumerate((GREYS[8], GREYS[10], WHITE)):
        r = br * (0.82 - i * 0.2)
        ox, oy = bx - br * (0.12 + i * 0.08), by - br * (0.12 + i * 0.08)
        d.ellipse(((ox - r) * SS, (oy - r) * SS, (ox + r) * SS, (oy + r) * SS),
                  fill=tone)
    for sx, sy in ((-8, -8), (8, -6), (-6, 8), (9, 9)):
        ss_star(d, bx + sx * k, by + sy * k, max(2.6, 6.2 * k), BLACK)
    d.ellipse(((bx - 19 * k) * SS, (by - 21 * k) * SS,
               (bx - 7 * k) * SS, (by - 13 * k) * SS), fill=WHITE)


def ball_shadow(img, bx, by, br):
    px = img.load()
    for y in range(int(by - br), int(by + br) + 1):
        for x in range(int(bx - br), int(bx + br) + 1):
            if not (0 <= x < img.width):
                continue
            dx, dy = x - bx, y - by
            if math.hypot(dx, dy) < br - 1.5 and (dx + dy) / br > 0.35 and \
                    px[x, y] != BLACK:
                t = min(1.0, ((dx + dy) / br - 0.35) * 1.6)
                if BAYER[y & 3][x & 3] < t * 10:
                    px[x, y] = GREYS[5]


def draw_left(rng, m):
    """Left strip of width m (native page to its right): an SFX focus panel
    and a four-star Dragon Ball panel, composed for that exact width."""
    x0, x1 = 5, m - 1
    pw = x1 - x0
    p1 = [(x0, 5), (x1, 5), (x1, 59), (x0, 63)]
    p2 = [(x0, 67), (x1, 63), (x1, 142), (x0, 142)]

    # Panel 1: focus lines + menacing "ゴ" lettering (as many as fit).
    c1 = big(m, WHITE)
    d = ImageDraw.Draw(c1)
    ss_focus(d, (x0 + pw * 0.64, 31), rng, 150, max(6, 16 * pw / 114), 150, BLACK)
    glyphs = 0 if pw < 26 else 1 if pw < 52 else 2 if pw < 84 else 3
    size = 25 if pw >= 30 else 20
    fnt = font("YuGothB.ttc", size * SS)
    for i in range(glyphs):
        f = 0.5 if glyphs == 1 else i / (glyphs - 1)
        x = x0 + pw * (0.2 + 0.6 * f)
        y = 21 + 24 * f if glyphs > 1 else 32
        outlined_text((x * SS, y * SS), "ゴ", fnt, WHITE, BLACK, int(2.4 * SS),
                      c1, angle=14)
    panel1 = snap(c1, GREYS)

    # Panel 2: the four-star Dragon Ball glowing over radial speed lines.
    c2 = big(m, GREYS[9])
    d = ImageDraw.Draw(c2)
    bx, by = x0 + pw / 2.0 + 1, 104
    br = min(27.0, (pw - 8) / 2.0)
    ss_focus(d, (bx, by), rng, 110, max(br, 4) + 4, 150, GREYS[6],
             width=(0.01, 0.03))
    if br >= 9:
        dragon_ball(c2, bx, by, br)
    panel2 = snap(c2, GREYS)
    if br >= 9:
        ball_shadow(panel2, bx, by, br)

    img = Image.new("RGB", (m, H), PAGE)
    img.paste(panel1, (0, 0), poly_mask(m, p1))
    img.paste(panel2, (0, 0), poly_mask(m, p2))
    d = ImageDraw.Draw(img)
    d.polygon(p1, outline=FRAME)
    d.polygon(p2, outline=FRAME)
    return img


def majin_m(d, cx, cy, size, fill, outline):
    fnt = font("BOD_BLAR.TTF", int(size * SS))
    d.text((cx * SS, cy * SS), "M", font=fnt, fill=fill, anchor="mm",
           stroke_width=int(1.6 * SS), stroke_fill=outline)


def steam_cloud(d, rng, x, y, r):
    lobes = [(x, y, r * 0.72)]
    for k in range(5):
        a = k * 2 * math.pi / 5 + rng.uniform(-0.3, 0.3)
        lobes.append((x + math.cos(a) * r * 0.42, y + math.sin(a) * r * 0.42,
                      r * rng.uniform(0.48, 0.6)))
    for lx, ly, lr in lobes:      # one shared outline
        lr += 1.3
        d.ellipse(((lx - lr) * SS, (ly - lr) * SS,
                   (lx + lr) * SS, (ly + lr) * SS), fill=PINK[4])
    for lx, ly, lr in lobes:      # body
        d.ellipse(((lx - lr) * SS, (ly - lr) * SS,
                   (lx + lr) * SS, (ly + lr) * SS), fill=PINK[0])
    for lx, ly, lr in lobes:      # lit upper-left of each lobe
        hr = lr * 0.7
        d.ellipse(((lx - hr - lr * 0.2) * SS, (ly - hr - lr * 0.25) * SS,
                   (lx + hr - lr * 0.2) * SS, (ly + hr - lr * 0.25) * SS),
                  fill=WHITE)
    if r > 6:                     # inner curl stroke
        d.arc(((x - r * 0.35) * SS, (y - r * 0.1) * SS,
               (x + r * 0.45) * SS, (y + r * 0.6) * SS),
              200, 340, fill=PINK[3], width=SS)


def draw_right(rng, m):
    """Right strip of width m (native page to its left): Babidi's Majin
    emblem and Buu's head steam, composed for that exact width."""
    x1 = m - 6
    pw = x1
    p3 = [(0, 5), (x1, 5), (x1, 46), (0, 52)]
    p4 = [(0, 56), (x1, 50), (x1, 142), (0, 142)]
    s = min(1.0, pw / 114.0)

    # Panel 3: the Majin "M" glowing on a dark sky.
    c3 = big(m, GREYS[3])
    d = ImageDraw.Draw(c3)
    cx = pw / 2.0
    ss_focus(d, (cx, 28), rng, 90, 14 * s + 4, 130, GREYS[1], width=(0.01, 0.03))
    if pw >= 24:
        msize = min(34.0, pw * 0.62)
        e = msize / 34.0
        d.ellipse(((cx - 27 * e) * SS, (28 - 20 * e) * SS,
                   (cx + 27 * e) * SS, (28 + 20 * e) * SS), fill=GREYS[5])
        d.ellipse(((cx - 19 * e) * SS, (28 - 15 * e) * SS,
                   (cx + 19 * e) * SS, (28 + 15 * e) * SS), fill=GREYS[7])
        majin_m(d, cx, 28, msize, BLACK, WHITE)
    panel3 = snap(c3, GREYS)
    px = panel3.load()
    for _ in range(max(4, int(30 * s))):
        x, y = rng.randint(1, max(1, pw - 2)), rng.randint(7, 44)
        if px[x, y] in (GREYS[3], GREYS[1]):
            px[x, y] = WHITE

    # Panel 4: pink steam plumes rising from the lower left + "プシュー".
    c4 = big(m, PINK[1])
    d = ImageDraw.Draw(c4)
    ss_focus(d, (pw * 0.12, 140), rng, 120, 10, 190, PINK[3],
             width=(0.006, 0.02))
    puffs = []
    for t in range(7):
        f = t / 6.0
        puffs.append((pw * (0.16 + 0.68 * f) + 6 * s * math.sin(f * 7),
                      132 - 70 * f, (15 - 9 * f) * max(0.55, s)))
    if pw >= 52:
        for t in range(4):
            f = t / 3.0
            puffs.append((pw * (0.07 + 0.35 * f), 108 - 46 * f +
                          4 * math.sin(f * 6), (9 - 5 * f) * max(0.6, s)))
    puffs.reverse()
    for x, y, r in puffs:  # far (small) first, each cloud fully finished
        steam_cloud(d, rng, x, y, r)
    if pw >= 60:
        fnt = font("YuGothB.ttc", int(15 * s * SS))
        outlined_text((pw * 0.68 * SS, 124 * SS), "プシュー", fnt, WHITE,
                      PINK[6], int(1.8 * SS), c4, angle=18)
    panel4 = snap(c4, PINKS)

    img = Image.new("RGB", (m, H), PAGE)
    img.paste(panel3, (0, 0), poly_mask(m, p3))
    img.paste(panel4, (0, 0), poly_mask(m, p4))
    d = ImageDraw.Draw(img)
    d.polygon(p3, outline=FRAME)
    d.polygon(p4, outline=FRAME)
    return img


NAVY = (0, 0, 57)
BALL_CENTRE = (133, 102)    # native title coordinates, fitted to the rim
BALL_RADIUS = 137.5
BALL_OUTER = ((255, 173, 0), (255, 181, 41))
BALL_INNER = ((255, 181, 41), (255, 214, 0))


def draw_usa(side):
    """side = -1 (left strip, hx -120..-1) or +1 (right strip, hx 240..359)."""
    img = Image.new("RGB", (W, H), NAVY)
    px = img.load()
    for y in range(H):
        for x in range(W):
            hx = x - W if side < 0 else 240 + x
            r = math.hypot(hx + 0.5 - BALL_CENTRE[0], y + 0.5 - BALL_CENTRE[1])
            if r > BALL_RADIUS:
                continue
            if r > BALL_RADIUS - 11:
                a, b = BALL_OUTER
                t = (BALL_RADIUS - r) / 11.0
            else:
                a, b = BALL_INNER
                t = min(1.0, (BALL_RADIUS - 11 - r) / 14.0)
            px[x, y] = b if BAYER[y & 3][x & 3] < t * 16 else a
    return img


def rgb555(c):
    return (c[0] >> 3) | ((c[1] >> 3) << 5) | ((c[2] >> 3) << 10)


ES_WIDTHS = tuple(range(24, 121, 12))  # one composition per margin width


def c_array(f, name, img):
    px = img.load()
    f.write("constexpr std::uint16_t %s[%d * %d] = {\n" % (name, H, img.width))
    for y in range(H):
        f.write("  " + ",".join("0x%04X" % rgb555(px[x, y])
                                for x in range(img.width)) + ",\n")
    f.write("};\n")


HEADER = [
    "// Generated by tools/make_title_margin_art.py - do not edit.",
    "// Original margin art for the opt-in widescreen title page.",
    "#pragma once",
    "#include <cstdint>",
    "",
    "namespace buus_fury {",
    "constexpr int kTitleArtH = %d;" % H,
    "// One composition per strip width; the strip's inner edge touches the",
    "// native page. left/right are [kTitleArtH][width], row-major RGB555.",
    "struct TitleArtVariant {",
    "  int width;",
    "  const std::uint16_t* left;",
    "  const std::uint16_t* right;",
    "};",
]


def emit(es, usa, path):
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(HEADER) + "\n")
        for tag, variants in (("Es", es), ("Usa", usa)):
            for w, (l, r) in variants:
                c_array(f, "kTitle%sLeft%d" % (tag, w), l)
                c_array(f, "kTitle%sRight%d" % (tag, w), r)
            f.write("constexpr TitleArtVariant kTitle%sVariants[] = {\n" % tag)
            for w, _ in variants:
                f.write("  {%d, kTitle%sLeft%d, kTitle%sRight%d},\n"
                        % (w, tag, w, tag, w))
            f.write("};\n")
        f.write("}  // namespace buus_fury\n")


def compose(native, left, right, margin):
    """What the game shows at a given margin: strips cropped at the far side."""
    wide = Image.new("RGB", (240 + 2 * margin, 160))
    wide.paste(left.crop((left.width - margin, 0, left.width, H)), (0, 0))
    wide.paste(native, (margin, 0))
    wide.paste(right.crop((0, 0, margin, H)), (margin + 240, 0))
    return wide


def pick(variants, margin):
    for w, pair in variants:
        if w >= margin:
            return pair
    return variants[-1][1]


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    es = []
    for w in ES_WIDTHS:
        rng = random.Random(0xB005 + w)
        es.append((w, (draw_left(rng, w), draw_right(rng, w))))
    usa = [(W, (draw_usa(-1), draw_usa(+1)))]
    emit(es, usa, os.path.join(root, "src", "title_margin_art.h"))
    for arg, variants in zip(sys.argv[1:], (es, usa)):
        native = Image.open(arg).convert("RGB")
        stem = os.path.splitext(arg)[0]
        rows = [compose(native, *pick(variants, m), m) for m in (22, 40, 66, 96, 120)]
        sheet = Image.new("RGB", (480, sum(r.height + 4 for r in rows)), (30, 30, 30))
        y = 0
        for r in rows:
            sheet.paste(r, ((480 - r.width) // 2, y)); y += r.height + 4
        sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(
            stem + "_sheet2x.png")
        print("preview", stem + "_sheet2x.png")


if __name__ == "__main__":
    main()
