#include "capout_model.h"
#include <string.h>

// Decode just enough of the PIO encoding to run capstream: OUT PINS with side-set, and MOV (nop)
// with side-set. Anything else is a program change that should fail loudly rather than be guessed.
#define INS_OUT 0x6000u
#define INS_MOV 0xA000u

unsigned capout_sim(const uint16_t *insns, unsigned n_insns,
                    const uint32_t *words, unsigned n_words,
                    capout_cycle_t *out, unsigned cap) {
    uint32_t osr = 0;
    unsigned shifted = 32;              // start "empty" so the first OUT autopulls
    unsigned widx = 0, pc = 0, t = 0;
    uint8_t pins = 0;

    while (t < cap) {
        uint16_t ins = insns[pc % n_insns];
        uint8_t side = (uint8_t)((ins >> 12) & 1u);   // 1 side-set bit, no delay bits used
        uint16_t op = ins & 0xE000u;

        if (op == INS_OUT) {
            if (shifted >= 32) {
                if (widx >= n_words) break;           // FIFO dry: stop rather than model a stall
                osr = words[widx++];
                shifted = 0;
            }
            pins = (uint8_t)(osr & 0xFu);
            osr >>= 4;
            shifted += 4;
        } else if (op != INS_MOV) {
            break;                                     // unmodeled instruction
        }
        out[t].clk = side;
        out[t].pins = pins;
        t++;
        pc++;
    }
    return t;
}

unsigned capout_receive(const capout_cycle_t *cyc, unsigned n, uint8_t *out, unsigned cap) {
    unsigned nib = 0;
    memset(out, 0, cap);
    for (unsigned i = 1; i < n; i++) {
        if (cyc[i].clk && !cyc[i - 1].clk) {           // rising edge
            unsigned byte = nib >> 1;
            if (byte >= cap) break;
            out[byte] |= (uint8_t)((cyc[i].pins & 0xFu) << (4 * (nib & 1u)));
            nib++;
        }
    }
    return nib >> 1;
}

bool capout_setup_ok(const capout_cycle_t *cyc, unsigned n, unsigned *first_bad) {
    for (unsigned i = 1; i < n; i++) {
        if (cyc[i].clk && !cyc[i - 1].clk && cyc[i].pins != cyc[i - 1].pins) {
            if (first_bad) *first_bad = i;
            return false;
        }
    }
    return true;
}
