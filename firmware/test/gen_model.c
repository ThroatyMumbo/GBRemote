#include "gen_model.h"
#include "pinmap.h"
#include <string.h>

#define CYC_PER_US   150u       // the model's own clock; gendev is edge-driven, so the rate is inert
#define WRITE_RD_US    2u       // 68000: write TH, then read $A10003
#define INTER_US       4u       // rest of the gap before the next TH write

#define D_MASK  (0xfull << PIN_GEN_D0)
#define TT_MASK (0x3ull << PIN_GEN_TL)

static uint64_t pins_get(void *u) {
    const gen_sm_t *s = u;
    return (uint64_t)s->m->th << PIN_GEN_TH;
}
static void pins_set(void *u, uint64_t v, uint64_t mask) {
    gen_model_t *m = ((gen_sm_t *)u)->m;
    m->out = (m->out & ~mask) | (v & mask);
}
static void dirs_set(void *u, uint64_t v, uint64_t mask) {
    gen_model_t *m = ((gen_sm_t *)u)->m;
    m->dirs = (m->dirs & ~mask) | (v & mask);
}
static bool irq_get(void *u, unsigned n) { (void)u; (void)n; return false; }
static void irq_set(void *u, unsigned n, bool v) { (void)n; if (v) ((gen_sm_t *)u)->irqs++; }
static bool rx_push(void *u, uint32_t w) { (void)u; (void)w; return false; }
static bool tx_pop(void *u, uint32_t *w) {
    gen_sm_t *s = u;
    if (s->rd == s->wr) return false;
    *w = s->fifo[s->rd++ & 3u];
    return true;
}

// gendev.c's drive_patch(): the .pio is push-pull, and open drain is every OUT retargeted at
// PINDIRS. n_out is unchanged either way, so the delay bits and the count ride through untouched.
static void sm_init(gen_model_t *m, gen_sm_t *s, unsigned out_base, unsigned n_out,
                    const uint16_t *prog, unsigned len, unsigned wrap_target, unsigned wrap) {
    memset(s, 0, sizeof *s);
    s->m = m;
    for (unsigned i = 0; i < len && i < 32u; i++) {
        uint16_t v = prog[i];
        if (m->open_drain && ((v >> 13) & 7u) == 3u && ((v >> 5) & 7u) == 0u) v |= 4u << 5;
        s->prog[i] = v;
    }
    pio_core_cfg_t c = { 0 };
    c.in_base = PIN_GEN_TH; c.jmp_pin = PIN_GEN_TH;
    c.out_base = (uint8_t)out_base; c.out_count = (uint8_t)n_out;
    c.out_shift_right = true;   // no autopull; both programs consume exactly 32 bits per pull
    c.wrap_target = (uint8_t)wrap_target; c.wrap = (uint8_t)wrap;
    pio_core_init(&s->sm, s->prog, &c, (uint8_t)wrap_target);
    pio_core_restart(&s->sm);
    s->sm.x = m->open_drain ? 0u : 0xffffffffu;     // gendev_prime(): the released wire word
}

void gen_model_init(gen_model_t *m, int open_drain,
                    const uint16_t *dpad, unsigned dpad_len, unsigned dpad_wt, unsigned dpad_wrap,
                    const uint16_t *tltr, unsigned tltr_len, unsigned tltr_wt, unsigned tltr_wrap) {
    memset(m, 0, sizeof *m);
    m->open_drain = open_drain;
    sm_init(m, &m->dpad, PIN_GEN_D0, 4, dpad, dpad_len, dpad_wt, dpad_wrap);
    sm_init(m, &m->tltr, PIN_GEN_TL, 2, tltr, tltr_len, tltr_wt, tltr_wrap);
    // gendev_prime(): level first, then direction. Push-pull drives the released high.
    m->out = m->dirs = open_drain ? 0u : (D_MASK | TT_MASK);
    m->th = 1;
}

static uint32_t wire_word(const gen_model_t *m, uint32_t pressed) {
    return m->open_drain ? pressed : ~pressed;
}

static void sm_push(gen_sm_t *s, uint32_t w) {
    if (s->wr - s->rd >= 4u) return;            // full: dropped, as gendev_put() does
    s->fifo[s->wr++ & 3u] = w;
}

void gen_model_put(gen_model_t *m, uint32_t dpad_word, uint32_t tltr_word) {
    sm_push(&m->dpad, wire_word(m, dpad_word));
    sm_push(&m->tltr, wire_word(m, tltr_word));
}

static void run(gen_model_t *m, unsigned cycles) {
    static const pio_core_bus_t tmpl = { pins_get, pins_set, dirs_set, irq_get, irq_set,
                                         rx_push, tx_pop, NULL };
    pio_core_bus_t buses[2] = { tmpl, tmpl };
    buses[0].u = &m->dpad;
    buses[1].u = &m->tltr;
    pio_core_sm_t *sms[2] = { &m->dpad.sm, &m->tltr.sm };
    m->now += cycles;
    pio_core_run(sms, buses, 2, m->now);
}

void gen_model_idle(gen_model_t *m, unsigned us) { run(m, us * CYC_PER_US); }

// Mirrors gendev_resync(): clear the FIFO, reset the shift counters, jump to the wrap target, on
// both SMs. X survives, exactly as pio_sm_restart() leaves it.
static void sm_rewind(gen_sm_t *s) {
    s->rd = s->wr = 0;
    pio_core_restart(&s->sm);
    s->sm.pc = s->sm.cfg.wrap_target;
}

void gen_model_rewind(gen_model_t *m) {
    sm_rewind(&m->dpad);
    sm_rewind(&m->tltr);
}

void gen_model_resync(gen_model_t *m, uint32_t dpad_word, uint32_t tltr_word) {
    gen_model_rewind(m);
    gen_model_put(m, dpad_word, tltr_word);   // re-prime, exactly as sm_rewind() does on the target
}

// A line reads low when it is an output and its level is 0 — open drain holds the level at 0 and
// push-pull holds the direction at 1, so one expression covers both.
uint8_t gen_model_sample(const gen_model_t *m) {
    uint64_t low = m->dirs & ~m->out;
    return (uint8_t)(((low >> PIN_GEN_D0) & 0xfu) | (((low >> PIN_GEN_TL) & 0x3u) << 4));
}

void gen_model_read(gen_model_t *m, unsigned ncycles, gen_trace_t *out) {
    for (unsigned k = 0; k < ncycles && out->n < GEN_MODEL_MAX_CYCLES; k++) {
        m->th = (k & 1u) ? 0u : 1u;             // cycle 1 is TH high, and needs no leading edge
        run(m, WRITE_RD_US * CYC_PER_US);
        out->th[out->n] = m->th;
        out->line[out->n] = gen_model_sample(m);
        out->n++;
        run(m, INTER_US * CYC_PER_US);
    }
    m->th = 1;                                  // a read loop leaves TH high
}
