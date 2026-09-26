// theme.h — the controller ROM's look over canvas.c: title bar, framed lists, message line and the
// cable footer. ROM-only; ui.c and menu.c stay console-based for the demos that share them.
#ifndef THEME_H
#define THEME_H

#include <stdint.h>
#include "canvas.h"
#include "menu.h"
#include "ui_tiles.h"

#define TH_PAL_TEXT 0
#define TH_PAL_DIM  1
#define TH_PAL_FOOT 2
#define TH_PAL_LIVE 3
#define TH_PAL_HEAD 4
#define TH_PAL_SEL  5
#define TH_PAL_FBAD 6
#define TH_PAL_BAD  7

#define TH_MSG_Y    14
#define TH_FOOT_Y   16

#define TH_LABEL_X  3           // list text, right of the cursor
#define TH_MARK_X   17

void th_init(void);             // boot: font, frame and chrome tiles
void th_open(void);             // from enter(): LCD off, fresh canvas, palettes; th_show() ends it
void th_show(void);             // flush and LCD on

void th_begin(const char *title);   // start a frame: blank canvas, centred title bar
void th_end(void);                  // footer, then flush

void th_row(uint8_t y, uint8_t sel, uint8_t dim, const char *label, uint8_t mark);
void th_menu(const menu_t *m);      // a menu_item_t[] list; the caller frames it
void th_msg(const char *s, uint8_t bad);
void th_center(uint8_t y, const char *s);
void th_hex(uint8_t x, uint8_t y, uint8_t v);

// Call from every update(): marks the screen dirty when the link changes, and steps the cable's
// bead while linked.
void th_tick(void);

// For a RUN screen, which has no dispatcher draw: the footer, composed but not flushed. bead 0
// leaves it still. th_foot_moved() is 1 when what the footer shows is out of date.
void    th_footer(uint8_t bead);
uint8_t th_foot_moved(void);

#endif
