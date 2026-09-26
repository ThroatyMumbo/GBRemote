// Host tests for the Dreamcast Maple protocol: frame build, mapping, PIO tx/rx timing, capture replay, and VMU block IO.
#include "test.h"
#include "config_map.h"
#include "maple_proto.h"
#include "maple_model.h"
#include "maple_capture.h"
#include "mapledev_insns.h"

// Wire order: maple_tx shifts each FIFO word MSB first, so every 4-byte group leaves reversed.
static unsigned maple_wire_bytes(const uint8_t *frame, unsigned n, uint8_t *wire) {
    uint32_t w[MAPLE_MAX_WORDS];
    unsigned nw = maple_pack_words(frame, n, w);
    for (unsigned i = 0; i < nw; i++)
        for (unsigned b = 0; b < 4; b++)
            wire[i * 4 + b] = (uint8_t)(w[i] >> (24 - 8 * b));
    return n;                   // the bit-pair count stops the SM after exactly n bytes
}

static unsigned maple_make_req(uint8_t *req, uint8_t cmd, uint8_t port, uint32_t func, bool data) {
    req[0] = cmd;
    req[1] = (uint8_t)(MAPLE_ADDR_CONTROLLER | port);
    req[2] = (uint8_t)(MAPLE_ADDR_DC | port);
    req[3] = data ? 1 : 0;
    if (!data) return 4;
    req[4] = (uint8_t)(func >> 24); req[5] = (uint8_t)(func >> 16);
    req[6] = (uint8_t)(func >> 8);  req[7] = (uint8_t)func;
    return 8;
}

// A bare HKT-7700 with empty expansion slots. cond is held by pointer, so a test may keep
// rewriting its own copy after this returns.
static const maple_bus_t *maple_test_bus(maple_cond_t *cond) {
    static maple_bus_t  b;
    static maple_unit_t u;
    maple_bus_init(&b);
    maple_controller_unit(&u, cond);
    maple_bus_add(&b, &u);
    return &b;
}

// The configuration the cart presents: a controller with a Visual Memory in slot 1.
static const maple_bus_t *maple_test_bus_vmu(maple_cond_t *cond) {
    static maple_bus_t  b;
    static maple_unit_t uc, uv;
    static maple_vmu_t  vmu;
    static uint8_t      card[MAPLE_VMU_BLOCKS * MAPLE_VMU_BLOCK];
    maple_bus_init(&b);
    maple_controller_unit(&uc, cond);
    maple_bus_add(&b, &uc);
    maple_vmu_format(card);     // GET_MEDIA_INFO reads the root block, so an unformatted card lies
    maple_vmu_unit(&uv, &vmu, card, MAPLE_SUB_UNIT(1));
    maple_bus_add(&b, &uv);
    return &b;
}

static void test_maple_frame(void) {
    uint8_t req[16], out[MAPLE_MAX_FRAME], wire[MAPLE_MAX_FRAME + 4];
    maple_cond_t c;
    maple_cond_default(&c);
    const maple_bus_t *bus = maple_test_bus(&c);
    c.buttons = (uint16_t)~(MAPLE_BTN_A | MAPLE_BTN_START);
    c.trig_l = 0x11; c.trig_r = 0x22;

    unsigned rn = maple_make_req(req, MAPLE_CMD_GET_CONDITION, 0, MAPLE_FUNC_CONTROLLER, true);
    unsigned n = maple_build_reply(bus, req, rn, out);
    CHECK(n == 4 + 12 + 1, "condition reply is %u bytes, want 17", n);
    CHECK(maple_cmd(out) == MAPLE_CMD_RESP_DATA_XFER, "condition cmd %02x", maple_cmd(out));
    CHECK(maple_dst(out) == MAPLE_ADDR_DC, "condition dst %02x", maple_dst(out));
    CHECK(maple_src(out) == MAPLE_ADDR_CONTROLLER, "condition src %02x", maple_src(out));
    CHECK(maple_nwords(out) == 3, "condition nwords %u", maple_nwords(out));

    // A frame carrying its own checksum XORs to zero, which is exactly the receive-side test.
    CHECK(maple_checksum(out, n) == 0, "frame + checksum != 0");

    // The frame word reaches the wire reversed: nwords, src, dst, cmd.
    maple_wire_bytes(out, n, wire);
    CHECK(wire[0] == 3 && wire[1] == MAPLE_ADDR_CONTROLLER && wire[2] == MAPLE_ADDR_DC
          && wire[3] == MAPLE_CMD_RESP_DATA_XFER,
          "frame word on the wire: %02x %02x %02x %02x", wire[0], wire[1], wire[2], wire[3]);
    // A payload 32-bit field is stored byte-swapped and the word reverses again on the wire, so
    // the function code leads with its low byte. This is what both references put on the bus.
    CHECK(wire[4] == 0x01 && wire[5] == 0x00 && wire[6] == 0x00 && wire[7] == 0x00,
          "function echo on the wire: %02x %02x %02x %02x", wire[4], wire[5], wire[6], wire[7]);
    CHECK(wire[8] == 0x11 && wire[9] == 0x22, "triggers on the wire: %02x %02x", wire[8], wire[9]);
    CHECK(wire[10] == (uint8_t)(c.buttons >> 8) && wire[11] == (uint8_t)c.buttons,
          "buttons on the wire: %02x %02x", wire[10], wire[11]);
    // The checksum is the last byte transmitted, and it is the whole reason the final word is
    // packed MSB-justified rather than sequentially.
    CHECK(wire[n - 1] == out[n - 1], "checksum not last on the wire: %02x vs %02x",
          wire[n - 1], out[n - 1]);

    // Port bits are echoed into both address bytes, so they cancel in the XOR and a precomputed
    // checksum survives a port change. That is what lets the fixed replies be built once.
    uint8_t out_b[MAPLE_MAX_FRAME];
    rn = maple_make_req(req, MAPLE_CMD_GET_CONDITION, 0x40, MAPLE_FUNC_CONTROLLER, true);
    unsigned nb = maple_build_reply(bus, req, rn, out_b);
    CHECK(nb == n, "port B changed the reply length");
    CHECK(maple_dst(out_b) == 0x40 && maple_src(out_b) == 0x60,
          "port not echoed: dst %02x src %02x", maple_dst(out_b), maple_src(out_b));
    CHECK(out_b[n - 1] == out[n - 1], "port change moved the checksum");

    // Device info and all-status are fixed sizes the console relies on.
    rn = maple_make_req(req, MAPLE_CMD_DEVICE_REQUEST, 0, 0, false);
    n = maple_build_reply(bus, req, rn, out);
    CHECK(n == 4 + 112 + 1, "device info is %u bytes, want 117", n);
    CHECK(maple_nwords(out) == 28, "device info nwords %u", maple_nwords(out));
    CHECK(maple_data_word(out, 0) == MAPLE_FUNC_CONTROLLER, "device info function code");
    CHECK(maple_data_word(out, 1) == 0x000f06feu, "device info FuncData[0]");
    CHECK(out[4 + 16] == 0xff && out[4 + 17] == 0x00, "area code / connector direction");
    // Both strings are space padded to their full width and never NUL terminated.
    CHECK(out[4 + 18 + 29] == ' ' && out[4 + 48 + 59] == ' ', "name/license not space padded");
    CHECK(maple_checksum(out, n) == 0, "device info checksum");

    rn = maple_make_req(req, MAPLE_CMD_ALL_STATUS_REQUEST, 0, 0, false);
    n = maple_build_reply(bus, req, rn, out);
    CHECK(n == 4 + 192 + 1, "all-status is %u bytes, want 197", n);
    CHECK(maple_nwords(out) == 48, "all-status nwords %u", maple_nwords(out));

    rn = maple_make_req(req, MAPLE_CMD_RESET_DEVICE, 0, 0, false);
    n = maple_build_reply(bus, req, rn, out);
    CHECK(n == 5, "ack is %u bytes, want 5", n);
    CHECK(maple_cmd(out) == MAPLE_CMD_RESP_ACK && maple_nwords(out) == 0, "ack shape");

    // A well-formed request we do not implement is refused, not ignored, as a real HKT-7700 does:
    // BADCMD to GET_MEDIA_INFO for its own function, BADFUNC for a function it lacks.
    rn = maple_make_req(req, MAPLE_CMD_GET_CONDITION, 0, 2 /* storage */, true);
    n = maple_build_reply(bus, req, rn, out);
    CHECK(n == 5 && maple_cmd(out) == MAPLE_RESP_UNKNOWN_FUNC && maple_nwords(out) == 0,
          "foreign function code: %u bytes, cmd %02x", n, maple_cmd(out));
    for (unsigned cmd = 0; cmd < 256; cmd++) {
        if (cmd == MAPLE_CMD_DEVICE_REQUEST || cmd == MAPLE_CMD_ALL_STATUS_REQUEST
            || cmd == MAPLE_CMD_RESET_DEVICE || cmd == MAPLE_CMD_GET_CONDITION) continue;
        rn = maple_make_req(req, (uint8_t)cmd, 0, MAPLE_FUNC_CONTROLLER, true);
        n = maple_build_reply(bus, req, rn, out);
        CHECK(n == 5 && maple_cmd(out) == MAPLE_RESP_UNKNOWN_CMD && maple_nwords(out) == 0,
              "command %02x: %u bytes, cmd %02x", cmd, n, maple_cmd(out));
    }

    // Silence for someone else's frame and for a malformed one; the malformed case is a choice, since
    // the survey only ever sent well-formed requests.
    rn = maple_make_req(req, MAPLE_CMD_GET_CONDITION, 0, MAPLE_FUNC_CONTROLLER, false);
    CHECK(maple_build_reply(bus, req, rn, out) == 0, "replied to GET_CONDITION with no data word");
    rn = maple_make_req(req, MAPLE_CMD_DEVICE_REQUEST, 0, 0, false);
    req[1] = 0x01;                                  // addressed to a sub-peripheral, not to us
    CHECK(maple_build_reply(bus, req, rn, out) == 0, "replied to a sub-peripheral address");
}

static void test_maple_mapping(void) {
    uint8_t cfg[CFG_LEN];
    pad_map_t m;
    memset(cfg, 0xff, sizeof cfg);
    map_compile(cfg, &m);

    const uint16_t absent = MAPLE_BTN_C | MAPLE_BTN_Z | MAPLE_BTN_D;

    for (unsigned gb = 0; gb < 256; gb++) {
        pad_state_t p;
        maple_cond_t c;
        map_apply(&m, (uint8_t)gb, &p);
        maple_from_pad(&p, false, &c);

        // An HKT-7700 has no C, Z or D. Asserting one would advertise a pad we are not.
        CHECK((c.buttons & absent) == absent, "gb %02x asserted a button the HKT-7700 lacks", gb);

        if (gb & PAD_A)     CHECK(!(c.buttons & MAPLE_BTN_A), "gb %02x A missing", gb);
        if (gb & PAD_B)     CHECK(!(c.buttons & MAPLE_BTN_B), "gb %02x B missing", gb);
        if (gb & PAD_START) CHECK(!(c.buttons & MAPLE_BTN_START), "gb %02x START missing", gb);

        // L+R and U+D cancel, exactly as they do for the N64 stick.
        if ((gb & PAD_LEFT) && (gb & PAD_RIGHT))
            CHECK((c.buttons & (MAPLE_BTN_LEFT | MAPLE_BTN_RIGHT)) ==
                  (MAPLE_BTN_LEFT | MAPLE_BTN_RIGHT), "gb %02x L+R did not cancel", gb);
        if ((gb & PAD_UP) && (gb & PAD_DOWN))
            CHECK((c.buttons & (MAPLE_BTN_UP | MAPLE_BTN_DOWN)) ==
                  (MAPLE_BTN_UP | MAPLE_BTN_DOWN), "gb %02x U+D did not cancel", gb);

        // With no analog action the sticks must sit dead center, or every game drifts.
        CHECK(c.joy_x2 == MAPLE_AXIS_CENTER && c.joy_y2 == MAPLE_AXIS_CENTER,
              "gb %02x moved the second stick", gb);
        CHECK(c.joy_x == MAPLE_AXIS_CENTER && c.joy_y == MAPLE_AXIS_CENTER,
              "gb %02x moved the stick in d-pad mode", gb);
    }

    // Nothing pressed is the all-released frame: this is what a stale pad decays to.
    pad_state_t idle;
    maple_cond_t c;
    pad_zero(&idle);
    maple_from_pad(&idle, false, &c);
    CHECK(c.buttons == 0xffff, "released buttons %04x, want ffff", c.buttons);
    CHECK(c.trig_l == 0 && c.trig_r == 0, "released triggers %02x %02x", c.trig_l, c.trig_r);

    // The d-pad reaches the d-pad by default, and the stick only when asked — the inverse of N64.
    pad_state_t p;
    map_apply(&m, PAD_UP, &p);
    maple_from_pad(&p, false, &c);
    CHECK(!(c.buttons & MAPLE_BTN_UP) && c.joy_y == MAPLE_AXIS_CENTER, "default UP -> d-pad");
    maple_from_pad(&p, true, &c);
    CHECK((c.buttons & MAPLE_BTN_UP) && c.joy_y < MAPLE_AXIS_CENTER, "analog UP -> stick up");
    map_apply(&m, PAD_DOWN, &p);
    maple_from_pad(&p, true, &c);
    CHECK(c.joy_y > MAPLE_AXIS_CENTER, "analog DOWN -> stick down");

    // L and R are the analog triggers; the Dreamcast has no digital shoulders.
    map_apply(&m, 0, &p);
    p.buttons = PAD_L | PAD_R;
    maple_from_pad(&p, false, &c);
    CHECK(c.trig_l == 0xff && c.trig_r == 0xff, "L/R did not reach the triggers");
}

// The claim mapledev.pio cannot make for itself: compressing Charlie Cole's 29-word maple_tx to
// 26 moved no edge. Both programs are run against the same payload and the traces diffed.
static void test_maple_tx(void) {
    static maple_trace_t a, b;
    static uint32_t ref_words[64], new_words[64];
    static uint32_t payload[48];

    static const struct { const char *name; unsigned nbytes, np; uint32_t fill; } cases[] = {
        { "condition",   4 + 12 + 1,  6,  0x00000000u },
        { "ones",        4 + 12 + 1,  6,  0xffffffffu },
        { "alt 10",      4 + 12 + 1,  6,  0xaaaaaaaau },
        { "alt 01",      4 + 12 + 1,  6,  0x55555555u },
        { "device info", 4 + 112 + 1, 30, 0x89abcdefu },
        { "ack",         4 + 1,       2,  0x07002000u },
    };
    uint32_t cycles[sizeof cases / sizeof *cases];

    for (unsigned k = 0; k < sizeof cases / sizeof *cases; k++) {
        for (unsigned i = 0; i < 48; i++) payload[i] = cases[k].fill;
        uint32_t bp = maple_bit_pairs_minus1(cases[k].nbytes);

        unsigned nr = 0, nn = 0;
        ref_words[nr++] = bp;
        new_words[nn++] = 3;                    // sync pulse count, a data word in ours
        new_words[nn++] = bp;
        for (unsigned i = 0; i < cases[k].np; i++) {
            ref_words[nr++] = payload[i];
            new_words[nn++] = payload[i];
        }

        maple_tx_sim(maple_tx_reference, 29, MAPLE_TX_REF_WRAP_TARGET, MAPLE_TX_REF_WRAP,
                     ref_words, nr, &a);
        maple_tx_sim(maple_tx_insns, sizeof maple_tx_insns / sizeof *maple_tx_insns,
                     MAPLE_TX_WRAP_TARGET, MAPLE_TX_WRAP, new_words, nn, &b);

        CHECK(!a.overflow && !b.overflow, "%s: trace overflowed", cases[k].name);
        CHECK(a.n == b.n, "%s: %u states vs the reference's %u", cases[k].name, b.n, a.n);
        unsigned lim = a.n < b.n ? a.n : b.n, bad = 0;
        // State 0 is the idle before the frame: arming latency, not a wire property.
        for (unsigned i = 1; i < lim; i++)
            if (a.state[i] != b.state[i] || a.hold[i] != b.hold[i]) bad++;
        CHECK(bad == 0, "%s: %u of %u bus states differ from the reference",
              cases[k].name, bad, lim);

        // The only cycle we spend differently is the arming preamble, with the bus released.
        CHECK(b.cycles + 2u == a.cycles, "%s: %u cycles against the reference's %u",
              cases[k].name, b.cycles, a.cycles);
        cycles[k] = b.cycles;
    }

    // The bit cell is deliberately data-independent — that is what the pad in the zero branch
    // buys, and why it is not a spare instruction. Cases 0-3 are the same length, different data.
    for (unsigned k = 1; k < 4; k++)
        CHECK(cycles[k] == cycles[0], "%s: frame length varies with data, %u vs %u",
              cases[k].name, cycles[k], cycles[0]);
}

// The longest frame on the bus, a block-read reply, spelled out rather than taken from
// MAPLE_MAX_FRAME so the test cannot just agree with the constant.
#define MAPLE_LONGEST_FRAME (4u + 4u + 4u + 512u + 1u)

static unsigned maple_fill_long(uint8_t *frame, uint32_t *words) {
    frame[0] = MAPLE_CMD_RESP_DATA_XFER;
    frame[1] = MAPLE_ADDR_DC;
    frame[2] = 0x21;                                    // main peripheral + slot 1
    frame[3] = (uint8_t)((MAPLE_LONGEST_FRAME - 5u) / 4u);
    for (unsigned i = 4; i < MAPLE_LONGEST_FRAME - 1u; i++) frame[i] = (uint8_t)(i * 7u + 1u);
    frame[MAPLE_LONGEST_FRAME - 1u] = maple_checksum(frame, MAPLE_LONGEST_FRAME - 1u);

    words[0] = 3;
    words[1] = maple_bit_pairs_minus1(MAPLE_LONGEST_FRAME);
    return maple_pack_words(frame, MAPLE_LONGEST_FRAME, words + 2) + 2u;
}

// A trace too small to hold that waveform does not fail loudly: maple_tx_sim sets its overflow
// flag and every comparison downstream then diffs truncated traces against each other.
static void test_maple_trace_capacity(void) {
    static uint8_t  frame[MAPLE_LONGEST_FRAME];
    static uint32_t words[MAPLE_LONGEST_FRAME / 4u + 3u];
    static maple_trace_t tr;

    unsigned nw = maple_fill_long(frame, words);
    CHECK(nw == 134, "a %u-byte frame packed to %u words, want 134", MAPLE_LONGEST_FRAME, nw);

    maple_tx_sim(maple_tx_insns, sizeof maple_tx_insns / sizeof *maple_tx_insns,
                 MAPLE_TX_WRAP_TARGET, MAPLE_TX_WRAP, words, nw, &tr);
    CHECK(!tr.overflow, "MAPLE_MAX_TRACE %u cannot hold a %u-byte frame (%u states, %u cycles)",
          MAPLE_MAX_TRACE, MAPLE_LONGEST_FRAME, tr.n, tr.cycles);
    // The bit cell is data-independent — test_maple_tx proves it — so the cycle count is a wire
    // fact: 152 of preamble, 216 a byte, 104 of end pattern. 113656 cycles is 2.73 ms at 41.67 MHz.
    unsigned want_cycles = 152u + 216u * MAPLE_LONGEST_FRAME + 104u;
    CHECK(tr.cycles == want_cycles, "a %u-byte frame is %u cycles, want %u",
          MAPLE_LONGEST_FRAME, tr.cycles, want_cycles);
    // The state count does vary with the data (8419 for this fill), so headroom is the claim.
    CHECK(tr.n + 512u < MAPLE_MAX_TRACE, "%u states leaves no headroom under MAPLE_MAX_TRACE %u",
          tr.n, MAPLE_MAX_TRACE);
}

// A half-done constant bump — MAPLE_MAX_FRAME raised with MAPLE_MAX_WORDS or maple_rx_t.buf left
// behind — never fails to compile. It shows up as long frames silently refused.
static void test_maple_frame_limits(void) {
    static uint8_t  frame[MAPLE_LONGEST_FRAME], back[MAPLE_MAX_FRAME], wire[MAPLE_MAX_FRAME + 4u];
    static uint32_t words[MAPLE_LONGEST_FRAME / 4u + 3u];
    maple_rx_t probe;

    CHECK(MAPLE_MAX_FRAME >= MAPLE_LONGEST_FRAME, "MAPLE_MAX_FRAME %u cannot hold %u bytes",
          MAPLE_MAX_FRAME, MAPLE_LONGEST_FRAME);
    CHECK(MAPLE_MAX_WORDS * 4u >= MAPLE_MAX_FRAME, "MAPLE_MAX_WORDS %u is short of %u bytes",
          MAPLE_MAX_WORDS, MAPLE_MAX_FRAME);
    CHECK(sizeof probe.buf >= MAPLE_LONGEST_FRAME, "maple_rx_t.buf is %u bytes, want >= %u",
          (unsigned)sizeof probe.buf, MAPLE_LONGEST_FRAME);

    unsigned nw = maple_fill_long(frame, words);
    CHECK(nw - 2u == 132u, "%u bytes packed to %u payload words, want 132",
          MAPLE_LONGEST_FRAME, nw - 2u);

    // The checksum has to come back out of the top byte of the final short word.
    maple_wire_bytes(frame, MAPLE_LONGEST_FRAME, wire);
    unsigned bn = maple_unpack(wire, MAPLE_LONGEST_FRAME, back);
    CHECK(bn == MAPLE_LONGEST_FRAME - 1u, "unpacked %u bytes, want %u",
          bn, MAPLE_LONGEST_FRAME - 1u);
    CHECK(bn == MAPLE_LONGEST_FRAME - 1u && memcmp(back, frame, bn) == 0,
          "a %u-byte frame did not survive pack/unpack", MAPLE_LONGEST_FRAME);
}

// The longest frame the console sends is an LCD write (block writes come in 128-byte phases).
// Also exercises maple_rx_in_frame(), which mapledev_poll()'s deadline depends on.
static void test_maple_rx_long_frame(void) {
    enum { LCD_FRAME = 4u + 4u + 4u + 192u + 1u };
    static uint8_t  frame[LCD_FRAME], back[MAPLE_MAX_FRAME];
    static uint32_t words[LCD_FRAME / 4u + 3u];
    static maple_trace_t tr;
    static maple_rx_t rx;

    frame[0] = 12;                                      // block write
    frame[1] = 0x01;                                    // a sub-peripheral in slot 1
    frame[2] = MAPLE_ADDR_DC;
    frame[3] = (uint8_t)((LCD_FRAME - 5u) / 4u);
    for (unsigned i = 4; i < LCD_FRAME - 1u; i++) frame[i] = (uint8_t)(i * 3u);
    frame[LCD_FRAME - 1u] = maple_checksum(frame, LCD_FRAME - 1u);

    words[0] = 3;
    words[1] = maple_bit_pairs_minus1(LCD_FRAME);
    unsigned nw = maple_pack_words(frame, LCD_FRAME, words + 2) + 2u;
    maple_tx_sim(maple_tx_insns, sizeof maple_tx_insns / sizeof *maple_tx_insns,
                 MAPLE_TX_WRAP_TARGET, MAPLE_TX_WRAP, words, nw, &tr);
    CHECK(!tr.overflow, "a %u-byte frame overflowed the trace", (unsigned)LCD_FRAME);

    maple_rx_init(&rx);
    CHECK(!maple_rx_in_frame(&rx), "a reset decoder reports a frame in progress");

    unsigned got = 0, errs = 0, seen_in_frame = 0;
    for (unsigned i = 0; i < tr.n && !got; i++) {
        maple_rx_ev_t ev = maple_rx_feed(&rx, tr.state[i]);
        if (ev == MAPLE_RX_FRAME) got = 1;
        else if (ev == MAPLE_RX_ERROR) errs++;
        else if (maple_rx_in_frame(&rx)) seen_in_frame++;
    }
    CHECK(got, "a %u-byte frame never decoded (%u errors in %u samples)",
          (unsigned)LCD_FRAME, errs, tr.n);
    CHECK(errs == 0, "%u decode errors on a %u-byte frame", errs, (unsigned)LCD_FRAME);
    CHECK(seen_in_frame > 0, "maple_rx_in_frame never went true while a frame was arriving");
    CHECK(!maple_rx_in_frame(&rx), "maple_rx_in_frame still set after the frame completed");
    CHECK(!got || rx.len == LCD_FRAME, "decoded %u bytes, sent %u", rx.len, (unsigned)LCD_FRAME);

    unsigned bn = got ? maple_unpack(rx.buf, rx.len, back) : 0;
    CHECK(bn == LCD_FRAME - 1u && memcmp(back, frame, bn) == 0,
          "a %u-byte frame did not survive the wire", (unsigned)LCD_FRAME);
}

#define FNV1A_INIT 2166136261u
static uint32_t fnv1a_add(uint32_t h, const uint8_t *p, size_t n) {
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}

// Every '=' record in the capture is a real device's reply that maple_build_reply() must match.
// A section whose unit we do not present is counted, not asserted.
#define MCAP_CMD_UNDEFINED 0x1fu    // what vmudiff.c sends to provoke a BADCMD

static const maple_bus_t *g_mcap_bus;

// The bus itself decides, so registering a unit claims its capture section with no edit here.
static bool mcap_unit_present(uint8_t unit) {
    uint8_t addr = unit ? (uint8_t)(1u << (unit - 1)) : (uint8_t)MAPLE_ADDR_CONTROLLER;
    return g_mcap_bus && maple_bus_find(g_mcap_bus, addr) >= 0;
}

static uint8_t mcap_unit_addr(uint8_t unit) {
    return unit ? (uint8_t)(1u << (unit - 1)) : (uint8_t)MAPLE_ADDR_CONTROLLER;
}

// dst carries no port bits; w[] holds logical values, stored big-endian as the payload wants them.
unsigned maple_make_req_to(uint8_t *req, uint8_t cmd, uint8_t dst, uint8_t port,
                           const uint32_t *w, unsigned nw) {
    req[0] = cmd;
    req[1] = (uint8_t)(dst | port);
    req[2] = (uint8_t)(MAPLE_ADDR_DC | port);
    req[3] = (uint8_t)nw;
    for (unsigned i = 0; i < nw; i++) {
        req[4 + 4 * i + 0] = (uint8_t)(w[i] >> 24);
        req[4 + 4 * i + 1] = (uint8_t)(w[i] >> 16);
        req[4 + 4 * i + 2] = (uint8_t)(w[i] >> 8);
        req[4 + 4 * i + 3] = (uint8_t)w[i];
    }
    return 4 + 4 * nw;
}

// True if the reply matches the record. Kept free of CHECK so a pending key can be tested for
// still-failing without printing.
static bool mcap_eval(const mcap_sect_t *s, const mcap_rec_t *r, const maple_bus_t *bus,
                      char *why, size_t nwhy) {
    mcap_key_t k;
    uint8_t req[32], out[MAPLE_MAX_FRAME], cmd, src;
    uint32_t w[2];
    unsigned nw = 0, rn, n;

    mcap_key_parse(r->key, &k);                 // the loader already validated it

    switch (k.kind) {
    case MK_DEVINFO: cmd = MAPLE_CMD_DEVICE_REQUEST;     break;
    case MK_ALLINFO: cmd = MAPLE_CMD_ALL_STATUS_REQUEST; break;
    case MK_BADCMD:  cmd = MCAP_CMD_UNDEFINED;           break;
    // The key spells console-side values; a request payload holds them byte-swapped.
    case MK_COND:  cmd = MAPLE_CMD_GET_CONDITION;
                   w[nw++] = __builtin_bswap32(k.func); break;
    case MK_MINFO: cmd = MAPLE_CMD_GET_MEDIA_INFO;
                   w[nw++] = __builtin_bswap32(k.func);
                   w[nw++] = __builtin_bswap32(k.arg);   break;
    case MK_BREAD: cmd = MAPLE_CMD_BLOCK_READ;
                   w[nw++] = __builtin_bswap32(k.func);
                   w[nw++] = __builtin_bswap32(k.arg);   break;
    default:
        snprintf(why, nwhy, "no request for this key kind");
        return false;
    }

    rn = maple_make_req_to(req, cmd, mcap_unit_addr(s->unit), s->port, w, nw);
    n = maple_build_reply(bus, req, rn, out);

    if (n == 0) { snprintf(why, nwhy, "stayed off the bus; a real device answered %02x", r->resp); return false; }
    if (n != 4u + (unsigned)r->nwords * 4u + 1u) {
        snprintf(why, nwhy, "reply is %u bytes, want %u", n, 4u + r->nwords * 4u + 1u);
        return false;
    }
    if (maple_cmd(out) != r->resp) {
        snprintf(why, nwhy, "response %02x, want %02x", maple_cmd(out), r->resp);
        return false;
    }
    if (maple_nwords(out) != r->nwords) {
        snprintf(why, nwhy, "nwords %u, want %u", maple_nwords(out), r->nwords);
        return false;
    }
    if (r->nbytes && memcmp(out + 4, r->data, r->nbytes) != 0) {
        unsigned i = 0;
        while (i < r->nbytes && out[4 + i] == r->data[i]) i++;
        snprintf(why, nwhy, "payload differs at +%u: %02x, want %02x", i, out[4 + i], r->data[i]);
        return false;
    }
    if (maple_checksum(out, n) != 0) { snprintf(why, nwhy, "frame + checksum != 0"); return false; }
    if (maple_dst(out) != (uint8_t)(MAPLE_ADDR_DC | s->port)) {
        snprintf(why, nwhy, "dst %02x, want %02x", maple_dst(out), MAPLE_ADDR_DC | s->port);
        return false;
    }

    // src is the port bits, the main-peripheral flag, and a low-5-bit map of what is plugged into us.
    src = maple_src(out);
    if ((src & MAPLE_PORT_MASK) != s->port) {
        snprintf(why, nwhy, "src port %02x, want %02x", src & MAPLE_PORT_MASK, s->port);
        return false;
    }
    if ((src & MAPLE_ADDR_CONTROLLER) != (r->src & MAPLE_ADDR_CONTROLLER)) {
        snprintf(why, nwhy, "src main-peripheral flag %02x, want %02x",
                 src & MAPLE_ADDR_CONTROLLER, r->src & MAPLE_ADDR_CONTROLLER);
        return false;
    }
    if ((src & MAPLE_SUB_MASK) != (r->src & MAPLE_SUB_MASK)) {
        snprintf(why, nwhy, "src sub-device bitmap %02x, want %02x",
                 src & MAPLE_SUB_MASK, r->src & MAPLE_SUB_MASK);
        return false;
    }
    return true;
}

static void test_maple_capture(void) {
    static mcap_t cap;
    const char *err = NULL;
    maple_cond_t cond;
    unsigned claimed = 0, unclaimed = 0, submask = 0;
    int rc;

    // Not a skip: this capture is tracked source, so its absence is a broken checkout.
    rc = mcap_load(MAPLE_CAPTURE, &cap, &err);
    CHECK(rc != -1, "capture %s is missing", MAPLE_CAPTURE);
    CHECK(rc != -2, "capture %s is malformed: %s", MAPLE_CAPTURE, err ? err : "?");
    if (rc != 0) return;

    // A floor, not an exact count: the survey grows. The reference holds 2 sections and 48 records.
    CHECK(cap.nsect >= 2 && cap.nrec >= 40, "capture has %u sections and %u records, want >= 2 / 40",
          cap.nsect, cap.nrec);

    maple_cond_default(&cond);
    const maple_bus_t *bus = maple_test_bus_vmu(&cond);
    g_mcap_bus = bus;

    for (unsigned si = 0; si < cap.nsect; si++) {
        const mcap_sect_t *s = &cap.sect[si];
        bool mine = mcap_unit_present(s->unit);

        for (unsigned i = s->first; i < s->first + s->count; i++) {
            const mcap_rec_t *r = &cap.rec[i];
            mcap_key_t k;
            char why[128];
            bool ok;

            mcap_key_parse(r->key, &k);
            if (k.kind == MK_ADDR) {
                // About the port, not the device: a main peripheral reports what is plugged into it.
                if (mine && (r->src & MAPLE_SUB_MASK)) submask++;
                continue;
            }
            if (!mine) { unclaimed++; continue; }

            ok = mcap_eval(s, r, bus, why, sizeof why);
            // A '~' record is live state, so only its shape is fixed — except the controller's own
            // condition, which at rest is exactly what maple_cond_default() builds.
            if (!r->stable && !(k.kind == MK_COND && k.func == 0x01000000u)) {
                CHECK(ok || strstr(why, "payload differs") != NULL,
                      "capture %s/%s (line %u): %s", s->name, r->key, r->line, why);
                claimed++;
                continue;
            }
            CHECK(ok, "capture %s/%s (line %u): %s", s->name, r->key, r->line, why);
            claimed++;
        }
    }

    printf("maple capture: %u asserted, %u in sections we do not present"
           "%s\n", claimed, unclaimed,
           submask ? ", sub-device bitmap pending" : "");
}

// The write half. A card that only reads is not a VMU: the console writes a block, syncs it, and
// expects to read back what it wrote.
static void test_maple_vmu_write(void) {
    static uint8_t card[MAPLE_VMU_BLOCKS * MAPLE_VMU_BLOCK];
    uint8_t req[16 + MAPLE_VMU_BLOCK], out[MAPLE_MAX_FRAME], blk[MAPLE_VMU_BLOCK];
    maple_bus_t  b;
    maple_unit_t uc, uv;
    maple_vmu_t  vmu;
    maple_cond_t c;
    uint32_t w[2];
    unsigned n, rn;

    maple_cond_default(&c);
    maple_bus_init(&b);
    maple_controller_unit(&uc, &c);
    maple_bus_add(&b, &uc);
    maple_vmu_unit(&uv, &vmu, card, MAPLE_SUB_UNIT(1));
    maple_bus_add(&b, &uv);

    maple_vmu_format(card);

    // The geometry a real card reports, read back out of the root block we just wrote.
    const uint8_t *root = card + VMU_ROOT_BLOCK * MAPLE_VMU_BLOCK;
    unsigned fmt = 1;
    for (unsigned i = 0; i < 16; i++) if (root[i] != 0x55) fmt = 0;
    CHECK(fmt, "root block is not marked formatted");
    CHECK(root[VMU_ROOT_GEOM + 6] == VMU_FAT_BLOCK && root[VMU_ROOT_GEOM + 10] == VMU_DIR_BLOCK
          && root[VMU_ROOT_GEOM + 12] == VMU_DIR_BLOCKS
          && root[VMU_ROOT_GEOM + 16] == (VMU_USER_BLOCKS & 0xff),
          "root geometry: fat=%u dir=%u ndir=%u user=%u", root[VMU_ROOT_GEOM + 6],
          root[VMU_ROOT_GEOM + 10], root[VMU_ROOT_GEOM + 12], root[VMU_ROOT_GEOM + 16]);

    // Write a block the way KOS does, four phases of 128 bytes, then read it back whole. A
    // single-phase write ACKs and silently loses three quarters of the block.
    for (unsigned i = 0; i < MAPLE_VMU_BLOCK; i++) blk[i] = (uint8_t)(i * 7u + 3u);
    w[0] = MAPLE_FUNC_MEMCARD;
    for (unsigned phase = 0; phase < 4; phase++) {
        w[1] = 42u | (phase << 16);             // block 42, this phase, partition 0
        rn = maple_make_req_to(req, MAPLE_CMD_BLOCK_WRITE, MAPLE_SUB_UNIT(1), 0, w, 2);
        memcpy(req + rn, blk + 128u * phase, 128u);
        req[3] = 2u + 128u / 4u;
        rn += 128u;
        n = maple_build_reply(&b, req, rn, out);
        CHECK(n == 5 && maple_cmd(out) == MAPLE_CMD_RESP_ACK,
              "block write phase %u: %u bytes, cmd %02x", phase, n, n ? maple_cmd(out) : 0);
    }
    w[1] = 42;

    rn = maple_make_req_to(req, MAPLE_CMD_BLOCK_SYNC, MAPLE_SUB_UNIT(1), 0, w, 1);
    n = maple_build_reply(&b, req, rn, out);
    CHECK(n == 5 && maple_cmd(out) == MAPLE_CMD_RESP_ACK, "block sync: %u bytes, cmd %02x",
          n, n ? maple_cmd(out) : 0);

    rn = maple_make_req_to(req, MAPLE_CMD_BLOCK_READ, MAPLE_SUB_UNIT(1), 0, w, 2);
    n = maple_build_reply(&b, req, rn, out);
    CHECK(n == 4 + 8 + MAPLE_VMU_BLOCK + 1, "read back is %u bytes", n);
    CHECK(maple_cmd(out) == MAPLE_CMD_RESP_DATA_XFER && maple_nwords(out) == 130,
          "read back shape: cmd %02x nwords %u", maple_cmd(out), maple_nwords(out));
    CHECK(maple_data_word(out, 1) == 42u, "read back echoed block %lu",
          (unsigned long)maple_data_word(out, 1));
    CHECK(memcmp(out + 4 + 8, blk, MAPLE_VMU_BLOCK) == 0, "read back differs from what was written");

    // A write past the last block, or past the fourth phase, is refused rather than landing
    // somewhere else on the card.
    static const uint32_t k_bad[] = { MAPLE_VMU_BLOCKS, 42u | (4u << 16) };
    for (unsigned i = 0; i < 2; i++) {
        w[1] = k_bad[i];
        rn = maple_make_req_to(req, MAPLE_CMD_BLOCK_WRITE, MAPLE_SUB_UNIT(1), 0, w, 2);
        req[3] = 2u + 128u / 4u;
        rn += 128u;
        n = maple_build_reply(&b, req, rn, out);
        CHECK(n == 5 && maple_cmd(out) == MAPLE_RESP_FILE_ERR,
              "bad write %08lx: cmd %02x", (unsigned long)k_bad[i], n ? maple_cmd(out) : 0);
    }
    w[1] = 42;

    // The LCD acknowledges a frame; a console that draws to the screen must not be left waiting.
    w[0] = MAPLE_FUNC_LCD;
    w[1] = 0;
    rn = maple_make_req_to(req, MAPLE_CMD_BLOCK_WRITE, MAPLE_SUB_UNIT(1), 0, w, 2);
    req[3] = 2u + 48u;
    rn += 192u;
    n = maple_build_reply(&b, req, rn, out);
    CHECK(n == 5 && maple_cmd(out) == MAPLE_CMD_RESP_ACK, "LCD write: cmd %02x",
          n ? maple_cmd(out) : 0);
}

// Every reply the controller can be made to produce, hashed. A deliberate behavior change re-pins
// it in the same commit.
static void test_maple_reply_sweep(void) {
    static const uint8_t  k_ports[] = { 0x00, 0x40, 0x80, 0xc0 };
    static const uint8_t  k_dsts[]  = { MAPLE_ADDR_CONTROLLER, 0x00, 0x01, 0x02, 0x21, 0x3f };
    static const uint32_t k_funcs[] = { 0, MAPLE_FUNC_CONTROLLER, 2, 8, 0x0e, 0xffffffffu };
    uint8_t req[32], out[MAPLE_MAX_FRAME];
    maple_cond_t c;
    uint32_t h = FNV1A_INIT;
    unsigned replies = 0, silent = 0;

    maple_cond_default(&c);
    const maple_bus_t *bus = maple_test_bus(&c);

    for (unsigned cmd = 0; cmd < 256; cmd++)
        for (unsigned di = 0; di < sizeof k_dsts; di++)
            for (unsigned pi = 0; pi < sizeof k_ports; pi++)
                for (unsigned fi = 0; fi < sizeof k_funcs / sizeof k_funcs[0]; fi++)
                    for (unsigned nw = 0; nw <= 2; nw++) {
                        uint32_t w[2] = { k_funcs[fi], 0x01020304u };
                        unsigned rn = maple_make_req_to(req, (uint8_t)cmd, k_dsts[di],
                                                        k_ports[pi], w, nw);
                        unsigned n = maple_build_reply(bus, req, rn, out);
                        uint8_t len[2] = { (uint8_t)n, (uint8_t)(n >> 8) };

                        h = fnv1a_add(h, len, sizeof len);
                        if (n) { h = fnv1a_add(h, out, n); replies++; } else silent++;
                    }

    // 110592 requests; one destination in six is ours, less GETCOND's 24 malformed frames.
    CHECK(replies == 18408 && silent == 92184, "sweep: %u replies, %u silent", replies, silent);
    CHECK(h == 0x556baf3du, "maple reply sweep fnv1a=%08x, want 556baf3d", h);
}

// The same sweep over the bus we actually present — the card changes the controller's source byte
// and adds a whole unit — with the clock frozen at now_ms 0 so the hash is reproducible.
static void test_maple_vmu_sweep(void) {
    static const uint8_t  k_ports[] = { 0x00, 0x40, 0x80, 0xc0 };
    static const uint8_t  k_dsts[]  = { MAPLE_ADDR_CONTROLLER, 0x00, 0x01, 0x02, 0x21, 0x3f };
    static const uint32_t k_funcs[] = { 0, MAPLE_FUNC_CONTROLLER, 2, 4, 8, 0x0e, 0xffffffffu };
    uint8_t req[32], out[MAPLE_MAX_FRAME];
    maple_cond_t c;
    uint32_t h = FNV1A_INIT;
    unsigned replies = 0, silent = 0;

    maple_cond_default(&c);
    const maple_bus_t *bus = maple_test_bus_vmu(&c);

    for (unsigned cmd = 0; cmd < 256; cmd++)
        for (unsigned di = 0; di < sizeof k_dsts; di++)
            for (unsigned pi = 0; pi < sizeof k_ports; pi++)
                for (unsigned fi = 0; fi < sizeof k_funcs / sizeof k_funcs[0]; fi++)
                    for (unsigned nw = 0; nw <= 3; nw++) {
                        uint32_t w[3] = { k_funcs[fi], cmd | ((nw & 3u) << 16), 0xdeadbeefu };
                        unsigned rn = maple_make_req_to(req, (uint8_t)cmd, k_dsts[di],
                                                        k_ports[pi], w, nw);
                        unsigned n = maple_build_reply(bus, req, rn, out);
                        uint8_t len[2] = { (uint8_t)n, (uint8_t)(n >> 8) };

                        h = fnv1a_add(h, len, sizeof len);
                        if (n) { h = fnv1a_add(h, out, n); replies++; } else silent++;
                    }

    CHECK(replies == 57064 && silent == 114968, "vmu sweep: %u replies, %u silent", replies, silent);
    CHECK(h == 0x429f6895u, "maple vmu sweep fnv1a=%08x, want 429f6895", h);
}

// Build a reply, clock it out through the real maple_tx, and decode it back through the RX decoder.
static void test_maple_roundtrip(void) {
    static maple_rx_t rx;
    static maple_trace_t tr;
    static uint32_t words[64];
    uint8_t req[16], frame[MAPLE_MAX_FRAME];
    maple_cond_t c;

    maple_rx_init(&rx);
    CHECK(rx.nstates == MAPLE_RX_STATES, "dfa built %u states, want %u",
          rx.nstates, MAPLE_RX_STATES);

    maple_cond_default(&c);
    c.buttons = (uint16_t)~(MAPLE_BTN_A | MAPLE_BTN_LEFT);
    c.joy_x = 0x20; c.trig_r = 0x80;
    const maple_bus_t *bus = maple_test_bus(&c);

    static const uint8_t kinds[] = {
        MAPLE_CMD_GET_CONDITION, MAPLE_CMD_DEVICE_REQUEST,
        MAPLE_CMD_ALL_STATUS_REQUEST, MAPLE_CMD_RESET_DEVICE,
    };

    for (unsigned k = 0; k < sizeof kinds / sizeof *kinds; k++) {
        bool cond = kinds[k] == MAPLE_CMD_GET_CONDITION;
        unsigned rn = maple_make_req(req, kinds[k], 0x80, MAPLE_FUNC_CONTROLLER, cond);
        unsigned n = maple_build_reply(bus, req, rn, frame);
        CHECK(n != 0, "kind %02x produced no reply", kinds[k]);
        if (!n) continue;

        words[0] = 3;
        words[1] = maple_bit_pairs_minus1(n);
        unsigned nw = maple_pack_words(frame, n, words + 2);
        maple_tx_sim(maple_tx_insns, sizeof maple_tx_insns / sizeof *maple_tx_insns,
                     MAPLE_TX_WRAP_TARGET, MAPLE_TX_WRAP, words, nw + 2, &tr);
        CHECK(!tr.overflow, "kind %02x overflowed the trace", kinds[k]);

        maple_rx_reset(&rx);
        unsigned got = 0, errs = 0, tail_errs = 0, at = 0;
        for (unsigned i = 0; i < tr.n && !got; i++) {
            maple_rx_ev_t ev = maple_rx_feed(&rx, tr.state[i]);
            if (ev == MAPLE_RX_FRAME) { got = 1; at = i; }
            else if (ev == MAPLE_RX_ERROR) errs++;
        }
        CHECK(got, "kind %02x never decoded (%u errors in %u samples)", kinds[k], errs, tr.n);
        CHECK(errs == 0, "kind %02x: %u decode errors", kinds[k], errs);
        if (!got) continue;

        // The frame event is raised two bus states early on purpose; the end pattern that follows
        // must not decode as an error.
        {
            maple_rx_t tail = rx;
            for (unsigned i = at + 1; i < tr.n; i++)
                if (maple_rx_feed(&tail, tr.state[i]) == MAPLE_RX_ERROR) tail_errs++;
            CHECK(tail_errs == 0, "kind %02x: %u decode errors in the %u samples after the frame",
                  kinds[k], tail_errs, tr.n - at - 1);
        }

        CHECK(rx.len == n, "kind %02x decoded %u bytes, sent %u", kinds[k], rx.len, n);
        if (rx.len != n) continue;

        // What comes off the wire is in wire order; unpacking must give back exactly what was built.
        uint8_t back[MAPLE_MAX_FRAME];
        unsigned bn = maple_unpack(rx.buf, rx.len, back);
        CHECK(bn == n - 1u, "kind %02x unpacked %u bytes, want %u", kinds[k], bn, n - 1u);
        CHECK(bn == n - 1u && memcmp(back, frame, bn) == 0, "kind %02x round trip corrupted",
              kinds[k]);
        // The port we were addressed on has to survive the round trip in both address bytes.
        CHECK(maple_dst(back) == 0x80 && maple_src(back) == 0xa0,
              "kind %02x lost the port: dst %02x src %02x",
              kinds[k], maple_dst(back), maple_src(back));
    }

    // A single corrupted bus state must be rejected, never delivered as a short frame.
    unsigned rn = maple_make_req(req, MAPLE_CMD_GET_CONDITION, 0, MAPLE_FUNC_CONTROLLER, true);
    unsigned n = maple_build_reply(bus, req, rn, frame);
    words[0] = 3;
    words[1] = maple_bit_pairs_minus1(n);
    unsigned nw = maple_pack_words(frame, n, words + 2);
    maple_tx_sim(maple_tx_insns, sizeof maple_tx_insns / sizeof *maple_tx_insns,
                 MAPLE_TX_WRAP_TARGET, MAPLE_TX_WRAP, words, nw + 2, &tr);
    for (unsigned flip = 40; flip < tr.n && flip < 60; flip++) {
        maple_rx_reset(&rx);
        unsigned delivered = 0;
        for (unsigned i = 0; i < tr.n; i++) {
            uint8_t s = (i == flip) ? (uint8_t)(tr.state[i] ^ 1u) : tr.state[i];
            if (maple_rx_feed(&rx, s) == MAPLE_RX_FRAME) delivered++;
        }
        uint8_t back[MAPLE_MAX_FRAME];
        CHECK(delivered == 0 || (maple_unpack(rx.buf, rx.len, back) == n - 1u
                                 && memcmp(back, frame, n - 1u) == 0),
              "a corrupted sample at %u was delivered as a valid frame", flip);
    }
}

void tests_maple(void) {
    test_maple_frame();
    test_maple_mapping();
    test_maple_tx();
    test_maple_trace_capacity();
    test_maple_frame_limits();
    test_maple_rx_long_frame();
    test_maple_capture();
    test_maple_vmu_write();
    test_maple_reply_sweep();
    test_maple_vmu_sweep();
    test_maple_roundtrip();
}
