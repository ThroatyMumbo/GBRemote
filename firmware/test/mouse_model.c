#include "mouse_model.h"
#include "pinmap.h"
#include <string.h>

#define PIO_HZ     25000000ull          // snesmouse_CLK_HZ
#define NS_PER_CYC (1000000000ull / PIO_HZ)
#define SERVICE_NS 1000000ull           // core1's wait_latch timeout, idle_us = 1000

static uint64_t pins_get(void *u) {
    mm_t *m = u;
    uint64_t p = m->pins | (1ull << PIN_SNES_DATA1);
    if (m->data_dir) p &= ~(1ull << PIN_SNES_DATA1);   // open drain against the console's pull-up
    return p;
}
static void pins_set(void *u, uint64_t v, uint64_t mask) { (void)u; (void)v; (void)mask; }
static void dirs_set(void *u, uint64_t v, uint64_t mask) {
    mm_t *m = u;
    if (mask & (1ull << PIN_SNES_DATA1)) m->data_dir = (v >> PIN_SNES_DATA1) & 1u;
}
static bool irq_get(void *u, unsigned n) { return (((mm_t *)u)->irq >> n) & 1u; }
static void irq_set(void *u, unsigned n, bool v) {
    mm_t *m = u;
    if (v) m->irq |= (uint8_t)(1u << n); else m->irq &= (uint8_t)~(1u << n);
}
static bool rx_push(void *u, uint32_t w) {
    mm_t *m = u;
    if (m->rx_wr - m->rx_rd >= 4) return false;
    m->rx[m->rx_wr++ % 4u] = w;
    return true;
}
static bool tx_pop(void *u, uint32_t *w) {
    mm_t *m = u;
    if (m->tx_rd == m->tx_wr) return false;
    *w = m->tx[m->tx_rd++ % 4u];
    return true;
}

bool mm_push(mm_t *m, uint32_t w) {
    if (m->tx_wr - m->tx_rd >= 4) return false;
    m->tx[m->tx_wr++ % 4u] = w;
    return true;
}
bool mm_pop(mm_t *m, uint32_t *w) {
    if (m->rx_rd == m->rx_wr) return false;
    *w = m->rx[m->rx_rd++ % 4u];
    return true;
}
bool mm_irq_take(mm_t *m, unsigned n) {
    bool v = (m->irq >> n) & 1u;
    m->irq &= (uint8_t)~(1u << n);
    return v;
}

void mm_init(mm_t *m, const uint16_t *insns, unsigned len, unsigned wrap_target, unsigned wrap,
             mm_device_fn dev, void *u) {
    memset(m, 0, sizeof *m);
    memcpy(m->prog, insns, len * sizeof *insns);
    pio_core_cfg_t c = { 0 };
    c.in_base = PIN_SNES_LATCH; c.jmp_pin = PIN_SNES_LATCH;
    c.out_base = PIN_SNES_DATA1; c.out_count = 1;
    c.set_base = PIN_SNES_DATA1; c.set_count = 1;
    c.out_shift_right = true; c.out_pull_threshold = 32; c.in_push_threshold = 32;
    c.wrap_target = (uint8_t)wrap_target; c.wrap = (uint8_t)wrap;
    pio_core_init(&m->sm, m->prog, &c, (uint8_t)wrap_target);
    m->dev = dev; m->dev_u = u;
    m->manual_low_ns = 250;
    m->pins = 1ull << PIN_SNES_CLK;                  // CLK idles high
}

static void run_to(mm_t *m, uint64_t ns) {
    static const pio_core_bus_t bus_tmpl = { pins_get, pins_set, dirs_set, irq_get, irq_set,
                                             rx_push, tx_pop, NULL };
    pio_core_bus_t bus = bus_tmpl;
    bus.u = m;
    pio_core_sm_t *sms[1] = { &m->sm };
    // core1 runs every 1 ms or as soon as the RX FIFO has something, as wait_latch returns early.
    while (m->ns < ns) {
        uint64_t step = ns - m->ns < 1000 ? ns - m->ns : 1000;
        m->ns += step;
        pio_core_run(sms, &bus, 1, m->ns / NS_PER_CYC);
        if (m->dev && (m->rx_rd != m->rx_wr || (m->irq >> 6) & 1u || m->ns >= m->next_service_ns)) {
            m->dev(m, m->dev_u);
            m->next_service_ns = m->ns + SERVICE_NS;
        }
    }
}

void mm_wait_ns(mm_t *m, uint64_t ns) { run_to(m, m->ns + ns); }

static void pin(mm_t *m, unsigned p, int v) {
    if (v) m->pins |= 1ull << p; else m->pins &= ~(1ull << p);
}

// The console reads a low line as 1.
static unsigned sample(mm_t *m) { return m->data_dir ? 1u : 0u; }

// A $4016 read: CLK pulses low for the read cycle and the bit is taken at its end.
static unsigned manual_read(mm_t *m) {
    pin(m, PIN_SNES_CLK, 0);
    mm_wait_ns(m, m->manual_low_ns);
    unsigned b = sample(m);
    pin(m, PIN_SNES_CLK, 1);
    return b;
}

// Auto-joypad read: LATCH 12 us, then 16 clocks of 6 us low / 6 us high (measured on a real SNES).
static void auto_read(mm_t *m) {
    pin(m, PIN_SNES_LATCH, 1);
    mm_wait_ns(m, 12000);
    pin(m, PIN_SNES_LATCH, 0);
    mm_wait_ns(m, 6000);
    uint16_t w = 0;
    for (int i = 0; i < 16; i++) {
        pin(m, PIN_SNES_CLK, 0);
        mm_wait_ns(m, 3000);
        w = (uint16_t)((w << 1) | sample(m));       // first bit lands in $4219 bit 7
        mm_wait_ns(m, 3000);
        pin(m, PIN_SNES_CLK, 1);
        mm_wait_ns(m, 6000);
    }
    m->r4219 = (uint8_t)(w >> 8);
    m->r4218 = (uint8_t)w;
}

// CODE_01DB25: LATCH high, one $4016 read, LATCH low.
static void strobe(mm_t *m) {
    pin(m, PIN_SNES_LATCH, 1);
    mm_wait_ns(m, 1500);
    (void)manual_read(m);
    mm_wait_ns(m, 1500);
    pin(m, PIN_SNES_LATCH, 0);
    m->strobes++;
}

static int to_signed(uint8_t v) { return (v & 0x80) ? -(int)(v & 0x7f) : (int)(v & 0x7f); }

void mm_frame(mm_t *m) {
    auto_read(m);
    m->present = (m->r4218 & 0x0f) == 0x01;
    m->dx = m->dy = 0;
    if (!m->present) return;
    uint8_t x = 0, y = 0;                            // $04C6, $04C8
    for (int i = 0; i < 16; i++) {
        mm_wait_ns(m, 8000);                         // LSR / ROL / ROL / DEY / BNE at 2.68 MHz
        unsigned c = manual_read(m);
        unsigned cx = x >> 7;
        x = (uint8_t)((x << 1) | c);
        y = (uint8_t)((y << 1) | cx);
    }
    m->dx = to_signed(x);
    m->dy = to_signed(y);
    m->right = (m->r4218 >> 7) & 1u;
    m->left  = (m->r4218 >> 6) & 1u;
    m->speed = (m->r4218 >> 4) & 3u;
    // CODE_01DA68 and CODE_01DA88 run first. Then CODE_01DAF9 re-tests the stale auto-read, so a
    // mismatch strobes until $04C3 runs out: 31.
    mm_wait_ns(m, 20000);
    if (m->speed != m->want_speed)
        for (int i = 0; i < 31; i++) { strobe(m); mm_wait_ns(m, 25000); }
}
