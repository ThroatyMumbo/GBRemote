#pragma bank 255
#include <gb/gb.h>
#include "screens.h"
#include "menu.h"
#include "catalog.h"
#include "mapinfo.h"
#include "mbox.h"
#include "theme.h"

#define BOX_Y 3

typedef struct { uint8_t proto; menu_item_t item; } extra_t;

// Screens that exist for one controller only. Gamepad and Button mapping are every driver's.
static const extra_t k_extras[] = {
    { PROTO_N64, { "Transfer Pak", &scr_roms, 1 } },
};

static menu_item_t items[2 + sizeof k_extras / sizeof k_extras[0]];
static menu_t m;
static uint8_t shown_st, shown_act, shown_ad;

static void pad_draw(void);

static uint8_t pinless(void)
{
    uint8_t i, n = cat_count();
    for (i = 0; i < n; i++) if (cat_proto(i) == g_emu_proto) return cat_flags(i) & CATF_PINLESS;
    return 0;
}

// Why this controller is not the one core1 owns; 0 when it is.
static const char *why_not(void)
{
    uint8_t act = ST_ACTIVE;

    if (!(ST_STATUS & ST_ALIVE)) return "Cart not answering";
    if (act == g_emu_proto) return 0;
    // drv_apply() returns PAD_OK having started nothing with no cable, so only ST_ACTIVE shows it.
    if (ST_ADAPTER == ADAPT_NONE && !pinless()) return "Plug in the cable";
    if (act != PROTO_NONE || mbox_cfg(CFG_PROTO_SEL) != g_emu_proto) return "Needs its own cable";
    if (ST_DIAG != DIAG_OK) return "Couldn't start";
    return "Not running";
}

static void pad_enter(void)
{
    uint8_t i;

    items[0].label = "Gamepad";
    items[0].target = &scr_emu;
    items[0].enabled = 1;
    items[1].label = "Button mapping";
    items[1].target = &scr_map;
    items[1].enabled = mp_find(g_emu_proto) && mp_map_off();
    m.n = 2;
    for (i = 0; i < sizeof k_extras / sizeof k_extras[0]; i++)
        if (k_extras[i].proto == g_emu_proto) items[m.n++] = k_extras[i].item;

    m.items = items;
    m.rows  = m.n;
    m.y0    = BOX_Y + 1;
    menu_clamp(&m);

    th_open();
    pad_draw();
    th_show();
}

static void pad_update(uint8_t keys, uint8_t hit)
{
    uint8_t st = ST_STATUS, act = ST_ACTIVE, ad = ST_ADAPTER;
    const menu_item_t *it = &items[m.sel];

    keys;
    th_tick();
    if (menu_nav(&m, hit)) ui_dirty();
    if (st != shown_st || act != shown_act || ad != shown_ad) ui_dirty();

    if (hit & J_B) { ui_pop(); return; }
    if (!(hit & J_A) || !it->enabled) return;

    if (it->target == &scr_emu && why_not()) return;    // the message line already says why
    ui_push(it->target);
}

static void pad_draw(void)
{
    shown_st  = ST_STATUS;              // before why_not() reads them: a change after is caught
    shown_act = ST_ACTIVE;
    shown_ad  = ST_ADAPTER;

    th_begin(g_emu_name);
    cv_box(0, BOX_Y, CV_COLS - 1, (uint8_t)(BOX_Y + m.n + 1));
    th_menu(&m);
    th_msg(why_not(), 1);
    th_end();
}

BANKREF(scr_pad)
BANKREF_EXTERN(scr_pad)
screen_t scr_pad = {
    0, pad_enter, 0, pad_update, pad_draw, 0, UI_BANK(scr_pad)
};
