#pragma bank 255
#include <gb/gb.h>
#include "screens.h"
#include "menu.h"
#include "catalog.h"
#include "mbox.h"
#include "theme.h"

char    g_emu_name[16];
uint8_t g_emu_proto;

#define ROW_OFF 0                   // row 0 is the synthesised "Off"; catalog entry i is row i+1
#define APPLY_TIMEOUT 90            // frames, 1.5 s — config_save() erases a sector before it acks
#define BOX_Y     2
#define LIST_ROWS 10                // rows 3-12 inside the frame; the message line is row 14

static menu_t   m;
static uint8_t  shown_act;
static uint8_t  waiting, wait_ct, wait_seq, want_proto, msg_bad;
static const char *msg;

static void ctrl_draw(void);

static uint8_t row_proto(uint8_t i) { return i == ROW_OFF ? PROTO_NONE : cat_proto((uint8_t)(i - 1)); }
static uint8_t row_flags(uint8_t i) { return i == ROW_OFF ? CATF_SUPPORTED : cat_flags((uint8_t)(i - 1)); }

static void row_name(uint8_t i, char *out)
{
    if (i == ROW_OFF) { out[0] = 'O'; out[1] = 'f'; out[2] = 'f'; out[3] = 0; return; }
    cat_name((uint8_t)(i - 1), out);
}

static void open_pad(void)
{
    row_name(m.sel, g_emu_name);
    g_emu_proto = want_proto;
    ui_push(&scr_pad);
}

static void ctrl_enter(void)
{
    m.items = 0;                    // data-driven: our own row painter, shared navigation
    m.n     = (uint8_t)(cat_count() + 1);
    m.rows  = LIST_ROWS;
    m.y0    = BOX_Y + 1;

    // Land on whatever is resident so the list opens where the user left it.
    {
        uint8_t i, act = ST_ACTIVE;
        m.sel = ROW_OFF;
        for (i = 0; i < m.n; i++) if (row_proto(i) == act) { m.sel = i; break; }
    }
    menu_clamp(&m);

    waiting = 0;
    msg     = 0;

    th_open();
    ctrl_draw();
    th_show();
}

static void say(const char *s, uint8_t bad)
{
    msg = s;
    msg_bad = bad;
}

// The status block is re-read every pass; m.n is not, because $0300 is seeded once at boot.
// The shown_* copies exist only to decide whether to repaint and are never used as the value.
static void ctrl_update(uint8_t keys, uint8_t hit)
{
    uint8_t act = ST_ACTIVE;

    keys;
    th_tick();

    if (waiting) {
        // Wait for the firmware to actually act, not for a guessed delay. ST_APPLYSEQ ticks once
        // per drv_apply(), so this ends as soon as core1 has been round the loop. Whether it
        // started is the controller menu's to say: mapping and the pak work without a cable.
        uint8_t done = ST_APPLYSEQ != wait_seq;
        if (done || --wait_ct == 0) {
            waiting = 0;
            if (want_proto != PROTO_NONE) {
                msg = 0;
                open_pad();
            } else {
                if (!done)                  say("No answer from cart", 1);
                else if (act == PROTO_NONE) say("Controller off", 0);
                else                        say("Couldn't switch", 1);
            }
            ui_dirty();
        }
        return;                     // no navigation while a switch is in flight
    }

    if (menu_nav(&m, hit)) { msg = 0; ui_dirty(); }
    if (act != shown_act) ui_dirty();

    if (hit & J_B) { ui_pop(); return; }

    if (hit & J_A) {
        if (!(row_flags(m.sel) & CATF_SUPPORTED)) {
            say("Not available", 1);        // no PIO left beside the serve path
            ui_dirty();
            return;
        }
        want_proto = row_proto(m.sel);
        // Already what the cart runs: no teardown, and no flash erase to say so again.
        if (want_proto != PROTO_NONE && want_proto == act && mbox_cfg(CFG_PROTO_SEL) == act) {
            open_pad();
            return;
        }
        // Snapshot the sequence BEFORE writing. DRV_SETTLE_MS makes the other order safe in
        // practice, but capturing it after leaves a window where the bump we are waiting for has
        // already happened and we would sit out the whole timeout.
        wait_seq = ST_APPLYSEQ;
        mbox_set_cfg(CFG_PROTO_SEL, want_proto);
        mbox_commit();              // a cart should remember what it is
        wait_ct  = APPLY_TIMEOUT;
        waiting  = 1;
        say("Switching...", 0);
        ui_dirty();
    }
}

static void ctrl_draw(void)
{
    uint8_t act = ST_ACTIVE;
    uint8_t i, end;
    char name[CAT_NAME_MAX];

    th_begin("CONTROLLERS");
    if (!cat_ok()) {
        th_msg("Firmware too old", 1);
    } else {
        end = (uint8_t)(m.top + m.rows);
        if (end > m.n) end = m.n;
        cv_box(0, BOX_Y, CV_COLS - 1, (uint8_t)(BOX_Y + end - m.top + 1));
        for (i = m.top; i < end; i++) {
            row_name(i, name);
            // The dot is what core1 actually owns, not what the config asked for.
            th_row((uint8_t)(m.y0 + i - m.top), i == m.sel,
                   (uint8_t)!(row_flags(i) & CATF_SUPPORTED), name,
                   row_proto(i) == act ? UI_T_DOT : 0);
        }
        th_msg(msg, msg_bad);
    }
    th_end();
    shown_act = act;
}

BANKREF(scr_ctrl)
BANKREF_EXTERN(scr_ctrl)
screen_t scr_ctrl = {
    0, ctrl_enter, 0, ctrl_update, ctrl_draw, 0, UI_BANK(scr_ctrl)
};
