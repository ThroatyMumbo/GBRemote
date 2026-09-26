// n64.c — the N64 controller driver, with no accessory or an emulated Transfer Pak (N64C_TPAK_SLOT)
// whose store core0 stages into pad_arena through gb_stage.h.
#include "pad_driver.h"
#include "config_map.h"
#include "pinmap.h"
#include "jbdev.h"
#include "jbdev.pio.h"
#include "devcommon.h"
#include "n64_proto.h"
#include "tpak_proto.h"
#include "pad_arena.h"
#include "gb_stage.h"
#include "hardware/timer.h"
#include "pico/time.h"
#include <stdio.h>

// CFG_DRV block, owned by this driver.
enum { N64C_FLAGS = 0, N64C_CARDINAL = 1, N64C_DIAGONAL = 2, N64C_TPAK_SLOT = GB_CFG_SLOT_OFF };
enum { N64F_DPAD_DIGITAL = 1U << 0, N64F_EMPTY_TPAK = 1U << 1 };

static uint8_t  g_cardinal = N64_STICK_CARDINAL;
static uint8_t  g_diagonal = N64_STICK_DIAGONAL;
static bool     g_dpad_digital;
static bool     g_want_empty;               // N64F_EMPTY_TPAK: a pak with no cartridge, not no pak
static uint8_t  g_status = N64_ST_ACC_ABSENT;
static uint32_t g_last_cmd_ms;
static uint8_t  g_tpak_slot;
static bool     g_tpak_empty;

// Pre-encoded so the frame path never touches the pad seqlock: one aligned 32-bit load.
static volatile uint32_t g_poll_report;
static const n64_pak_t  *g_pak;             // NULL: no accessory
static n64_pak_t         g_pak_vtbl;
static tpak_t            g_tpak;

static void n64_load_cfg(const uint8_t *cfg) {
    uint8_t f = cfg_get(cfg, N64C_FLAGS, 0);
    g_dpad_digital = (f & N64F_DPAD_DIGITAL) != 0;
    g_want_empty   = (f & N64F_EMPTY_TPAK) != 0;
    g_cardinal = cfg_get_nz(cfg, N64C_CARDINAL, N64_STICK_CARDINAL);
    g_diagonal = cfg_get_nz(cfg, N64C_DIAGONAL, N64_STICK_DIAGONAL);
}

// Hand the arena back only once core0 has stopped copying into it.
static void tpak_detach(void) {
    gb_stage_t *s = gb_stage();
    if (s->epoch & 1U) {
        // Flush before the arena is reset; a W25Q128 sector erase is up to 400 ms, so allow a second.
        uint32_t f = ++s->flush_req;
        __dmb();
        for (unsigned i = 0; i < 1000U && s->flush_ack != f; i++) { busy_wait_us_32(1000); }

        uint32_t q = ++s->quiesce;
        __dmb();
        for (unsigned i = 0; i < 50000U && s->quiesce_ack != q; i++) { busy_wait_us_32(1); }

        s->epoch++;                         // even again: no session
        __dmb();
        pad_arena_reset();
    }
    g_pak = NULL;                           // an empty pak has no session, and still detaches
    g_tpak_slot = 0;
    g_tpak_empty = false;
    g_status = (uint8_t)((g_status & ~N64_ST_ACC_MASK) | N64_ST_ACC_ABSENT);
}

// A Transfer Pak with no cartridge: no session, reads answer zeros, the feed window still works.
static void tpak_attach_empty(void) {
    tpak_init(&g_tpak, NULL);
    tpak_set_feed(&g_tpak, gb_feed_push, gb_stage());
    tpak_set_window(&g_tpak, gb_win_pull, gb_stage());
    tpak_bind(&g_tpak, &g_pak_vtbl);
    g_pak = &g_pak_vtbl;
    g_tpak_empty = true;
    g_status = (uint8_t)((g_status & ~N64_ST_ACC_MASK) | N64_ST_ACC_CHANGED);
}

static pad_err_t tpak_attach(uint8_t slot) {
    gb_stage_t *s = gb_stage();
    gb_slot_info_t info;

    if (!gb_slot_info(&info) || !info.valid || s->slot != slot) { return PAD_ERR_CONFIG; }
    if (info.ram_len > GB_SRAM_MAX) { return PAD_ERR_CONFIG; }

    gbcart_reset(&s->cart);
    s->cart.mbc       = info.mbc;
    s->cart.has_rtc   = info.has_rtc;
    s->cart.rom_banks = info.rom_banks;
    s->cart.ram_len   = info.ram_len;
    s->cart.win[0]    = pad_arena_alloc(GB_ROM_WINDOW, 32);
    s->cart.win[1]    = pad_arena_alloc(GB_ROM_WINDOW, 32);
    s->cart.sram      = info.ram_len ? pad_arena_alloc(info.ram_len, 32) : NULL;
    if (!s->cart.win[0] || !s->cart.win[1] || (info.ram_len && !s->cart.sram)) {
        pad_arena_reset();
        return PAD_ERR_NO_RESOURCE;
    }

    tpak_init(&g_tpak, &s->cart);
    tpak_set_feed(&g_tpak, gb_feed_push, s);
    tpak_set_window(&g_tpak, gb_win_pull, s);
    tpak_bind(&g_tpak, &g_pak_vtbl);
    s->save_flushed = s->cart.save_dirty;   // gbcart_reset() left it at 0; agree with it
    __dmb();
    s->epoch++;                             // odd: the pointers above are live for core0
    g_pak = &g_pak_vtbl;
    g_tpak_slot = slot;
    g_status = (uint8_t)((g_status & ~N64_ST_ACC_MASK) | N64_ST_ACC_CHANGED);
    return PAD_OK;
}

static void n64_apply_tpak(const uint8_t *cfg) {
    uint8_t want = cfg_get(cfg, N64C_TPAK_SLOT, 0);
    bool empty = !want && g_want_empty;
    if (want == g_tpak_slot && empty == g_tpak_empty) { return; }
    tpak_detach();
    if (want) {
        tpak_attach(want); // a failed attach leaves the pad working, pak absent
    } else if (empty) {
        tpak_attach_empty();
    }
}

static pad_err_t n64_init(const pad_driver_t *d, const uint8_t *cfg) {
    (void)d;
    n64_load_cfg(cfg);
    g_status = N64_ST_ACC_ABSENT;
    g_last_cmd_ms = 0;
    g_poll_report = 0;
    g_pak = NULL;
    g_tpak_slot = 0;
    g_tpak_empty = false;
    if (!jbdev_init(pio2, PIN_JOYBUS)) { return PAD_ERR_NO_RESOURCE; }
    n64_apply_tpak(cfg);
    return PAD_OK;
}

static void n64_deinit(const pad_driver_t *d) {
    (void)d;
    tpak_detach();
    jbdev_deinit();
}

static pad_err_t n64_reconfig(const pad_driver_t *d, const uint8_t *cfg) {
    (void)d;
    n64_load_cfg(cfg);
    n64_apply_tpak(cfg);
    return PAD_OK;
}

static bool n64_link_up(const pad_driver_t *d) {
    (void)d;
    return pad_link_recent(g_last_cmd_ms, to_ms_since_boot(get_absolute_time()));
}

#ifdef BENCH_PROTO
// Per opcode, so the counts line up one for one with rom-n64test's per-test transaction counts.
typedef struct { uint32_t seen, sent, trunc, nostop, late; } n64_opstat_t;
static n64_opstat_t g_ops[4];
static uint32_t     g_unknown;
#define OPSTAT(op, field) do { if ((op) < 4) g_ops[op].field++; } while (0)

// Accessory traffic, sized for a retail game's whole detection walk; only the 'u' dump clears it.
typedef struct { uint16_t addr; uint8_t op, val; } n64_trace_t;
static n64_trace_t g_trace[192];
static uint16_t    g_trace_n;

// The first window reads, kept whole to diff against the ROM: the header lives in bytes 1-31.
#define TRACE_BLOCKS 12
static struct { uint16_t addr; uint8_t data[N64_BLOCK]; } g_blk[TRACE_BLOCKS];
static uint8_t g_blk_n;

static void trace_acc(uint8_t op, const uint8_t *req, const uint8_t *out) {
    uint16_t a = (uint16_t)((((uint16_t)req[1] << 8) | req[2]) & 0xFFE0u);
    if (op == N64_CMD_ACC_READ && a >= 0xC000u && g_blk_n < TRACE_BLOCKS) {
        g_blk[g_blk_n].addr = a;
        memcpy(g_blk[g_blk_n].data, out, N64_BLOCK);
        g_blk_n++;
    }
    if (g_trace_n >= (uint16_t)(sizeof g_trace / sizeof g_trace[0])) return;
    n64_trace_t *e = &g_trace[g_trace_n++];
    e->addr = a;
    e->op   = op;
    e->val  = (op == N64_CMD_ACC_WRITE) ? req[3] : out[0];
}

void n64_bench_report(void) {
    const jbdev_stats_t *s = jbdev_stats();
    printf("  n64 seen/sent/trunc/nostop/late:");
    for (unsigned i = 0; i < 4; i++)
        printf("  %02x %lu/%lu/%lu/%lu/%lu", i,
               (unsigned long)g_ops[i].seen, (unsigned long)g_ops[i].sent,
               (unsigned long)g_ops[i].trunc, (unsigned long)g_ops[i].nostop,
               (unsigned long)g_ops[i].late);
    printf("  unk=%lu\n", (unsigned long)g_unknown);
    printf("  jbdev frames=%lu replies=%lu trunc=%lu nostop=%lu short=%lu resync=%lu\n",
           (unsigned long)s->frames, (unsigned long)s->replies, (unsigned long)s->truncated,
           (unsigned long)s->no_stop, (unsigned long)s->short_sends, (unsigned long)s->resyncs);

    // Any nonzero faults is a bug: the console's bank-select transaction should cover every refill.
    const gb_stage_t *g = gb_stage();
    printf("  tpak slot=%u%s pwr=%u acc=%u bank=%u faults=%lu dirty=%lu/%lu arena=%u/%u\n",
           g_tpak_slot, g_tpak_empty ? " (no cart)" : "", g_tpak.powered, g_tpak.access, g_tpak.bank,
           (unsigned long)g_tpak.faults, (unsigned long)g->cart.save_dirty,
           (unsigned long)g->save_flushed,
           (unsigned)pad_arena_used(), (unsigned)pad_arena_capacity());

    printf("  tpak walk: %u entries (bench 'u' dumps)\n", g_trace_n);
}

// Bench 'u'. Separate from the tick so a walk survives until it is asked for.
void n64_trace_dump(void) {
    printf("  tpak walk (%u):", g_trace_n);
    for (unsigned i = 0; i < g_trace_n; i++)
        printf(" %c%04x=%02x", g_trace[i].op == N64_CMD_ACC_WRITE ? 'W' : 'R',
               g_trace[i].addr, g_trace[i].val);
    printf("\n");
    for (unsigned i = 0; i < g_blk_n; i++) {
        printf("  blk %04x:", g_blk[i].addr);
        for (unsigned j = 0; j < N64_BLOCK; j++) printf(" %02x", g_blk[i].data[j]);
        printf("\n");
    }
    g_trace_n = 0;
    g_blk_n = 0;
}
#else
#define OPSTAT(op, field) ((void)0)
#define trace_acc(op, req, out) ((void)0)
#endif

static void __not_in_flash_func(n64_service)(const pad_driver_t *d, const pad_state_t *s,
                                             uint32_t idle_us) {
    (void)d;

    // Re-encode in the idle gap, never inside a frame.
    n64_state_t st;
    n64_from_pad(s, g_cardinal, g_diagonal, g_dpad_digital, &st);
    g_poll_report = ((uint32_t)(st.buttons >> 8) << 24) | ((uint32_t)(st.buttons & 0xff) << 16)
                  | ((uint32_t)(uint8_t)st.stick_x << 8) | (uint32_t)(uint8_t)st.stick_y;

    uint8_t req[35];
    uint8_t out[N64_BLOCK + 1];
    uint8_t op;
    if (jbdev_wait_opcode(&op, idle_us) != JBDEV_OK) { return; }

    OPSTAT(op, seen);
    uint8_t need = n64_request_len(op);
    if (need == 0) {                                    // unknown opcode: never reply
#ifdef BENCH_PROTO
        g_unknown++;
#endif
        jbdev_resync();
        return;
    }

    req[0] = op;
    if (need > 1 && jbdev_read_request(&req[1], need - 1U) != JBDEV_OK) {
        OPSTAT(op, trunc);
        jbdev_resync();
        return;
    }

    // Before building: the 32-byte CRC outruns the ~3 us before the stop bit.
    bool armed = (jbdev_await_stop() == JBDEV_OK);

    uint32_t r = g_poll_report;
    n64_state_t wire = {.buttons = (uint16_t)((r >> 16) & 0xffffU),
                        .stick_x = (int8_t)((r >> 8) & 0xffU),
                        .stick_y = (int8_t)(r & 0xffU)};

    uint8_t n = n64_build_reply(op, req, need, &wire, &g_status, g_pak, out);
    if (op == N64_CMD_ACC_READ || op == N64_CMD_ACC_WRITE) { trace_acc(op, req, out); }
    if (n && !armed) {
        OPSTAT(op, nostop);
    } else if (n) {
        jbdev_stage_reply(out, n);
        jbdev_send_reply();
        if (jbdev_wait_sent() != JBDEV_OK) { // NOLINT(bugprone-branch-clone): empty OPSTAT
            OPSTAT(op, late);
        } else {
            OPSTAT(op, sent);
        }
    }

    g_last_cmd_ms = to_ms_since_boot(get_absolute_time());
    // Only accessory traffic holds the flash gate, stamped after the reply to keep the turnaround clear.
    if (op == N64_CMD_ACC_READ || op == N64_CMD_ACC_WRITE) { gb_stage()->busy_ms = g_last_cmd_ms; }
    jbdev_resync();     // unconditional: RX sampled our own stop bit as a stray '1'
}

// The directions are the stick unless N64F_DPAD_DIGITAL, so no separate stick rows.
static const pad_target_t k_targets[] = {
    { ACT_PAD(PAD_A), "A" },           { ACT_PAD(PAD_B), "B" },
    { ACT_PAD(PAD_SELECT), "Z" },      { ACT_PAD(PAD_START), "Start" },
    { ACT_PAD(PAD_L), "L" },           { ACT_PAD(PAD_R), "R" },
    { ACT_PAD(PAD_C_UP), "C-Up" },     { ACT_PAD(PAD_C_DOWN), "C-Down" },
    { ACT_PAD(PAD_C_LEFT), "C-Left" }, { ACT_PAD(PAD_C_RIGHT), "C-Right" },
    PT_DPAD,
};

const pad_driver_t drv_n64 = {
    .name = "n64",
    .label = "Nintendo 64",
    .proto = PROTO_N64,
    PAD_TARGETS(k_targets),
    .res =
        {
            .ctrl_mask = 1U << 0, // CTRL0 = GP32, the Joybus data line
            .ctrl_out_mask = 1U << 0,
            .pio_sms = 2,
            .pio_words = PIO_WORDS(jbdev_rx) + PIO_WORDS(jbdev_tx),
            .dma_chans = 1,
            .logic_5v = 0, // Joybus is 3.3 V
            .needs_5v = 0,
        },
    .init = n64_init,
    .deinit = n64_deinit,
    .service = n64_service,
    .reconfig = n64_reconfig,
    .link_up = n64_link_up,
};
