#include "capenc.h"
#include "pinmap.h"
#include <string.h>

// A0-A13 arrive in wire order; a bit loop measured 171 cycles/event against a 71-cycle budget, so
// two 128-entry tables un-permute in two loads.
static const uint8_t k_wire_pos[14] = A0_13_WIRE_POS;
static uint16_t k_perm_lo[128], k_perm_hi[128];
static bool k_perm_ready;

// Split by source (wire) bit, not destination: a low address bit need not land on a low wire bit.
static void perm_build(void) {
    for (uint32_t v = 0; v < 128; v++) {
        uint16_t lo = 0, hi = 0;
        for (int i = 0; i < 14; i++) {
            uint8_t src = k_wire_pos[i];
            if (src < 7) {
                if ((v >> src) & 1u) lo |= (uint16_t)(1u << i);
            } else {
                if ((v >> (src - 7)) & 1u) hi |= (uint16_t)(1u << i);
            }
        }
        k_perm_lo[v] = lo;
        k_perm_hi[v] = hi;
    }
    k_perm_ready = true;
}

#define T_RD        (1u << 0)
#define T_WR        (1u << 1)
#define T_CS        (1u << 2)
#define T_ADDR_WIDE (1u << 3)
#define T_HAS_DATA  (1u << 4)
#define RUN_SHIFT   5
#define RUN_ESC     7

_Static_assert(PIN_NRD == PIN_NWR + 1 && PIN_NCS == PIN_NWR + 2,
               "k_strobe indexes /WR,/RD,/CS as one contiguous field");
// index bit0 = /WR, bit1 = /RD, bit2 = /CS, all active low -> asserted-true T_* bits
static const uint8_t k_strobe[8] = {
    T_WR | T_RD | T_CS, T_RD | T_CS, T_WR | T_CS, T_CS,
    T_WR | T_RD,        T_RD,        T_WR,        0,
};

static uint32_t k_crc_tab[256];
static bool k_crc_ready;

static void crc_build(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
        k_crc_tab[i] = c;
    }
    k_crc_ready = true;
}

uint32_t capenc_crc32(uint32_t crc, const uint8_t *p, size_t n) {
    if (!k_crc_ready) crc_build();
    crc = ~crc;
    while (n--) crc = k_crc_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

void capenc_init(capenc_t *e, capenc_sink_t sink, void *ctx) {
    memset(e, 0, sizeof *e);
    e->prev_addr = -1;
    e->first = true;
    e->sink = sink;
    e->ctx = ctx;
    if (!k_crc_ready) crc_build();
    if (!k_perm_ready) perm_build();
}

void capenc_drop(capenc_t *e, uint32_t n) { e->dropped += n; }

// Cart-space writes, ROM and cart RAM: the only data the bus really carries.
static inline bool cart_space(uint16_t a) { return a < 0x8000 || (a >= 0xA000 && a < 0xC000); }

static inline __attribute__((always_inline)) uint16_t word_addr(uint32_t w) {
    uint32_t f = (w >> PIN_A0) & 0x3FFFu;
    uint16_t lo = (uint16_t)(k_perm_lo[f & 0x7Fu] | k_perm_hi[(f >> 7) & 0x7Fu]);
    return (uint16_t)(lo | (((w >> PIN_A14) & 1u) << 14) | (((w >> PIN_A15) & 1u) << 15));
}

void capmrg_init(capmrg_t *m) {
    memset(m, 0, sizeof *m);
    if (!k_perm_ready) perm_build();
}

void capmrg_push_wr(capmrg_t *m, uint32_t w) {
    if (m->count == CAPMRG_Q) {          // oldest write loses its home rather than stalling capture
        m->head = (uint8_t)((m->head + 1u) % CAPMRG_Q);
        m->count--;
        m->lost++;
    }
    m->q[(m->head + m->count) % CAPMRG_Q] = w;
    m->count++;
}

uint32_t capmrg_apply(capmrg_t *m, uint32_t main_word) {
    if (!m->count) return main_word;
    // A read is never the write's cycle: `or [hl] / ld [hl],a` on cart RAM reads the address first.
    if (!(main_word & (1u << PIN_NRD))) return main_word;
    uint32_t wr = m->q[m->head];
    if (word_addr(wr) != word_addr(main_word)) return main_word;
    m->head = (uint8_t)((m->head + 1u) % CAPMRG_Q);
    m->count--;
    // adopt the write's data and /CS, and assert /WR (active low) on the merged word
    main_word &= ~((0xFFu << PIN_D0) | (1u << PIN_NWR) | (1u << PIN_NCS));
    main_word |= (wr & (0xFFu << PIN_D0));
    main_word |= (wr & (1u << PIN_NCS));
    return main_word;
}

static void pkt_emit(capenc_t *e) {
    if (!e->len) return;
    uint16_t flags = (uint16_t)((e->first ? CAPENC_F_RESET : 0) | (e->pkt_rst ? CAPENC_F_RST : 0));
    memcpy(e->pkt, "CPKT", 4);
    put32(e->pkt + 4, e->seq);
    e->pkt[8] = (uint8_t)e->len; e->pkt[9] = (uint8_t)(e->len >> 8);
    e->pkt[10] = (uint8_t)flags; e->pkt[11] = (uint8_t)(flags >> 8);
    put32(e->pkt + 12, e->dropped);
    e->dropped = 0;
    size_t body = CAPENC_HDR + e->len;
    put32(e->pkt + body, capenc_crc32(0, e->pkt, body));
    e->sink(e->pkt, body + 4, e->ctx);
    e->seq++;
    e->len = 0;
    e->prev_addr = -1;                      // every packet is self-contained
    e->first = false;
}

// Serialize the pending record into buf, given prev_addr. Returns its length.
static size_t rec_build(const capenc_t *e, int32_t prev_addr, uint8_t *buf) {
    size_t n = 0;
    bool wide = true;
    int32_t d = 0;
    if (prev_addr >= 0) {
        d = (int32_t)((uint16_t)(e->p_addr - (uint16_t)prev_addr));
        if (d >= 0x8000) d -= 0x10000;
        wide = !(d >= -128 && d <= 127);
    }
    uint8_t token = (uint8_t)(e->p_strobes
                              | (e->p_has_data ? T_HAS_DATA : 0)
                              | (wide ? T_ADDR_WIDE : 0)
                              | (uint8_t)((e->p_run < RUN_ESC ? e->p_run : RUN_ESC) << RUN_SHIFT));
    buf[n++] = token;
    if (e->p_run >= RUN_ESC) {
        uint32_t v = e->p_run;
        do {
            uint8_t b = v & 0x7F;
            v >>= 7;
            buf[n++] = (uint8_t)(b | (v ? 0x80 : 0));
        } while (v);
    }
    if (wide) {
        buf[n++] = (uint8_t)e->p_addr;
        buf[n++] = (uint8_t)(e->p_addr >> 8);
    } else {
        buf[n++] = (uint8_t)d;
    }
    if (e->p_has_data) buf[n++] = e->p_data;
    return n;
}

static void rec_commit(capenc_t *e) {
    if (!e->have) return;
    if (e->len && e->p_rst != e->pkt_rst) pkt_emit(e);      // /RST rides the header
    if (!e->len) e->pkt_rst = e->p_rst;

    uint8_t buf[16];
    size_t n = rec_build(e, e->prev_addr, buf);
    if (e->len && e->len + n > CAPENC_PAYLOAD_MAX) {
        pkt_emit(e);
        e->pkt_rst = e->p_rst;
        n = rec_build(e, -1, buf);                          // rebuilt absolute for the new packet
    }
    uint8_t *dst = e->pkt + CAPENC_HDR + e->len; // n is 3-6 in practice; memcpy() calls out for it
    for (size_t k = 0; k < n; k++) dst[k] = buf[k];
    e->len += n;
    e->prev_addr = (int32_t)(uint16_t)(e->p_addr + e->p_run);
    e->have = false;
}

// Keeps the pending record in registers across a run: per-event struct traffic alone cost 72 of
// the 71-cycle budget.
void capenc_words(capenc_t *e, const uint32_t *ws, size_t n) {
    bool     have  = e->have;
    uint16_t p_add = e->p_addr;
    uint8_t  p_str = e->p_strobes, p_dat = e->p_data;
    bool     p_hd  = e->p_has_data, p_rst = e->p_rst;
    uint32_t p_run = e->p_run;

    for (size_t i = 0; i < n; i++) {
        uint32_t w = ws[i];
        uint16_t addr = word_addr(w);
        // /WR, /RD and /CS are GP2..GP4: one field, one load for all three asserted-true bits.
        uint8_t strobes = k_strobe[(w >> PIN_NWR) & 7u];
        bool rst = ((w >> PIN_RST) & 1u) != 0;
        bool has_data = (strobes & T_WR) && cart_space(addr);

        if (have && !has_data && strobes == p_str && rst == p_rst
            && addr == (uint16_t)(p_add + p_run + 1)) {
            p_run++;
            continue;
        }
        e->have = have; e->p_addr = p_add; e->p_strobes = p_str; e->p_data = p_dat;
        e->p_has_data = p_hd; e->p_rst = p_rst; e->p_run = p_run;
        rec_commit(e);
        have = true; p_add = addr; p_str = strobes; p_hd = has_data; p_rst = rst; p_run = 0;
        p_dat = (uint8_t)((w >> PIN_D0) & 0xFF);
    }
    e->have = have; e->p_addr = p_add; e->p_strobes = p_str; e->p_data = p_dat;
    e->p_has_data = p_hd; e->p_rst = p_rst; e->p_run = p_run;
}

void capenc_word(capenc_t *e, uint32_t w) { capenc_words(e, &w, 1); }

void capenc_flush(capenc_t *e) {
    rec_commit(e);
    pkt_emit(e);
}
