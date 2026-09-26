#include "cable_id.h"
#include "pad_driver.h"
#include "pinmap.h"
#ifndef PAD_HOST
#include "hardware/adc.h"
#endif

// 3300 * (R_id + 100) / (R_id + 10100) mV, R25 included (0R reads 33 mV), for the kit on hand:
// open, 33k, 22k, 10k, 5.6k, 4.7k, 2.2k, 1k, 470, 0R.
static const uint16_t k_id_mv[10] = { 3300, 2534, 2272, 1658, 1198, 1070, 617, 327, 178, 33 };

#define ID_TOL_MV  60           // under half the 128 mV 5.6k/4.7k gap; worst measured cable is 27 mV off
#define ID_OPEN_MV ((3300 + 2534) / 2)   // 2917: preserves the deliberate 0.77 V guard band

static uint8_t  g_slot = ADAPT_NONE, g_cand = ADAPT_NONE, g_agree;
static uint16_t g_mv;

uint8_t cable_id_classify(uint16_t mv) {
    if (mv >= ID_OPEN_MV) {
        return ADAPT_NONE; // nothing reads higher than open
    }
    uint8_t best = ADAPT_UNKNOWN;
    int bd = ID_TOL_MV + 1;
    for (int i = 1; i <= ADAPT_9; i++) {
        int d = (int)mv - (int)k_id_mv[i];
        if (d < 0) { d = -d; }
        if (d < bd) { bd = d; best = (uint8_t)i; }
    }
    return best;                                // nearest band WITH a tolerance, never plain nearest
}

void cable_id_reset(void) {
    g_slot = g_cand = ADAPT_NONE;
    g_agree = 0;
    g_mv = 0;
}

uint8_t cable_id_step(uint16_t mv) {
    g_mv = mv;
    uint8_t c = cable_id_classify(mv);

    if (c == ADAPT_NONE) {                      // unplugging latches at once
        g_slot = g_cand = ADAPT_NONE;
        g_agree = 0;
        return g_slot;
    }
    if (c == g_cand) {
        if (g_agree < 3) {
            g_agree++;
            if (g_agree == 3) { g_slot = c; }
        }
    } else {
        g_cand = c;
        g_agree = 1;
    }
    return g_slot;
}

uint8_t  cable_id_slot(void) { return g_slot; }
uint16_t cable_id_mv(void)   { return g_mv; }

// What each adapter is wired for, default first.
static const uint8_t k_cable_protos[ADAPT_9 + 1][2] = {
    [ADAPT_NES]     = { PROTO_NES },
    [ADAPT_DC]      = { PROTO_DREAMCAST },
    [ADAPT_SNES]    = { PROTO_SNES, PROTO_SNES_MOUSE },
    [ADAPT_GENESIS] = { PROTO_GENESIS },
    [ADAPT_N64]     = { PROTO_N64 },
};

uint8_t cable_id_proto(uint8_t slot, uint8_t sel) {
    if (slot > ADAPT_9 || !k_cable_protos[slot][0] || sel == PROTO_NONE || proto_pinless(sel)) {
        return sel;
    }
    for (int i = 0; i < 2; i++) {
        if (k_cable_protos[slot][i] == sel) { return sel; }
    }
    return k_cable_protos[slot][0];
}

#ifndef PAD_HOST
void cable_id_init(void) {
    adc_init();
    adc_gpio_init(PIN_CTRL_ID);         // funcsel NULL, pulls off, digital input buffer off
    adc_select_input(CTRL_ID_ADC);
    cable_id_reset();
}

// Eight back-to-back conversions, middle six averaged. No delay between them: the tick cadence
// covers the RC, and a stall here overruns the write_capture FIFO.
static uint16_t id_sample_mv(void) {
    uint16_t v[8];
    for (int i = 0; i < 8; i++) { v[i] = adc_read(); }
    for (int i = 1; i < 8; i++) {                       // insertion sort, 8 elements
        uint16_t k = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > k) { v[j + 1] = v[j]; j--; }
        v[j + 1] = k;
    }
    uint32_t sum = 0;
    for (int i = 1; i < 7; i++) { sum += v[i]; }
    return (uint16_t)((sum / 6U) * 3300U / 4095U);
}

uint8_t cable_id_tick(void) { return cable_id_step(id_sample_mv()); }
#endif
