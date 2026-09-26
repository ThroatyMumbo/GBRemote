#!/usr/bin/env python3
"""The Game Boy cartridge on the Game Pak shelf: one 24x32 icon drawn with SDFs, a grey slashed
variant for "no pak", and one badged tile for the cart that is staged.

Every measurement below is a fraction of a reference cart's own bounding box (W/H = 0.875). Four
colours fixed by role — 0 the page, 1 the label and the plate, 2 the shell, 3 ink — so one CGB
palette draws a whole cart and a game's colour is two words of it.
    mkcart.py <generated-dir>
"""
import argparse
import os

import numpy as np

COLS, ROWS = 3, 4
W, H = COLS * 8, ROWS * 8
SS = 4                                          # supersample, then majority-filter to keep it crisp
BASE = 0x60                                     # the island between GBDK's font and BT_BASE

# 21 px of the 24 wide: the 1.5 px of margin either side is what separates two carts on the shelf,
# where five of them abut to fit inside an 18-column frame. The height follows from the aspect.
ASPECT = 0.875
BODY_W = 21.0
BODY_H = BODY_W / ASPECT
# Both on half-pixel centres, so the outline band falls inside one row rather than straddling two
# and coming out of down() two pixels thick.
BODY_X, BODY_Y = (W - BODY_W) / 2.0, 4.5
CORNER = 0.9

# Fractions of the body. The top-right step is the cart's one asymmetry and the thing that makes a
# 24 px icon read as a Game Boy cartridge rather than as a floppy disk.
NOTCH_U, NOTCH_V = 0.910, 0.049
# The plate's measured bottom is 0.247; snapped up to a pixel boundary, because the reference's gap
# to the window below is 0.84 px and a half-covered row merges the two into one light block.
OVAL = (0.114, 0.063, 0.884, 0.229)             # the embossed "Nintendo GAME BOY" plate
WINDOW = (0.113, 0.282, 0.884, 0.876)           # the recessed label window
TAB = (0.431, 0.569, 0.915, 0.985)              # the little triangle under the label

# "This cart is staged", a tick on the label window: two strokes, sized to survive down().
BADGE = ((9.3, 19.2, 11.2, 21.1), (11.2, 21.1, 14.8, 16.9), 1.0)
BADGE_CELL = (1, 2)
SLASH = (3.5, 5.5, 20.5, 26.5, 1.6, 1.3)        # x0, y0, x1, y1, half-width, halo

# Every shell colour the shelf can draw, in CGB 5-bit: {light, dark}. The first seven are the ones
# real cartridges came in; the rest exist so a game with no table entry still gets its own.
HUES = [
    ("RED",     (30, 8, 6),   (17, 2, 2)),
    ("BLUE",    (9, 14, 31),  (2, 6, 19)),
    ("GREEN",   (9, 24, 9),   (2, 13, 4)),
    ("YELLOW",  (31, 26, 5),  (20, 14, 1)),
    ("GOLD",    (27, 20, 6),  (16, 11, 1)),
    ("SILVER",  (23, 25, 29), (13, 15, 20)),
    ("CRYSTAL", (17, 28, 31), (6, 16, 25)),
    ("PURPLE",  (20, 9, 28),  (10, 2, 16)),
    ("ORANGE",  (31, 17, 3),  (19, 8, 1)),
    ("TEAL",    (5, 25, 22),  (1, 13, 12)),
    ("PINK",    (31, 13, 23), (19, 4, 12)),
    ("OLIVE",   (20, 22, 7),  (10, 12, 2)),
]
# Palette 0 on this screen: the chrome every text cell sits on, and the grey of the no-pak cart.
CHROME = [(31, 31, 31), (21, 21, 22), (12, 12, 13), (0, 0, 0)]
HEAD = [(3, 6, 17), (8, 13, 24), (20, 24, 31), (31, 31, 31)]    # canvas.c's CV_PAL_HEAD, unchanged


def grid():
    y, x = np.mgrid[0:H * SS, 0:W * SS].astype(np.float32)
    return (x + 0.5) / SS, (y + 0.5) / SS


def ux(u):
    return BODY_X + u * BODY_W


def vy(v):
    return BODY_Y + v * BODY_H


def rbox(px, py, x0, y0, x1, y1, r=0.0):
    cx, cy, bx, by = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2
    qx, qy = np.abs(px - cx) - bx + r, np.abs(py - cy) - by + r
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r


def capsule(px, py, ax, ay, bx, by, r):
    vx, vy_ = bx - ax, by - ay
    t = np.clip(((px - ax) * vx + (py - ay) * vy_) / (vx * vx + vy_ * vy_), 0, 1)
    return np.hypot(px - ax - t * vx, py - ay - t * vy_) - r


def shell(px, py, o=0.0):
    """The body, with the top-right corner stepped out of it."""
    x0, y0, x1, y1 = BODY_X, BODY_Y, BODY_X + BODY_W, BODY_Y + BODY_H
    d = rbox(px - o, py - o, x0, y0, x1, y1, CORNER)
    notch = rbox(px - o, py - o, ux(NOTCH_U), y0 - 2.0, x1 + 2.0, vy(NOTCH_V))
    return np.maximum(d, -notch)


def render(px, py, slash=False, badge=False):
    img = np.zeros((H * SS, W * SS), np.uint8)
    body = shell(px, py)
    inside = body < 0

    # The one departure from the reference: there, the label is paper and the shell around it is
    # 2.4 units wide, which at 21 px leaves the colour a hairline. Here the label carries the shade
    # the light catches and the shell the one it does not, so the whole icon is the game's colour.
    img[inside] = 2

    # Neither gets an ink border. The shell is only 2.4 units wide, so a second ink line inside the
    # silhouette's own would leave no colour between the two at all.
    ov = rbox(px, py, ux(OVAL[0]), vy(OVAL[1]), ux(OVAL[2]), vy(OVAL[3]),
              (vy(OVAL[3]) - vy(OVAL[1])) / 2.0)
    img[ov < 0] = 1

    win = rbox(px, py, ux(WINDOW[0]), vy(WINDOW[1]), ux(WINDOW[2]), vy(WINDOW[3]), 0.6)
    img[win < 0] = 1

    tx0, tx1, ty0, ty1 = ux(TAB[0]), ux(TAB[1]), vy(TAB[2]), vy(TAB[3])
    cx, hw = (tx0 + tx1) / 2, (tx1 - tx0) / 2
    img[(py >= ty0) & (py <= ty1) & (np.abs(px - cx) <= hw * (ty1 - py) / (ty1 - ty0))] = 1

    img[np.abs(body) < 0.55] = 3
    if badge:
        for s in BADGE[:2]:
            img[capsule(px, py, *s, BADGE[2]) < 0] = 3
    if slash:
        sd = capsule(px, py, *SLASH[:4], SLASH[4])
        img[sd < SLASH[5]] = 0                               # a halo, so the bar reads over the ink
        img[sd < 0] = 3
    return down(img)


def down(img):
    """Majority filter, with ink winning early so a one-pixel outline survives the downsample."""
    blk = img.reshape(H, SS, W, SS).transpose(0, 2, 1, 3).reshape(H, W, SS * SS)
    cnt = (blk[..., None] == np.arange(4)).sum(2)
    return np.where(cnt[:, :, 3] >= SS * SS // 3, 3, cnt.argmax(2)).astype(np.uint8)


def cells(img):
    return [img[r * 8:r * 8 + 8, c * 8:c * 8 + 8] for r in range(ROWS) for c in range(COLS)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gen")
    a = ap.parse_args()

    px, py = grid()
    plain = render(px, py)
    none = render(px, py, slash=True)
    badged = render(px, py, badge=True)

    tiles, slots = [], {}
    def slot(blk):
        k = blk.tobytes()
        if k not in slots:
            slots[k] = len(tiles)
            tiles.append(blk)
        return BASE + slots[k]

    plain_map = [slot(b) for b in cells(plain)]
    none_map = [slot(b) for b in cells(none)]
    badge_tile = slot(cells(badged)[BADGE_CELL[1] * COLS + BADGE_CELL[0]])
    assert len(tiles) <= 0x80 - BASE, \
        "%d tiles, %d free between the font and BT_BASE" % (len(tiles), 0x80 - BASE)

    write(a.gen, tiles, plain_map, none_map, badge_tile)
    print("cart: %d unique tiles at 0x%02X (%d free to BT_BASE), %d hues"
          % (len(tiles), BASE, 0x80 - BASE - len(tiles), len(HUES)))


def row_planes(row):
    lo = hi = 0
    for x, v in enumerate(row):
        lo |= (int(v) & 1) << (7 - x)
        hi |= (int(v) >> 1 & 1) << (7 - x)
    return lo, hi


def c_array(ctype, name, vals, fmt="0x{:02X}"):
    lines = [", ".join(fmt.format(v) for v in vals[i:i + 16]) for i in range(0, len(vals), 16)]
    return f"const {ctype} {name}[{len(vals)}] = {{\n    " + ",\n    ".join(lines) + "\n};\n"


def pal_words(four):
    return [int(r | g << 5 | b << 10) for r, g, b in four]


def write(gen, tiles, plain_map, none_map, badge_tile):
    os.makedirs(gen, exist_ok=True)
    head = "// generated by tools/mkcart.py — do not edit\n"
    h = head + """#ifndef CART_GFX_H
#define CART_GFX_H

#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdint.h>

#define CART_BASE 0x%02X
#define CART_NTILES %d
#define CART_W %d
#define CART_H %d
#define CART_CELLS %d
#define CART_NHUE %d
#define CART_BADGE_X %d         // which cell of the cart the staged badge replaces
#define CART_BADGE_Y %d

""" % (BASE, len(tiles), COLS, ROWS, COLS * ROWS, len(HUES), BADGE_CELL[0], BADGE_CELL[1])
    h += "".join("#define CART_HUE_%-8s %d\n" % (n, i) for i, (n, _, _) in enumerate(HUES))
    h += """
extern const uint8_t cart_tiles[];
extern const uint8_t cart_map[];        // CART_CELLS absolute tile numbers, row major
extern const uint8_t cart_map_none[];   // the same cart, grey and slashed
extern const uint8_t cart_badge;
extern const palette_color_t cart_pal[];    // CART_NHUE x 4, white / light / dark / ink
extern const palette_color_t cart_chrome[]; // palette 0: text, boxes and the no-pak cart
extern const palette_color_t cart_head[];   // palette 1: the title bar

#endif
"""
    open(os.path.join(gen, "cart_gfx.h"), "w").write(h)

    data = []
    for blk in tiles:
        for row in blk:
            lo, hi = row_planes(row)
            data += [lo, hi]
    words = []
    for _, lt, dk in HUES:
        words += pal_words([(31, 31, 31), lt, dk, (0, 0, 0)])

    c = head + '#include "cart_gfx.h"\n\n'
    c += c_array("uint8_t", "cart_tiles", data) + "\n"
    c += c_array("uint8_t", "cart_map", plain_map) + "\n"
    c += c_array("uint8_t", "cart_map_none", none_map) + "\n"
    c += "const uint8_t cart_badge = 0x%02X;\n\n" % badge_tile
    c += c_array("palette_color_t", "cart_pal", words, "0x{:04X}") + "\n"
    c += c_array("palette_color_t", "cart_chrome", pal_words(CHROME), "0x{:04X}") + "\n"
    c += c_array("palette_color_t", "cart_head", pal_words(HEAD), "0x{:04X}") + "\n"
    open(os.path.join(gen, "cart_gfx.c"), "w").write(c)


main()
