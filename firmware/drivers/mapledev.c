#include "mapledev.h"
#include "mapledev.pio.h"
#include "devcommon.h"
#include "hardware/dma.h"
#include "hardware/timer.h"
#include "hardware/gpio.h"

#define MAPLE_IRQ 7

// Console-referenced turnaround: both lines released, then a settle.
#define MAPLEDEV_IDLE_TIMEOUT_US 30
#define MAPLEDEV_TURNAROUND_US    1

// How long a frame already in progress may run past the caller's idle budget. The longest frame
// on the bus is 525 bytes at 5.184 us a byte = 2.7 ms; this is that with headroom.
#define MAPLEDEV_FRAME_MAX_US  4000

// PIO2 is 32 words and this driver declares excl_ble, so it may have all of them. Break the build
// here rather than discovering it as a failed init on a console.
_Static_assert(PIO_WORDS(maple_tx) + PIO_WORDS(maple_rx_edge) + PIO_WORDS(maple_rx_sample) <= 32,
               "mapledev outgrew PIO2");

static pio_res_t g_res;
static PIO      g_pio;
static uint     g_sm_tx, g_sm_sample, g_sm_edge_a, g_sm_edge_b;
static uint     g_off_tx, g_off_edge, g_off_sample;
static uint     g_pin_a, g_pin_b;
static int      g_dma = -1;
static maple_rx_t g_rx;
static bool       g_locked;                 // a frame has decoded since the last re-arm
static mapledev_stats_t g_st;
static uint32_t g_last_drain_us;
static uint32_t   g_end_us;                 // timerawl at the decoder's end event

static void tx_park(void);

static void __not_in_flash_func(rx_set)(bool on) {
    uint32_t mask = (1U << g_sm_sample) | (1U << g_sm_edge_a) | (1U << g_sm_edge_b);
    pio_set_sm_mask_enabled(g_pio, mask, on);
}

bool mapledev_init(PIO pio, uint pin_a, uint pin_b) {
    static const struct pio_program *const progs[] = {
        &maple_tx_program, &maple_rx_edge_program, &maple_rx_sample_program,
    };

    g_pio = NULL;
    g_pin_a = pin_a;
    g_pin_b = pin_b;
    if (!pio_res_claim(&g_res, pio, progs, 3, 4)) { return false; }
    g_dma = dma_claim_unused_channel(false);
    if (g_dma < 0) { pio_res_release(&g_res); return false; }

    g_pio = pio;
    g_off_tx = g_res.off[0]; g_off_edge = g_res.off[1]; g_off_sample = g_res.off[2];
    g_sm_tx = g_res.sm[0]; g_sm_edge_a = g_res.sm[1]; g_sm_edge_b = g_res.sm[2]; g_sm_sample = g_res.sm[3];

    maple_pin_init(pio, pin_a, pin_b);
    maple_tx_program_init(pio, g_sm_tx, g_off_tx, pin_a, pin_b);
    maple_rx_edge_program_init(pio, g_sm_edge_a, g_off_edge, pin_a);
    maple_rx_edge_program_init(pio, g_sm_edge_b, g_off_edge, pin_b);
    maple_rx_sample_program_init(pio, g_sm_sample, g_off_sample, pin_a);

    dma_channel_config c = dma_channel_get_default_config((uint)g_dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, g_sm_tx, true));
    dma_channel_configure((uint)g_dma, &c, &pio->txf[g_sm_tx], NULL, 0, false);

    maple_rx_init(&g_rx);
    tx_park();                                  // idle is a stopped SM with the bus released
    mapledev_resync();
    return true;
}

void mapledev_deinit(void) {
    if (!g_pio) { return; }
    pio_sm_set_enabled(g_pio, g_sm_tx, false);
    rx_set(false);
    if (g_dma >= 0) { dma_channel_unclaim((uint)g_dma); g_dma = -1; }
    pio_res_release(&g_res);
    g_pio = NULL;
}

void __not_in_flash_func(mapledev_stage)(mapledev_pkt_t *pkt, const uint8_t *frame, unsigned n) {
    pkt->w[0] = 3;                              // sync pulses; a data word, not a `set y`
    pkt->w[1] = maple_bit_pairs_minus1(n);
    pkt->nw = maple_pack_words(frame, n, &pkt->w[2]) + 2U;
    pkt->nbytes = n;
}

void __not_in_flash_func(mapledev_set_port)(mapledev_pkt_t *pkt, uint8_t port) {
    // The frame word is packed little-endian as cmd | dst<<8 | src<<16 | nwords<<24.
    uint32_t w = pkt->w[2];
    uint8_t dst = (uint8_t)((w >> 8) & MAPLE_PERIPH_MASK) | port;
    uint8_t src = (uint8_t)((w >> 16) & MAPLE_PERIPH_MASK) | port;
    pkt->w[2] = (w & 0xff0000ffU) | ((uint32_t)dst << 8) | ((uint32_t)src << 16);
}

void __not_in_flash_func(mapledev_resync)(void) {
    uint32_t t0 = timer_hw->timerawl;
    rx_set(false);
    pio_sm_clear_fifos(g_pio, g_sm_sample);
    pio_sm_restart(g_pio, g_sm_edge_a);
    pio_sm_restart(g_pio, g_sm_edge_b);
    pio_sm_restart(g_pio, g_sm_sample);
    // pio_sm_restart() leaves the PC alone, so re-enter explicitly.
    pio_sm_exec(g_pio, g_sm_edge_a, pio_encode_jmp(g_off_edge));
    pio_sm_exec(g_pio, g_sm_edge_b, pio_encode_jmp(g_off_edge));
    pio_sm_exec(g_pio, g_sm_sample, pio_encode_jmp(g_off_sample));
    pio_interrupt_clear(g_pio, MAPLE_IRQ);
    maple_rx_reset(&g_rx);
    g_locked = false;
    rx_set(true);
    g_st.resyncs++;
    g_st.rearm_last = timer_hw->timerawl - t0;
    if (g_st.rearm_last > g_st.rearm_max) { g_st.rearm_max = g_st.rearm_last; }
}

bool __not_in_flash_func(mapledev_poll)(uint8_t *frame, unsigned *len, uint32_t timeout_us) {
    uint32_t t0 = timer_hw->timerawl;
    uint32_t stallbit = 1U << (PIO_FDEBUG_RXSTALL_LSB + g_sm_sample);
    uint32_t gap = t0 - g_last_drain_us;

    if (g_last_drain_us && gap > g_st.gap_max) { g_st.gap_max = gap; }
    if (g_pio->fdebug & stallbit) { g_pio->fdebug = stallbit; g_st.rx_stall++; }
    for (;;) {
        while (!pio_sm_is_rx_fifo_empty(g_pio, g_sm_sample)) {
            uint8_t v = (uint8_t)pio_sm_get(g_pio, g_sm_sample);
            if (g_pio->fdebug & stallbit) { g_pio->fdebug = stallbit; g_st.rx_stall_drain++; }
            for (int s = 3; s >= 0; s--) {              // oldest sample sits in the high bits
                maple_rx_ev_t ev = maple_rx_feed(&g_rx, (uint8_t)((v >> (s * 2)) & 3U));
                if (ev == MAPLE_RX_ERROR) {
                    if (g_locked) {
                        g_st.bad_ev++;
                    } else {
                        g_st.desync++;
                    }
                    continue;
                }
                if (ev != MAPLE_RX_FRAME) { continue; }

                unsigned n = maple_unpack(g_rx.buf, g_rx.len, frame);
                if (n == 0) { g_st.bad_frame++; continue; }
                *len = n;
                g_locked = true;                        // synchronized: errors now mean corruption
                g_st.frames++;
                g_end_us = timer_hw->timerawl;
                g_last_drain_us = g_end_us;
                return true;
            }
        }
        // Abandoning a frame mid-flight on the caller's idle budget would hand back a partial
        // decode that only the static g_rx happens to make survivable. Ride it out to the cap.
        uint32_t waited = timer_hw->timerawl - t0;
        if (waited > timeout_us) {
            g_last_drain_us = timer_hw->timerawl;
            if (!maple_rx_in_frame(&g_rx)) { return false; }
            if (waited > timeout_us + MAPLEDEV_FRAME_MAX_US) {
                g_st.frame_timeout++;
                mapledev_resync();
                return false;
            }
        }
    }
}

// gpio_get, not a masked sio_hw->gpio_in read: the controller port is GP32-39 and those live in
// gpio_hi_in, so `1u << pin` on the low register is a different pin entirely.
static bool __not_in_flash_func(wait_bus_idle)(void) {
    uint32_t t0 = timer_hw->timerawl;
    while (!(gpio_get(g_pin_a) && gpio_get(g_pin_b))) {
        if (timer_hw->timerawl - t0 > MAPLEDEV_IDLE_TIMEOUT_US) { return false; }
    }

    uint32_t t1 = timer_hw->timerawl;
    while (timer_hw->timerawl - t1 < MAPLEDEV_TURNAROUND_US) { }
    return true;
}

bool __not_in_flash_func(mapledev_send)(const mapledev_pkt_t *pkt) {
    rx_set(false);                              // or we decode our own frame and overrun the FIFO

    // The last frame left the OSR partial; restarting clears it (maple_model's one-frame bound).
    // Rearming drives nothing: the SM releases the bus and stalls on `out y,32`.
    pio_sm_set_enabled(g_pio, g_sm_tx, false);
    pio_sm_clear_fifos(g_pio, g_sm_tx);
    pio_sm_restart(g_pio, g_sm_tx);
    pio_sm_exec(g_pio, g_sm_tx, pio_encode_jmp(g_off_tx));
    pio_sm_set_enabled(g_pio, g_sm_tx, true);

    // Never drive into the console. Losing a reply costs one poll; a contention does not.
    if (!wait_bus_idle()) { g_st.no_idle++; return false; }

    dma_channel_set_read_addr((uint)g_dma, pkt->w, false);
    dma_channel_set_trans_count((uint)g_dma, pkt->nw, true);
    uint32_t turn = timer_hw->timerawl - g_end_us;
    g_st.turn_last = turn;
    if (turn > g_st.turn_max) { g_st.turn_max = turn; }
    if (!g_st.turn_min || turn < g_st.turn_min) { g_st.turn_min = turn; }
    g_st.replies++;
    return true;
}

// Restart clears the partial OSR, `set pindirs,0` releases the lines, and the SM stays disabled so it
// cannot wrap back into a frame.
static void __not_in_flash_func(tx_park)(void) {
    pio_sm_set_enabled(g_pio, g_sm_tx, false);
    pio_sm_clear_fifos(g_pio, g_sm_tx);
    pio_sm_restart(g_pio, g_sm_tx);
    pio_sm_exec(g_pio, g_sm_tx, pio_encode_set(pio_pindirs, 0));
    pio_sm_exec(g_pio, g_sm_tx, pio_encode_jmp(g_off_tx));
}

// The frame is out once the PC is back in the preamble; a fixed delay lets the SM re-drive the bus
// past `.wrap`.
static inline bool __not_in_flash_func(tx_wrapped)(void) {
    uint8_t pc = pio_sm_get_pc(g_pio, g_sm_tx);
    return (uint8_t)(pc - g_off_tx) <= 7U;
}

bool __not_in_flash_func(mapledev_wait_sent)(const mapledev_pkt_t *pkt) {
    // ~5.2 us a byte at the Maple bit rate, plus the start and end patterns.
    uint32_t budget = pkt->nbytes * 8U + 400U;
    uint32_t t0 = timer_hw->timerawl;

    while (dma_channel_is_busy((uint)g_dma)) {
        if (timer_hw->timerawl - t0 > budget) { goto late; }
    }
    while (!pio_sm_is_tx_fifo_empty(g_pio, g_sm_tx)) {
        if (timer_hw->timerawl - t0 > budget) { goto late; }
    }

    while (!tx_wrapped()) {
        if (timer_hw->timerawl - t0 > budget) { goto late; }
    }
    tx_park();
    g_st.tx_last = timer_hw->timerawl - t0;
    if (g_st.tx_last > g_st.tx_max) { g_st.tx_max = g_st.tx_last; g_st.tx_bytes_max = pkt->nbytes; }
    return true;
late:
    g_st.overruns++;                            // a nonzero count is the evidence, not a fix
    tx_park();                                  // never leave the bus driven, however we got here
    return false;
}

const mapledev_stats_t *mapledev_stats(void) { return &g_st; }

unsigned mapledev_rx_level(void) {
    return g_pio ? pio_sm_get_rx_fifo_level(g_pio, g_sm_sample) : 0U;
}
