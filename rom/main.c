// main.c — the GB-side half of the controller cart.
//
// A menu app. The firmware serves this as a static 32 KB ROM and rewrites bytes of it under us;
// mbox.h names every one of them. The root menu's Controllers section renders the driver catalog
// the firmware publishes at $0300, so a driver added to k_drivers[] appears here with no change
// to this ROM.
//
// SINGLE SPEED ONLY — never call cpu_fast(). The capture bench measured the serve path arriving
// one byte late in double speed at 150, 200 and 250 MHz: the first fetch after the switch executes
// $FF and the console sits in an rst $38 loop forever.
#include <gb/gb.h>
#include <gb/cgb.h>
#include <gbdk/console.h>
#include <stdint.h>
#include "screens.h"
#include "mbox.h"
#include "theme.h"

void main(void)
{
    // M_NO_SCROLL matters: a console scroll is a full-screen VRAM copy that would run outside
    // VBlank. The screens draw through canvas.c; the console is only cls() and the font.
    mode(M_TEXT_OUT | M_NO_SCROLL | M_NO_INTERP);
    th_init();

    MBOX_MODE = MODE_CONFIG;
    MBOX_BTN  = 0;

    ui_run(&scr_root);
}
