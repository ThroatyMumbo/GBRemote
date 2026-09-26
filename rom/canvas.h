// canvas.h — a whole-screen tile map built from state each draw(), then written out as the spans
// that actually changed. A full repaint through gotoxy()+setchar() is over 100 ms, long enough to
// swallow a press. Shared with demos/stadium; bank 0, because cv_put runs hundreds of times a draw.
//
// Text goes through the GBDK console font (tile = ASCII - 0x20); bytes >= 0x80 are tiles of the
// caller's own, loaded after cv_init()'s box frame.
#ifndef CANVAS_H
#define CANVAS_H

#include <stdint.h>
#include "cv_tiles.h"

#define CV_COLS 20
#define CV_ROWS 18

// 0-3 keep colour 3 black, so a cell left on one still reads as plain text. 4-7 are the menu
// chrome and move colour 3, so a screen that paints them must own the attribute plane.
#define CV_PAL_TEXT   0
#define CV_PAL_GREEN  1
#define CV_PAL_YELLOW 2
#define CV_PAL_RED    3
#define CV_PAL_HEAD   4         // title bar: white text on blue
#define CV_PAL_PANEL  5         // info panel: black text on a pale tint
#define CV_PAL_OK     6         // panel tint, green text
#define CV_PAL_BAD    7         // panel tint, red text

void cv_init(void);                     // box tiles and palettes; after the font, and again after
                                        // any screen that reused tiles 0x80+ (scr_emu's VMU)
void cv_pal_load(void);                 // put the palettes back, for a screen that installed its own
void cv_reset(void);                    // the screen has just been cls()'d; clears the attributes
void cv_clear(void);                    // start building a frame
void cv_map(const uint8_t *map);        // a whole-screen background, in place of cv_clear()
void cv_flush(void);                    // write the cells that changed
void cv_flush_rows(uint8_t y, uint8_t n);   // the same, rows y..y+n-1 only

void cv_put (uint8_t x, uint8_t y, uint8_t tile);
void cv_text(uint8_t x, uint8_t y, const char *s);
void cv_num (uint8_t x, uint8_t y, uint16_t v, uint8_t width);   // right-aligned, space-padded
void cv_box (uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1);    // frame, interior blanked
void cv_attr(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t pal);   // CGB only, no-op on DMG
void cv_attr_map(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *a);  // a per cell

// One attribute span per row, from inside a draw: skipped when the row already carries it, since a
// VRAM-safe write is ~0.15 ms a byte. cv_reset() forgets them all.
void cv_attr_row(uint8_t x, uint8_t y, uint8_t w, uint8_t pal);

#endif
