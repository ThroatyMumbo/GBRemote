#include "vmu_fb.h"
#include "vmu_lcd.h"
#include "vmu_stage.h"
#include "vmu_run.h"
#include "cartserve.h"
#include "cart_mbc.h"
#include "pico/time.h"
#include <string.h>

#define FB_STRIPS VMU_TILE_ROWS                 // one tile row converted and stored per pass
#define FB_TEST_MS 100u

// The second high bank. cs_bank(1) is the first, so a frame is written into whichever of the two
// is not currently mounted.
static uint8_t __attribute__((aligned(0x4000))) g_shadow[0x4000];

static uint16_t g_dst[VMU_TILE_BYTES];          // tile byte -> offset in a served bank
static uint8_t  g_tiles[VMU_TILE_ROW_BYTES];
static uint8_t  g_frame[MAPLE_VMU_LCD_BYTES], g_prepared[MAPLE_VMU_LCD_BYTES];
static uint8_t  g_seq, g_strip, g_on_shadow;
static uint32_t g_taken_seq, g_test_seq, g_test_ms;
static bool     g_busy, g_test, g_upright;

void vmu_fb_init(void) {
    memcpy(g_shadow, cs_bank(1), sizeof g_shadow);
    for (uint32_t i = 0; i < VMU_TILE_BYTES; i++) {
        g_dst[i] = cs_permute14((uint16_t)((VMU_FB_ADDR + i) & 0x3fffU));
    }
}

// Not in the bank table. It can still be live until the ROM re-selects bank 1, so check first.
static uint8_t *back_bank(void) { return g_on_shadow ? cs_bank(1) : g_shadow; }
static bool back_is_live(void) { return cs_rom_base()[1] == (uint32_t)back_bank(); }

uint8_t *vmu_fb_bank(unsigned i) { return i ? g_shadow : cs_bank(1); }

// The ROM's next bank-1 select mounts the frame, and it selects only after seeing $0016 move.
static uint8_t mount(void) {
    cs_mbc_remap(1, back_bank());
    g_on_shadow ^= 1U;
    if (++g_seq == 0) {
        g_seq = 1; // 0 is "nothing to publish" to the caller
    }
    return g_seq;
}

uint8_t vmu_fb_poll(void) {
    if (back_is_live()) { return 0; }
    if (!g_busy) {
        uint32_t seq;
        if (g_test) {
            uint32_t now = to_ms_since_boot(get_absolute_time());
            if ((now - g_test_ms) < FB_TEST_MS) { return 0; }
            g_test_ms = now;
            vmu_lcd_test_frame(g_frame, ++g_test_seq);      // its own counter, so leaving test
            g_upright = false;                              // mode cannot alias a real frame
        } else if (vmu_run_take_frame(g_frame)) {
            g_upright = true;                   // the card's own game drew it; see vmu_lcd.h
        } else {
            // The cheap test first: this runs every run_bus() pass and most of them have no frame.
            if (vmu_lcd_seq() == g_taken_seq) { return 0; }
            if (!vmu_lcd_take(g_frame, &seq) || seq == g_taken_seq) { return 0; }
            g_taken_seq = seq;
            g_upright = false;
        }
        // Byte order and rotation, once. An upright frame is turned by nobody; everything else
        // keeps the build's setting.
        vmu_lcd_prepare_ex(g_frame, g_prepared, g_upright ? false : (bool)VMU_ROTATE_180);
        g_strip = 0;
        g_busy = true;
        return 0;
    }

    // One tile row per pass: converted and stored, ~6 us, well inside the ~12 us of core0 that
    // starts costing mailbox writes.
    uint8_t *bank = back_bank();
    uint32_t at = g_strip * VMU_TILE_ROW_BYTES;

    vmu_lcd_tile_row(g_prepared, g_tiles, g_strip);
    for (uint32_t i = 0; i < VMU_TILE_ROW_BYTES; i++) { bank[g_dst[at + i]] = g_tiles[i]; }

    if (++g_strip < FB_STRIPS) { return 0; }
    g_busy = false;
    return mount();
}

void vmu_fb_test_toggle(void) { g_test = !g_test; g_test_ms = 0; }
bool vmu_fb_test_active(void) { return g_test; }
