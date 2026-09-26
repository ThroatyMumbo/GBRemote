// SNES/NES pad, SNES mouse and sneslink: mapping, protocol and the PIO wire against a console model.
#include "test.h"
#include "config_map.h"
#include "snes_proto.h"
#include "snes_model.h"
#include "snesdev_insns.h"
#include "snesmouse_insns.h"
#include "snesmouse_proto.h"
#include "mouse_model.h"
#include "sneslink_insns.h"
#include "sneslink_proto.h"
#include "link_model.h"

static void test_snes_mapping(void) {
    uint8_t cfg[CFG_LEN];
    memset(cfg, 0xff, sizeof cfg);                  // unprogrammed => identity map
    pad_map_t m;
    map_compile(cfg, &m);

    for (unsigned gb = 0; gb < 256; gb++) {
        pad_state_t p;
        map_apply(&m, (uint8_t)gb, &p);
        uint32_t s = snes_from_pad(&p, 0, SNES_DEADZONE_DEFAULT);
        uint32_t n = nes_from_pad(&p, 0, SNES_DEADZONE_DEFAULT);

        // The released tail is not a special case in the PIO; it is these bits staying zero.
        CHECK((s >> SNES_BITS) == 0, "gb %02x set a bit past the SNES report: %08x", gb, s);
        CHECK((n >> NES_BITS)  == 0, "gb %02x set a bit past the NES report: %08x", gb, n);

        // Wire order, LSB first: B Y SELECT START UP DOWN LEFT RIGHT A X L R.
        CHECK(!!(s & (1u << SNES_B))      == !!(gb & PAD_B),      "gb %02x SNES B", gb);
        CHECK(!!(s & (1u << SNES_SELECT)) == !!(gb & PAD_SELECT), "gb %02x SNES SELECT", gb);
        CHECK(!!(s & (1u << SNES_START))  == !!(gb & PAD_START),  "gb %02x SNES START", gb);
        CHECK(!!(s & (1u << SNES_UP))     == !!(gb & PAD_UP),     "gb %02x SNES UP", gb);
        CHECK(!!(s & (1u << SNES_DOWN))   == !!(gb & PAD_DOWN),   "gb %02x SNES DOWN", gb);
        CHECK(!!(s & (1u << SNES_LEFT))   == !!(gb & PAD_LEFT),   "gb %02x SNES LEFT", gb);
        CHECK(!!(s & (1u << SNES_RIGHT))  == !!(gb & PAD_RIGHT),  "gb %02x SNES RIGHT", gb);
        CHECK(!!(s & (1u << SNES_A))      == !!(gb & PAD_A),      "gb %02x SNES A", gb);
        // A GB pad reaches none of these; only a remap or a richer transport can.
        CHECK(!(s & ((1u << SNES_X) | (1u << SNES_Y) | (1u << SNES_L) | (1u << SNES_R))),
              "gb %02x reached X/Y/L/R through the identity map", gb);

        CHECK(!!(n & (1u << NES_A))      == !!(gb & PAD_A),      "gb %02x NES A", gb);
        CHECK(!!(n & (1u << NES_B))      == !!(gb & PAD_B),      "gb %02x NES B", gb);
        CHECK(!!(n & (1u << NES_SELECT)) == !!(gb & PAD_SELECT), "gb %02x NES SELECT", gb);
        CHECK(!!(n & (1u << NES_START))  == !!(gb & PAD_START),  "gb %02x NES START", gb);
    }

    // The canonical bits a GB byte cannot set still have to land where the console expects them.
    pad_state_t p;
    pad_zero(&p);
    p.buttons = PAD_X | PAD_Y | PAD_L | PAD_R;
    uint32_t s = snes_from_pad(&p, 0, SNES_DEADZONE_DEFAULT);
    CHECK(s == ((1u << SNES_X) | (1u << SNES_Y) | (1u << SNES_L) | (1u << SNES_R)),
          "X/Y/L/R projection = %08x", s);
    CHECK(nes_from_pad(&p, 0, SNES_DEADZONE_DEFAULT) == ((1u << NES_A) | (1u << NES_B)),
          "an NES pad has no X/Y; they must fold onto A/B");

    // Opposites are reachable through a remap and are off by default.
    p.buttons = PAD_LEFT | PAD_RIGHT | PAD_UP | PAD_DOWN;
    s = snes_from_pad(&p, 0, SNES_DEADZONE_DEFAULT);
    CHECK(s == ((1u << SNES_LEFT) | (1u << SNES_RIGHT) | (1u << SNES_UP) | (1u << SNES_DOWN)),
          "opposites should pass through by default: %08x", s);
    CHECK(snes_from_pad(&p, SNF_CANCEL_OPPOSITE, SNES_DEADZONE_DEFAULT) == 0,
          "SNF_CANCEL_OPPOSITE should clear both axes");

    // The axis projection only runs when asked, and respects the deadzone.
    pad_zero(&p);
    p.axis[AX_LY] = 100;
    CHECK(snes_from_pad(&p, 0, SNES_DEADZONE_DEFAULT) == 0, "axis must not move the d-pad by default");
    CHECK(snes_from_pad(&p, SNF_DPAD_FROM_AXIS, SNES_DEADZONE_DEFAULT) == (1u << SNES_UP),
          "axis +Y should reach UP");
    p.axis[AX_LY] = 10;
    CHECK(snes_from_pad(&p, SNF_DPAD_FROM_AXIS, SNES_DEADZONE_DEFAULT) == 0, "inside the deadzone");
}

// The assembled snesdev against a console-side waveform: the bit the console reads during clock N's
// low phase must be report bit N.
static void test_snes_wire(void) {
    static const uint32_t cases[] = {
        0x0000, 0xffff, 0x0001, 0x8000, 0x0555, 0x0aaa,
        (1u << SNES_B) | (1u << SNES_START),
        (1u << SNES_A) | (1u << SNES_LEFT) | (1u << SNES_R),
    };

    for (unsigned c = 0; c < sizeof cases / sizeof *cases; c++) {
        uint32_t rep = cases[c] & 0x0fffu;          // 12 buttons; the top nibble is never pressed
        snes_sm_t sm;
        snes_trace_t t;
        snes_sm_init(&sm, snesdev_insns, sizeof snesdev_insns / sizeof *snesdev_insns,
                     SNESDEV_WRAP_TARGET, SNESDEV_WRAP, SNES_BITS);
        snes_sm_push(&sm, rep);
        snes_sm_idle(&sm, 100);                     // the idle loop pulls it into X
        snes_sm_poll(&sm, SNES_BITS, &t);

        CHECK(t.n == SNES_BITS, "case %u sampled %u bits", c, t.n);
        for (unsigned i = 0; i < t.n; i++)
            CHECK(t.bit[i] == ((rep >> i) & 1u),
                  "case %u bit %u: wire %u, report %u", c, i, t.bit[i], (rep >> i) & 1u);
        CHECK(sm.irq == 1, "case %u raised no latch IRQ", c);
    }

    // The ID nibble reads released and every clock past it pressed, as a real pad's 4021 does:
    // SMAS's games take a released 17th bit to mean no pad is plugged in.
    snes_sm_t sm;
    snes_trace_t t;
    snes_sm_init(&sm, snesdev_insns, sizeof snesdev_insns / sizeof *snesdev_insns,
                 SNESDEV_WRAP_TARGET, SNESDEV_WRAP, SNES_BITS);
    snes_sm_push(&sm, 0x0fffu);
    snes_sm_idle(&sm, 100);
    snes_sm_poll(&sm, 24, &t);
    CHECK(t.n == 24, "tail poll sampled %u bits", t.n);
    for (unsigned i = 12; i < t.n; i++)
        CHECK(t.bit[i] == (i >= SNES_BITS), "clock %u past the buttons read %u", i, t.bit[i]);

    // A second poll must pick up a report written since the first — this is the property the
    // draining idle loop exists for, and a blocking `pull` would fail it by a whole frame.
    snes_sm_push(&sm, (1u << SNES_START));
    snes_sm_idle(&sm, 1000);
    snes_sm_poll(&sm, SNES_BITS, &t);
    for (unsigned i = 0; i < t.n; i++)
        CHECK(t.bit[i] == (i == SNES_START), "second poll bit %u = %u", i, t.bit[i]);

    // With nothing new written, the SM repeats its last report rather than stalling the line.
    snes_sm_idle(&sm, 1000);
    snes_sm_poll(&sm, SNES_BITS, &t);
    for (unsigned i = 0; i < t.n; i++)
        CHECK(t.bit[i] == (i == SNES_START), "repeat poll bit %u = %u", i, t.bit[i]);

    // NES is the same program at eight bits.
    snes_sm_init(&sm, snesdev_insns, sizeof snesdev_insns / sizeof *snesdev_insns,
                 SNESDEV_WRAP_TARGET, SNESDEV_WRAP, NES_BITS);
    snes_sm_push(&sm, (1u << NES_A) | (1u << NES_RIGHT));
    snes_sm_idle(&sm, 100);
    snes_sm_poll(&sm, 16, &t);
    for (unsigned i = 0; i < t.n; i++)
        CHECK(t.bit[i] == (i == NES_A || i == NES_RIGHT || i >= NES_BITS), "nes bit %u = %u", i, t.bit[i]);
}

static void test_snes_mouse_proto(void) {
    for (int dx = -SMOUSE_MAX; dx <= SMOUSE_MAX; dx += 7)
        for (int dy = -SMOUSE_MAX; dy <= SMOUSE_MAX; dy += 11)
            for (unsigned sp = 0; sp < 3; sp++) {
                uint32_t w = smouse_pack(dx, dy, sp & 1, sp == 2, sp);
                int ux, uy;
                bool l, r;
                unsigned us;
                smouse_unpack(w, &ux, &uy, &l, &r, &us);
                CHECK(ux == dx && uy == dy && us == sp && l == (sp & 1) && r == (sp == 2),
                      "pack/unpack %d,%d,%u -> %d,%d,%u", dx, dy, sp, ux, uy, us);
                CHECK((w & 0xffu) == 0 && ((w >> 12) & 7u) == 0 && ((w >> SMB_SIG) & 1u),
                      "fixed bits wrong in %08x", w);
            }
    CHECK(smouse_pack(-200, 300, 0, 0, 0) == smouse_pack(-SMOUSE_MAX, SMOUSE_MAX, 0, 0, 0),
          "magnitude does not clamp to 127");

    // A tap is exactly one pixel, delivered once.
    smouse_t m;
    smouse_reset(&m);
    smouse_tick(&m, PAD_RIGHT, 1000);
    smouse_tick(&m, 0, 1000);
    uint32_t w = smouse_report(&m);
    int dx, dy;
    bool l, r;
    unsigned sp;
    smouse_unpack(w, &dx, &dy, &l, &r, &sp);
    CHECK(dx == 1 && dy == 0, "tap reported %d,%d, want 1,0", dx, dy);
    smouse_rx(&m, w);
    smouse_unpack(smouse_report(&m), &dx, &dy, &l, &r, &sp);
    CHECK(dx == 0 && dy == 0, "tap delivered twice: %d,%d after the echo", dx, dy);

    // Held, it waits out the tap delay, then ramps to its top speed.
    smouse_reset(&m);
    int32_t prev = 0, early = 0, late = 0;
    for (unsigned t = 0; t < 2000; t++) {
        smouse_tick(&m, PAD_UP, 1000);
        int32_t step = prev - m.acc_y;
        if (t == 100) early = step;
        if (t == 1990) late = step;
        prev = m.acc_y;
        if (m.acc_y < -(100 << 8)) { smouse_rx(&m, smouse_report(&m)); prev = m.acc_y; }
    }
    CHECK(early == 0, "moved %d/256 px in the tap delay", early);
    CHECK(late > 80 && late < 100, "top speed %d/256 px per ms, want ~92", late);

    // A strobe cycles speed mod 3 and moves nothing.
    smouse_reset(&m);
    for (unsigned i = 0; i < 31; i++) smouse_rx(&m, 0xffffffffu);
    CHECK(m.speed == 1 && m.acc_x == 0 && m.acc_y == 0, "31 strobes left speed %u", m.speed);

    // SELECT cycles the top speed; held with START (the exit combo) it does not.
    static const unsigned want_max[SMOUSE_SPEEDS] = { 7, 3, 1 };      // px in one 16.68 ms frame
    smouse_reset(&m);
    smouse_tick(&m, PAD_A | PAD_B | PAD_SELECT | PAD_START, 1000);
    smouse_tick(&m, 0, 1000);
    CHECK(m.top == 0, "the exit combo cycled the top speed to %u", m.top);
    for (unsigned lvl = 0; lvl < SMOUSE_SPEEDS; lvl++) {
        if (lvl) { smouse_tick(&m, PAD_SELECT, 1000); smouse_tick(&m, 0, 1000); }
        CHECK(m.top == lvl, "SELECT press %u left top speed %u", lvl, m.top);
        unsigned most = 0, frames = 0;
        uint32_t since = 0;
        for (unsigned t = 0; t < 4000; t++) {                      // 4 s held, 1 ms ticks
            smouse_tick(&m, PAD_RIGHT | PAD_DOWN, 1000);
            if ((since += 1000) < 16683) continue;
            since -= 16683;
            uint32_t w = smouse_report(&m);
            smouse_unpack(w, &dx, &dy, &l, &r, &sp);
            if (t > 1000 && (unsigned)dx > most) most = (unsigned)dx;
            if (t > 1000 && dx == 0) frames++;
            smouse_rx(&m, w);
        }
        smouse_tick(&m, 0, 1000);
        CHECK(most == want_max[lvl], "top speed %u: at most %u px a frame, want %u", lvl, most,
              want_max[lvl]);
        if (lvl == SMOUSE_SPEEDS - 1)
            CHECK(frames < 20, "the drawing speed stalled on %u of ~180 frames", frames);
    }
    smouse_tick(&m, PAD_SELECT, 1000);
    CHECK(m.top == 0, "SELECT wraps back to the fastest, got %u", m.top);

    // A diagonal moves both axes on the same frames, whatever fraction an earlier move banked.
    for (unsigned lvl = 0; lvl < SMOUSE_SPEEDS; lvl++) {
        smouse_reset(&m);
        for (unsigned i = 0; i < lvl; i++) { smouse_tick(&m, PAD_SELECT, 1000); smouse_tick(&m, 0, 1000); }
        unsigned stagger = 0, frames = 0;
        uint32_t since = 0;
        for (unsigned t = 0; t < 4000; t++) {
            uint32_t b = t < 700 ? PAD_RIGHT : t < 800 ? 0 : t < 807 ? PAD_RIGHT : PAD_RIGHT | PAD_DOWN;
            smouse_tick(&m, b, 1000);
            if ((since += 1000) < 16683) continue;
            since -= 16683;
            uint32_t w = smouse_report(&m);
            smouse_unpack(w, &dx, &dy, &l, &r, &sp);
            if (t > 850) { frames++; if (dx != dy) stagger++; }
            smouse_rx(&m, w);
        }
        CHECK(stagger == 0, "top speed %u: the diagonal staggered on %u of %u frames", lvl, stagger,
              frames);
    }
}

typedef struct {
    smouse_t m;
    uint32_t buttons;
    uint64_t last_ns;
    int64_t  gen_x, gen_y;
    unsigned echoes, strobes;
    uint8_t  cmd;                   // the link command riding in $4219
} mdev_t;

// core1's mouse_service(), against the model's FIFOs.
static void mdev_service(mm_t *mm, void *u) {
    mdev_t *d = u;
    uint32_t dt = (uint32_t)((mm->ns - d->last_ns) / 1000u);
    d->last_ns += (uint64_t)dt * 1000u;
    int32_t ax = d->m.acc_x, ay = d->m.acc_y;
    smouse_tick(&d->m, d->buttons, dt);
    d->gen_x += d->m.acc_x - ax;
    d->gen_y += d->m.acc_y - ay;
    uint32_t w;
    while (mm_pop(mm, &w)) {
        if (w == 0xffffffffu) d->strobes++; else d->echoes++;
        smouse_rx(&d->m, w);
    }
    mm_push(mm, smouse_with_cmd(smouse_report(&d->m), d->cmd));
    (void)mm_irq_take(mm, 6);
}

static void test_snes_mouse_wire(void) {
    static const unsigned lows[] = { 80, 250, 1000 };
    for (unsigned li = 0; li < sizeof lows / sizeof *lows; li++) {
        static mm_t mm;
        static mdev_t d;
        memset(&d, 0, sizeof d);
        mm_init(&mm, snesmouse_insns, sizeof snesmouse_insns / sizeof *snesmouse_insns,
                SNESMOUSE_WRAP_TARGET, SNESMOUSE_WRAP, mdev_service, &d);
        mm.manual_low_ns = lows[li];
        mm_wait_ns(&mm, 3000000);

        long sum_x = 0, sum_y = 0;
        unsigned frames = 0, absent = 0, clicks = 0, speed_ok_at = 0, cmd_seen = 0, cmd_stray = 0;
        for (unsigned f = 0; f < 100; f++) {
            if (f == 10) mm.want_speed = 2;
            d.buttons = (f >= 20 && f < 80) ? (PAD_RIGHT | PAD_DOWN) : 0;
            if (f >= 50 && f < 56) d.buttons |= PAD_A;
            d.cmd = (f >= 30 && f < 40) ? 0xa5 : 0;
            uint64_t t0 = mm.ns;
            mm_frame(&mm);
            frames++;
            if (mm.r4219 == 0xa5) cmd_seen++;
            else if (mm.r4219) cmd_stray++;
            if (!mm.present) { absent++; continue; }
            sum_x += mm.dx;
            sum_y += mm.dy;
            clicks += mm.left;
            if (!speed_ok_at && mm.speed == 2) speed_ok_at = f;
            mm_wait_ns(&mm, 16683000u - (mm.ns - t0));
        }
        mm_wait_ns(&mm, 2000000);
        unsigned lo = lows[li];
        CHECK(absent == 0, "low=%uns: %u frames lost the signature", lo, absent);
        CHECK(cmd_seen >= 9 && cmd_seen <= 11 && !cmd_stray,
              "low=%uns: command in $4219 on %u frames of 10, %u stray", lo, cmd_seen, cmd_stray);
        CHECK(speed_ok_at >= 10 && speed_ok_at <= 12, "low=%uns: speed reached 2 at frame %u", lo,
              speed_ok_at);
        CHECK(mm.strobes == 62 && d.strobes == 62, "low=%uns: game strobed %u, mouse counted %u",
              lo, mm.strobes, d.strobes);
        CHECK(d.echoes == frames, "low=%uns: %u echoes for %u frames", lo, d.echoes, frames);
        CHECK(clicks >= 5, "low=%uns: left button seen in %u frames", lo, clicks);
        CHECK(sum_x > 100 && sum_y > 100, "low=%uns: moved only %ld,%ld", lo, sum_x, sum_y);
        CHECK(sum_x * 256 + d.m.acc_x == d.gen_x && sum_y * 256 + d.m.acc_y == d.gen_y,
              "low=%uns: delivered %ld,%ld px + banked %d,%d != generated %lld,%lld /256", lo,
              sum_x, sum_y, d.m.acc_x, d.m.acc_y, (long long)d.gen_x, (long long)d.gen_y);
    }
}

// The SNES half of the link, transcribed from demos/mariopaint/snes/link.asm: its bit handling is
// the oracle for sneslink_proto, and its master-cycle costs clock the model.

static unsigned snes_take(unsigned t, unsigned v) {             // lsr / rol t / lsr / rol t
    t = t << 1 | (v & 1u);
    return t << 1 | ((v >> 1) & 1u);
}

static void test_sneslink_proto(void) {
    for (unsigned b = 0; b < 256; b++) {
        uint8_t four[4] = { (uint8_t)b, 0, 0, 0 };
        uint32_t w = slk_tx_word(four);
        unsigned t = 0;
        for (unsigned k = 0; k < 4; k++) t = snes_take(t, ~(w >> (2 * k)) & 3u);
        CHECK((t & 0xffu) == b, "tx %02x read back as %02x", b, t & 0xffu);

        uint8_t a = (uint8_t)b;
        unsigned c = b >> 7;                                     // cmp #$80
        uint32_t rx = SLK_RX_CLK;
        for (unsigned k = 0; k < 8; k++) {
            rx |= (uint32_t)((a >> 6) & 1u) << (3 * (7 - k) + 2);  // sta $4201, bit 6 is IOBIT
            unsigned nc = a >> 7;
            a = (uint8_t)(a << 1 | c);                           // rol a
            c = nc;
        }
        CHECK(slk_rx_byte(rx) == b && slk_rx_clean(rx), "rx %02x decoded as %02x", b,
              slk_rx_byte(rx));
    }
    uint8_t buf[8];
    slk_frame_t f;
    CHECK(!slk_frame_init(&f, true, buf, NULL, 6), "a 6-byte frame to the SNES is not whole words");
    CHECK(!slk_frame_init(&f, false, NULL, buf, 3), "a 3-byte frame from the SNES is not 16 clocks");
    CHECK(slk_frame_init(&f, true, buf, NULL, 24580) && slk_frame_clocks(&f) == 98320 &&
          f.tx_total == 6146, "the PUT canvas frame: %u clocks, %u TX words",
          slk_frame_clocks(&f), f.tx_total);
}

// lda $4016 / and #3 / cmp #3 / beq / dex / bne
static bool snes_ready(lm_t *m, unsigned tries) {
    while (tries--) {
        if (lm_read(m) == 3) { lm_wait_mc(m, 16 + 16 + 22); return true; }
        lm_wait_mc(m, 16 + 16 + 16 + 14 + 22);
    }
    return false;
}

// lda [p],y / cmp #$80, then 8 x (sta $4201 / nop x n / bit $4016 / rol a), iny / bne
static void snes_send(lm_t *m, const uint8_t *p, uint32_t n, unsigned nops) {
    for (uint32_t i = 0; i < n; i++) {
        uint8_t a = p[i];
        unsigned c = a >> 7;
        lm_wait_mc(m, 56 + 16);
        for (unsigned k = 0; k < 8; k++) {
            lm_wait_mc(m, 24);
            lm_iobit(m, (a >> 6) & 1u);
            lm_wait_mc(m, 6 + 14 * nops);
            (void)lm_read(m);
            unsigned nc = a >> 7;
            a = (uint8_t)(a << 1 | c);
            c = nc;
            lm_wait_mc(m, 14);
        }
        lm_wait_mc(m, 14 + 22);
    }
    lm_iobit(m, true);
}

// 4 x (lda $4016 / lsr / rol t / lsr / rol t), then lda t / sta [p],y / iny / bne
static void snes_recv(lm_t *m, uint8_t *p, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        unsigned t = 0;
        for (unsigned k = 0; k < 4; k++) {
            t = snes_take(t, lm_read(m));
            lm_wait_mc(m, 2 * (14 + 38));
        }
        p[i] = (uint8_t)t;
        lm_wait_mc(m, 30 + 56 + 14 + 22);
    }
}

typedef struct {
    slk_frame_t f[2];
    unsigned n, cur;
    bool armed;
    uint64_t arm_after_ps, arm_delay_ps;
} ldev_t;

// core1's link pass, against the model's FIFOs.
static void ldev_service(lm_t *m, void *u) {
    ldev_t *d = u;
    if (d->cur >= d->n) return;
    slk_frame_t *f = &d->f[d->cur];
    uint32_t w;
    if (!d->armed) {
        if (m->ps < d->arm_after_ps) return;
        lm_arm(m);
        while (slk_frame_tx_peek(f, &w) && lm_tx(m, w)) slk_frame_tx_next(f);
        lm_go(m);
        d->armed = true;
        return;
    }
    while (lm_rx(m, &w))
        if (slk_frame_rx(f, w)) {
            d->cur++;
            d->armed = false;
            d->arm_after_ps = m->ps + d->arm_delay_ps;
            return;
        }
    while (slk_frame_tx_peek(f, &w) && lm_tx(m, w)) slk_frame_tx_next(f);
}

static void test_sneslink_wire(void) {
    static const unsigned lows[] = { 80, 559, 1000 };
    static const uint64_t services[] = { 500000, 50000000 };        // ps: a tight loop, a slow one
    static const uint64_t delays[] = { 0, 300000000 };              // ps before the next frame arms
    uint8_t up[64], down[64], got_up[64], got_down[64];
    uint32_t seed = 1;
    for (unsigned i = 0; i < 64; i++) {
        seed = seed * 1103515245u + 12345u; up[i] = (uint8_t)(seed >> 16);
        seed = seed * 1103515245u + 12345u; down[i] = (uint8_t)(seed >> 16);
    }
    for (unsigned li = 0; li < 3; li++)
        for (unsigned si = 0; si < 2; si++)
            for (unsigned di = 0; di < 2; di++) {
                static lm_t m;
                static ldev_t d;
                memset(&d, 0, sizeof d);
                memset(got_up, 0, sizeof got_up);
                memset(got_down, 0, sizeof got_down);
                slk_frame_init(&d.f[0], false, NULL, got_up, sizeof up);
                slk_frame_init(&d.f[1], true, down, NULL, sizeof down);
                d.n = 2;
                d.arm_delay_ps = delays[di];
                lm_init(&m, sneslink_insns, sizeof sneslink_insns / sizeof *sneslink_insns,
                        SNESLINK_WRAP_TARGET, SNESLINK_WRAP, SNESLINK_START, ldev_service, &d);
                m.read_low_ns = lows[li];
                m.service_ps = services[si];
                d.arm_after_ps = ~0ull;                         // the session has not begun
                CHECK(lm_read(&m) == 0, "an unarmed link must read not-ready");
                d.arm_after_ps = 0;
                lm_iobit(&m, true);
                bool r0 = snes_ready(&m, 1000);
                snes_send(&m, up, sizeof up, 0);
                unsigned polls0 = m.reads;
                bool r1 = snes_ready(&m, 1000);
                unsigned polls = m.reads - polls0;
                snes_recv(&m, got_down, sizeof got_down);
                lm_wait_mc(&m, 20000);
                const char *tag = di ? "late arm" : "prompt arm";
                CHECK(r0 && r1, "low=%uns svc=%lluns %s: ready %d %d", lows[li],
                      (unsigned long long)services[si] / 1000, tag, r0, r1);
                CHECK(!memcmp(got_up, up, sizeof up) && d.f[0].dirty == 0,
                      "low=%uns svc=%lluns %s: SNES->cart differs, %u dirty words", lows[li],
                      (unsigned long long)services[si] / 1000, tag, d.f[0].dirty);
                CHECK(!memcmp(got_down, down, sizeof down),
                      "low=%uns svc=%lluns %s: cart->SNES differs (first %02x want %02x)",
                      lows[li], (unsigned long long)services[si] / 1000, tag, got_down[0], down[0]);
                CHECK(d.cur == 2, "low=%uns: device finished %u of 2 frames", lows[li], d.cur);
                if (di) CHECK(polls > 10, "a late arm must be polled through, took %u", polls);
            }
}

void tests_snes(void) {
    test_snes_mapping();
    test_snes_wire();
    test_snes_mouse_proto();
    test_snes_mouse_wire();
    test_sneslink_proto();
    test_sneslink_wire();
}
