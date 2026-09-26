// Host tests for the capture wire encoder: the delta-RLE codec, the /WR-stream merge, and the capstream PIO wire model.
#include "test.h"
#include "capout_insns.h"
#include "capout_model.h"
#include "capenc.h"
#include "pinmap.h"

// Minimal decoder for wire format v1. Test-only: the product never decodes, and the receiver that
// does is demos/capture/tools/capenc.py.
typedef struct { uint16_t addr; uint8_t data, strobes; bool has_data, rst; } dec_ev_t;

static unsigned capenc_decode(const uint8_t *s, unsigned n, dec_ev_t *out, unsigned cap) {
    unsigned ne = 0, i = 0;
    while (i + CAPENC_HDR <= n) {
        if (memcmp(s + i, "CPKT", 4)) return ne;
        unsigned plen = (unsigned)s[i + 8] | ((unsigned)s[i + 9] << 8);
        unsigned flags = (unsigned)s[i + 10] | ((unsigned)s[i + 11] << 8);
        const uint8_t *p = s + i + CAPENC_HDR;
        uint32_t want = capenc_crc32(0, s + i, CAPENC_HDR + plen);
        uint32_t got = (uint32_t)p[plen] | ((uint32_t)p[plen + 1] << 8)
                     | ((uint32_t)p[plen + 2] << 16) | ((uint32_t)p[plen + 3] << 24);
        CHECK(want == got, "packet crc %08x != %08x", (unsigned)want, (unsigned)got);
        bool rst = (flags & 2u) != 0;
        int32_t prev = -1;
        unsigned j = 0;
        while (j < plen && ne < cap) {
            uint8_t tok = p[j++];
            uint32_t run = (uint32_t)(tok >> 5);
            if (run == 7u) {
                run = 0; unsigned sh = 0; uint8_t b;
                do { b = p[j++]; run |= (uint32_t)(b & 0x7Fu) << sh; sh += 7; } while (b & 0x80u);
            }
            uint16_t addr;
            if (tok & 0x08u) { addr = (uint16_t)(p[j] | (p[j + 1] << 8)); j += 2; }
            else { int8_t d = (int8_t)p[j++]; addr = (uint16_t)(prev + d); }
            uint8_t data = 0;
            if (tok & 0x10u) data = p[j++];
            for (uint32_t k = 0; k <= run && ne < cap; k++) {
                out[ne].addr = (uint16_t)(addr + k);
                out[ne].data = k ? 0 : data;
                out[ne].strobes = tok & 0x07u;
                out[ne].has_data = k ? false : ((tok & 0x10u) != 0);
                out[ne].rst = rst;
                ne++;
            }
            prev = (int32_t)(uint16_t)(addr + run);
        }
        i += CAPENC_HDR + plen + 4;
    }
    return ne;
}

#define CAPBUF 8192
static uint8_t g_cap_stream[CAPBUF];
static unsigned g_cap_len;
static void cap_sink(const uint8_t *p, size_t n, void *ctx) {
    (void)ctx;
    if (g_cap_len + n <= sizeof g_cap_stream) { memcpy(g_cap_stream + g_cap_len, p, n); g_cap_len += n; }
}

// One raw capture word, built the way capture.pio samples the bus. Mirrors capfile.encode().
static uint32_t mkword(uint16_t addr, uint8_t data, bool wr, bool rd, bool cs, bool rst) {
    uint32_t w = (uint32_t)(addr & 0x3FFFu) << PIN_A0;
    w |= (uint32_t)((addr >> 14) & 1u) << PIN_A14;
    w |= (uint32_t)((addr >> 15) & 1u) << PIN_A15;
    w |= (uint32_t)data << PIN_D0;
    if (!wr) w |= 1u << PIN_NWR;
    if (!rd) w |= 1u << PIN_NRD;
    if (!cs) w |= 1u << PIN_NCS;
    if (rst) w |= 1u << PIN_RST;
    return w;
}

static void test_capenc(void) {
    // Sequential run, an address jump past the 1-byte delta, a long run past the 3-bit escape,
    // a cart-space write carrying data, and the +/-127 delta boundary in both directions.
    struct { uint16_t a; bool wr; uint8_t d; } seq[] = {
        {0x0100,0,0}, {0x0101,0,0}, {0x0102,0,0},          // run of 3
        {0x8400,0,0},                                       // internal cycle: far jump
        {0x0103,0,0},
        {0x0183,0,0},                                       // +128: just past the signed delta
        {0x0182,0,0},                                       // -1
        {0x6000,1,0x5a},                                    // cart-space write, carries data
    };
    dec_ev_t ev[64];
    g_cap_len = 0;
    capenc_t e;
    capenc_init(&e, cap_sink, NULL);
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0]; i++)
        capenc_word(&e, mkword(seq[i].a, seq[i].d, seq[i].wr, !seq[i].wr, false, true));
    for (unsigned i = 0; i < 40; i++) capenc_word(&e, mkword((uint16_t)(0x4000 + i), 0, 0, 1, 0, 1));
    capenc_flush(&e);

    unsigned n = capenc_decode(g_cap_stream, g_cap_len, ev, 64);
    CHECK(n == sizeof seq / sizeof seq[0] + 40, "decoded %u events, want %zu",
          n, sizeof seq / sizeof seq[0] + 40);
    for (unsigned i = 0; i < sizeof seq / sizeof seq[0] && i < n; i++) {
        CHECK(ev[i].addr == seq[i].a, "event %u addr %04x want %04x", i, ev[i].addr, seq[i].a);
        CHECK(ev[i].has_data == seq[i].wr, "event %u has_data %d", i, ev[i].has_data);
        if (seq[i].wr) CHECK(ev[i].data == seq[i].d, "event %u data %02x", i, ev[i].data);
    }
    for (unsigned i = 0; i < 40; i++) {
        unsigned k = sizeof seq / sizeof seq[0] + i;
        if (k < n) CHECK(ev[k].addr == (uint16_t)(0x4000 + i), "run event %u addr %04x", i, ev[k].addr);
    }
    // A packet must be self-contained: its first record carries an absolute address.
    CHECK((g_cap_stream[CAPENC_HDR] & 0x08u) != 0, "first record of a packet is not absolute");
}

// capfile.merge()'s rule on-chip: a /WR-latched word claims the next main-stream event at its
// address with /RD high, so interleaved streams must encode exactly like a pre-merged one.
static void test_capmrg(void) {
    // The last pair is cart RAM's `or [hl] / ld [hl],a`: the read must not take the write.
    struct { uint16_t a; bool wr; uint8_t d; } t[] = {
        {0x0100,0,0}, {0x0101,0,0}, {0x6000,1,0x5a}, {0x0102,0,0},
        {0x6001,1,0x01}, {0x0103,0,0}, {0x0104,0,0}, {0x6000,1,0xff},
        {0x0105,0,0}, {0xac60,0,0}, {0x0106,0,0}, {0xac60,1,0x42},
    };
    const unsigned N = sizeof t / sizeof t[0];

    // capfile.merge() sets wr/data/cs and leaves /RD as sampled: high on every write in the corpus.
    g_cap_len = 0;
    capenc_t e;
    capenc_init(&e, cap_sink, NULL);
    for (unsigned i = 0; i < N; i++)
        capenc_word(&e, mkword(t[i].a, t[i].d, t[i].wr, !t[i].wr, false, true));
    capenc_flush(&e);
    unsigned ref_len = g_cap_len;
    static uint8_t ref[CAPBUF];
    memcpy(ref, g_cap_stream, ref_len);

    // bus_capture never sees /WR: a write shows no strobe and stale data, and its value arrives on
    // the /WR stream, possibly ahead of a read of the same address.
    g_cap_len = 0;
    capenc_init(&e, cap_sink, NULL);
    capmrg_t m;
    capmrg_init(&m);
    bool pushed[sizeof t / sizeof t[0]] = { 0 };
    for (unsigned i = 0; i < N; i++) {
        for (unsigned j = i; j < N && j <= i + 2; j++) {
            if (!t[j].wr || pushed[j] || (j > i && t[j].a != t[i].a)) continue;
            capmrg_push_wr(&m, mkword(t[j].a, t[j].d, true, false, false, true));
            pushed[j] = true;
        }
        uint32_t main_w = mkword(t[i].a, t[i].wr ? 0xAA : t[i].d, false, !t[i].wr, false, true);
        capenc_word(&e, capmrg_apply(&m, main_w));
    }
    capenc_flush(&e);

    CHECK(g_cap_len == ref_len, "merged stream is %u bytes, pre-merged is %u", g_cap_len, ref_len);
    CHECK(g_cap_len == ref_len && memcmp(ref, g_cap_stream, ref_len) == 0,
          "on-chip merge did not reproduce the pre-merged encoding");
    CHECK(m.lost == 0, "%lu write words lost", (unsigned long)m.lost);

    // a write with no matching main-stream event must not consume a later one
    capmrg_init(&m);
    capmrg_push_wr(&m, mkword(0x7fff, 0x11, true, false, false, true));
    uint32_t w = mkword(0x0200, 0, false, true, false, true);
    CHECK(capmrg_apply(&m, w) == w, "unmatched write word altered an unrelated event");

    // the queue must evict rather than stall, and say so
    capmrg_init(&m);
    for (unsigned i = 0; i < CAPMRG_Q + 3; i++)
        capmrg_push_wr(&m, mkword((uint16_t)(0x6000 + i), (uint8_t)i, true, false, false, true));
    CHECK(m.lost == 3, "queue overflow lost %lu, want 3", (unsigned long)m.lost);
}

static void test_capenc_dropped(void) {
    g_cap_len = 0;
    capenc_t e;
    capenc_init(&e, cap_sink, NULL);
    capenc_drop(&e, 7);
    for (unsigned i = 0; i < 8; i++)
        capenc_word(&e, mkword((uint16_t)(0x300 + i * 64), 0, false, true, false, true));
    capenc_flush(&e);
    uint32_t d = (uint32_t)g_cap_stream[12] | ((uint32_t)g_cap_stream[13] << 8)
               | ((uint32_t)g_cap_stream[14] << 16) | ((uint32_t)g_cap_stream[15] << 24);
    CHECK(d == 7, "dropped field is %lu, want 7", (unsigned long)d);
    dec_ev_t ev[32];
    CHECK(capenc_decode(g_cap_stream, g_cap_len, ev, 32) == 8, "dropped broke the decode");
}

static void test_capout_wire(void) {
    static capout_cycle_t cyc[16384];
    uint8_t src[256], got[256];
    for (unsigned i = 0; i < sizeof src; i++) src[i] = (uint8_t)(i * 7u + 3u);
    uint32_t words[sizeof src / 4];
    for (unsigned i = 0; i < sizeof words / 4; i++)
        words[i] = (uint32_t)src[4*i] | ((uint32_t)src[4*i+1] << 8)
                 | ((uint32_t)src[4*i+2] << 16) | ((uint32_t)src[4*i+3] << 24);

    unsigned n = capout_sim(capstream_insns, sizeof capstream_insns / sizeof capstream_insns[0],
                            words, sizeof words / 4, cyc, sizeof cyc / sizeof cyc[0]);
    CHECK(n == 2u * 2u * sizeof src, "simulated %u cycles, want %zu", n, 4 * sizeof src);

    unsigned first_bad = 0;
    CHECK(capout_setup_ok(cyc, n, &first_bad),
          "data moved on the sampling edge at cycle %u — side-set values are swapped", first_bad);

    unsigned nb = capout_receive(cyc, n, got, sizeof got);
    CHECK(nb == sizeof src, "recovered %u bytes, want %zu", nb, sizeof src);
    CHECK(memcmp(src, got, sizeof src) == 0, "byte stream did not survive the wire model");
}

void tests_capenc(void) { test_capenc(); test_capenc_dropped(); test_capmrg(); test_capout_wire(); }
