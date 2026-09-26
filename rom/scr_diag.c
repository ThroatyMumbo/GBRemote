#pragma bank 255
#include <gb/gb.h>
#include "screens.h"
#include "mbox.h"
#include "catalog.h"
#include "mapinfo.h"
#include "theme.h"

// Everything the firmware publishes, raw. This is the instrument for bringing up every other
// screen — if the catalog or the mirror is wrong, this is where it shows.
#define CAT_Y  10
#define CAT_SHOWN 3

static uint8_t tick, pad;

static void diag_draw(void);

static void diag_enter(void)
{
    th_open();
    diag_draw();
    th_show();
}

static void diag_update(uint8_t keys, uint8_t hit)
{
    pad = keys;
    th_tick();
    if (hit & J_B) ui_pop();
    // Everything here is live, so it repaints on a timer rather than on a change.
    if ((++tick & 7) == 0) ui_dirty();
}

static void field(uint8_t x, uint8_t y, const char *label, uint8_t v)
{
    cv_text(x, y, label);
    th_hex(x < 10 ? 8 : 16, y, v);
}

static void diag_draw(void)
{
    uint8_t o, i, n;
    char name[CAT_NAME_MAX];

    th_begin("DIAGNOSTICS");
    cv_box(0, 1, CV_COLS - 1, CAT_Y - 1);
    field(1, 2, "Status", ST_STATUS);   field(11, 2, "Ver", ST_VERSION);
    field(1, 3, "Cable",  ST_ADAPTER);  field(11, 3, "Err", ST_DIAG);
    field(1, 4, "Driver", ST_ACTIVE);   field(11, 4, "Seq", ST_APPLYSEQ);
    field(1, 5, "Pad",    pad);         field(11, 5, "Own", mbox_cfg(CFG_DRV_OWNER));
    cv_text(1, 6, "Cfg");
    th_hex(8, 6, mbox_cfg(CFG_VERSION));
    th_hex(11, 6, mbox_cfg(CFG_PROTO_SEL));
    th_hex(14, 6, mbox_cfg(CFG_FLAGS));
    // The selected controller's map; each proto has its own.
    o = mp_find(mbox_cfg(CFG_PROTO_SEL)) ? mp_map_off() : 0;
    cv_text(1, 7, "Map");
    if (!o) cv_text(8, 7, "-");
    else for (i = 0; i < 8; i++) th_hex((uint8_t)(2 + 2 * i), 8, mbox_cfg((uint8_t)(o + i)));

    cv_box(0, CAT_Y, CV_COLS - 1, CAT_Y + CAT_SHOWN + 2);
    if (!cat_ok()) {
        cv_text(1, CAT_Y + 1, "Catalog absent");
    } else {
        n = cat_count();
        cv_text(1, CAT_Y + 1, "Catalog");
        cv_num(9, CAT_Y + 1, n, 2);
        for (i = 0; i < n && i < CAT_SHOWN; i++) {
            o = (uint8_t)(CAT_Y + 2 + i);
            cat_name(i, name);
            th_hex(1, o, cat_proto(i));
            th_hex(4, o, cat_flags(i));
            cv_text(7, o, name);
        }
    }
    th_end();
}

BANKREF(scr_diag)
BANKREF_EXTERN(scr_diag)
screen_t scr_diag = {
    0, diag_enter, 0, diag_update, diag_draw, 0, UI_BANK(scr_diag)
};
