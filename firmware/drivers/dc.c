// dc.c — the Dreamcast driver: an HKT-7700 with a Visual Memory in slot 1 (storage, clock, LCD), no
// rumble. The card itself lives in vmu_stage.h, which core0 writes to flash.
#include <stdio.h>
#include <string.h>
#include "pad_driver.h"
#include "config_map.h"
#include "pinmap.h"
#include "mapledev.h"
#include "mapledev.pio.h"
#include "devcommon.h"
#include "maple_proto.h"
#include "vmu_stage.h"
#include "pico/time.h"

// CFG_DRV block, owned by this driver.
enum { DCC_FLAGS = 0 };
enum { DCF_DPAD_ANALOG = 1U << 0 };

// The next request follows a reply by microseconds, and a trip through driver_core misses its start
// pattern, so stay in the drain loop this long. Took missed block reads from 5 per survey to 0.
#define DC_BURST_US 300

static bool         g_dpad_analog;
static uint32_t     g_last_cmd_ms;
static maple_cond_t g_cond;         // pre-encoded in the idle gap, never inside a frame

// Which addresses we answer for: an HKT-7700 with a Visual Memory in slot 1.
static maple_bus_t  g_bus;
static maple_unit_t g_unit_controller, g_unit_vmu;
static maple_vmu_t  g_vmu;
static uint32_t     g_clock_pub_secs, g_clock_pub_ms;

#ifdef BENCH_PROTO
// Bench 'u'. A frame we heard but did not answer is a different fault from hearing none at all,
// and the header says which — without it the two are one silent symptom.
static uint8_t  g_last_hdr[4];
static uint32_t g_seen, g_dropped, g_bread_seen, g_bread_sent;
#define DC_STAT(stmt) do { stmt; } while (0)
#else
#define DC_STAT(stmt) do { } while (0)
#endif

// Enumeration replies are pre-packed so a DEVICE_REQUEST is only a port patch inside the turnaround.
// One pair per unit, or the VMU enumerates as a second controller.
static mapledev_pkt_t g_info[MAPLE_MAX_UNITS], g_allinfo[MAPLE_MAX_UNITS], g_reply;

// Static, not stack: core1's stack is 2 KB and a block-read reply is 525 bytes. dc_prebuild()
// runs in init and dc_service() after it, so one pair serves both.
static uint8_t g_req[MAPLE_MAX_FRAME], g_frame[MAPLE_MAX_FRAME];

static void dc_load_cfg(const uint8_t *cfg) {
    if (!cfg) { g_dpad_analog = false; return; }
    uint8_t f = cfg[DCC_FLAGS];
    g_dpad_analog = (f != 0xff) && (f & DCF_DPAD_ANALOG);
}

// After every maple_bus_add(): the sub-unit bits enter the frame before its checksum, and only the
// port bits cancel in the XOR for mapledev_set_port() to patch later.
static void dc_prebuild(void) {
    uint8_t req[4];

    req[2] = MAPLE_ADDR_DC;
    req[3] = 0;

    for (unsigned i = 0; i < g_bus.n; i++) {
        req[1] = g_bus.unit[i]->addr;

        req[0] = MAPLE_CMD_DEVICE_REQUEST;
        unsigned n = maple_build_reply(&g_bus, req, 4, g_frame);
        mapledev_stage(&g_info[i], g_frame, n);

        req[0] = MAPLE_CMD_ALL_STATUS_REQUEST;
        n = maple_build_reply(&g_bus, req, 4, g_frame);
        mapledev_stage(&g_allinfo[i], g_frame, n);
    }
}

static void on_card_write(void *ctx, unsigned block, uint32_t now_ms) {
    (void)block;                            // core0 diffs the card itself; see vmu_stage.h
    vmu_mark(ctx, now_ms);
}

static void on_lcd_frame(void *ctx, const uint8_t *frame, unsigned n) {
    vmu_lcd_publish(ctx, frame, n);
}

// The card is not formatted here: dc_init() runs on every drv_apply(), so formatting would wipe
// the save on a protocol change. vmu_store_init() has already laid one down if flash had none.
static pad_err_t dc_init(const pad_driver_t *d, const uint8_t *cfg) {
    (void)d;
    vmu_stage_t *st = vmu_stage();

    dc_load_cfg(cfg);
    g_last_cmd_ms = 0;
    maple_cond_default(&g_cond);
    maple_bus_init(&g_bus);
    maple_controller_unit(&g_unit_controller, &g_cond);
    maple_bus_add(&g_bus, &g_unit_controller);
    maple_vmu_unit(&g_unit_vmu, &g_vmu, vmu_card(), MAPLE_SUB_UNIT(1));
    g_vmu.on_write = on_card_write;
    g_vmu.on_lcd   = on_lcd_frame;
    g_vmu.ctx      = st;
    g_vmu.now_ms   = to_ms_since_boot(get_absolute_time());

    // dc_init() runs on every drv_apply(), so the clock has to survive one: carry the running
    // value forward, and fall back to the date the last save carried only on the first attach.
    if (g_clock_pub_secs) {
        g_vmu.clock_secs = g_clock_pub_secs + (g_vmu.now_ms - g_clock_pub_ms) / 1000U;
    } else if (st->boot_secs) {
        g_vmu.clock_secs = st->boot_secs;
    }
    g_vmu.clock_base_ms = g_vmu.now_ms;
    g_clock_pub_secs = g_vmu.clock_secs;
    g_clock_pub_ms   = g_vmu.clock_base_ms;
    vmu_clock_publish(st, g_clock_pub_secs, g_clock_pub_ms);
    maple_bus_add(&g_bus, &g_unit_vmu);
    if (!mapledev_init(pio2, PIN_MAPLE_A, PIN_MAPLE_B)) { return PAD_ERR_NO_RESOURCE; }
    dc_prebuild();
    st->epoch++;
    return PAD_OK;
}

// Ask for the writeback and leave: core0 finishes it long after the arena is gone, and waiting
// here is what costs the Transfer Pak a quarter second of dead pad.
static void dc_deinit(const pad_driver_t *d) {
    (void)d;
    vmu_stage_t *st = vmu_stage();
    st->flush_req++;
    st->epoch++;
    mapledev_deinit();
}

static pad_err_t dc_reconfig(const pad_driver_t *d, const uint8_t *cfg) {
    (void)d;
    dc_load_cfg(cfg);
    return PAD_OK;
}

static bool dc_link_up(const pad_driver_t *d) {
    (void)d;
    return pad_link_recent(g_last_cmd_ms, to_ms_since_boot(get_absolute_time()));
}

static void __not_in_flash_func(dc_service)(const pad_driver_t *d, const pad_state_t *s,
                                            uint32_t idle_us) {
    (void)d;

    // Re-encode in the idle gap. Inside a frame nothing reads the pad, only g_cond.
    maple_from_pad(s, g_dpad_analog, &g_cond);
    g_vmu.now_ms = to_ms_since_boot(get_absolute_time());    // the clock's only time source

    // The console sets the date once at power-up, so this publishes on change: core0 needs the
    // base to stamp a save with, and a seqlock write every pass would make it retry for nothing.
    if (g_vmu.clock_secs != g_clock_pub_secs || g_vmu.clock_base_ms != g_clock_pub_ms) {
        g_clock_pub_secs = g_vmu.clock_secs;
        g_clock_pub_ms   = g_vmu.clock_base_ms;
        vmu_clock_publish(g_vmu.ctx, g_clock_pub_secs, g_clock_pub_ms);
    }

    // Also the idle gap: a block core0 staged goes down whole, before any frame can read it.
    vmu_poke_take(g_vmu.ctx, g_vmu.card, g_vmu.now_ms);

    uint32_t budget = idle_us;
    bool served = false;

    for (;;) {
        unsigned rn;
        if (!mapledev_poll(g_req, &rn, budget)) { break; }

        DC_STAT(g_seen++; memcpy(g_last_hdr, g_req, sizeof g_last_hdr));

        // maple_build_reply()'s own decode, so one address table; this also drops our own replies
        // (addressed to the Dreamcast) and frames for sub-peripherals we do not have.
        int ui = maple_bus_find(&g_bus, maple_dst(g_req));
        if (ui < 0) {
            DC_STAT(g_dropped++);
            mapledev_resync();
            budget = DC_BURST_US;
            continue;
        }

        mapledev_pkt_t *pkt = NULL;
        switch (maple_cmd(g_req)) {
        case MAPLE_CMD_DEVICE_REQUEST:     pkt = &g_info[ui];    break;
        case MAPLE_CMD_ALL_STATUS_REQUEST: pkt = &g_allinfo[ui]; break;
        default: {
            unsigned n = maple_build_reply(&g_bus, g_req, rn, g_frame);
            if (n) { mapledev_stage(&g_reply, g_frame, n); pkt = &g_reply; }
            break;
        }
        }

        // Which side loses a block read: the request we never heard, or the reply that did not
        // land. Every other counter here is blind to that distinction.
        if (maple_cmd(g_req) == MAPLE_CMD_BLOCK_READ) { DC_STAT(g_bread_seen++); }

        if (pkt) {
            mapledev_set_port(pkt, maple_src(g_req) & MAPLE_PORT_MASK);
            if (mapledev_send(pkt) && mapledev_wait_sent(pkt) &&
                maple_cmd(g_req) == MAPLE_CMD_BLOCK_READ) {
                DC_STAT(g_bread_sent++);
            }
        }

        // Re-arm before anything else: a 64-bit time divide here is enough to miss the next
        // request's start pattern.
        mapledev_resync();      // unconditional: the receiver watched our own reply go out
        served = true;
        budget = DC_BURST_US;
    }

    if (served) { g_last_cmd_ms = to_ms_since_boot(get_absolute_time()); }
}

#ifdef BENCH_PROTO
// Bench 'u'.
void dc_trace_dump(void) {
    const mapledev_stats_t *s = mapledev_stats();
    printf("  dc: seen=%lu dropped=%lu bread=%lu/%lu last=%02x %02x %02x %02x  bus n=%u sub=%02x\n",
           (unsigned long)g_seen, (unsigned long)g_dropped,
           (unsigned long)g_bread_sent, (unsigned long)g_bread_seen,
           g_last_hdr[0], g_last_hdr[1], g_last_hdr[2], g_last_hdr[3],
           g_bus.n, g_bus.sub_mask);
    printf("  maple: frames=%lu replies=%lu bad_ev=%lu bad_frame=%lu desync=%lu ovr=%lu "
           "no_idle=%lu resync=%lu ftmo=%lu turn=%lu/%lu/%luus\n",
           (unsigned long)s->frames, (unsigned long)s->replies, (unsigned long)s->bad_ev,
           (unsigned long)s->bad_frame, (unsigned long)s->desync, (unsigned long)s->overruns,
           (unsigned long)s->no_idle, (unsigned long)s->resyncs, (unsigned long)s->frame_timeout,
           (unsigned long)s->turn_min, (unsigned long)s->turn_last, (unsigned long)s->turn_max);
    printf("  tx: last=%luus max=%luus over %lu bytes (%lu ns/byte)  rearm=%lu/%luus\n",
           (unsigned long)s->tx_last, (unsigned long)s->tx_max, (unsigned long)s->tx_bytes_max,
           s->tx_bytes_max ? (unsigned long)(s->tx_max * 1000u / s->tx_bytes_max) : 0ul,
           (unsigned long)s->rearm_last, (unsigned long)s->rearm_max);
    printf("  rx fifo level now=%u of 8, sampler stalled (samples lost) %lu times, "
           "longest unattended gap %luus (FIFO holds ~16us)\n",
           mapledev_rx_level(), (unsigned long)s->rx_stall, (unsigned long)s->gap_max);
    printf("  of those, %lu happened WHILE draining (decoder too slow for the sample rate)\n",
           (unsigned long)s->rx_stall_drain);
}
#endif

static const pad_target_t k_targets[] = {
    { ACT_PAD(PAD_A), "A" },     { ACT_PAD(PAD_B), "B" },
    { ACT_PAD(PAD_X), "X" },     { ACT_PAD(PAD_Y), "Y" },
    { ACT_PAD(PAD_START), "Start" },
    { ACT_PAD(PAD_L), "L Trig" }, { ACT_PAD(PAD_R), "R Trig" },
    PT_DPAD, PT_STICK,
};

const pad_driver_t drv_dc = {
    .name = "dreamcast",
    .label = "Dreamcast",
    .proto = PROTO_DREAMCAST,
    PAD_TARGETS(k_targets),
    .res = {
        .ctrl_mask = 0x03,              // CTRL0 = SDCKA, CTRL1 = SDCKB, and they must be adjacent
        .ctrl_out_mask = 0x03,
        .pio_sms = 4,
        .pio_words = PIO_WORDS(maple_tx) + PIO_WORDS(maple_rx_edge) + PIO_WORDS(maple_rx_sample),
        .dma_chans = 1,
        .logic_5v = 0,                  // both references drive Maple straight from 3.3 V pads
        .needs_5v = 1,                  // the console's controller port sources it
        .excl_ble = 1,                  // takes all of PIO2, so no concurrent CYW43 gSPI
    },
    .init = dc_init, .deinit = dc_deinit, .service = dc_service,
    .reconfig = dc_reconfig, .link_up = dc_link_up,
};
