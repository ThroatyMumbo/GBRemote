#include "link_model.h"
#include "pinmap.h"
#include <string.h>

#define PIO_HZ 25000000ull              // sneslink_CLK_HZ
#define PS_PER_CYC (1000000000000ull / PIO_HZ)

static uint64_t pins_get(void *u) {
    lm_t *m = u;
    uint64_t p = m->pins;
    for (unsigned i = 0; i < 2; i++) {
        bool level = (m->dir >> i) & 1u ? (m->out >> i) & 1u : true;     // the console's pull-up
        if (level) p |= 1ull << (PIN_SNES_DATA1 + i);
    }
    return p;
}
static void pins_set(void *u, uint64_t v, uint64_t mask) {
    lm_t *m = u;
    for (unsigned i = 0; i < 2; i++)
        if (mask & (1ull << (PIN_SNES_DATA1 + i))) {
            if ((v >> (PIN_SNES_DATA1 + i)) & 1u) m->out |= (uint8_t)(1u << i);
            else m->out &= (uint8_t)~(1u << i);
        }
}
static void dirs_set(void *u, uint64_t v, uint64_t mask) { (void)u; (void)v; (void)mask; }
static bool irq_get(void *u, unsigned n) { (void)u; (void)n; return false; }
static void irq_set(void *u, unsigned n, bool v) { (void)u; (void)n; (void)v; }
static bool rx_push(void *u, uint32_t w) {
    lm_t *m = u;
    if (m->rx_wr - m->rx_rd >= 4) return false;
    m->rx[m->rx_wr++ % 4u] = w;
    return true;
}
static bool tx_pop(void *u, uint32_t *w) {
    lm_t *m = u;
    if (m->tx_rd == m->tx_wr) return false;
    *w = m->tx[m->tx_rd++ % 4u];
    return true;
}

bool lm_tx(lm_t *m, uint32_t w) {
    if (m->tx_wr - m->tx_rd >= 4) return false;
    m->tx[m->tx_wr++ % 4u] = w;
    return true;
}
bool lm_rx(lm_t *m, uint32_t *w) {
    if (m->rx_rd == m->rx_wr) return false;
    *w = m->rx[m->rx_rd++ % 4u];
    return true;
}

void lm_init(lm_t *m, const uint16_t *insns, unsigned len, unsigned wrap_target, unsigned wrap,
             unsigned start, lm_device_fn dev, void *u) {
    memset(m, 0, sizeof *m);
    memcpy(m->prog, insns, len * sizeof *insns);
    pio_core_cfg_t c = { 0 };
    c.in_base = PIN_SNES_LATCH;
    c.out_base = PIN_SNES_DATA1; c.out_count = 2;
    c.set_base = PIN_SNES_DATA1; c.set_count = 2;
    c.out_shift_right = true; c.out_autopull = true; c.out_pull_threshold = 32;
    c.in_shift_right = false; c.in_autopush = true; c.in_push_threshold = 24;
    c.wrap_target = (uint8_t)wrap_target; c.wrap = (uint8_t)wrap;
    pio_core_init(&m->sm, m->prog, &c, (uint8_t)start);
    m->start = (uint8_t)start;
    m->out = 3;
    m->dir = 3;                                     // sneslink_init: released, driven
    m->pins = 1ull << PIN_SNES_CLK;
    m->dev = dev; m->dev_u = u;
    m->service_ps = 500000;
    m->read_low_ns = (12u * LM_MC_PS) / 1000u;
}

void lm_arm(lm_t *m) {
    m->enabled = false;
    m->tx_rd = m->tx_wr = m->rx_rd = m->rx_wr = 0;
    pio_core_restart(&m->sm);
    m->out = 3;
    m->sm.pc = m->start;
}

void lm_go(lm_t *m) {
    m->enabled = true;
    m->sm.cycle = m->ps / PS_PER_CYC;
}

static void run_to(lm_t *m, uint64_t ps) {
    static const pio_core_bus_t tmpl = { pins_get, pins_set, dirs_set, irq_get, irq_set,
                                         rx_push, tx_pop, NULL };
    pio_core_bus_t bus = tmpl;
    bus.u = m;
    pio_core_sm_t *sms[1] = { &m->sm };
    while (m->ps < ps) {
        uint64_t step = ps - m->ps < 40000 ? ps - m->ps : 40000;
        m->ps += step;
        if (m->enabled) pio_core_run(sms, &bus, 1, m->ps / PS_PER_CYC);
        if (m->dev && m->ps >= m->next_service_ps) {
            m->next_service_ps = m->ps + m->service_ps;
            m->dev(m, m->dev_u);
        }
    }
}

void lm_wait_mc(lm_t *m, unsigned mc) { run_to(m, m->ps + (uint64_t)mc * LM_MC_PS); }

static void pin(lm_t *m, unsigned p, bool v) {
    if (v) m->pins |= 1ull << p; else m->pins &= ~(1ull << p);
}

// Opcode and operand fetch, then the XSlow read cycle with CLK low; data is taken at its end.
unsigned lm_read(lm_t *m) {
    lm_wait_mc(m, 24);
    pin(m, PIN_SNES_CLK, false);
    run_to(m, m->ps + (uint64_t)m->read_low_ns * 1000u);
    uint64_t p = pins_get(m);
    unsigned v = (((p >> PIN_SNES_DATA1) & 1u) ? 0u : 1u) | (((p >> PIN_SNES_DATA2) & 1u) ? 0u : 2u);
    pin(m, PIN_SNES_CLK, true);
    m->reads++;
    return v;
}

void lm_iobit(lm_t *m, bool high) { pin(m, PIN_SNES_IOBIT, high); }
