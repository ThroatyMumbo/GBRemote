#pragma bank 255
#include <gb/gb.h>
#include <gb/cgb.h>
#include "screens.h"
#include "slots.h"
#include "mbox.h"
#include "canvas.h"
#include "gamedb.h"
#include "cart_gfx.h"
#include "cart_gfx.c"               // the one consumer: included, so the art lands in this bank

#define ENT_OFF 0                   // entry 0 is "No pak"; slot entry i is entry i+1
#define APPLY_TIMEOUT 90            // frames, 1.5 s — config_save() erases a sector before it acks

#define TITLE    "TRANSFER PAK"
#define TITLE_X  ((CV_COLS - (sizeof TITLE - 1)) / 2)
#define MAX_ENT  9                  // eight slots plus No pak
#define GRID_COLS 5
#define CELL_H   (CART_H + 1)       // the cart, plus the row the selected one rises into
#define SHELF_Y  2
#define SHELF_H  12
#define PANE_Y   14                 // four rows: frame, name, status, frame
#define PANE_X   1
#define NPAL     6                  // BG palettes 2-7; 0 is the chrome and 1 the title bar

static uint8_t n, sel, box_y, box_h;
static uint8_t shown_cur, shown_act;
static uint8_t waiting, wait_ct, wait_seq, want_slot;
static uint8_t cx[MAX_ENT], cy[MAX_ENT], pal[MAX_ENT];
static const char *msg;

static uint8_t ent_slot(uint8_t e) { return e == ENT_OFF ? 0 : slot_index((uint8_t)(e - 1)); }

// The selected slot is the config mirror, not the slot page: the page is the static directory.
static uint8_t sel_slot(void)
{
    if (mbox_cfg(CFG_DRV_OWNER) != PROTO_N64) return 0;
    return mbox_cfg((uint8_t)(CFG_DRV + CFG_TPAK_SLOT));
}

// CFG_PROTO_SEL, not ST_ACTIVE: the firmware stamps CFG_DRV_OWNER from the SELECTED protocol, so
// that is what decides whether this byte is honoured at all. ST_ACTIVE only says it is running.
static uint8_t for_n64(void) { return mbox_cfg(CFG_PROTO_SEL) == PROTO_N64; }

// The shelf: rows of five, each row centred on its own count, the block centred in SHELF_H.
static void layout(void)
{
    uint8_t rows = (uint8_t)((n + GRID_COLS - 1) / GRID_COLS);
    uint8_t r, i, e = 0, cnt, x, gy, pitch;

    box_h = (uint8_t)(rows * CELL_H + 2);       // the grid, plus a row of padding either side
    box_y = (uint8_t)(SHELF_Y + (SHELF_H - box_h) / 2);
    gy = (uint8_t)(box_y + 1);

    for (r = 0; r < rows; r++) {
        cnt = (uint8_t)(n - r * GRID_COLS);
        if (cnt > GRID_COLS) cnt = GRID_COLS;
        // A gutter wherever there is room for one: only a full row of five has to abut to fit.
        pitch = (uint8_t)(cnt < GRID_COLS ? CART_W + 1 : CART_W);
        x = (uint8_t)((CV_COLS - (cnt * pitch - (pitch - CART_W))) / 2);
        for (i = 0; i < cnt; i++, e++) {
            cx[e] = (uint8_t)(x + i * pitch);
            cy[e] = (uint8_t)(gy + r * CELL_H);
        }
    }
}

// A palette per cart, so a hue two games both want still leaves them different colours. Past NPAL
// the last two entries of a full store reuse the first two; nothing else can be done with eight.
static void palettes(void)
{
    char name[SLOT_NAME_MAX];
    uint8_t use[NPAL], nuse = 0, e, h, t, j;

    pal[ENT_OFF] = 0;
    for (e = 1; e < n; e++) {
        if (nuse >= NPAL) { pal[e] = (uint8_t)(2 + (e - 1 - NPAL)); continue; }
        slot_name((uint8_t)(e - 1), name);
        h = game_hue(name);
        for (t = 0; t < CART_NHUE; t++) {
            for (j = 0; j < nuse && use[j] != h; j++) ;
            if (j == nuse) break;
            if (++h >= CART_NHUE) h = 0;
        }
        use[nuse] = h;
        pal[e] = (uint8_t)(2 + nuse);
        set_bkg_palette((uint8_t)(2 + nuse), 1, cart_pal + (uint16_t)h * 4);
        nuse++;
    }
}

static void put_cart(uint8_t e, uint8_t cur)
{
    const uint8_t *map = e == ENT_OFF ? cart_map_none : cart_map;
    uint8_t x = cx[e], y = (uint8_t)(cy[e] + (e == sel ? 0 : 1)), i, j;

    for (j = 0; j < CART_H; j++)
        for (i = 0; i < CART_W; i++)
            cv_put((uint8_t)(x + i), (uint8_t)(y + j), map[j * CART_W + i]);
    // The slashed cart has no room for the badge: the slash crosses the very tile it would replace.
    if (e != ENT_OFF && ent_slot(e) == cur)
        cv_put((uint8_t)(x + CART_BADGE_X), (uint8_t)(y + CART_BADGE_Y), cart_badge);
}

// Repeated subtraction, as cv_num() does it: SDCC's 16-bit divide is ~4 ms a call.
static char *app_u(char *p, uint16_t v)
{
    char d[5];
    uint8_t k = 0;

    do {
        uint16_t q = 0;
        while (v >= 10u) { v -= 10u; q++; }
        d[k++] = (char)('0' + v);
        v = q;
    } while (v && k < 5);
    while (k) *p++ = d[--k];
    return p;
}

static char *app_s(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    return p;
}

// "MBC3 2M save+rtc", "MBC5 512K save 32K": everything the old text list said, in one pane row.
static const char *detail(uint8_t k)
{
    static char buf[CV_COLS];
    uint8_t f = slot_flags(k);
    uint16_t banks = slot_banks(k);
    char *p = app_s(buf, slot_mbc_name(slot_mbc(k)));

    *p++ = ' ';
    if (banks >= 64) { p = app_u(p, (uint16_t)(banks >> 6)); *p++ = 'M'; }
    else             { p = app_u(p, (uint16_t)(banks << 4)); *p++ = 'K'; }
    if (f & SLOTF_SAVE) {
        if (f & SLOTF_RTC)        p = app_s(p, " save+rtc");
        else if (slot_ram_kb(k)) { p = app_s(p, " save "); p = app_u(p, slot_ram_kb(k)); *p++ = 'K'; }
        else                      p = app_s(p, " save 512B");
    } else if (f & SLOTF_RTC) {
        p = app_s(p, " rtc");
    }
    *p = 0;
    return buf;
}

static void draw_pane(uint8_t act)
{
    char name[SLOT_NAME_MAX];
    const char *st = msg;

    cv_box(0, PANE_Y, CV_COLS - 1, CV_ROWS - 1);
    if (!slot_ok()) {
        cv_text(PANE_X, PANE_Y + 1, "No game list:");
        cv_text(PANE_X, PANE_Y + 2, "Firmware too old");
        return;
    }
    // The pak only exists inside drv_n64, and nothing else can say so: a slot can be selected and
    // committed while a SNES pad is resident, and it will simply do nothing.
    if (!st) st = !for_n64() ? "Needs N64 selected" : act != PROTO_N64 ? "N64 not running" : 0;
    if (sel == ENT_OFF) {
        cv_text(PANE_X, PANE_Y + 1, "No pak");
        if (!st && n == 1) st = "No games stored";
    } else {
        slot_name((uint8_t)(sel - 1), name);
        cv_text(PANE_X, PANE_Y + 1, name);
        if (!st) st = detail((uint8_t)(sel - 1));
    }
    if (st) cv_text(PANE_X, PANE_Y + 2, st);
}

static void paint(void)
{
    uint8_t e, cur = sel_slot(), act = ST_ACTIVE;

    cv_clear();
    cv_text(TITLE_X, 0, TITLE);
    cv_box(0, box_y, CV_COLS - 1, (uint8_t)(box_y + box_h - 1));    // blanks its interior: before the carts
    for (e = 0; e < n; e++) put_cart(e, cur);
    draw_pane(act);
    shown_cur = cur;
    shown_act = act;
}

static void roms_enter(void)
{
    uint8_t e, cur = sel_slot();

    n = (uint8_t)(slot_count() + 1);
    if (n > MAX_ENT) n = MAX_ENT;
    sel = ENT_OFF;
    for (e = 1; e < n; e++) if (ent_slot(e) == cur) { sel = e; break; }
    waiting = 0;
    msg = 0;
    layout();

    // Behind DISPLAY_OFF: palette RAM is closed during mode 3, so a set_bkg_palette() that lands
    // there is simply dropped.
    DISPLAY_OFF;
    cv_reset();
    set_bkg_data(CART_BASE, CART_NTILES, cart_tiles);
    set_bkg_palette(0, 1, cart_chrome);
    set_bkg_palette(1, 1, cart_head);
    palettes();
    cv_attr(0, 0, CV_COLS, 1, 1);
    for (e = 0; e < n; e++) cv_attr(cx[e], cy[e], CART_W, CELL_H, pal[e]);
    paint();
    cv_flush();
    DISPLAY_ON;
}

// Left and right walk the whole shelf; up and down swap rows in the same column, which is the
// whole of it while nine entries is the ceiling.
static uint8_t nav(uint8_t hit)
{
    uint8_t s = sel;

    if (hit & J_LEFT)  s = s ? (uint8_t)(s - 1) : (uint8_t)(n - 1);
    if (hit & J_RIGHT) s = (uint8_t)(s + 1 < n ? s + 1 : 0);
    if (hit & J_UP)    s = (uint8_t)(s >= GRID_COLS ? s - GRID_COLS
                                     : (s + GRID_COLS < n ? s + GRID_COLS : s));
    if (hit & J_DOWN)  s = (uint8_t)(s + GRID_COLS < n ? s + GRID_COLS
                                     : (s >= GRID_COLS ? s - GRID_COLS : s));
    if (s == sel) return 0;
    sel = s;
    return 1;
}

static void roms_update(uint8_t keys, uint8_t hit)
{
    keys;
    if (waiting) {
        // Wait for core1 to act, not for a guessed delay. A slot change is a CFG_DRV write, which
        // drv_apply() takes through reconfig() without a teardown, so ST_ACTIVE never moves —
        // ST_APPLYSEQ is the only thing that reports it happened.
        if (ST_APPLYSEQ != wait_seq) {
            waiting = 0;
            // The N64 check first: without it the mismatch below is all the user ever sees, and
            // "not accepted" does not tell them the pak lives inside one particular driver.
            if (!for_n64())                   msg = "Select N64 first";
            else if (sel_slot() != want_slot) msg = "Couldn't insert";
            else if (want_slot == 0)          msg = "Pak removed";
            else if (ST_ACTIVE != PROTO_N64)  msg = "Saved, N64 idle";
            else                              msg = "Inserted";
            ui_dirty();
        } else if (--wait_ct == 0) {
            waiting = 0;
            msg = "No answer from cart";
            ui_dirty();
        }
        return;
    }

    if (nav(hit)) { msg = 0; ui_dirty(); }
    if (sel_slot() != shown_cur || ST_ACTIVE != shown_act) ui_dirty();

    if (hit & J_B) { ui_pop(); return; }

    if (hit & J_A) {
        want_slot = ent_slot(sel);
        // Snapshot before writing, for the reason scr_ctrl.c gives: capturing it after leaves a
        // window where the bump we are waiting for has already happened.
        wait_seq = ST_APPLYSEQ;
        mbox_set_cfg((uint8_t)(CFG_DRV + CFG_TPAK_SLOT), want_slot);
        mbox_commit();              // a cart should remember which game is in the pak
        wait_ct = APPLY_TIMEOUT;
        waiting = 1;
        msg     = "Inserting...";
        ui_dirty();
    }
}

static void roms_draw(void)
{
    paint();
    cv_flush();
}

BANKREF(scr_roms)
BANKREF_EXTERN(scr_roms)
screen_t scr_roms = {
    0, roms_enter, 0, roms_update, roms_draw, 0, UI_BANK(scr_roms)
};
