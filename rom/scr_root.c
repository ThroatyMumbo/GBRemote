#pragma bank 255
#include <gb/gb.h>
#include "screens.h"
#include "menu.h"
#include "theme.h"

// The whole feature list. Later sections — BLE pairing, IR blaster, capture arming — are rows
// here and nothing else; a feature of one controller lives in its own menu (scr_pad.c).
// Settings returns as a row once it has something to set.
static const menu_item_t k_items[] = {
    { "Controllers",  &scr_ctrl,     1 },
    { "Diagnostics",  &scr_diag,     1 },
};

#define N_ITEMS (sizeof k_items / sizeof k_items[0])
#define BOX_Y   5

static menu_t m;

static void root_draw(void)
{
    th_begin("GBC CONTROLLER");
    cv_box(0, BOX_Y, CV_COLS - 1, (uint8_t)(BOX_Y + N_ITEMS + 1));
    th_menu(&m);
    th_end();
}

static void root_enter(void)
{
    m.items = k_items;
    m.n     = N_ITEMS;
    m.rows  = m.n;
    m.y0    = BOX_Y + 1;
    menu_clamp(&m);

    th_open();
    root_draw();
    th_show();
}

static void root_update(uint8_t keys, uint8_t hit)
{
    keys;
    th_tick();
    if (menu_nav(&m, hit)) ui_dirty();
    if ((hit & J_A) && k_items[m.sel].enabled) ui_push(k_items[m.sel].target);
}

BANKREF(scr_root)
BANKREF_EXTERN(scr_root)
screen_t scr_root = {
    0, root_enter, 0, root_update, root_draw, 0, UI_BANK(scr_root)
};
