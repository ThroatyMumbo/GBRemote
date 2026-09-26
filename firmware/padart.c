#include "padart.h"
#include "padart_fmt.h"
#include "cartserve.h"
#include "vmu_fb.h"
#include "padart_blobs.h"

// Per high bank per pass: the same order as vmu_fb.c's 192 bytes in ~6 us, inside a pass's ~12 us.
#define ART_CHUNK 128

static const uint8_t *g_blob;
static uint16_t g_len, g_cursor;
static uint8_t  g_want = 0xff, g_have, g_seq;
static bool     g_busy;

static const uint8_t *find(uint8_t proto, uint16_t *len) {
    for (unsigned i = 0; i < sizeof k_padart / sizeof k_padart[0]; i++) {
        if (k_padart[i].proto == proto) { *len = k_padart[i].len; return k_padart[i].blob;
        }
    }
    return 0;
}

void padart_init(void) {
    g_want = 0xff;                      // no proto: the first poll stages whatever is resident
    g_have = 0;
}

uint8_t padart_proto(void) { return g_have; }

static uint8_t bump(void) {
    if (++g_seq == 0) {
        g_seq = 1; // 0 is "nothing published" to the ROM
    }
    return g_seq;
}

uint8_t padart_poll(uint8_t active_proto) {
    if (active_proto != g_want) {
        g_want   = active_proto;
        g_blob   = find(active_proto, &g_len);
        g_cursor = 0;
        g_have   = 0;                   // nothing to show until the whole blob is in place
        g_busy   = true;
        if (!g_blob) { g_busy = false; return bump(); }
    }
    if (!g_busy) { return 0; }

    uint16_t n = g_len - g_cursor;
    if (n > ART_CHUNK) { n = ART_CHUNK; }
    // Ungated even on the mounted bank (hw-test/dspeed: zero served-byte errors), and the ROM
    // reads nothing here until the sequence moves.
    for (unsigned i = 0; i < VMU_FB_BANKS; i++) {
        cs_write_span(vmu_fb_bank(i), (uint16_t)(ART_BASE + g_cursor), g_blob + g_cursor, n);
    }

    g_cursor = (uint16_t)(g_cursor + n);
    if (g_cursor < g_len) { return 0; }
    g_busy = false;
    g_have = g_want;
    return bump();
}
