#pragma bank 255
#include <gb/gb.h>
#include <string.h>
#include "screens.h"
#include "menu.h"
#include "mapinfo.h"
#include "mbox.h"
#include "theme.h"

#define NBTN  8
#define BOX_Y 3
#define TGT_X 10                // the action, between the selected row's arrows at 9 and 17
#define EDIT_X 18

// GBDK J_* bit order, which is also the map's byte order.
static const char *const k_gb[NBTN] = { "Right", "Left", "Up", "Down", "A", "B", "Select", "Start" };

static menu_t  m;
static uint8_t off, changed;
static uint8_t act[NBTN];       // the edit, not a cache: the mirror lags a write by a run_bus pass

static void map_draw(void);

static uint8_t effective(uint8_t i) { return act[i] == ACT_UNSET ? i : act[i]; }

// Position of button i's action in the driver's list; mp_n() stands for None.
static uint8_t choice(uint8_t i)
{
    uint8_t k = mp_index_of(effective(i));
    return k == 0xff ? mp_n() : k;
}

static void map_enter(void)
{
    uint8_t i;

    mp_find(g_emu_proto);
    off = mp_map_off();
    for (i = 0; i < NBTN; i++) act[i] = mbox_cfg((uint8_t)(off + i));
    changed = 0;

    m.items = 0;
    m.n     = NBTN;
    m.rows  = NBTN;
    m.y0    = BOX_Y + 1;
    menu_clamp(&m);

    th_open();
    map_draw();
    th_show();
}

static void set(uint8_t i, uint8_t a)
{
    if (a == i) a = ACT_UNSET;              // the identity is the default; keep it unset
    act[i] = a;
    mbox_set_cfg((uint8_t)(off + i), a);
    changed = 1;
    ui_dirty();
}

static void map_update(uint8_t keys, uint8_t hit)
{
    uint8_t i = m.sel, n = mp_n(), k;

    keys;
    th_tick();
    if (menu_nav(&m, hit)) ui_dirty();

    if (hit & (J_LEFT | J_RIGHT)) {
        k = choice(i);
        if (hit & J_RIGHT) k = k == n ? 0 : (uint8_t)(k + 1);
        else               k = k == 0 ? n : (uint8_t)(k - 1);
        set(i, k == n ? ACT_NONE : mp_action(k));
    }
    if (hit & J_START) set(i, ACT_UNSET);

    if (hit & J_B) {
        if (changed) mbox_commit();
        ui_pop();
    }
}

static void map_draw(void)
{
    uint8_t i, k, y;
    char label[MP_LABEL_MAX];

    th_begin("BUTTON MAPPING");
    th_center(1, g_emu_name);
    cv_box(0, BOX_Y, CV_COLS - 1, BOX_Y + NBTN + 1);
    for (i = 0; i < NBTN; i++) {
        y = (uint8_t)(m.y0 + i);
        th_row(y, i == m.sel, 0, k_gb[i], 0);
        k = choice(i);
        if (k == mp_n()) strcpy(label, "None");
        else             mp_label(k, label);
        cv_text(TGT_X, y, label);
        if (i == m.sel) {
            cv_put(TGT_X - 1, y, UI_T_ARR_L);
            cv_put((uint8_t)(TGT_X + strlen(label)), y, UI_T_ARR_R);
        }
        if (act[i] != ACT_UNSET) cv_put(EDIT_X, y, UI_T_EDIT);
    }
    th_end();
}

BANKREF(scr_map)
BANKREF_EXTERN(scr_map)
screen_t scr_map = {
    0, map_enter, 0, map_update, map_draw, 0, UI_BANK(scr_map)
};
