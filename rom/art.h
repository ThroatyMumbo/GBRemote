// art.h — reads the controller picture the cart stages at $4600.
//
// Same contract as slots.h and catalog.h: validated on every parse, because a firmware too old to
// stage one leaves the window as $FF filler and a stage still in flight leaves it half written.
// Unlike those two it is re-parsed whenever ART_SEQ moves, since the cart restages it on a driver
// change.
//
// Nothing here is copied. The blob IS the data, sitting in the served high bank, and the caller
// hands set_bkg_tiles()/set_bkg_palette() pointers straight into it — which is the whole reason
// the ROM can show every controller without carrying one.
//
// Pure: no VRAM, no GB headers, so firmware/test/ runs this parser against a blob the packer
// generated. scr_emu.c owns the drawing. Mirrors firmware/padart_fmt.h, which it includes.
#ifndef ART_H
#define ART_H

#include <stdint.h>
#include "padart_fmt.h"

// firmware/test/ points the parser at a host array with -DART_WIN_PTR=<name>. An array name is an
// address constant, so the GB build folds it exactly as before and the override costs nothing.
#ifndef ART_WIN_PTR
#define ART_WIN_PTR ((const volatile uint8_t *)ART_BASE)
#else
extern volatile uint8_t ART_WIN_PTR[ART_WINDOW];        // the test writes it; `win` adds the const
#endif

// 0 if the window holds no blob this ROM can draw. Whether it is the RIGHT blob — ART_PROTO
// against ST_ACTIVE — is the caller's question, not the format's.
uint8_t art_parse(void);

uint8_t  art_npals(void);
uint16_t art_ntiles(void);
uint16_t art_tiles_addr(void);          // GB address of the tile block, 16-byte aligned
uint8_t  art_ncells(void);
uint8_t  art_nbtn(void);
uint8_t  art_text_x(void);
uint8_t  art_text_y(void);

const volatile uint8_t *art_pal(void);
const volatile uint8_t *art_map(void);
const volatile uint8_t *art_attr(void);

uint8_t art_cell_x(uint8_t i);
uint8_t art_cell_y(uint8_t i);

// The tile a cell shows under this live pad state: $001B-$001D as three bytes, pad.h's PAD_* bits.
// A cell carries 1 << nb tiles, one per combination of just the buttons that touch it, which is
// what lets neighbouring C buttons and the d-pad's hub share cells and still show a press apart.
uint8_t art_cell_tile(uint8_t i, const uint8_t *live);

uint8_t art_btn_down(uint8_t b, const uint8_t *live);    // is button b pressed in this state?
uint8_t art_btn_first(uint8_t b);       // button b's span of art_btn_cell(), [first, last)
uint8_t art_btn_last(uint8_t b);
uint8_t art_btn_cell(uint8_t j);

#endif
