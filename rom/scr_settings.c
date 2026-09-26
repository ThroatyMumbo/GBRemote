#pragma bank 255
#include <gb/gb.h>
#include <gbdk/console.h>
#include <stdio.h>
#include "screens.h"
#include "menu.h"

// Real rows, all disabled. Each is a config block the firmware already honours and that nothing
// yet writes: the CFG_DRV block $0281+ and the exit combo. Building the dim/refuse path on day
// one is the point — it is exercised before a feature depends on it.
static const menu_item_t k_items[] = {
    { "Driver options", 0, 0 },
    { "Exit combo",     0, 0 },
};

static menu_t m;

static void set_enter(void)
{
    m.items = k_items;
    m.n     = sizeof k_items / sizeof k_items[0];
    m.rows  = m.n;
    m.y0    = 3;
    menu_clamp(&m);

    gotoxy(1, 9);
    printf("not yet built");
}

static void set_update(uint8_t keys, uint8_t hit)
{
    keys;
    if (menu_nav(&m, hit)) ui_dirty();
    if (hit & J_B) ui_pop();
}

static void set_draw(void) { menu_draw(&m); }

BANKREF(scr_settings)
BANKREF_EXTERN(scr_settings)
screen_t scr_settings = {
    "SETTINGS", set_enter, 0, set_update, set_draw, 0, UI_BANK(scr_settings)
};
