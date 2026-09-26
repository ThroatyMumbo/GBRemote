// Host tests for the N64 command layer, wire timing, and the jbdev PIO pair round trip.
#include "test.h"
#include "n64_proto.h"
#include "wire_model.h"
#include "jb_model.h"
#include "jbdev_insns.h"

static void test_crc(void) {
    uint8_t zeros[N64_BLOCK] = { 0 };
    CHECK(n64_data_crc(zeros) == 0, "crc(zeros) = %02x, want 00", n64_data_crc(zeros));
    // Hence the no-pak 0x02 reply's CRC byte is 0xff — a constant you can spot on an analyzer.
    CHECK((uint8_t)~n64_data_crc(zeros) == 0xff, "inverted crc(zeros) != ff");

    uint8_t ones[N64_BLOCK];
    memset(ones, 0xff, sizeof ones);
    CHECK(n64_data_crc(ones) != 0, "crc(ones) must not be 0");

    // An address that already carries its CRC is a fixed point; the top 11 bits never move.
    for (uint32_t a = 0; a < 0x10000u; a += 0x20u) {
        uint16_t c = n64_addr_crc((uint16_t)a);
        CHECK((c & 0xFFE0u) == (uint16_t)a, "addr_crc moved the address bits at %04x", (unsigned)a);
        CHECK(n64_addr_crc(c) == c, "addr_crc not idempotent at %04x", (unsigned)a);
    }
}

static void test_request_len(void) {
    for (unsigned op = 0; op < 256; op++) {
        uint8_t n = n64_request_len((uint8_t)op);
        switch (op) {
        case 0x00: case 0x01: case 0xff: CHECK(n == 1,  "op %02x len %u", op, n); break;
        case 0x02:                       CHECK(n == 3,  "op %02x len %u", op, n); break;
        case 0x03:                       CHECK(n == 35, "op %02x len %u", op, n); break;
        default:                         CHECK(n == 0,  "op %02x len %u", op, n); break;
        }
    }
}

static void test_replies(void) {
    uint8_t out[N64_BLOCK + 1], status = N64_ST_ACC_ABSENT;
    n64_state_t st = { .buttons = N64_BTN_A | N64_BTN_START, .stick_x = 80, .stick_y = -80 };
    uint8_t req[35] = { 0 };

    // The accessory field is 2 bits, not flags: absent is a steady 0x02 that must not decay to
    // 0x00, and only 0x03 (changed) self-clears — to 0x01, never to unsupported.
    req[0] = N64_CMD_IDENTIFY;
    CHECK(n64_build_reply(N64_CMD_IDENTIFY, req, 1, &st, &status, NULL, out) == 3, "identify len");
    CHECK(out[0] == 0x05 && out[1] == 0x00, "identify type %02x%02x", out[0], out[1]);
    CHECK(out[2] == N64_ST_ACC_ABSENT, "no-pak status %02x, want absent", out[2]);
    n64_build_reply(N64_CMD_IDENTIFY, req, 1, &st, &status, NULL, out);
    CHECK(out[2] == N64_ST_ACC_ABSENT, "absent must stay absent; got %02x", out[2]);

    uint8_t chg = N64_ST_ACC_CHANGED;
    n64_build_reply(N64_CMD_IDENTIFY, req, 1, &st, &chg, NULL, out);
    CHECK(out[2] == N64_ST_ACC_CHANGED, "changed must be reported once; got %02x", out[2]);
    n64_build_reply(N64_CMD_IDENTIFY, req, 1, &st, &chg, NULL, out);
    CHECK(out[2] == N64_ST_ACC_PRESENT, "changed must decay to present; got %02x", out[2]);

    req[0] = N64_CMD_POLL;
    CHECK(n64_build_reply(N64_CMD_POLL, req, 1, &st, &status, NULL, out) == 4, "poll len");
    CHECK(out[0] == (uint8_t)(st.buttons >> 8) && out[1] == (uint8_t)(st.buttons & 0xff),
          "poll buttons %02x%02x", out[0], out[1]);
    CHECK((int8_t)out[2] == 80 && (int8_t)out[3] == -80, "poll stick %d,%d",
          (int8_t)out[2], (int8_t)out[3]);

    // 0x02 with no pak: 32 zeros plus the inverted crc, which is what a host reads as "no pak".
    uint16_t addr = n64_addr_crc(0x8000);
    req[0] = N64_CMD_ACC_READ; req[1] = (uint8_t)(addr >> 8); req[2] = (uint8_t)addr;
    CHECK(n64_build_reply(N64_CMD_ACC_READ, req, 3, &st, &status, NULL, out) == N64_BLOCK + 1,
          "acc read len");
    for (int i = 0; i < N64_BLOCK; i++) CHECK(out[i] == 0, "acc read byte %d nonzero", i);
    CHECK(out[N64_BLOCK] == 0xff, "no-pak read crc %02x, want ff", out[N64_BLOCK]);

    // A bad address CRC must be reported. 0x8001 is the valid encoding of 0x8000, so corrupt the
    // CRC field to something else.
    status = 0;
    uint16_t bad = (uint16_t)((addr & 0xFFE0u) | ((addr + 1u) & 0x1Fu));
    CHECK(n64_addr_crc(bad) != bad, "test picked an address that is actually valid");
    req[1] = (uint8_t)(bad >> 8); req[2] = (uint8_t)bad;
    n64_build_reply(N64_CMD_ACC_READ, req, 3, &st, &status, NULL, out);
    CHECK(status & N64_ST_ADDR_CRC_ERR, "bad address CRC not flagged");

    // Measured on a real controller: the error is reported by one identify, then gone.
    req[0] = N64_CMD_IDENTIFY;
    n64_build_reply(N64_CMD_IDENTIFY, req, 1, &st, &status, NULL, out);
    CHECK(out[2] & N64_ST_ADDR_CRC_ERR, "addr CRC error not reported; got %02x", out[2]);
    n64_build_reply(N64_CMD_IDENTIFY, req, 1, &st, &status, NULL, out);
    CHECK(!(out[2] & N64_ST_ADDR_CRC_ERR), "addr CRC error is sticky; got %02x", out[2]);

    // And 0xFF answers like 0x00: it must not raise CHANGED on a present pak.
    uint8_t pres = N64_ST_ACC_PRESENT;
    req[0] = N64_CMD_RESET;
    n64_build_reply(N64_CMD_RESET, req, 1, &st, &pres, NULL, out);
    CHECK(out[2] == N64_ST_ACC_PRESENT, "0xff raised changed; got %02x", out[2]);

    status = 0;
    req[0] = N64_CMD_ACC_WRITE; req[1] = (uint8_t)(addr >> 8); req[2] = (uint8_t)addr;
    for (int i = 0; i < N64_BLOCK; i++) req[3 + i] = (uint8_t)i;
    CHECK(n64_build_reply(N64_CMD_ACC_WRITE, req, 35, &st, &status, NULL, out) == 1, "acc write len");
    CHECK(out[0] == (uint8_t)~n64_data_crc(&req[3]), "no-pak write crc not inverted");

    CHECK(n64_build_reply(0x7f, req, 1, &st, &status, NULL, out) == 0, "unknown opcode replied");
}

static void test_wire(void) {
    static uint8_t level[200000];
    wire_rx_t rx;

    // Every byte value survives the encode/sample round trip, which is the sample-point claim.
    for (unsigned v = 0; v < 256; v++) {
        uint8_t b = (uint8_t)v;
        unsigned t = wire_encode(level, sizeof level, &b, 1, true, 0);
        wire_rx_sim(level, t + 200 * WIRE_TICKS_US, &rx);
        CHECK(rx.n >= 1 && rx.bytes[0] == b, "byte %02x decoded as %02x (n=%u)",
              b, rx.n ? rx.bytes[0] : 0, rx.n);
    }

    // A 35-byte 0x03 request: no byte may be lost, and the idle detector must not fire inside it.
    uint8_t req[35];
    for (int i = 0; i < 35; i++) req[i] = (uint8_t)(i * 7 + 1);
    unsigned t = wire_encode(level, sizeof level, req, 35, true, 0);
    wire_rx_sim(level, t, &rx);
    CHECK(rx.n >= 35, "long frame lost bytes: got %u of 35", rx.n);
    for (int i = 0; i < 35 && i < (int)rx.n; i++)
        CHECK(rx.bytes[i] == req[i], "long frame byte %d: %02x != %02x", i, rx.bytes[i], req[i]);
    CHECK(rx.idle_events == 1, "idle fired %u times inside a frame (want the entry one only)",
          rx.idle_events);

    // The console's stop bit is sampled as a stray '1'. It must be dropped as a partial byte,
    // not carried into the next frame — that is what the unconditional post-reply resync is for.
    uint8_t op = N64_CMD_POLL;
    t = wire_encode(level, sizeof level, &op, 1, true, 0);
    wire_rx_sim(level, t + 200 * WIRE_TICKS_US, &rx);
    CHECK(rx.n == 1, "stop bit produced an extra byte: n=%u", rx.n);
    CHECK(rx.dropped_bits >= 1, "the stray stop-bit sample was not dropped");

    // A frame cut off mid-byte must reach frame_end, which is the whole resync path.
    t = wire_encode(level, sizeof level, req, 35, false, 12);
    wire_rx_sim(level, t + 200 * WIRE_TICKS_US, &rx);
    CHECK(rx.idle_events >= 2, "truncated frame did not raise the idle event");
    CHECK(rx.dropped_bits >= 4, "truncated partial byte not dropped");
}

// The assembled jbdev pair through a whole transaction on one line; emulator/wire/wire_n64.c
// stands on it.
static struct { uint8_t b[64]; unsigned n; } g_jb_rx;
static struct { uint32_t w[40]; unsigned rd, wr; } g_jb_tx;
static jb_bus_t    g_jb;
static jb_master_t g_jbm;
static bool        g_jb_listen;

static bool jb_test_push(void *ctx, uint32_t v) {
    (void)ctx;
    if (g_jb_rx.n >= sizeof g_jb_rx.b) return false;
    g_jb_rx.b[g_jb_rx.n++] = (uint8_t)v;
    return true;
}

static bool jb_test_pop(void *ctx, uint32_t *v) {
    (void)ctx;
    if (g_jb_tx.rd == g_jb_tx.wr) return false;
    *v = g_jb_tx.w[g_jb_tx.rd++];
    return true;
}

static void jb_test_run(unsigned us) {
    for (unsigned i = 0; i < us * JB_TICKS_US; i++) {
        jb_bus_step(&g_jb);
        if (g_jb_listen) jb_master_feed(&g_jbm, jb_line(&g_jb));
    }
}

static void jb_test_send(uint8_t b) {
    for (int i = 7; i >= 0; i--) {
        unsigned low = JB_LOW_US((b >> i) & 1u);
        g_jb.master_low = 1;
        jb_test_run(low);
        g_jb.master_low = 0;
        jb_test_run(JB_CELL_US - low);
    }
}

static void jb_test_init(void) {
    memset(&g_jb_rx, 0, sizeof g_jb_rx);
    memset(&g_jb_tx, 0, sizeof g_jb_tx);
    memset(&g_jb, 0, sizeof g_jb);
    jb_master_reset(&g_jbm);
    g_jb_listen = false;
    jb_rx_init(&g_jb.rx, jbdev_rx_insns, sizeof jbdev_rx_insns / sizeof *jbdev_rx_insns,
               JBDEV_RX_WRAP_TARGET, JBDEV_RX_WRAP, JBDEV_RX_FRAME_END, jb_test_push, NULL);
    jb_tx_init(&g_jb.tx, jbdev_tx_insns, sizeof jbdev_tx_insns / sizeof *jbdev_tx_insns,
               JBDEV_TX_WRAP_TARGET, JBDEV_TX_WRAP, JBDEV_TX_IDLE, jb_test_pop, NULL);
}

static void test_jb_roundtrip(void) {
    // jb_model.c hardcodes the side-set shape to decode the delay field; if the program's
    // declaration ever moves, every jbdev_tx cell length silently changes.
    CHECK(JBDEV_TX_SIDESET == 2u && JBDEV_TX_SIDESET_OPT == 1,
          "jbdev_tx side-set is %u/%d, jb_tx_init assumes 2/opt",
          JBDEV_TX_SIDESET, JBDEV_TX_SIDESET_OPT);
    CHECK(JBDEV_RX_SIDESET == 0u, "jbdev_rx grew a side-set; its delay field is no longer 5 bits");

    // A 0x02 block read, then the 33-byte answer. Both cell shapes appear in each direction.
    static const uint8_t req[3] = { N64_CMD_ACC_READ, 0x00, 0x1f };
    uint8_t reply[N64_BLOCK + 1];
    for (unsigned i = 0; i < sizeof reply; i++) reply[i] = (uint8_t)(i * 0x35u ^ 0xa5u);
    reply[0] = 0x00;
    reply[1] = 0xff;

    jb_test_init();
    for (unsigned i = 0; i < sizeof req; i++) jb_test_send(req[i]);
    g_jb.master_low = 1;                    // the console's stop bit
    jb_test_run(1);
    g_jb.master_low = 0;

    CHECK(g_jb_rx.n == sizeof req, "request sliced into %u bytes, want %zu",
          g_jb_rx.n, sizeof req);
    for (unsigned i = 0; i < sizeof req && i < g_jb_rx.n; i++)
        CHECK(g_jb_rx.b[i] == req[i], "request byte %u: %02x != %02x", i, g_jb_rx.b[i], req[i]);

    // jbdev_await_stop() silences RX before the turnaround, or it decodes our own reply.
    g_jb.rx.enabled = false;

    // The interlock: with the console still holding the line down, a staged reply must not drive.
    g_jb.master_low = 1;
    for (unsigned i = 0; i < sizeof reply; i++) g_jb_tx.w[g_jb_tx.wr++] = (uint32_t)reply[i] << 24;
    for (unsigned i = 0; i < 50u; i++) {
        jb_test_run(1);
        CHECK(g_jb.tx.pindir == 0, "jbdev_tx drove into the console's low at us %u", i);
    }
    g_jb.master_low = 0;

    g_jb_listen = true;
    for (unsigned t = 0; t < 40u * (N64_BLOCK + 1u) + 200u && !jb_master_done(&g_jbm); t++)
        jb_test_run(1);
    g_jb_listen = false;

    CHECK(g_jbm.n == sizeof reply, "reply decoded as %u bytes, want %zu", g_jbm.n, sizeof reply);
    for (unsigned i = 0; i < sizeof reply && i < g_jbm.n; i++)
        CHECK(g_jbm.bytes[i] == reply[i], "reply byte %u: %02x != %02x",
              i, g_jbm.bytes[i], reply[i]);
    // The SM has to be parked back on the blocking pull, or the next frame starts mid-program.
    CHECK(g_jb.tx.pc == JBDEV_TX_IDLE, "jbdev_tx parked at %u, want idle %u",
          g_jb.tx.pc, JBDEV_TX_IDLE);
    CHECK(g_jb.tx.pindir == 0, "jbdev_tx left the line driven after the stop bit");
}

void tests_n64(void) {
    test_crc();
    test_request_len();
    test_replies();
    test_wire();
    test_jb_roundtrip();
}
