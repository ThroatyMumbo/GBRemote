#include <gb/gb.h>
#include <gb/cgb.h>
#include <gbdk/font.h>
#include <string.h>
#include "theme.h"
#include "ui.h"
#include "mbox.h"
#include "catalog.h"

#define FOOT_ROWS   2
#define CABLE_X     2
#define CABLE_N     8           // cols 2-9, the bead's track; the plug and port follow
#define PLUG_X      (CABLE_X + CABLE_N)
#define ADAPT_CAPTURE 1         // firmware/cable_id.h

static const palette_color_t k_th[8 * 4] = {
    RGB(31, 31, 31), RGB(21, 21, 21), RGB(10, 10, 10), RGB(3, 5, 12),      // TEXT
    RGB(31, 31, 31), RGB(24, 24, 25), RGB(18, 19, 22), RGB(18, 19, 22),    // DIM
    RGB(3, 6, 17),   RGB(23, 25, 29), RGB(12, 14, 20), RGB(31, 31, 31),    // FOOT
    RGB(3, 6, 17),   RGB(14, 31, 12), RGB(3, 20, 8),   RGB(24, 31, 22),    // LIVE
    RGB(3, 6, 17),   RGB(8, 13, 24),  RGB(20, 24, 31), RGB(31, 31, 31),    // HEAD
    RGB(25, 28, 31), RGB(19, 23, 29), RGB(10, 12, 16), RGB(3, 5, 12),      // SEL
    RGB(3, 6, 17),   RGB(23, 25, 29), RGB(12, 14, 20), RGB(31, 14, 12),    // FBAD
    RGB(25, 28, 31), RGB(19, 23, 29), RGB(10, 12, 16), RGB(22, 0, 0),      // BAD
};

static uint8_t f_st, f_ad, f_act, f_step, f_ct, f_bead;
static uint8_t f_attr[FOOT_ROWS][CV_COLS];     // what the footer rows' attributes hold on screen

void th_init(void)
{
    font_init();
    font_set(font_load(font_ibm));      // tiles 0-95; the frame and ours sit at 0x80+
    cv_init();
    set_bkg_data(UI_T_BASE, UI_T_COUNT, k_ui_tiles);
}

void th_open(void)
{
    DISPLAY_OFF;
    cv_reset();
    if (_cpu == CGB_TYPE) set_bkg_palette(0, 8, k_th);
    memset(f_attr, TH_PAL_TEXT, sizeof f_attr);     // cv_reset() just zeroed the plane
    cv_attr(0, 0, CV_COLS, 1, TH_PAL_HEAD);
}

void th_show(void)
{
    cv_flush();
    DISPLAY_ON;
}

void th_center(uint8_t y, const char *s)
{
    cv_text((uint8_t)((CV_COLS - strlen(s)) >> 1), y, s);
}

void th_begin(const char *title)
{
    cv_clear();
    if (title) th_center(0, title);
}

void th_end(void)
{
    th_footer(1);
    cv_flush();
}

void th_row(uint8_t y, uint8_t sel, uint8_t dim, const char *label, uint8_t mark)
{
    if (sel) cv_put(1, y, UI_T_CURSOR);
    cv_text(TH_LABEL_X, y, label);
    if (mark) cv_put(TH_MARK_X, y, mark);
    cv_attr_row(1, y, CV_COLS - 2, sel ? TH_PAL_SEL : dim ? TH_PAL_DIM : TH_PAL_TEXT);
}

void th_menu(const menu_t *m)
{
    uint8_t i, end = (uint8_t)(m->top + m->rows);

    if (end > m->n) end = m->n;
    for (i = m->top; i < end; i++)
        th_row((uint8_t)(m->y0 + i - m->top), i == m->sel, (uint8_t)!m->items[i].enabled,
               m->items[i].label, 0);
}

void th_msg(const char *s, uint8_t bad)
{
    if (s) th_center(TH_MSG_Y, s);
    cv_attr_row(0, TH_MSG_Y, CV_COLS, !s ? TH_PAL_TEXT : bad ? TH_PAL_BAD : TH_PAL_SEL);
}

void th_hex(uint8_t x, uint8_t y, uint8_t v)
{
    uint8_t h = v >> 4, l = v & 15;
    cv_put(x, y, (uint8_t)(h < 10 ? '0' - 0x20 + h : 'A' - 0x20 - 10 + h));
    cv_put((uint8_t)(x + 1), y, (uint8_t)(l < 10 ? '0' - 0x20 + l : 'A' - 0x20 - 10 + l));
}

static uint8_t act_name(uint8_t act, char *out)
{
    uint8_t i, n = cat_count();
    for (i = 0; i < n; i++) if (cat_proto(i) == act) { cat_name(i, out); return 1; }
    return 0;
}

// Right-aligned against the last column, in `pal` over the footer ground.
static void foot_text(uint8_t y, const char *s, uint8_t pal, uint8_t *attr)
{
    uint8_t x = (uint8_t)(CV_COLS - strlen(s));
    cv_text(x, y, s);
    memset(attr + x, pal, CV_COLS - x);
}

void th_footer(uint8_t bead)
{
    uint8_t st = ST_STATUS, ad = ST_ADAPTER, act = ST_ACTIVE;     // fresh, never the shown copies
    uint8_t link = st & ST_LINK_UP, i, y;
    uint8_t attr[FOOT_ROWS][CV_COLS];
    char name[CAT_NAME_MAX];
    const char *who = 0;

    memset(attr, TH_PAL_FOOT, sizeof attr);
    f_bead = 0;
    cv_put(0, TH_FOOT_Y, UI_T_CART_TL);
    cv_put(1, TH_FOOT_Y, UI_T_CART_TR);
    cv_put(0, TH_FOOT_Y + 1, UI_T_CART_BL);
    cv_put(1, TH_FOOT_Y + 1, UI_T_CART_BR);

    if (!(st & ST_ALIVE)) {
        foot_text(TH_FOOT_Y + 1, "Cart not answering", TH_PAL_FBAD, attr[1]);
    } else if (ad == ADAPT_NONE && act == PROTO_NONE) {
        // The cable hangs loose off the cart: nothing on the other end.
        cv_put(CABLE_X, TH_FOOT_Y, UI_T_CABLE);
        cv_put(CABLE_X + 1, TH_FOOT_Y, UI_T_CABLE);
        cv_put(CABLE_X + 2, TH_FOOT_Y, UI_T_BEND_DN);
        cv_put(CABLE_X + 2, TH_FOOT_Y + 1, UI_T_BEND_RT);
        cv_put(CABLE_X + 3, TH_FOOT_Y + 1, UI_T_PLUG);
        cv_put(CABLE_X + 4, TH_FOOT_Y + 1, UI_T_TIP);
        foot_text(TH_FOOT_Y, "NO CABLE", TH_PAL_FOOT, attr[0]);
    } else {
        // A pinless driver runs with no cable at all, so there is none to draw.
        if (ad != ADAPT_NONE) {
            for (i = 0; i < CABLE_N; i++)
                cv_put((uint8_t)(CABLE_X + i), TH_FOOT_Y,
                       link && bead && i == f_step ? UI_T_BEAD : UI_T_CABLE);
            cv_put(PLUG_X, TH_FOOT_Y, UI_T_PLUG);
            cv_put(PLUG_X + 1, TH_FOOT_Y, UI_T_PORT);
            if (link) memset(attr[0] + CABLE_X, TH_PAL_LIVE, CABLE_N + 2);
            f_bead = link && bead;
        }
        if (act != PROTO_NONE) {
            foot_text(TH_FOOT_Y, link ? "LINKED" : "WAITING", link ? TH_PAL_LIVE : TH_PAL_FOOT, attr[0]);
            if (act_name(act, name)) who = name;
        } else {
            who = ad == ADAPT_UNKNOWN ? "Unknown cable" : ad == ADAPT_CAPTURE ? "Capture cable"
                                                                             : "Controller off";
        }
        if (who) foot_text(TH_FOOT_Y + 1, who, TH_PAL_FOOT, attr[1]);
    }

    for (y = 0; y < FOOT_ROWS; y++)
        if (memcmp(attr[y], f_attr[y], CV_COLS)) {
            cv_attr_map(0, (uint8_t)(TH_FOOT_Y + y), CV_COLS, 1, attr[y]);
            memcpy(f_attr[y], attr[y], CV_COLS);
        }

    f_st  = st & (ST_ALIVE | ST_LINK_UP);
    f_ad  = ad;
    f_act = act;
}

uint8_t th_foot_moved(void)
{
    return (ST_STATUS & (ST_ALIVE | ST_LINK_UP)) != f_st || ST_ADAPTER != f_ad || ST_ACTIVE != f_act;
}

// The bead moves by two cells and one row flush, never a redraw: a whole-screen compose costs
// a couple of frames, and one every 8 swallowed a 4-frame press in e2e-map.
void th_tick(void)
{
    if (th_foot_moved()) { ui_dirty(); return; }
    if (!f_bead || (++f_ct & 7)) return;
    cv_put((uint8_t)(CABLE_X + f_step), TH_FOOT_Y, UI_T_CABLE);
    if (++f_step == CABLE_N) f_step = 0;
    cv_put((uint8_t)(CABLE_X + f_step), TH_FOOT_Y, UI_T_BEAD);
    cv_flush_rows(TH_FOOT_Y, 1);
}
