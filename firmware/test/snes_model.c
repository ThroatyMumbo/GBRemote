#include "snes_model.h"
#include "pinmap.h"
#include <string.h>

#define CYC_PER_US  4u          // snesdev_CLK_HZ is 4 MHz
#define LATCH_US    12u
#define PHASE_US     6u         // clock low, then clock high
#define GAP_US       6u         // latch fall to the first clock fall

static uint64_t pins_get(void *u) {
    const snes_sm_t *s = u;
    return ((uint64_t)s->latch << PIN_SNES_LATCH) | ((uint64_t)s->clk << PIN_SNES_CLK);
}
static void pins_set(void *u, uint64_t v, uint64_t mask) { (void)u; (void)v; (void)mask; }
static void dirs_set(void *u, uint64_t v, uint64_t mask) {
    snes_sm_t *s = u;
    if (mask & (1ull << PIN_SNES_DATA1)) s->pindirs = (uint8_t)((v >> PIN_SNES_DATA1) & 1u);
}
static bool irq_get(void *u, unsigned n) { (void)n; return ((snes_sm_t *)u)->irq; }
static void irq_set(void *u, unsigned n, bool v) { (void)n; ((snes_sm_t *)u)->irq = v; }
static bool rx_push(void *u, uint32_t w) { (void)u; (void)w; return false; }
static bool tx_pop(void *u, uint32_t *w) {
    snes_sm_t *s = u;
    if (s->rd == s->wr) return false;
    *w = s->fifo[s->rd++ & 3u];
    return true;
}

void snes_sm_push(snes_sm_t *s, uint32_t report) {
    if (s->wr - s->rd >= 4u) return;                // full: dropped, as snesdev_put_report does
    s->fifo[s->wr++ & 3u] = report;
}

// Mirrors snesdev_program_init(): IN base and JMP pin are LATCH, so `wait n pin 1` is CLK.
void snes_sm_init(snes_sm_t *s, const uint16_t *prog, unsigned len,
                  unsigned wrap_target, unsigned wrap, unsigned nbits) {
    memset(s, 0, sizeof *s);
    memcpy(s->prog, prog, (len < 32u ? len : 32u) * sizeof *prog);
    pio_core_cfg_t c = { 0 };
    c.in_base = PIN_SNES_LATCH; c.jmp_pin = PIN_SNES_LATCH;
    c.out_base = PIN_SNES_DATA1; c.out_count = 1;
    c.set_base = PIN_SNES_DATA1; c.set_count = 1;
    c.out_shift_right = true;   // no autopull: an exhausted OSR shifts zeros, releasing the line
    c.wrap_target = (uint8_t)wrap_target; c.wrap = (uint8_t)wrap;
    pio_core_init(&s->sm, s->prog, &c, (uint8_t)wrap_target);
    pio_core_restart(&s->sm);
    s->sm.isr = nbits - 1u;
    s->clk = 1;                 // CLK idles high and pulses low; LATCH idles low
}

static void run(snes_sm_t *s, unsigned cycles) {
    static const pio_core_bus_t tmpl = { pins_get, pins_set, dirs_set, irq_get, irq_set,
                                         rx_push, tx_pop, NULL };
    pio_core_bus_t bus = tmpl;
    bus.u = s;
    pio_core_sm_t *sms[1] = { &s->sm };
    s->now += cycles;
    pio_core_run(sms, &bus, 1, s->now);
}

void snes_sm_idle(snes_sm_t *s, unsigned us) {
    s->latch = 0;
    s->clk = 1;
    run(s, us * CYC_PER_US);
}

void snes_sm_poll(snes_sm_t *s, unsigned nclocks, snes_trace_t *out) {
    memset(out, 0, sizeof *out);
    if (nclocks > SNES_MODEL_MAX_CLOCKS) nclocks = SNES_MODEL_MAX_CLOCKS;

    s->clk = 1;
    s->latch = 1;
    run(s, LATCH_US * CYC_PER_US);
    s->latch = 0;
    run(s, GAP_US * CYC_PER_US);

    const unsigned half = PHASE_US * CYC_PER_US;
    for (unsigned k = 0; k < nclocks; k++) {
        s->clk = 0;
        run(s, half / 2u);
        // Mid-low is where the console reads. The level is held 0, so a driven line is a pressed bit.
        out->bit[out->n++] = s->pindirs;
        run(s, half - half / 2u);
        s->clk = 1;
        run(s, half);
    }
    s->clk = 1;
}
