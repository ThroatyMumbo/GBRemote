#include <gb/gb.h>
#include <gb/cgb.h>
#include <gb/hardware.h>
#include <string.h>
#include "screens.h"
#include "mbox.h"
#include "input.h"
#include "art.h"
#include "theme.h"

#define EXIT_COMBO (J_A | J_B | J_SELECT | J_START)

#define VMU_TILE_BASE 160           // 0xA0-0xFF: clear of the font and of theme.c's tiles at 0x80+
#define VMU_VRAM      (0x8000 + VMU_TILE_BASE * 16)
#define VMU_ORG_X     4             // 96x64 px, centred across the 20 columns
#define VMU_ORG_Y     3
#define VMU_HALF      (VMU_TILES / 2)

#define BGMAP ((volatile uint8_t *)0x9800)

static uint8_t shown_st, shown_vst, shown_seq, vmu_on, map_row, half;
static uint8_t art_on, shown_art;
static uint8_t live[3], seen[3], shown[ART_MAX_BTN], paint_b, paint_j, paint_on;

// The span's stores, worked out outside VBlank. The longest span any pad has is 8 (the SNES's B);
// a longer one is prepared and drained in chunks.
#define PREP_MAX 16
static uint16_t prep_at[PREP_MAX];
static uint8_t  prep_t[PREP_MAX], prep_n, prep_j;
static void art_prep(void);

#define ART_SPLIT   128         // see art_load(): the $9000/$8800 signed tile base

// The console font out of VRAM bank 0, written where the blob's attribute plane left palette 0 and
// bank 0. printf() would do, but it blocks until VBlank — ~14 ms, a fifth of the polls this loop
// exists to make.
// LY per store, like art_step(): a store past the end of VBlank is dropped with nothing to say so,
// and nothing else ever repaints this row, so a torn line stands forever. 0 means the line is half
// written and the caller must not mark it shown — the retry starts at LY 144 with the whole of
// VBlank, because the pass that failed leaves LY below 144 and the gate above holds the next one.
static uint8_t art_text(const char *s)
{
    uint16_t at = ((uint16_t)art_text_y() << 5) + art_text_x();
    uint8_t x = art_text_x(), i;

    VBK_REG = 0;
    for (i = 0; s[i] && x + i < UI_COLS; i++) {
        if (LY_REG < 144) return 0;
        BGMAP[at + i] = (uint8_t)(s[i] - 0x20);
    }
    return 1;
}

// LCD off throughout: this writes VRAM directly.
//
// set_bkg_data(), NOT a GDMA. A general-purpose DMA moves two bytes per M-cycle, which is
// double-speed timing at the cart edge, and the tile block is the only thing here read at
// anything but CPU speed — the map, the attributes and the palettes are ordinary loads. Tiles
// arriving wrong while the layout looks right is exactly what that produces on a console, and
// nothing in the emulator can see it: the default serve model has no timing at all, which is also
// why the VMU blit's identical GDMA has never been shown to work on real hardware.
//
// The split at tile 128 stays: mode()'s text console leaves the BG tile base at $8800, so tiles
// 0-127 are at $9000 and 128-255 at $8800, and one call spanning the boundary would run off
// $97FF into the map. A count of 0 would mean 256 to set_bkg_data(); both halves are under 128.
static void art_load(void)
{
    uint16_t n = art_ntiles();
    const uint8_t *src = (const uint8_t *)art_tiles_addr();
    uint8_t i;

    // Served bank straight into VRAM bank 1. The console never touches a pixel of this, the same
    // way it never touches a pixel of the VMU screen at $4000.
    VBK_REG = 1;
    set_bkg_data(0, (uint8_t)(n > ART_SPLIT ? ART_SPLIT : n), src);
    if (n > ART_SPLIT)
        set_bkg_data(ART_SPLIT, (uint8_t)(n - ART_SPLIT), src + ART_SPLIT * 16);
    set_bkg_tiles(0, 0, ART_COLS, ART_ROWS, (const uint8_t *)art_attr());
    VBK_REG = 0;

    set_bkg_palette(0, art_npals(), (const palette_color_t *)art_pal());
    set_bkg_tiles(0, 0, ART_COLS, ART_ROWS, (const uint8_t *)art_map());

    live[0] = live[1] = live[2] = 0;
    seen[0] = seen[1] = seen[2] = 0;
    memset(shown, 0, sizeof shown);
    paint_b = paint_j = paint_on = 0;
    for (i = 0; i < art_ncells(); i++)
        BGMAP[((uint16_t)art_cell_y(i) << 5) + art_cell_x(i)] = art_cell_tile(i, live);
}

// Three separate pokes on the cart side, so a press that moves bits in two of them can be caught
// half applied — the N64's C buttons straddle $001C and $001D. The sequence is written last,
// which is what makes one retry enough.
static void art_take(void)
{
    uint8_t s;

    do {
        s = PAD_LIVE_SEQ;
        live[0] = PAD_LIVE0;
        live[1] = PAD_LIVE1;
        live[2] = PAD_LIVE2;
    } while (s != PAD_LIVE_SEQ);
}

// Which button the screen owes a repaint. shown[] is what the SCREEN shows, per button — not what
// the cart last reported, which is the difference that makes a lost write recoverable.
//
// Run only when the live state moves or a button finishes, never per pass: fourteen calls is about
// five scanlines, and this loop gets two or three passes inside a VBlank, not thousands. Running
// it every pass pushed LY past the end of VBlank before the write, and nothing was drawn at all.
static void art_scan(void)
{
    uint8_t b;

    for (b = 0; b < art_nbtn(); b++)
        if ((art_btn_down(b, live) ? 1 : 0) != shown[b]) {
            // Always from the start of the span, never resuming: if live moved while a span was
            // half painted, its earlier cells hold the older state, and marking the button shown
            // would freeze that difference in place.
            paint_b = b;
            paint_j = 0;
            paint_on = 1;
            // 0xff is "mid-change", and it is what makes a press shorter than its own repaint
            // safe. Without it a button pressed and released before its span finished never
            // recorded the press, so shown[] said nothing had changed and the cells it had
            // already painted kept the pressed tile for good. 3141 of 4000 modelled press/release
            // pairs did exactly that (firmware/test/ art_incremental).
            shown[b] = 0xff;
            art_prep();
            return;
        }
    paint_on = 0;
}

// The lookups for the cells from paint_j on. One cell's worth of them is more than VBlank has left
// after a store, so doing them inside VBlank painted one cell a frame — a press visibly wiping on.
static void art_prep(void)
{
    uint8_t i = (uint8_t)(art_btn_first(paint_b) + paint_j), last = art_btn_last(paint_b), c;

    for (prep_n = 0; i < last && prep_n < PREP_MAX; i++, prep_n++) {
        c = art_btn_cell(i);
        prep_at[prep_n] = ((uint16_t)art_cell_y(c) << 5) + art_cell_x(c);
        prep_t[prep_n]  = art_cell_tile(c, live);
    }
    prep_j = 0;
}

// Only stores, inside VBlank. A cell carries 1 << nb tiles, one per combination of just the buttons
// that touch it, which is what lets neighbouring C buttons and the d-pad's hub share cells and
// still show a press apart; art_prep() picked each one against live, and a live change re-runs
// art_scan() and so re-prepares from the start of the span.
//
// LY is read right before every store. A write that lands outside VBlank is dropped with nothing
// to say so, and its cell then stays stale FOREVER, because nothing repaints a button already
// marked shown — measured as three of the A button's four cells updating and the fourth keeping
// the pressed tile for good. Marking the button shown only once its whole span is on screen is
// the other half of that.
static void art_step(void)
{
    while (prep_j < prep_n) {
        if (LY_REG < 144) return;       // out of VBlank; the next pass resumes here
        BGMAP[prep_at[prep_j]] = prep_t[prep_j];
        prep_j++;
        paint_j++;
    }
    if ((uint8_t)(art_btn_first(paint_b) + paint_j) < art_btn_last(paint_b)) {
        art_prep();                     // a span longer than PREP_MAX: the next chunk
        return;
    }
    shown[paint_b] = art_btn_down(paint_b, live) ? 1 : 0;
    paint_j = 0;
    art_scan();
}

// The text and VMU shapes: composed whole, then only the changed cells written. The VMU's own
// cells stay blank in the canvas, so a flush never touches what vmu_map_row() put there.
// What the card owes flash is the only way to know a save landed before pulling the cart.
static void emu_paint(uint8_t vst)
{
    th_begin(g_emu_name);
    if (vmu_on) {
        cv_box(VMU_ORG_X - 1, VMU_ORG_Y - 1, VMU_ORG_X + VMU_COLS, VMU_ORG_Y + VMU_ROWS);
        th_center(VMU_ORG_Y + VMU_ROWS + 1, (vst & VMU_ST_SAVING) ? "Saving..."
                                          : (vst & VMU_ST_DIRTY)  ? "Not saved yet"
                                                                  : "Memory card saved");
    } else {
        cv_box(0, 4, CV_COLS - 1, 8);
        th_center(6, "Gamepad active");
    }
    th_center(14, "Hold A+B+SEL+START");
    th_center(15, "to go back");
    cv_attr_row(0, 14, CV_COLS, TH_PAL_DIM);
    cv_attr_row(0, 15, CV_COLS, TH_PAL_DIM);
    th_footer(0);
}

// One general-purpose DMA, served ROM -> VRAM. Half a frame per call: 96 blocks is ~770 of
// VBlank's ~1140 single-speed cycles, and a GDMA that runs past VBlank writes into mode 3.
static void vmu_blit(uint8_t h)
{
    uint16_t src = VMU_FB_ADDR + (uint16_t)h * VMU_HALF * 16;
    uint16_t dst = VMU_VRAM + (uint16_t)h * VMU_HALF * 16;

    // Mounts the frame the cart last published (vmu_fb.c); not on half 1, or a frame can tear.
    if (h == 0) SWITCH_ROM(1);
    VBK_REG   = 0;
    HDMA1_REG = (uint8_t)(src >> 8);
    HDMA2_REG = (uint8_t)(src & 0xF0);
    HDMA3_REG = (uint8_t)((dst >> 8) & 0x1F);
    HDMA4_REG = (uint8_t)(dst & 0xF0);
    HDMA5_REG = (uint8_t)(VMU_HALF - 1);
}

// Row by row inside the VBlank gate rather than in enter(): set_bkg_tiles() does not wait for
// VRAM, and enter() runs wherever the dispatcher happens to be on the screen.
static void vmu_map_row(uint8_t y)
{
    uint8_t row[VMU_COLS], x;

    for (x = 0; x < VMU_COLS; x++) row[x] = (uint8_t)(VMU_TILE_BASE + y * VMU_COLS + x);
    set_bkg_tiles(VMU_ORG_X, (uint8_t)(VMU_ORG_Y + y), VMU_COLS, 1, row);
}

// Three shapes of this screen, all real: the controller's own picture when the cart has staged
// one, the Visual Memory for a Dreamcast that has not, and the text status line for a DMG or a
// driver with no art yet. Chosen on entry and re-chosen only when ART_SEQ moves.
static void emu_pick(void)
{
    // Whose picture it is, then whether it is whole, then whether it is well formed. ART_SEQ 0 is
    // a firmware too old to stage one at all; a mismatched proto is one staged for another driver.
    art_on = (_cpu == CGB_TYPE) && ART_SEQ != 0 && ART_PROTO != 0
             && ART_PROTO == ST_ACTIVE && art_parse();
    shown_art = ART_SEQ;
    if (!art_on) return;

    DISPLAY_OFF;
    art_load();
    DISPLAY_ON;
    // The blob reserves one short span on its own text row — eight columns on the N64, which is
    // the status and nothing else. Which controller it is, the picture already says.
    shown_st = (uint8_t)~ST_STATUS;
}

static void emu_enter(void)
{
    th_open();
    emu_pick();
    if (art_on) {
        // A Visual Memory shares the high bank but not the screen: art wins where both exist.
        // The canvas stays blank, so nothing of the theme is ever flushed over the picture.
        vmu_on = 0;
        return;
    }

    // A Visual Memory is the Dreamcast driver's, and the blit is a CGB general-purpose DMA.
    vmu_on = (_cpu == CGB_TYPE) && ST_ACTIVE == PROTO_DREAMCAST;
    map_row = 0;
    half = 0;
    shown_vst = ST_VMU_STAT;
    shown_seq = ST_VMU_SEQ;
    emu_paint(shown_vst);
    th_show();
}

// A RUN screen: owns the CPU, must never call vsync(). Interrupts are off for the whole loop, so
// vsync() would spin on a flag nothing sets.
static void emu_run(void)
{
    hold_t  hold;
    uint8_t pad, st, vst, seq, ly;

    MBOX_MODE = MODE_CAPTURE;
    MBOX_BTN  = 0;
    disable_interrupts();
    hold_reset(&hold);

    for (;;) {
        pad = joypad();
        MBOX_BTN = pad;                 // every pass; one bus cycle, cheaper than comparing

        st  = ST_STATUS;                // re-read; never cached across an iteration
        vst = ST_VMU_STAT;
        seq = ST_VMU_SEQ;
        if (st & ST_FORCE_CFG) break;   // the firmware's escape hatch

        if ((pad & EXIT_COMBO) == EXIT_COMBO) { if (hold_tick(&hold)) break; }
        else                                    hold_reset(&hold);

        // Outside the VBlank gate: this touches no VRAM, only the mailbox.
        if (art_on) {
            art_take();
            if (live[0] != seen[0] || live[1] != seen[1] || live[2] != seen[2]) {
                seen[0] = live[0];
                seen[1] = live[1];
                seen[2] = live[2];
                art_scan();
            }
        }

        // The LCD stays on, but VRAM is touched only inside VBlank and only on a change, so the
        // common case costs one compare. The PPU steals no CPU cycles, so the poll rate stands.
        // The screen takes priority: the two text lines change rarely, a dropped frame does not.
        ly = LY_REG;
        if (ly >= 144) {
            if (art_on) {
                // A re-stage means the cart changed which driver it is showing. hold_reset()
                // because art_load()'s DISPLAY_OFF stalls far past DIV's 15.6 ms wrap, and
                // hold_tick()'s 8-bit delta would alias.
                if (ART_SEQ != shown_art) { emu_pick(); hold_reset(&hold); }
                else if (paint_on)        { art_step(); }
                else if (st != shown_st)  { if (art_text((st & ST_LINK_UP) ? "LINKED  "
                                                                          : "WAITING "))
                                                shown_st = st; }
            } else if (!vmu_on) {
                if (th_foot_moved()) { emu_paint(vst); cv_flush(); }
            } else if (map_row < VMU_ROWS) {
                vmu_map_row(map_row++);
            } else if (seq != shown_seq) {
                // 48 blocks is 384 cycles, three and a half lines, and a GDMA that runs past
                // VBlank writes into mode 3 and loses whatever it was copying. Start it early in
                // VBlank or not at all; the loop spins, so the next pass catches the window.
                if (ly <= 148) {
                    vmu_blit(half);
                    if (++half == 2) { half = 0; shown_seq = seq; }
                }
            } else if (th_foot_moved() || vst != shown_vst) {
                emu_paint(vst);
                cv_flush();
                shown_vst = vst;
            }
        }
    }

    // The combo is forwarded normally while held, so a short accidental press behaves like any
    // other input. Only on leaving do we send an explicit all-released state — without it the
    // target is left holding four buttons forever.
    MBOX_BTN  = 0;
    MBOX_MODE = MODE_CONFIG;
    enable_interrupts();

    // No wait for release: activate()'s in_reset() keeps the still-held combo from reading as edges.
    ui_pop();
}

// The screen underneath re-enters through th_open(), which puts back the attributes, palettes and
// tiles the picture replaced.
static void emu_leave(void)
{
    art_on = 0;
}

screen_t scr_emu = {
    0, emu_enter, emu_leave, 0, 0, emu_run
};
