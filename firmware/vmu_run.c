#include "vmu_run.h"
#include "vmu_stage.h"
#include "drivers/maple_proto.h"
#include "vmu_core.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>

_Static_assert(VMU_CARD_BYTES == VMU_FLASH_SIZE, "the card IS the LC8670's flash");
_Static_assert(VMU_LCD_BYTES == MAPLE_VMU_LCD_BYTES, "one frame format, two sources");

// Core0's share per pass, under the ~12 us that starts costing mailbox writes. Time past the budget
// is dropped, so a slow game runs slow rather than eating the bus.
#define EMU_BUDGET_US  8u
#define EMU_CHUNK_US   8u

// A flash erase or a status print is a gap the game must not try to replay.
#define EMU_LAG_CAP_US 20000u

static vmu_t    g_vmu;
static uint8_t  g_fb[VMU_LCD_BYTES];
static bool     g_fb_ready, g_active, g_have_game;
static uint32_t g_last_us, g_seen_writes, g_frames, g_dropped, g_busy_max, g_calls;

// GBDK: RIGHT 0x01 LEFT 0x02 UP 0x04 DOWN 0x08 A 0x10 B 0x20 SELECT 0x40 START 0x80.
// P3:   UP 0x01 DOWN 0x02 LEFT 0x04 RIGHT 0x08 A 0x10 B 0x20 MODE 0x40 SLEEP 0x80.
static inline uint8_t j_to_p3(uint8_t j) {
    return (uint8_t)(((j & 0x04) >> 2) | ((j & 0x08) >> 2) | ((j & 0x02) << 1)
                   | ((j & 0x01) << 3) | (j & 0xf0));
}

// The game reads the clock — Chao Adventure grows its Chao on it — so it gets the card's own
// date, the one the console set, not the core's build date.
static void take_date(uint32_t now_ms) {
    uint32_t secs = vmu_clock_now(now_ms);
    uint16_t y;
    uint8_t mo;
    uint8_t d;
    uint8_t h;
    uint8_t mi;
    uint8_t s;

    if (!secs) { return; }
    maple_date_from_secs(secs, &y, &mo, &d, &h, &mi, &s);
    vmu_set_datetime(&g_vmu, y, mo, d, h, mi, s);
}

void vmu_run_init(void) {
    uint32_t base;
    uint32_t end;

    vmu_init(&g_vmu, vmu_card(), NULL);
    g_have_game   = vmu_game_range(&g_vmu, &base, &end);
    g_seen_writes = g_vmu.flash_writes;
    printf("vmu: %s\n", g_have_game ? "card carries a GAME" : "no GAME on the card");
}

bool vmu_run_active(void)    { return g_active; }

void vmu_run_reset(uint32_t now_ms) {
    uint32_t base;
    uint32_t end;

    vmu_reset(&g_vmu);
    take_date(now_ms);
    g_have_game   = vmu_game_range(&g_vmu, &base, &end);
    g_seen_writes = g_vmu.flash_writes;
    g_fb_ready    = false;
}

void vmu_run_gate(bool console_quiet, uint32_t now_ms) {
    bool want = console_quiet && g_have_game;

    if (want == g_active) { return; }
    g_active = want;
    if (!want) { return; }
    vmu_run_reset(now_ms);
    g_last_us = time_us_32();
    printf("vmu: running the card's game\n");
}

void vmu_run_poll(uint32_t now_ms, uint8_t gb_buttons) {
    uint32_t t0;
    uint32_t owed;
    uint32_t busy;
    uint32_t step;

    if (!g_active) { return; }

    t0   = time_us_32();
    owed = t0 - g_last_us;
    g_last_us = t0;
    if (owed > EMU_LAG_CAP_US) { g_dropped += owed - EMU_LAG_CAP_US; owed = EMU_LAG_CAP_US; }
    if (!owed) { return; }
    g_calls++;

    // At least one emulated cycle per step: on the 32 kHz quartz /6 a cycle is 183 us, and 8 us
    // steps would spend the whole budget executing nothing.
    step = vmu_tcyc_ns(&g_vmu) / 1000U;
    if (step < EMU_CHUNK_US) { step = EMU_CHUNK_US; }

    vmu_set_buttons(&g_vmu, j_to_p3(gb_buttons));
    while (owed) {
        uint32_t chunk = owed < step ? owed : step;
        vmu_run_us(&g_vmu, chunk);
        owed -= chunk;
        if ((time_us_32() - t0) >= EMU_BUDGET_US) {
            g_dropped += owed;
            break;
        }
    }

    busy = (time_us_32() - t0);
    if (busy > g_busy_max) { g_busy_max = busy; }

    if (vmu_lcd_frame(&g_vmu, g_fb)) { g_fb_ready = true; g_frames++; }

    // The game's saves ride the card's writeback; core1 is not writing the card while this runs,
    // so core0 is the single writer here.
    if (g_vmu.flash_writes != g_seen_writes) {
        g_seen_writes = g_vmu.flash_writes;
        vmu_mark(vmu_stage(), now_ms);
    }
}

bool vmu_run_take_frame(uint8_t out[MAPLE_VMU_LCD_BYTES]) {
    if (!g_fb_ready) { return false; }
    memcpy(out, g_fb, sizeof g_fb);
    g_fb_ready = false;
    return true;
}

void vmu_run_report(void) {
    uint8_t icons = vmu_lcd_icons(&g_vmu);

    printf("  emu: pc=%04x instr=%llu frames=%lu calls=%lu busy_max=%luus dropped=%luus "
           "icons=%c%c%c%c ocr=%02x wr=%lu%s\n",
           g_vmu.pc, (unsigned long long)g_vmu.instructions, (unsigned long)g_frames,
           (unsigned long)g_calls, (unsigned long)g_busy_max, (unsigned long)g_dropped,
           (icons & VMU_LCD_ICON_FILE)  ? 'F' : '.', (icons & VMU_LCD_ICON_GAME)  ? 'G' : '.',
           (icons & VMU_LCD_ICON_CLOCK) ? 'C' : '.', (icons & VMU_LCD_ICON_FLASH) ? 'W' : '.',
           g_vmu.sfr[0x0e], (unsigned long)g_vmu.flash_writes,
           g_vmu.unknown_ops ? " UNKNOWN-OPS" : "");
    g_busy_max = g_dropped = g_calls = 0;
}
