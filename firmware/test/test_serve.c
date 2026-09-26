// Host tests for serve.pio — the cart bus, run out of PIO memory against a synthesized bus cycle.
#include "test.h"
#include "serve_insns.h"
#include "read_addr_insns.h"
#include "pio_core.h"
#include "pinmap.h"

// The configs in sv_reset() transcribe serve.pio's *_program_init() helpers; test_serve_meta()
// guards what a transcription can get silently wrong.
enum { SV_REGION, SV_LOW, SV_HIGH, SV_DATA, SV_WCAP, SV_BSEL, SV_RADDR, SV_N };

typedef struct {
    uint32_t w[8];
    unsigned rd, wr, depth;
} sv_fifo_t;

typedef struct {
    uint64_t ext, pio_lvl, pio_dir;     // externally driven levels, and what the SMs drive
    uint8_t  irq;
    sv_fifo_t rx[SV_N], tx[SV_N];
    pio_core_sm_t sm[SV_N];
} sv_t;

typedef struct { sv_t *b; unsigned i; } sv_ctx_t;

static sv_t      g_sv;
static sv_ctx_t  g_sv_ctx[SV_N];

static bool sv_push(sv_fifo_t *f, uint32_t v) {
    if (f->wr - f->rd >= f->depth) return false;
    f->w[f->wr++ % 8u] = v;
    return true;
}
static bool sv_pop(sv_fifo_t *f, uint32_t *v) {
    if (f->rd == f->wr) return false;
    *v = f->w[f->rd++ % 8u];
    return true;
}
static unsigned sv_count(const sv_fifo_t *f) { return f->wr - f->rd; }

static uint64_t sv_pins_get(void *u) {
    const sv_t *b = ((sv_ctx_t *)u)->b;
    return (b->pio_dir & b->pio_lvl) | (~b->pio_dir & b->ext);
}
static void sv_pins_set(void *u, uint64_t v, uint64_t m) {
    sv_t *b = ((sv_ctx_t *)u)->b;
    b->pio_lvl = (b->pio_lvl & ~m) | (v & m);
}
static void sv_dirs_set(void *u, uint64_t v, uint64_t m) {
    sv_t *b = ((sv_ctx_t *)u)->b;
    b->pio_dir = (b->pio_dir & ~m) | (v & m);
}
static bool sv_irq_get(void *u, unsigned n) { return (((sv_ctx_t *)u)->b->irq >> n) & 1u; }
static void sv_irq_set(void *u, unsigned n, bool v) {
    sv_t *b = ((sv_ctx_t *)u)->b;
    if (v) b->irq |= (uint8_t)(1u << n); else b->irq &= (uint8_t)~(1u << n);
}
static bool sv_rx_push(void *u, uint32_t w) {
    sv_ctx_t *c = u;
    return sv_push(&c->b->rx[c->i], w);
}
static bool sv_tx_pop(void *u, uint32_t *w) {
    sv_ctx_t *c = u;
    return sv_pop(&c->b->tx[c->i], w);
}

static const pio_core_bus_t k_sv_bus[SV_N] = {
#define SV_BUS(n) { sv_pins_get, sv_pins_set, sv_dirs_set, sv_irq_get, sv_irq_set, \
                    sv_rx_push, sv_tx_pop, &g_sv_ctx[n] }
    SV_BUS(SV_REGION), SV_BUS(SV_LOW), SV_BUS(SV_HIGH), SV_BUS(SV_DATA), SV_BUS(SV_WCAP),
    SV_BUS(SV_BSEL), SV_BUS(SV_RADDR)
#undef SV_BUS
};

static void sv_reset(void) {
    memset(&g_sv, 0, sizeof g_sv);
    for (unsigned i = 0; i < SV_N; i++) {
        g_sv_ctx[i] = (sv_ctx_t){ &g_sv, i };
        g_sv.rx[i].depth = 8;                       // PIO_FIFO_JOIN_RX on every responder
        g_sv.tx[i].depth = 4;
    }
    // Idle bus: every strobe released high, A15 (ROM /CS) included, CLK high, address low.
    g_sv.ext = (1ull << PIN_CLK) | (1ull << PIN_NWR) | (1ull << PIN_NRD) | (1ull << PIN_NCS)
             | (1ull << PIN_A15);

    pio_core_cfg_t c = { 0 };
    c.in_base = PIN_A15; c.jmp_pin = PIN_A14; c.in_push_threshold = 32;
    c.wrap_target = A15_DETECT_WRAP_TARGET; c.wrap = A15_DETECT_WRAP;
    pio_core_init(&g_sv.sm[SV_REGION], a15_detect_insns, &c, A15_DETECT_ENTRY);

    c = (pio_core_cfg_t){ 0 };
    c.in_base = PIN_A0; c.jmp_pin = PIN_NRD; c.in_autopush = true; c.in_push_threshold = 14;
    c.wrap_target = ROM_LOW_WRAP_TARGET; c.wrap = ROM_LOW_WRAP;
    pio_core_init(&g_sv.sm[SV_LOW], rom_low_insns, &c, ROM_LOW_WRAP_TARGET);
    c.wrap_target = ROM_HIGH_WRAP_TARGET; c.wrap = ROM_HIGH_WRAP;
    pio_core_init(&g_sv.sm[SV_HIGH], rom_high_insns, &c, ROM_HIGH_WRAP_TARGET);

    c = (pio_core_cfg_t){ 0 };
    c.out_base = PIN_D0; c.out_count = 8; c.out_shift_right = true; c.out_pull_threshold = 8;
    c.in_push_threshold = 32;
    c.wrap_target = SERVE_DATA_WRAP_TARGET; c.wrap = SERVE_DATA_WRAP;
    pio_core_init(&g_sv.sm[SV_DATA], serve_data_insns, &c, SERVE_DATA_WRAP_TARGET);

    c = (pio_core_cfg_t){ 0 };
    c.in_base = PIN_A14; c.jmp_pin = PIN_A15; c.in_autopush = true; c.in_push_threshold = 26;
    c.wrap_target = WRITE_CAPTURE_WRAP_TARGET; c.wrap = WRITE_CAPTURE_WRAP;
    pio_core_init(&g_sv.sm[SV_WCAP], write_capture_insns, &c, WRITE_CAPTURE_WRAP_TARGET);

    static const uint8_t wpos[14] = A0_13_WIRE_POS;
    c = (pio_core_cfg_t){ 0 };
    c.in_base = PIN_A14; c.jmp_pin = (uint8_t)(PIN_A0 + wpos[13]);
    c.in_autopush = true; c.in_push_threshold = 11;
    c.wrap_target = BANK_SEL_WRAP_TARGET; c.wrap = BANK_SEL_WRAP;
    pio_core_init(&g_sv.sm[SV_BSEL], bank_sel_insns, &c, BANK_SEL_WRAP_TARGET);

    c = (pio_core_cfg_t){ 0 };
    c.in_base = PIN_A14; c.jmp_pin = PIN_NRD; c.in_autopush = true; c.in_push_threshold = 26;
    c.wrap_target = READ_ADDR_WRAP_TARGET; c.wrap = READ_ADDR_WRAP;
    pio_core_init(&g_sv.sm[SV_RADDR], read_addr_insns, &c, READ_ADDR_WRAP_TARGET);
}

static void sv_run(unsigned cycles) {
    static pio_core_sm_t *sms[SV_N];
    uint64_t until = 0;
    for (unsigned i = 0; i < SV_N; i++) {
        sms[i] = &g_sv.sm[i];
        if (g_sv.sm[i].cycle > until) until = g_sv.sm[i].cycle;
    }
    for (unsigned i = 0; i < SV_N; i++)
        if (g_sv.sm[i].cycle < until) g_sv.sm[i].cycle = until;   // line them up, then advance
    pio_core_run(sms, k_sv_bus, SV_N, until + cycles);
}

static void sv_pin(unsigned pin, int level) {
    if (level) g_sv.ext |= 1ull << pin; else g_sv.ext &= ~(1ull << pin);
}
static void sv_addr(uint16_t addr) {
    for (unsigned i = 0; i < 14; i++) sv_pin(PIN_A0 + i, (addr >> i) & 1u);
    sv_pin(PIN_A14, (addr >> 14) & 1u);
    sv_pin(PIN_A15, (addr >> 15) & 1u);
}
static void sv_data(uint8_t v) {
    for (unsigned i = 0; i < 8; i++) sv_pin(PIN_D0 + i, (v >> i) & 1u);
}

// One read cycle as the CGB drives it: CLK rises with A15 high, then the address,
// the A15 strobe and /RD arrive together, CLK falls, CLK rises again.
static void sv_read_cycle(uint16_t addr, bool rd_low) {
    sv_pin(PIN_CLK, 1); sv_pin(PIN_NRD, 1); sv_pin(PIN_A15, 1); sv_run(8);
    sv_addr(addr); sv_pin(PIN_NRD, !rd_low); sv_run(4);
    sv_pin(PIN_CLK, 0); sv_run(44);
    sv_pin(PIN_CLK, 1); sv_run(16);
}

// One write cycle: /WR falls mid-cycle, the data byte arrives after it, as measured on the bench.
static void sv_write_cycle(uint16_t addr, uint8_t v, uint8_t pre) {
    sv_pin(PIN_CLK, 1); sv_pin(PIN_NWR, 1); sv_pin(PIN_A15, 1); sv_run(8);
    sv_addr(addr); sv_data(pre); sv_run(4);
    sv_pin(PIN_CLK, 0); sv_run(4);
    sv_pin(PIN_NWR, 0); sv_run(4);
    sv_data(v);    sv_run(24);
    sv_pin(PIN_NWR, 1); sv_pin(PIN_CLK, 1); sv_run(8);
}

static uint32_t sv_wcap_word(uint16_t addr, uint8_t v) {
    return (uint32_t)((addr >> 14) & 1u)
         | (uint32_t)(((addr >> 15) & 1u) << 1)
         | ((uint32_t)v << 2)
         | ((uint32_t)(addr & 0x3fff) << 12);
}

static void test_serve_meta(void) {
    // Side-set steals from the delay field, so a program growing one silently changes every [n].
    CHECK(A15_DETECT_SIDESET == 0 && ROM_LOW_SIDESET == 0 && ROM_HIGH_SIDESET == 0 &&
          SERVE_DATA_SIDESET == 0 && WRITE_CAPTURE_SIDESET == 0,
          "a serve program grew a side-set; its delay field is no longer 5 bits");
    CHECK(A15_DETECT_WRAP == 9u && ROM_LOW_WRAP == 2u && SERVE_DATA_WRAP == 7u &&
          WRITE_CAPTURE_WRAP == 4u, "serve.pio wrap bounds moved");
    // cartserve.c's a15_detect_timed() rewrites the delay of the word at `hdma`: it must be that jmp.
    CHECK((a15_detect_insns[A15_DETECT_HDMA] & 0xe0e0u) == 0u
          && (a15_detect_insns[A15_DETECT_HDMA] & 0x1fu) == 8u,
          "a15_detect's hdma word is not `jmp cont`");
    // serve.pio's `.define PIN_CLK/PIN_NWR/PIN_A15` are its only literal pins: check the assembled words.
    unsigned waits = 0;
    for (unsigned i = 0; i < sizeof a15_detect_insns / sizeof a15_detect_insns[0]; i++)
        if ((a15_detect_insns[i] & 0xe060u) == 0x2000u) waits |= 1u << (a15_detect_insns[i] & 0x1fu);
    CHECK(waits == ((1u << PIN_CLK) | (1u << PIN_A15)),
          "a15_detect waits on gpio mask %x, want CLK and A15 (%x)", waits,
          (1u << PIN_CLK) | (1u << PIN_A15));
    CHECK((serve_data_insns[4] & 0x1fu) == PIN_CLK, "serve_data waits on the wrong gpio");
    CHECK((write_capture_insns[0] & 0x1fu) == PIN_NWR,
          "write_capture waits on gpio %u, PIN_NWR is %u", write_capture_insns[0] & 0x1fu, PIN_NWR);
}

static void test_serve_wcap(void) {
    // The exact word run_bus() and cs_wcap_decode() decode: bit0 A14, bit1 A15, bits2-9 D0-D7, bits12-25 A0-A13.
    sv_reset();
    sv_write_cycle(0x6003, 0x5a, 0xff);
    CHECK(sv_count(&g_sv.rx[SV_WCAP]) == 1, "one write, %u captured", sv_count(&g_sv.rx[SV_WCAP]));
    uint32_t w = 0;
    sv_pop(&g_sv.rx[SV_WCAP], &w);
    CHECK(w == sv_wcap_word(0x6003, 0x5a), "wcap word %08x, want %08x", w, sv_wcap_word(0x6003, 0x5a));
    CHECK((w & 1u) == 1u, "A14 is not bit 0");
    CHECK((w & 2u) == 0u, "A15 is not bit 1");
    CHECK(((w >> 2) & 0xffu) == 0x5a, "D0-D7 are not bits 2-9");
    CHECK(((w >> 12) & 0x3fffu) == 0x2003, "A0-A13 are not bits 12-25");

    // GP16/GP17 land in bits 10/11 — the two-pin gap pinmap.h calls load-bearing.
    sv_reset();
    sv_pin(PIN_RST, 1);
    sv_pin(17, 1);
    sv_write_cycle(0x6000, 0x00, 0x00);
    sv_pop(&g_sv.rx[SV_WCAP], &w);
    CHECK((w & 0x0c00u) == 0x0c00u, "GP16/GP17 are not bits 10/11 (word %08x)", w);
    CHECK(((w >> 12) & 0x3fffu) == 0x2000, "the GP16/GP17 gap moved A0 off bit 12");
}

static void test_serve_wcap_gates(void) {
    // The A15 gate is the program's `jmp pin wstart`, and it is what keeps VRAM/WRAM spam out.
    sv_reset();
    sv_write_cycle(0x9800, 0x42, 0x00);
    sv_write_cycle(0xc000, 0x42, 0x00);
    CHECK(sv_count(&g_sv.rx[SV_WCAP]) == 0, "A15-high writes reached the FIFO");
    sv_write_cycle(0x2000, 0x42, 0x00);
    CHECK(sv_count(&g_sv.rx[SV_WCAP]) == 1, "the A15 gate swallowed a cart write");

    // WR_SETTLE: the byte latched is the one present 8 cycles after /WR falls, not at the edge.
    sv_reset();
    sv_write_cycle(0x6000, 0xa5, 0x3c);
    uint32_t w = 0;
    sv_pop(&g_sv.rx[SV_WCAP], &w);
    CHECK(((w >> 2) & 0xffu) == 0xa5, "latched %02x, want the post-settle byte a5", (w >> 2) & 0xffu);

    // Autopush into a full RX FIFO stalls the SM at the `in`; it does not drop the word.
    sv_reset();
    for (unsigned i = 0; i < 8; i++) sv_write_cycle(0x6000, (uint8_t)i, 0);
    CHECK(sv_count(&g_sv.rx[SV_WCAP]) == 8, "FIFO depth is not 8");
    sv_write_cycle(0x6000, 0xee, 0);
    CHECK(g_sv.sm[SV_WCAP].push_pending, "a 9th write did not stall write_capture");
    uint32_t first = 0;
    sv_pop(&g_sv.rx[SV_WCAP], &first);
    sv_run(4);
    CHECK(!g_sv.sm[SV_WCAP].push_pending && sv_count(&g_sv.rx[SV_WCAP]) == 8,
          "draining one word did not let the stalled push through");
}

// bank_sel: a $2000-$3FFF write, and nothing else, pushes the written byte already shifted to an
// 8-byte table stride.
static void test_bank_sel(void) {
    sv_reset();
    sv_write_cycle(0x2000, 0x5a, 0xff);
    CHECK(sv_count(&g_sv.rx[SV_BSEL]) == 1, "a $2000 select did not reach the FIFO");
    uint32_t w = 0;
    sv_pop(&g_sv.rx[SV_BSEL], &w);
    CHECK(w == 0x5au << 3, "bank_sel pushed %08x, want the byte at stride 8 (%08x)", w, 0x5au << 3);

    // Nothing else may reach it. $A000-$BFFF matters most: it shares A13 with $2000 and a game
    // writes it constantly, so only the A14/A15 test keeps save RAM out of the bank table.
    sv_reset();
    const uint16_t k_quiet[] = { 0x0000, 0x1fff, 0x4000, 0x5fff, 0x6000, 0x7fff,
                                 0x8000, 0x9800, 0xa000, 0xbfff, 0xc000, 0xffff };
    for (unsigned i = 0; i < sizeof k_quiet / sizeof *k_quiet; i++) {
        sv_write_cycle(k_quiet[i], 0x42, 0x00);
        CHECK(sv_count(&g_sv.rx[SV_BSEL]) == 0, "a write to $%04x armed the bank table", k_quiet[i]);
    }

    // The whole $2000-$3FFF range does select, MBC5's $3000 high-bit register included — which is
    // why that register is the documented hole in a byte-indexed table.
    sv_reset();
    sv_write_cycle(0x3fff, 0xff, 0x00);
    CHECK(sv_count(&g_sv.rx[SV_BSEL]) == 1 && (sv_pop(&g_sv.rx[SV_BSEL], &w), w == 0xffu << 3),
          "the top of the select range pushed %08x", w);

    // Same WR_SETTLE as write_capture: the byte latched is the settled one, not the one on the
    // bus at the edge. A mount off the pre-settle byte would be a jump into the wrong bank.
    sv_reset();
    sv_write_cycle(0x2100, 0xa5, 0x3c);
    sv_pop(&g_sv.rx[SV_BSEL], &w);
    CHECK(w == 0xa5u << 3, "bank_sel latched the pre-settle byte (%08x)", w);
}

// test/read_addr.pio: write_capture's word layout, on read cycles instead.
static void test_read_addr(void) {
    sv_reset();
    sv_read_cycle(0x4123, true);
    CHECK(sv_count(&g_sv.rx[SV_RADDR]) == 1, "a read cycle did not reach the sampler");
    uint32_t w = 0;
    sv_pop(&g_sv.rx[SV_RADDR], &w);
    CHECK(((w >> 12) & 0x3fffu) == 0x0123 && (w & 1u) == 1u && (w & 2u) == 0u,
          "sampler word %08x is not $4123", w);

    sv_read_cycle(0x0040, true);
    sv_pop(&g_sv.rx[SV_RADDR], &w);
    CHECK(((w >> 12) & 0x3fffu) == 0x0040 && (w & 3u) == 0u, "sampler word %08x is not $0040", w);

    // A write cycle is not a read: /RD stays high and the sampler must ignore it, or the resume
    // address would be taken from the very write that armed the trap.
    sv_reset();
    sv_write_cycle(0x2000, 0x01, 0x00);
    CHECK(sv_count(&g_sv.rx[SV_RADDR]) == 0, "the sampler latched a write cycle");
}

static void test_serve_region(void) {
    // A15 high serves nothing at all; A14 picks the ring.
    sv_reset();
    sv_read_cycle(0x8000, true);
    CHECK(g_sv.irq == 0, "a15_detect fired an IRQ for $8000+");
    CHECK(sv_count(&g_sv.rx[SV_LOW]) == 0 && sv_count(&g_sv.rx[SV_HIGH]) == 0,
          "a responder answered an internal-space read");

    sv_reset();
    sv_read_cycle(0x1234, true);
    CHECK(sv_count(&g_sv.rx[SV_LOW]) == 1 && sv_count(&g_sv.rx[SV_HIGH]) == 0,
          "$1234 did not route to the low ring alone");
    uint32_t w = 0;
    sv_pop(&g_sv.rx[SV_LOW], &w);
    CHECK(w == 0x1234, "low ring pushed %04x, want the 14-bit wire address 1234", w);

    sv_reset();
    sv_read_cycle(0x5678, true);
    CHECK(sv_count(&g_sv.rx[SV_HIGH]) == 1 && sv_count(&g_sv.rx[SV_LOW]) == 0,
          "$5678 did not route to the high ring alone");
    sv_pop(&g_sv.rx[SV_HIGH], &w);
    CHECK(w == 0x1678, "high ring pushed %04x, want 1678 (A14 is the region, not an address bit)", w);

    // `wait 1 irq 4` clears the flag on satisfy: one region pulse serves exactly one cycle.
    CHECK(g_sv.irq == 0, "an IRQ flag survived its responder (irq=%02x)", g_sv.irq);
}

static void test_serve_rd_gate(void) {
    // /RD high is a write cycle: a15_detect still fires, the responder consumes the flag and
    // pushes nothing, so serve_data never gets a byte and never drives the bus.
    sv_reset();
    sv_read_cycle(0x1234, false);
    CHECK(sv_count(&g_sv.rx[SV_LOW]) == 0, "the /RD gate let a write cycle into the ring");
    CHECK(g_sv.irq == 0, "the responder did not consume irq 4 on a gated cycle");
    CHECK(g_sv.sm[SV_LOW].insns > 0, "rom_low never ran");
}

static void test_serve_data(void) {
    // Drive: the byte is on D0-D7 one cycle before the pindirs go output, and stays across CLK low.
    sv_reset();
    sv_push(&g_sv.tx[SV_DATA], 0x3c);
    sv_pin(PIN_CLK, 0);
    sv_run(6);
    const uint64_t dmask = 0xffull << PIN_D0;
    CHECK((g_sv.pio_dir & dmask) == dmask, "serve_data did not drive D0-D7");
    CHECK(((g_sv.pio_lvl >> PIN_D0) & 0xffu) == 0x3c,
          "D0-D7 carry %02x, want 3c", (unsigned)((g_sv.pio_lvl >> PIN_D0) & 0xffu));

    sv_run(30);
    CHECK((g_sv.pio_dir & dmask) == dmask, "D0-D7 released during the CLK low phase");

    // Release: after the rise plus the [3] delay, the pins go back to inputs.
    sv_pin(PIN_CLK, 1);
    sv_run(8);
    CHECK((g_sv.pio_dir & dmask) == 0, "D0-D7 were not released after the CLK rise");

    // Underrun: with nothing in the TX FIFO the SM parks at `pull` and drives nothing at all.
    sv_reset();
    sv_pin(PIN_CLK, 0);
    sv_run(64);
    CHECK((g_sv.pio_dir & dmask) == 0, "serve_data drove the bus with an empty TX FIFO");
    CHECK(g_sv.sm[SV_DATA].pc == SERVE_DATA_WRAP_TARGET, "serve_data is not parked at its pull");
}

// Cycles from the A15 strobe to a byte on D0-D7 with the ring modeled as free; the bench measured
// 25 with the DMA in.
#define SV_CHAIN 9u
static unsigned sv_chain_cycles(uint16_t addr) {
    const uint64_t dmask = 0xffull << PIN_D0;
    sv_reset();
    sv_pin(PIN_NRD, 0);
    sv_run(8);                                  // a15_detect parks on the strobe
    sv_addr(addr);
    for (unsigned c = 1; c <= 64; c++) {
        sv_run(1);
        uint32_t w;
        if (sv_pop(&g_sv.rx[SV_LOW], &w)) sv_push(&g_sv.tx[SV_DATA], 0x3c);
        if ((g_sv.pio_dir & dmask) == dmask) return c;
    }
    return 0;
}

static void test_serve_chain(void) {
    const unsigned c = sv_chain_cycles(0x0104);
    // Not a timing result — a cycle count for the assembled programs. It moves when serve.pio
    // changes, which is the point: the detect's and the responder's shapes are both in it.
    CHECK(c == SV_CHAIN, "serve chain is %u cycles from the A15 strobe, expected %u", c, SV_CHAIN);
}

// An HDMA read holds A15 low through the CLK rise; the timed path must still route it by A14.
static void test_serve_hdma(void) {
    sv_reset();
    sv_addr(0x5678);
    sv_pin(PIN_NRD, 0);
    sv_pin(PIN_CLK, 0); sv_run(8);
    sv_pin(PIN_CLK, 1); sv_run(64);
    CHECK(sv_count(&g_sv.rx[SV_HIGH]) == 1 && sv_count(&g_sv.rx[SV_LOW]) == 0,
          "an HDMA read at $5678 did not reach the high ring alone");
    uint32_t w = 0;
    sv_pop(&g_sv.rx[SV_HIGH], &w);
    CHECK(w == 0x1678, "HDMA read pushed %04x, want 1678", w);
}

void tests_serve(void) {
    test_serve_meta();
    test_serve_chain();
    test_serve_hdma();
    test_serve_wcap();
    test_serve_wcap_gates();
    test_serve_region();
    test_serve_rd_gate();
    test_serve_data();
    test_bank_sel();
    test_read_addr();
}
