// ui.h — the screen abstraction.
//
// Two shapes of screen exist and both are real:
//   - a FRAME screen supplies update()/draw() and the dispatcher owns the loop: one vsync, one
//     joypad read, one update, one draw. Every menu is this shape.
//   - a RUN screen supplies run() and owns the CPU until it returns. Emulation is this shape
//     because its whole job is a free-running poll that must not be paced at 60 Hz. run() is not
//     an escape hatch for emulation; it is the second half of the abstraction.
//
// A RUN screen must never call vsync(). Emulation runs with interrupts off, and vsync() spins on
// a flag only the VBlank ISR sets — with IME clear it hangs forever.
//
// enter() runs EVERY time the screen becomes top of stack, including on return from a child, and
// leave() every time it stops being top. So enter() is always a full repaint and no screen has to
// save a backdrop.
//
// draw() may be SKIPPED if update() overran VBlank, so it must repaint from state and never react
// to an event. ui_dirty() is sticky until a draw actually happens.
#ifndef UI_H
#define UI_H

#include <stdint.h>

typedef struct screen {
    const char *title;                              // drawn by the chrome; NULL = no chrome
    void (*enter) (void);
    void (*leave) (void);
    void (*update)(uint8_t keys, uint8_t hit);      // FRAME: state only, never touches VRAM
    void (*draw)  (void);                           // FRAME: VRAM only, called inside VBlank
    void (*run)   (void);                           // RUN: owns the CPU; must ui_pop() itself
    const void *bank;                               // UI_BANK(x) for a banked file; 0 = bank 0
} screen_t;

// A banked screen: BANKREF(scr_x) + BANKREF_EXTERN(scr_x), UI_BANK(scr_x) as the last field, and
// a non-const descriptor, so it sits in WRAM where ui.c can read it before mapping the bank.
// The symbol's ADDRESS is the bank number, which makes it a constant initialiser; BANK() is not.
#define UI_BANK(x) ((const void *)&__bank_ ## x)

#define UI_STACK_MAX 5
#define UI_COLS      20
#define UI_ROWS      18

void ui_run  (const screen_t *root);    // never returns
void ui_push (const screen_t *s);       // only from update() or run(); applied after it returns
void ui_pop  (void);
void ui_dirty(void);                    // request a draw() this frame or the next one

// Blank a whole row. printf leaves whatever was longer behind it, so anything that shrinks needs
// this first.
void ui_clear_row(uint8_t y);

// The console's surface: attribute plane cleared, BG palettes installed. cls() writes neither, so
// a screen that touched either hands it back through this — and main() opens with the same call,
// so the boot state and the restored state cannot drift apart.
void ui_surface_reset(void);

#endif
