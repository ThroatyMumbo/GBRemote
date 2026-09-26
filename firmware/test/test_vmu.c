// Host tests for the VMU: the card store format, the LCD frame format, and the real-time clock.
#include "test.h"
#include "maple_proto.h"
#include "vmu_store_fmt.h"
#include "vmu_lcd.h"

// The card store's format, without any flash under it. What this pins is that a torn or foreign
// slot is refused rather than served as a card, and that the newer of the two always wins.
static void test_vmu_store_fmt(void) {
    static uint8_t card[VMU_CARD_BYTES];
    vmu_hdr_t a, b;

    for (unsigned i = 0; i < VMU_CARD_BYTES; i++) card[i] = (uint8_t)(i * 5u + 1u);
    uint32_t crc = vmu_crc32(0, card, VMU_CARD_BYTES);

    // The standard CRC-32, so a value can be checked against any other implementation.
    CHECK(vmu_crc32(0, (const uint8_t *)"123456789", 9) == 0xcbf43926u,
          "crc32(\"123456789\") = %08x, want cbf43926", vmu_crc32(0, (const uint8_t *)"123456789", 9));

    card[777] ^= 0x01;
    CHECK(vmu_crc32(0, card, VMU_CARD_BYTES) != crc, "one flipped bit did not move the crc");
    card[777] ^= 0x01;

    a = (vmu_hdr_t){ .magic = VMU_HDR_MAGIC, .seq = 7, .len = VMU_CARD_BYTES, .crc = crc };
    b = a;
    CHECK(vmu_hdr_ok(&a), "a good header was refused");

    vmu_hdr_t bad = a;
    bad.magic = 0;
    CHECK(!vmu_hdr_ok(&bad), "a header with no magic was accepted");
    bad = a;
    bad.len = VMU_CARD_BYTES / 2u;
    CHECK(!vmu_hdr_ok(&bad), "a header for half a card was accepted");

    b.seq = 8;
    CHECK(vmu_hdr_pick(&a, &b) == 1 && vmu_hdr_pick(&b, &a) == 0, "the newer slot did not win");
    b.seq = a.seq - 1u;
    CHECK(vmu_hdr_pick(&a, &b) == 0, "the older slot won");

    // Sequence numbers are compared as a difference, so a wrap must not hand back the old card.
    a.seq = 0xffffffffu;
    b.seq = 0;
    CHECK(vmu_hdr_pick(&a, &b) == 1, "a wrapped sequence picked the older slot");

    b.magic = 0;
    CHECK(vmu_hdr_pick(&a, &b) == 0, "a valid slot lost to an empty one");
    a.magic = 0;
    CHECK(vmu_hdr_pick(&a, &b) == -1, "two empty slots reported a card");
}

// The screen. The frame has to survive the trip from a BLOCK_WRITE to the callback byte for byte,
// and come out of the formatter as doubled dots where the rotation puts them.
static uint8_t  g_lcd_seen[MAPLE_VMU_LCD_BYTES];
static unsigned g_lcd_calls, g_lcd_len;

static void lcd_sink(void *ctx, const uint8_t *frame, unsigned n) {
    (void)ctx;
    g_lcd_calls++;
    g_lcd_len = n;
    if (n <= sizeof g_lcd_seen) memcpy(g_lcd_seen, frame, n);
}

// One doubled dot of the formatted frame: 12 tiles across, 16 bytes a tile, two planes a row.
static bool vmu_px(const uint8_t *tiles, unsigned dx, unsigned dy) {
    unsigned at = (dy >> 3) * VMU_TILE_COLS * 16u + (dy & 7u) * 2u + (dx >> 3) * 16u;
    return (tiles[at] >> (7u - (dx & 7u))) & 1u;
}

static void test_vmu_lcd(void) {
    static uint8_t card[MAPLE_VMU_BLOCKS * MAPLE_VMU_BLOCK];
    static uint8_t tiles[VMU_TILE_BYTES];
    uint8_t req[16 + MAPLE_VMU_LCD_BYTES], out[MAPLE_MAX_FRAME], px[MAPLE_VMU_LCD_BYTES];
    maple_bus_t  b;
    maple_unit_t uc, uv;
    maple_vmu_t  st;
    maple_cond_t c;
    uint32_t w[2];
    unsigned n, rn;

    maple_cond_default(&c);
    maple_bus_init(&b);
    maple_controller_unit(&uc, &c);
    maple_bus_add(&b, &uc);
    maple_vmu_unit(&uv, &st, card, MAPLE_SUB_UNIT(1));
    st.on_lcd = lcd_sink;
    maple_bus_add(&b, &uv);

    for (unsigned i = 0; i < sizeof px; i++) px[i] = (uint8_t)(i + 1u);
    w[0] = MAPLE_FUNC_LCD;
    w[1] = 0;
    rn = maple_make_req_to(req, MAPLE_CMD_BLOCK_WRITE, MAPLE_SUB_UNIT(1), 0, w, 2);
    memcpy(req + rn, px, sizeof px);
    req[3] = 2u + (uint8_t)(sizeof px / 4u);
    n = maple_build_reply(&b, req, rn + sizeof px, out);
    CHECK(n == 5 && maple_cmd(out) == MAPLE_CMD_RESP_ACK, "LCD write: cmd %02x",
          n ? maple_cmd(out) : 0);
    CHECK(g_lcd_calls == 1 && g_lcd_len == MAPLE_VMU_LCD_BYTES,
          "the sink saw %u call(s) of %u bytes", g_lcd_calls, g_lcd_len);
    CHECK(memcmp(g_lcd_seen, px, sizeof px) == 0, "the frame reached the sink altered");

    // Half a frame is worse on screen than a dropped one, so a short write is ACKed and dropped.
    req[3] = 2u + 4u;
    n = maple_build_reply(&b, req, rn + 16u, out);
    CHECK(n == 5 && maple_cmd(out) == MAPLE_CMD_RESP_ACK, "short LCD write: cmd %02x",
          n ? maple_cmd(out) : 0);
    CHECK(g_lcd_calls == 1, "a partial frame reached the sink");

    // Every lit dot becomes a 2x2 block, and only that, so the count is the whole check.
    static const struct { unsigned x, y; } k_dots[] = { { 0, 0 }, { 47, 31 }, { 1, 0 }, { 30, 17 } };
    for (unsigned i = 0; i < sizeof k_dots / sizeof k_dots[0]; i++) {
        unsigned sx = k_dots[i].x, sy = k_dots[i].y, lit = 0;
        memset(px, 0, sizeof px);
        px[sy * VMU_LCD_STRIDE + (sx >> 3)] = (uint8_t)(0x80u >> (sx & 7u));
        vmu_lcd_tiles(px, tiles);

        unsigned rx = VMU_ROTATE_180 ? VMU_LCD_W - 1u - sx : sx;
        unsigned ry = VMU_ROTATE_180 ? VMU_LCD_H - 1u - sy : sy;
        for (unsigned dy = 0; dy < VMU_TILE_ROWS * 8u; dy++)
            for (unsigned dx = 0; dx < VMU_TILE_COLS * 8u; dx++)
                if (vmu_px(tiles, dx, dy)) {
                    lit++;
                    CHECK(dx >> 1 == rx && dy >> 1 == ry,
                          "dot %u,%u lit %u,%u, want the 2x2 block at %u,%u",
                          sx, sy, dx >> 1, dy >> 1, rx, ry);
                }
        CHECK(lit == 4, "dot %u,%u lit %u dots, want 4", sx, sy, lit);

        // Both bit planes carry it, or a lit dot lands on color 1 instead of 3.
        unsigned at = ((ry * 2u) >> 3) * VMU_TILE_COLS * 16u + ((ry * 2u) & 7u) * 2u
                    + ((rx * 2u) >> 3) * 16u;
        CHECK(tiles[at] == tiles[at + 1u], "the two bit planes differ at %u,%u", rx, ry);
    }
}

// The card must survive a driver restart: dc_init() runs on every drv_apply(), and formatting
// there is the one edit that would quietly destroy a save on a protocol change.
static void test_vmu_reinit_keeps_card(void) {
    static uint8_t card[MAPLE_VMU_BLOCKS * MAPLE_VMU_BLOCK];
    maple_unit_t u;
    maple_vmu_t  st;

    maple_vmu_format(card);
    maple_vmu_unit(&u, &st, card, MAPLE_SUB_UNIT(1));
    card[42 * MAPLE_VMU_BLOCK] = 0xa5;

    maple_vmu_unit(&u, &st, card, MAPLE_SUB_UNIT(1));
    CHECK(card[42 * MAPLE_VMU_BLOCK] == 0xa5, "re-attaching the unit cleared the card");
    CHECK(st.clock_secs == MAPLE_VMU_EPOCH && st.on_write == NULL,
          "the unit's own state did not start clean");
}

// A real card's clock ticks and takes the date the console sets at power-up.
static bool vmu_clock_get(const maple_bus_t *b, maple_vmu_t *st, uint32_t ms, uint8_t got[8]) {
    uint8_t req[16], out[MAPLE_MAX_FRAME];
    uint32_t w[2] = { MAPLE_FUNC_CLOCK, 0 };
    st->now_ms = ms;
    unsigned rn = maple_make_req_to(req, MAPLE_CMD_BLOCK_READ, MAPLE_SUB_UNIT(1), 0, w, 2);
    unsigned n = maple_build_reply(b, req, rn, out);
    if (n != 4 + 12 + 1 || maple_cmd(out) != MAPLE_CMD_RESP_DATA_XFER) return false;
    memcpy(got, out + 8, 8);
    return true;
}

static uint8_t vmu_clock_set(const maple_bus_t *b, maple_vmu_t *st, uint32_t ms,
                             const uint8_t *date, unsigned nbytes) {
    uint8_t req[32], out[MAPLE_MAX_FRAME];
    uint32_t w[2] = { MAPLE_FUNC_CLOCK, 0 };
    st->now_ms = ms;
    unsigned rn = maple_make_req_to(req, MAPLE_CMD_BLOCK_WRITE, MAPLE_SUB_UNIT(1), 0, w, 2);
    memcpy(req + rn, date, nbytes);
    req[3] = (uint8_t)(2u + nbytes / 4u);
    unsigned n = maple_build_reply(b, req, rn + nbytes, out);
    return n ? maple_cmd(out) : 0;
}

static void vmu_clock_expect(const uint8_t g[8], uint16_t y, uint8_t mo, uint8_t d,
                             uint8_t h, uint8_t mi, uint8_t s, const char *what) {
    CHECK((uint16_t)(g[0] | (g[1] << 8)) == y && g[2] == mo && g[3] == d
          && g[4] == h && g[5] == mi && g[6] == s && g[7] == 0,
          "%s: %u-%02u-%02u %02u:%02u:%02u wd=%u, want %u-%02u-%02u %02u:%02u:%02u",
          what, (unsigned)(g[0] | (g[1] << 8)), g[2], g[3], g[4], g[5], g[6], g[7],
          y, mo, d, h, mi, s);
}

static void test_vmu_clock(void) {
    static uint8_t card[MAPLE_VMU_BLOCKS * MAPLE_VMU_BLOCK];
    uint8_t req[16], out[MAPLE_MAX_FRAME], got[8];
    maple_bus_t  b;
    maple_unit_t uc, uv;
    maple_vmu_t  vmu;
    maple_cond_t c;
    uint32_t w[2];
    unsigned n, rn;

    maple_cond_default(&c);
    maple_bus_init(&b);
    maple_controller_unit(&uc, &c);
    maple_bus_add(&b, &uc);
    maple_vmu_unit(&uv, &vmu, card, MAPLE_SUB_UNIT(1));
    maple_bus_add(&b, &uv);
    maple_vmu_format(card);

    // Unset, it reads the BIOS default date, which keeps the capture survey matching.
    CHECK(vmu_clock_get(&b, &vmu, 0, got), "clock read has the wrong shape");
    vmu_clock_expect(got, 1999, 4, 28, 0, 0, 0, "power-on default");
    CHECK(vmu_clock_get(&b, &vmu, 3723000u, got), "clock read has the wrong shape");
    vmu_clock_expect(got, 1999, 4, 28, 1, 2, 3, "ticked an hour in");
    CHECK(vmu_clock_get(&b, &vmu, 86400000u, got), "clock read has the wrong shape");
    vmu_clock_expect(got, 1999, 4, 29, 0, 0, 0, "ticked past midnight");

    // The date the console sets, read back from a different base time.
    static const uint8_t k_set[8] = { 0xea, 0x07, 9, 17, 1, 18, 44, 0 };   // 2026-09-17 01:18:44
    CHECK(vmu_clock_set(&b, &vmu, 100000u, k_set, 8) == MAPLE_CMD_RESP_ACK, "clock write refused");
    CHECK(vmu_clock_get(&b, &vmu, 100000u, got), "clock read has the wrong shape");
    vmu_clock_expect(got, 2026, 9, 17, 1, 18, 44, "as set");
    CHECK(vmu_clock_get(&b, &vmu, 101999u, got), "clock read has the wrong shape");
    vmu_clock_expect(got, 2026, 9, 17, 1, 18, 45, "a second after being set");

    static const uint8_t k_eve[8] = { 0xea, 0x07, 12, 31, 23, 59, 59, 0 };
    CHECK(vmu_clock_set(&b, &vmu, 0, k_eve, 8) == MAPLE_CMD_RESP_ACK, "clock write refused");
    CHECK(vmu_clock_get(&b, &vmu, 1000u, got), "clock read has the wrong shape");
    vmu_clock_expect(got, 2027, 1, 1, 0, 0, 0, "new year");

    static const uint8_t k_leap[8] = { 0xe8, 0x07, 2, 28, 23, 59, 59, 0 };  // 2024 is a leap year
    CHECK(vmu_clock_set(&b, &vmu, 0, k_leap, 8) == MAPLE_CMD_RESP_ACK, "clock write refused");
    CHECK(vmu_clock_get(&b, &vmu, 1000u, got), "clock read has the wrong shape");
    vmu_clock_expect(got, 2024, 2, 29, 0, 0, 0, "leap day");

    // A refused write must leave the clock alone, not land a partial date.
    static const uint8_t k_bad[8] = { 0xea, 0x07, 13, 17, 1, 18, 44, 0 };   // month 13
    CHECK(vmu_clock_set(&b, &vmu, 0, k_bad, 8) == MAPLE_RESP_FILE_ERR, "month 13 was accepted");
    CHECK(vmu_clock_set(&b, &vmu, 0, k_set, 4) == MAPLE_RESP_FILE_ERR, "a 4-byte date was accepted");
    CHECK(vmu_clock_get(&b, &vmu, 1000u, got), "clock read has the wrong shape");
    vmu_clock_expect(got, 2024, 2, 29, 0, 0, 0, "after two refused writes");

    // The rest of the clock function: a condition read, and no block sync.
    w[0] = MAPLE_FUNC_CLOCK;
    rn = maple_make_req_to(req, MAPLE_CMD_GET_CONDITION, MAPLE_SUB_UNIT(1), 0, w, 1);
    n = maple_build_reply(&b, req, rn, out);
    CHECK(n == 4 + 8 + 1 && maple_cmd(out) == MAPLE_CMD_RESP_DATA_XFER
          && out[8] == 0xff && out[9] == 0 && out[10] == 0 && out[11] == 0,
          "clock condition: %u bytes, cmd %02x", n, n ? maple_cmd(out) : 0);

    rn = maple_make_req_to(req, MAPLE_CMD_BLOCK_SYNC, MAPLE_SUB_UNIT(1), 0, w, 1);
    n = maple_build_reply(&b, req, rn, out);
    CHECK(n == 5 && maple_cmd(out) == MAPLE_RESP_UNKNOWN_FUNC, "clock sync: cmd %02x",
          n ? maple_cmd(out) : 0);
}

void tests_vmu(void) {
    test_vmu_clock();
    test_vmu_store_fmt();
    test_vmu_lcd();
    test_vmu_reinit_keeps_card();
}
