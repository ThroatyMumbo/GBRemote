// menu.h — the vertical-list widget.
//
// Split deliberately: menu_draw() serves a static menu_item_t[] table, but navigation works on
// nothing but a count. A data-driven list (scr_ctrl's catalog) reuses menu_nav/menu_clamp/
// menu_cursor with its own row painter, instead of pretending the catalog is a menu_item_t[].
#ifndef MENU_H
#define MENU_H

#include <stdint.h>
#include "ui.h"

typedef struct {
    const char     *label;
    const screen_t *target;     // pushed on A
    uint8_t         enabled;    // 0 -> drawn dim, A does nothing
} menu_item_t;

typedef struct {
    const menu_item_t *items;   // NULL for a data-driven list
    uint8_t n, sel, top, rows, y0;
} menu_t;

void    menu_clamp (menu_t *m);                 // keeps sel inside n and scrolls top to follow it
uint8_t menu_nav   (menu_t *m, uint8_t hit);    // 1 if sel moved
void    menu_draw  (const menu_t *m);           // table-driven rows, items != NULL only
void    menu_row   (const menu_t *m, uint8_t i, const char *label, uint8_t dim, char mark);

#endif
