#!/usr/bin/env python3
"""The controller ROM's chrome: the list cursor and marks, and the footer's cart, cable and plug,
emitted as 2bpp right after tools/mktiles.py's box frame.
    mkuitiles.py <generated-dir>
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from mktiles import BASE, BOX, emit, flip_h                     # noqa: E402

UI_BASE = BASE + len(BOX)
UI_END = 0xA0                   # scr_emu's VMU frame owns 0xA0-0xFF

# '.' background, '1' light, '2' shade, '3' ink. Footer cells: 1/2 are the cable, 3 the bead.
ART = {
    "CURSOR": ["........",
               "..3.....",
               "..33....",
               "..333...",
               "..3333..",
               "..333...",
               "..33....",
               "..3....."],
    "DOT":    ["........",
               "........",
               "...33...",
               "..3333..",
               "..3333..",
               "...33...",
               "........",
               "........"],
    "EDIT":   ["........",
               "...3....",
               "..333...",
               ".33333..",
               "..333...",
               "...3....",
               "........",
               "........"],
    "ARR_R":  ["........",
               "...3....",
               "...33...",
               "...333..",
               "...33...",
               "...3....",
               "........",
               "........"],
    "CABLE":  ["........",
               "........",
               "........",
               "11111111",
               "22222222",
               "........",
               "........",
               "........"],
    "BEAD":   ["........",
               "........",
               "...33...",
               "11333311",
               "22333322",
               "...33...",
               "........",
               "........"],
    "BEND_DN": ["........",
                "........",
                "........",
                "1111111.",
                "2222212.",
                ".....12.",
                ".....12.",
                ".....12."],
    "BEND_RT": [".....12.",
                ".....12.",
                ".....12.",
                ".....111",
                ".....222",
                "........",
                "........",
                "........"],
    "PLUG":   ["........",
               "..22222.",
               ".2111112",
               "11111112",
               "22111112",
               ".2111112",
               "..22222.",
               "........"],
    "TIP":    ["........",
               "........",
               "........",
               "333.....",
               "333.....",
               "........",
               "........",
               "........"],
    "PORT":   ["...2222.",
               "...2112.",
               "...2112.",
               "33321121",
               "33321121",
               "...2112.",
               "...2112.",
               "...2222."],
}
ART["ARR_L"] = flip_h(ART["ARR_R"])

# The footer's cart, 2x2 cells, flush right so the cable leaves from its edge. mkcart.py's measured
# fractions snapped to a 12x14 body: plate row 3, label rows 5-12, the top-right step held at 2 px
# (it measures 0.7), since the step is what makes it a Game Boy cart and not a floppy.
CART = ["................",
        "....2222222222..",
        "....2111111112..",
        "....212222222122",
        "....211111111112",
        "....212222222212",
        "....213333333312",
        "....213333333312",
        "....213333333312",
        "....213333333312",
        "....213333333312",
        "....213333333312",
        "....212222222212",
        "....211111111112",
        "....222222222222",
        "................"]


def mini_cart():
    return {n: [r[c * 8:c * 8 + 8] for r in CART[rr * 8:rr * 8 + 8]]
            for n, rr, c in (("CART_TL", 0, 0), ("CART_TR", 0, 1), ("CART_BL", 1, 0), ("CART_BR", 1, 1))}


ORDER = ["CURSOR", "DOT", "EDIT", "ARR_L", "ARR_R",
         "CART_TL", "CART_TR", "CART_BL", "CART_BR",
         "CABLE", "BEAD", "BEND_DN", "BEND_RT", "PLUG", "TIP", "PORT"]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gen")
    a = ap.parse_args()

    art = dict(ART, **mini_cart())
    tiles = [(n, art[n]) for n in ORDER]
    assert UI_BASE + len(tiles) <= UI_END, "%d tiles, %d free" % (len(tiles), UI_END - UI_BASE)
    emit(a.gen, "ui_tiles", "UI_T_", "k_ui_tiles", UI_BASE, tiles, "tools/mkuitiles.py")


if __name__ == "__main__":
    main()
