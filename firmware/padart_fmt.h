// padart_fmt.h — the controller-art blob core0 stages at $4600 in the served high bank.
#ifndef PADART_FMT_H
#define PADART_FMT_H

#include <stdint.h>

#define ART_BASE     0x4600     // GB address of the blob in the served high bank
#define ART_WINDOW   (0x8000 - ART_BASE)        // 14848
#define ART_MAGIC0   'G'
#define ART_MAGIC1   'A'
#define ART_VERSION  1

// Pinned, not computed: the ROM's GDMA needs the tile block 16-byte aligned in GB space.
#define ART_OFF_TILES 0x40
#define ART_HDR_LEN   ART_OFF_TILES

// The ROM's own ceilings, asserted by the packer so a bad blob never reaches a console.
#define ART_MAX_TILES 256       // one VRAM bank of BG tiles; ARTH_NTILES 0 means 256, as in set_bkg_data()
#define ART_MAX_PALS  8
#define ART_COLS      20        // the whole screen; a picture that is not full-screen pads itself
#define ART_ROWS      18
#define ART_MAX_BTN   24        // pad.h's PAD_* bits published at $001B-$001D
#define ART_MAX_K     4         // 1 << nb variants per cell; the ROM's mask is a 4-entry lookup

enum {
    ARTH_MAGIC0 = 0,
    ARTH_MAGIC1 = 1,
    ARTH_VERSION = 2,
    ARTH_PROTO = 3,             // PROTO_*, so the ROM can tell whose picture the window holds
    ARTH_HDR_LEN = 4,
    ARTH_NTILES = 5,
    ARTH_NPALS = 6,
    ARTH_NBTN = 7,
    ARTH_NCELLS = 8,            // cells at least one button repaints
    ARTH_COLS = 9,
    ARTH_ROWS = 10,
    ARTH_TEXT_X = 11,           // where the ROM may print a status line; attr 0 there
    ARTH_TEXT_Y = 12,
    ARTH_MAXK = 13,             // buttons allowed to touch one cell; the ROM refuses > ART_MAX_K
    // u16 little-endian from here, all offsets from the blob base.
    ARTH_OFF_TILES = 14,
    ARTH_OFF_PAL = 16,          // npals * 8 bytes, CGB 5-bit
    ARTH_OFF_MAP = 18,          // cols * rows, the released frame
    ARTH_OFF_ATTR = 20,         // cols * rows, palette | 0x08 (tile VRAM bank 1)
    ARTH_OFF_CELL_X = 22,
    ARTH_OFF_CELL_Y = 24,
    ARTH_OFF_CELL_NB = 26,      // how many buttons repaint this cell
    ARTH_OFF_CELL_BIDX = 28,    // where its button list starts in cell_btn
    ARTH_OFF_CELL_BTN = 30,
    ARTH_OFF_CELL_OFF = 32,     // u16 per cell: where its 1 << nb variants start in cell_var
    ARTH_OFF_CELL_VAR = 34,
    // An art button is the PAD_* bit that lights it, read from pad.h by the generator.
    ARTH_OFF_BTN_BIT = 36,      // nbtn bytes, each a PAD_* bit number
    ARTH_OFF_BTN_OFF = 38,      // nbtn + 1 bytes: button b's span of btn_cell is [b] to [b + 1]
    ARTH_OFF_BTN_CELL = 40,     // cell indices, for repainting just what a press moved
    ARTH_TOTAL_LEN = 42,        // u16
};

#endif
