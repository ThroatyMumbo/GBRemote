// LCD: samples XRAM on the controller's refresh cadence into a packed 48x32 frame. Transcribed
// from libevmu's evmu_lcd.c (MIT, Falco Girgis) minus the ghosting model.
#include "vmu_internal.h"
#include <string.h>

// libevmu: 12 or 6 "ticks" per refresh times EVMU_LCD_SCREEN_REFRESH_DIVISOR (199), in microseconds.
#define REFRESH_NS_MCR4_SET   (12u * 199u * 1000u)
#define REFRESH_NS_MCR4_CLEAR ( 6u * 199u * 1000u)

static uint32_t period_ns_(const vmu_t *v) {
    return (sfrc_(v, SFR_MCR) & MCR_MCR4) ? REFRESH_NS_MCR4_SET : REFRESH_NS_MCR4_CLEAR;
}

// Walk order is the hardware's: STAD picks the start, rows are 6 bytes inside 16-byte blocks of
// two rows each, bank 1 follows bank 0, and the 6 leading bytes of bank 2 wrap back to bank 0.
void vmu_lcd_sample(vmu_t *v) {
    uint8_t frame[VMU_LCD_BYTES];
    bool blank = !(sfrc_(v, SFR_VCCR) & VCCR_VCCR7) || (sfrc_(v, SFR_PCON) & PCON_HOLD);
    if (blank) {
        memset(frame, 0, sizeof frame);
    } else {
        unsigned p = sfrc_(v, SFR_STAD);
        if (p >= 0x83) { p -= 0x83; }
        unsigned b = p >> 6;
        p = (p & 0x3f) * 2;
        for (unsigned y = 0; y < VMU_LCD_H; y++) {
            for (unsigned k = 0; k < VMU_LCD_STRIDE; k++) {
                frame[y * VMU_LCD_STRIDE + k] = v->xram[b > 2 ? 0 : b][p++ & 0x7f];
                if ((p & 0xf) >= 12) { p += 4; }
                if (p >= 128) { b++; p -= 128; }
                if (b == 2 && p >= 6) { b = 0; p -= 6; }
            }
        }
    }
    if (memcmp(frame, v->fb, sizeof frame) != 0) {
        memcpy(v->fb, frame, sizeof frame);
        v->fb_seq++;
    }
}

void vmu_lcd_advance_ns(vmu_t *v, uint32_t ns) {
    uint32_t period = period_ns_(v);
    v->lcd_ns += ns;
    if (!(sfrc_(v, SFR_MCR) & MCR_MCR3)) {
        if (v->lcd_ns > period) { v->lcd_ns = period; }
        return;
    }
    while (v->lcd_ns >= period) {
        v->lcd_ns -= period;
        vmu_lcd_sample(v);
    }
}

bool vmu_lcd_frame(vmu_t *v, uint8_t out[VMU_LCD_BYTES]) {
    memcpy(out, v->fb, VMU_LCD_BYTES);
    bool changed = v->fb_seq != v->fb_seq_seen;
    v->fb_seq_seen = v->fb_seq;
    return changed;
}

uint8_t vmu_lcd_icons(const vmu_t *v) {
    uint8_t icons = 0;
    if (v->xram[2][XRAM_ICN_FILE - XRAM_BASE] & 0x40) { icons |= VMU_LCD_ICON_FILE; }
    if (v->xram[2][XRAM_ICN_GAME - XRAM_BASE] & 0x10) { icons |= VMU_LCD_ICON_GAME; }
    if (v->xram[2][XRAM_ICN_CLOCK - XRAM_BASE] & 0x04) { icons |= VMU_LCD_ICON_CLOCK; }
    if (v->xram[2][XRAM_ICN_FLASH - XRAM_BASE] & 0x01) { icons |= VMU_LCD_ICON_FLASH; }
    return icons;
}
