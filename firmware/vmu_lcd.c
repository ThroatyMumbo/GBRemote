#include "vmu_lcd.h"
#include <string.h>

static uint8_t g_dbl[16];               // nibble -> that nibble with every bit doubled

static void build_dbl(void) {
    if (g_dbl[15]) { return; }
    for (uint32_t i = 0; i < 16; i++) {
        uint8_t v = 0;
        for (uint32_t b = 0; b < 4; b++) {
            if (i & (1U << b)) { v |= (uint8_t)(3U << (2 * b)); }
        }
        g_dbl[i] = v;
    }
}

static uint8_t bitrev8(uint8_t v) {
    v = (uint8_t)((v >> 4) | (v << 4));
    v = (uint8_t)(((v & 0xCC) >> 2) | ((v & 0x33) << 2));
    return (uint8_t)(((v & 0xAA) >> 1) | ((v & 0x55) << 1));
}

static void vmu_lcd_prepare(const uint8_t frame[MAPLE_VMU_LCD_BYTES], uint8_t out[MAPLE_VMU_LCD_BYTES]) {
    vmu_lcd_prepare_ex(frame, out, VMU_ROTATE_180);
}

void vmu_lcd_prepare_ex(const uint8_t frame[MAPLE_VMU_LCD_BYTES], uint8_t out[MAPLE_VMU_LCD_BYTES],
                        bool rotate) {
    uint8_t buf[MAPLE_VMU_LCD_BYTES];

    build_dbl();
#if VMU_WORD_SWAP
    for (uint32_t i = 0; i < MAPLE_VMU_LCD_BYTES; i += 4) {
        buf[i] = frame[i + 3]; buf[i + 1] = frame[i + 2];
        buf[i + 2] = frame[i + 1]; buf[i + 3] = frame[i];
    }
#else
    memcpy(buf, frame, sizeof buf);
#endif

    // Reversing the whole bit string turns both axes at once, exact here because a row is a whole
    // 6 bytes.
    if (rotate) {
        for (uint32_t i = 0; i < MAPLE_VMU_LCD_BYTES; i++) {
            out[i] = bitrev8(buf[MAPLE_VMU_LCD_BYTES - 1U - i]);
        }
    } else {
        memcpy(out, buf, MAPLE_VMU_LCD_BYTES);
    }
}

// A destination byte is one source nibble doubled, so the 2x expansion is a lookup. Both planes take
// it: a lit dot is color 3, the darkest in the ROM's text palette.
void vmu_lcd_tile_row(const uint8_t prepared[MAPLE_VMU_LCD_BYTES],
                      uint8_t row[VMU_TILE_ROW_BYTES], unsigned r) {
    for (uint32_t y = 0; y < 8U; y++) {
        uint32_t sy = (r * 8U + y) >> 1;
        uint8_t *at = &row[y * 2U];
        for (uint32_t tx = 0; tx < VMU_TILE_COLS; tx++) {
            uint8_t s = prepared[sy * VMU_LCD_STRIDE + (tx >> 1)];
            uint8_t b = g_dbl[(tx & 1U) ? (s & 0x0FU) : (uint8_t)(s >> 4)];
            at[tx * 16U] = b;
            at[tx * 16U + 1U] = b;
        }
    }
}

void vmu_lcd_tiles(const uint8_t frame[MAPLE_VMU_LCD_BYTES], uint8_t tiles[VMU_TILE_BYTES]) {
    uint8_t prepared[MAPLE_VMU_LCD_BYTES];

    vmu_lcd_prepare(frame, prepared);
    for (unsigned r = 0; r < VMU_TILE_ROWS; r++) {
        vmu_lcd_tile_row(prepared, &tiles[r * VMU_TILE_ROW_BYTES], r);
    }
}

static void px(uint8_t *fb, uint32_t x, uint32_t y) {
    fb[y * VMU_LCD_STRIDE + (x >> 3)] |= (uint8_t)(0x80U >> (x & 7U));
}

// Asymmetric on purpose: the solid block marks the frame's top left as it arrives, so it reads the
// rotation setting back at a glance instead of looking plausible either way.
void vmu_lcd_test_frame(uint8_t frame[MAPLE_VMU_LCD_BYTES], uint32_t t) {
    memset(frame, 0, MAPLE_VMU_LCD_BYTES);
    for (uint32_t x = 0; x < VMU_LCD_W; x++) { px(frame, x, 0);
        px(frame, x, VMU_LCD_H - 1U);
    }
    for (uint32_t y = 0; y < VMU_LCD_H; y++) { px(frame, 0, y);
        px(frame, VMU_LCD_W - 1U, y);
    }
    for (uint32_t y = 2; y < 8; y++) {
        for (uint32_t x = 2; x < 8; x++) { px(frame, x, y); }
    }
    for (uint32_t y = 2; y < VMU_LCD_H - 2U; y++) { px(frame, 2U + (t % (VMU_LCD_W - 5U)), y); }
}
