// Genesis/Mega Drive pad report mapping and gendev PIO wire behavior.
#include "test.h"
#include "config_map.h"
#include "gen_proto.h"
#include "gen_model.h"
#include "gendev_insns.h"

#define NIB(w, c) (((w) >> (4u * (c))) & 0xfu)      // c is the 0-based cycle index

static void test_genesis_mapping(void) {
    uint8_t cfg[CFG_LEN];
    memset(cfg, 0xff, sizeof cfg);                  // unprogrammed => identity map
    pad_map_t m;
    map_compile(cfg, &m);

    for (unsigned gb = 0; gb < 256; gb++) {
        pad_state_t p;
        map_apply(&m, (uint8_t)gb, &p);
        gen_report_t r;
        gen_from_pad(&p, 0, GEN_DEADZONE_DEFAULT, &r);

        uint32_t hi = ((gb & PAD_UP)   ? 1u << GEN_D0 : 0) | ((gb & PAD_DOWN)  ? 1u << GEN_D1 : 0)
                    | ((gb & PAD_LEFT) ? 1u << GEN_D2 : 0) | ((gb & PAD_RIGHT) ? 1u << GEN_D3 : 0);
        uint32_t lo = (hi & 0x3u) | 0xcu;           // cycles 2/4 drive D2/D3 low, always

        CHECK(NIB(r.dpad, 0) == hi, "gb %02x cycle 1 = %x, want %x", gb, NIB(r.dpad, 0), hi);
        CHECK(NIB(r.dpad, 1) == lo, "gb %02x cycle 2 = %x, want %x", gb, NIB(r.dpad, 1), lo);
        CHECK(NIB(r.dpad, 2) == hi && NIB(r.dpad, 4) == hi, "gb %02x cycles 3/5 differ", gb);
        CHECK(NIB(r.dpad, 3) == lo, "gb %02x cycle 4 differs", gb);
        // The ID pair a console detects a 6-button pad with: wire 0000 then wire 1111.
        CHECK(NIB(r.dpad, 5) == 0xfu, "gb %02x cycle 6 = %x, want all four driven", gb,
              NIB(r.dpad, 5));
        CHECK(NIB(r.dpad, 7) == 0x0u, "gb %02x cycle 8 = %x, want all four released", gb,
              NIB(r.dpad, 7));
        // GB SELECT has nowhere else to go on a Genesis pad, so it lands on MODE.
        CHECK(NIB(r.dpad, 6) == ((gb & PAD_SELECT) ? (1u << GEN_D3) : 0u),
              "gb %02x cycle 7 = %x", gb, NIB(r.dpad, 6));

        uint32_t tltr = ((gb & PAD_B) ? 1u << GEN_TL : 0) | ((gb & PAD_A) ? 1u << GEN_TR : 0)
                      | ((gb & PAD_START) ? (1u << GEN_TR) << 2 : 0);
        CHECK(r.tltr == tltr, "gb %02x tltr = %02x, want %02x", gb, r.tltr, tltr);
    }

    // A 3-button pad is the same word with cycles 6-8 made ordinary — no ID, no top row.
    pad_state_t p;
    gen_report_t r;
    pad_zero(&p);
    p.buttons = PAD_UP | PAD_LEFT;
    gen_from_pad(&p, GNF_THREE_BUTTON, GEN_DEADZONE_DEFAULT, &r);
    CHECK(NIB(r.dpad, 5) == NIB(r.dpad, 1) && NIB(r.dpad, 7) == NIB(r.dpad, 1),
          "3-button cycles 6/8 must be ordinary TH-low reads: %08x", r.dpad);
    CHECK(NIB(r.dpad, 6) == NIB(r.dpad, 0),
          "3-button cycle 7 must be an ordinary TH-high read: %08x", r.dpad);

    // The canonical bits a GB byte cannot set, on the standard 6-button layout.
    pad_zero(&p);
    p.buttons = PAD_R | PAD_X | PAD_L | PAD_MODE;
    gen_from_pad(&p, 0, GEN_DEADZONE_DEFAULT, &r);
    CHECK(NIB(r.dpad, 6) == ((1u << GEN_D0) | (1u << GEN_D1) | (1u << GEN_D2) | (1u << GEN_D3)),
          "Z/Y/X/MODE projection = %x", NIB(r.dpad, 6));
    pad_zero(&p);
    p.buttons = PAD_Y;
    gen_from_pad(&p, 0, GEN_DEADZONE_DEFAULT, &r);
    CHECK(r.tltr == ((1u << GEN_TL) << 2), "PAD_Y is Genesis A, on TL's TH-low half: %02x", r.tltr);

    // Opposites are reachable through a remap and are off by default.
    pad_zero(&p);
    p.buttons = PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN;
    gen_from_pad(&p, 0, GEN_DEADZONE_DEFAULT, &r);
    CHECK(NIB(r.dpad, 0) == 0xfu, "opposites should pass through by default: %x", NIB(r.dpad, 0));
    gen_from_pad(&p, GNF_CANCEL_OPPOSITE, GEN_DEADZONE_DEFAULT, &r);
    CHECK(NIB(r.dpad, 0) == 0x0u, "GNF_CANCEL_OPPOSITE should clear both axes");

    // The axis projection only runs when asked, and respects the deadzone.
    pad_zero(&p);
    p.axis[AX_LY] = 100;
    gen_from_pad(&p, 0, GEN_DEADZONE_DEFAULT, &r);
    CHECK(NIB(r.dpad, 0) == 0, "axis must not move the d-pad by default");
    gen_from_pad(&p, GNF_DPAD_FROM_AXIS, GEN_DEADZONE_DEFAULT, &r);
    CHECK(NIB(r.dpad, 0) == (1u << GEN_D0), "axis +Y should reach UP");
    p.axis[AX_LY] = 10;
    gen_from_pad(&p, GNF_DPAD_FROM_AXIS, GEN_DEADZONE_DEFAULT, &r);
    CHECK(NIB(r.dpad, 0) == 0, "inside the deadzone");
}

// The wire test runs once per drive mode; the console must not be able to tell them apart.
static int g_gen_od;

static void gen_start(gen_model_t *m) {
    gen_model_init(m, g_gen_od,
                   gen_dpad_insns, sizeof gen_dpad_insns / sizeof *gen_dpad_insns,
                   GEN_DPAD_WRAP_TARGET, GEN_DPAD_WRAP,
                   gen_tltr_insns, sizeof gen_tltr_insns / sizeof *gen_tltr_insns,
                   GEN_TLTR_WRAP_TARGET, GEN_TLTR_WRAP);
}

static void test_genesis_wire(void) {
    static const uint32_t cases[] = {
        0, PAD_UP | PAD_LEFT, PAD_RIGHT | PAD_A | PAD_B, PAD_START | PAD_SELECT,
        PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT | PAD_A | PAD_B | PAD_Y | PAD_START
            | PAD_X | PAD_L | PAD_R | PAD_MODE,
    };

    for (unsigned c = 0; c < sizeof cases / sizeof *cases; c++) {
        pad_state_t p;
        gen_report_t r;
        pad_zero(&p);
        p.buttons = cases[c];
        gen_from_pad(&p, 0, GEN_DEADZONE_DEFAULT, &r);

        gen_model_t m;
        gen_trace_t t = { 0 };
        gen_start(&m);
        gen_model_put(&m, r.dpad, r.tltr);
        gen_model_idle(&m, 100);                    // the idle loops pull it into X
        gen_model_read(&m, GEN_CYCLES, &t);

        CHECK(t.n == GEN_CYCLES, "case %u sampled %u cycles", c, t.n);
        for (unsigned k = 0; k < t.n; k++) {
            unsigned want_lo = NIB(r.dpad, k);
            unsigned want_hi = t.th[k] ? (r.tltr & 0x3u) : ((r.tltr >> 2) & 0x3u);
            CHECK((t.line[k] & 0xfu) == want_lo,
                  "case %u cycle %u: D0-3 wire %x, table %x", c, k + 1, t.line[k] & 0xfu, want_lo);
            CHECK((t.line[k] >> 4) == want_hi,
                  "case %u cycle %u: TL/TR wire %x, want %x", c, k + 1, t.line[k] >> 4, want_hi);
        }
        // TL/TR key off the level, so every TH-high cycle agrees with every other one.
        CHECK((t.line[0] >> 4) == (t.line[2] >> 4) && (t.line[2] >> 4) == (t.line[6] >> 4),
              "case %u TL/TR drifted with the cycle counter", c);
        // The poll signal is a TH edge counted on core1; a PIO irq would also fire on a rewind.
        CHECK(m.dpad.irqs == 0, "case %u: gen_dpad must raise no IRQ", c);
    }

    // The 6-button ID pair on the wire: cycle 6 reads 0000 and cycle 8 reads 1111 on D0-D3.
    pad_state_t p;
    gen_report_t r;
    gen_model_t m;
    gen_trace_t t = { 0 };
    pad_zero(&p);
    p.buttons = PAD_UP | PAD_RIGHT;
    gen_from_pad(&p, 0, GEN_DEADZONE_DEFAULT, &r);
    gen_start(&m);
    gen_model_put(&m, r.dpad, r.tltr);
    gen_model_idle(&m, 100);
    gen_model_read(&m, GEN_CYCLES, &t);
    CHECK((t.line[5] & 0xfu) == 0xfu, "cycle 6 must drive all four low: %x", t.line[5] & 0xfu);
    CHECK((t.line[7] & 0xfu) == 0x0u, "cycle 8 must release all four: %x", t.line[7] & 0xfu);

    // A report written since the last read reaches the next one; a blocking pull would be a frame late.
    pad_zero(&p);
    p.buttons = PAD_LEFT;
    gen_from_pad(&p, 0, GEN_DEADZONE_DEFAULT, &r);
    gen_model_put(&m, r.dpad, r.tltr);
    gen_model_idle(&m, 1000);
    t.n = 0;
    gen_model_read(&m, GEN_CYCLES, &t);
    CHECK((t.line[0] & 0xfu) == (1u << GEN_D2), "second read did not pick up the new report: %x",
          t.line[0] & 0xfu);

    // With nothing new written the SM repeats its last report rather than stalling the port.
    gen_model_idle(&m, 1000);
    t.n = 0;
    gen_model_read(&m, GEN_CYCLES, &t);
    CHECK((t.line[0] & 0xfu) == (1u << GEN_D2), "repeat read = %x", t.line[0] & 0xfu);

    // A 3-button game reads two cycles per frame, so without the counter reset it reaches the ID
    // nibble by frame three.
    pad_zero(&p);
    p.buttons = PAD_UP | PAD_LEFT;
    gen_from_pad(&p, 0, GEN_DEADZONE_DEFAULT, &r);
    unsigned want_lo = (1u << GEN_D0) | 0xcu;       // cycle 2: UP held, D2/D3 driven

    gen_start(&m);
    gen_model_put(&m, r.dpad, r.tltr);
    gen_model_idle(&m, 100);
    for (unsigned f = 0; f < 3; f++) {
        t.n = 0;
        gen_model_idle(&m, 16000);
        gen_model_read(&m, 2, &t);
    }
    CHECK((t.line[1] & 0xfu) == 0xfu,
          "without a rewind the third 2-cycle read should hit the ID nibble, got %x",
          t.line[1] & 0xfu);

    gen_start(&m);
    gen_model_put(&m, r.dpad, r.tltr);
    gen_model_idle(&m, 100);
    for (unsigned f = 0; f < 5; f++) {
        t.n = 0;
        gen_model_idle(&m, 16000);
        gen_model_resync(&m, r.dpad, r.tltr);       // what the reset does, once per idle gap
        gen_model_idle(&m, 100);
        gen_model_read(&m, 2, &t);
        CHECK((t.line[0] & 0xfu) == ((1u << GEN_D0) | (1u << GEN_D2)),
              "frame %u cycle 1 = %x after a rewind", f, t.line[0] & 0xfu);
        CHECK((t.line[1] & 0xfu) == want_lo, "frame %u cycle 2 = %x after a rewind", f,
              t.line[1] & 0xfu);
    }

    // A real console parks TH low and pulses it high 6 us per frame, so the reset must fire on the
    // low side too.
    unsigned want_hi = (1u << GEN_D0) | (1u << GEN_D2);
    gen_start(&m);
    gen_model_put(&m, r.dpad, r.tltr);
    m.th = 0;
    gen_model_idle(&m, 100);
    for (unsigned f = 0; f < 8; f++) {
        gen_model_idle(&m, 1500);                   // g_reset_us into the low park
        gen_model_resync(&m, r.dpad, r.tltr);
        gen_model_idle(&m, 15000);                  // the rest of the ~16.7 ms frame
        CHECK((gen_model_sample(&m) & 0xfu) == want_lo,
              "TH-idles-low frame %u: the TH=0 read got %x", f, gen_model_sample(&m) & 0xfu);
        m.th = 1;                                   // the 6 us pulse
        gen_model_idle(&m, 3);
        CHECK((gen_model_sample(&m) & 0xfu) == want_hi,
              "TH-idles-low frame %u: the TH=1 read got %x", f, gen_model_sample(&m) & 0xfu);
        gen_model_idle(&m, 3);
        m.th = 0;
    }

    // GNF_THREE_BUTTON: an eight-cycle read never shows the ID nibble, so a 6-button-aware game
    // sees a 3-button pad and stops probing.
    gen_from_pad(&p, GNF_THREE_BUTTON, GEN_DEADZONE_DEFAULT, &r);
    gen_start(&m);
    gen_model_put(&m, r.dpad, r.tltr);
    gen_model_idle(&m, 100);
    t.n = 0;
    gen_model_read(&m, GEN_CYCLES, &t);
    for (unsigned k = 0; k < t.n; k++)
        CHECK((t.line[k] & 0xfu) == (t.th[k] ? ((1u << GEN_D0) | (1u << GEN_D2)) : want_lo),
              "3-button cycle %u = %x", k + 1, t.line[k] & 0xfu);
}

void tests_genesis(void) {
    test_genesis_mapping();
    for (g_gen_od = 0; g_gen_od < 2; g_gen_od++)
        test_genesis_wire();
}
