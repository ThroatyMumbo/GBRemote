// screens.h — the screen registry.
//
// A new feature is one new scr_*.c, one line here, and one row in scr_root.c's table (or in
// scr_pad.c's k_extras[] for a feature of one controller). Nothing in
// ui.c, menu.c, input.c, mbox.c or catalog.c ever learns that a screen exists.
#ifndef SCREENS_H
#define SCREENS_H

#include "ui.h"

// Non-const: each is in WRAM so ui.c can read it before mapping its bank (ui.h UI_BANK). A new
// screen is `#pragma bank 255` unless it reads $4000-$7FFF, and tools/mkromh.py enforces that.
extern screen_t scr_root;
extern screen_t scr_ctrl;
extern screen_t scr_pad;
extern screen_t scr_map;
extern screen_t scr_emu;
extern screen_t scr_settings;
extern screen_t scr_diag;
extern screen_t scr_roms;

// scr_ctrl hands the chosen row to the controller's menu and everything under it.
extern char    g_emu_name[16];
extern uint8_t g_emu_proto;

#endif
