#include <gb/gb.h>
#include <gb/cgb.h>
#include <gb/hardware.h>
#include <gbdk/console.h>
#include <stdio.h>
#include "ui.h"
#include "input.h"

#define BGMAP ((volatile uint8_t *)0x9800)

static const screen_t *stk[UI_STACK_MAX];
static const screen_t *pend[UI_STACK_MAX];
static uint8_t depth, pend_push_n, pend_pop_n, dirty;

// CGB mode installs no palette and ignores BGP_REG; without this it is white on white.
static const palette_color_t k_textpal[4] = {
    RGB(31, 31, 31), RGB(21, 21, 21), RGB(10, 10, 10), RGB(0, 0, 0)
};

// LCD off for the 360 attribute stores: this is a full screen of VRAM, far past one VBlank, and a
// store that lands in mode 3 is dropped with nothing to say so.
void ui_surface_reset(void)
{
    uint8_t x, y;

    DISPLAY_OFF;
    VBK_REG = 1;
    for (y = 0; y < UI_ROWS; y++)
        for (x = 0; x < UI_COLS; x++) BGMAP[((uint16_t)y << 5) + x] = 0;
    VBK_REG = 0;
    BGP_REG = 0xE4;
    if (_cpu == CGB_TYPE)
        for (x = 0; x < 8; x++) set_bkg_palette(x, 1, k_textpal);
    DISPLAY_ON;
}

// Deferred, so a screen can never be destroyed inside its own callback.
void ui_push(const screen_t *s) { if (pend_push_n < UI_STACK_MAX) pend[pend_push_n++] = s; }
void ui_pop(void)               { pend_pop_n++; }
void ui_dirty(void)             { dirty = 1; }

void ui_clear_row(uint8_t y)
{
    uint8_t x;
    // setchar() never advances the cursor, so every column needs its own gotoxy().
    for (x = 0; x < UI_COLS; x++) { gotoxy(x, y); setchar(' '); }
}

// Before every callback and every read through a descriptor's pointers. A bank-0 screen gets
// bank 1, the firmware's window, which is what scr_emu and art.c read. Only on a change: every
// $2000 write also lands in write_capture's 8-deep FIFO, and a config_save() erase stalls its drain.
static void map(const screen_t *s)
{
    uint8_t b = (uint8_t)(uint16_t)s->bank;
    if (!b) b = 1;
    if (_current_bank != b) SWITCH_ROM(b);
}

static void activate(const screen_t *s)
{
    map(s);
    cls();
    if (s->title) { gotoxy(0, 0); printf("%s", s->title); }
    if (s->enter) s->enter();
    in_reset();     // the A that got us here must not be seen again by the screen below
    dirty = 1;
}

static void settle(void)
{
    while (pend_pop_n && depth > 1) {
        pend_pop_n--;
        map(stk[depth - 1]);
        if (stk[depth - 1]->leave) stk[depth - 1]->leave();
        depth--;
        activate(stk[depth - 1]);
    }
    pend_pop_n = 0;

    while (pend_push_n) {
        const screen_t *s = pend[0];
        uint8_t i;
        for (i = 1; i < pend_push_n; i++) pend[i - 1] = pend[i];
        pend_push_n--;
        if (depth >= UI_STACK_MAX) continue;    // refuse silently rather than corrupt the stack
        map(stk[depth - 1]);
        if (stk[depth - 1]->leave) stk[depth - 1]->leave();
        stk[depth++] = s;
        activate(s);
    }
}

void ui_run(const screen_t *root)
{
    stk[0] = root;
    depth  = 1;
    activate(root);

    for (;;) {
        const screen_t *s = stk[depth - 1];

        map(s);
        if (s->run) {
            s->run();
        } else {
            uint8_t keys, hit;
            vsync();
            // Draw first: an LY gate after update() starved any screen whose update ran past LY 152.
            if (dirty) {
                if (s->draw) s->draw();
                dirty = 0;
            }
            hit = in_poll(&keys);
            if (s->update) s->update(keys, hit);
        }
        settle();
    }
}
