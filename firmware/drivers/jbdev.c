#include "jbdev.h"
#include "jbdev.pio.h"
#include "pio_run.h"
#include "devcommon.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

#define RX_IRQ 4

_Static_assert(PIO_WORDS(jbdev_rx) + PIO_WORDS(jbdev_tx) <= PIO2_SHARED_WORDS,
               "jbdev outgrew PIO2's budget with the CYW43 gSPI resident");

static pio_res_t g_res;
static PIO      g_pio;
static uint     g_sm_rx, g_sm_tx, g_off_rx, g_off_tx, g_pin;
static int      g_dma = -1;
static uint32_t g_reply[JBDEV_MAX_REPLY];
static unsigned g_reply_n;
static jbdev_stats_t g_st;

bool jbdev_init(PIO pio, uint pin) {
    static const struct pio_program *const progs[] = { &jbdev_rx_program, &jbdev_tx_program };

    g_pio = NULL;
    g_pin = pin;
    if (!pio_res_claim(&g_res, pio, progs, 2, 2)) { return false; }
    g_dma = dma_claim_unused_channel(false);
    if (g_dma < 0) { pio_res_release(&g_res); return false; }

    g_pio = pio;
    g_off_rx = g_res.off[0]; g_off_tx = g_res.off[1];
    g_sm_rx  = g_res.sm[0];  g_sm_tx  = g_res.sm[1];

    jbdev_pin_init(pio, pin);
    jbdev_rx_program_init(pio, g_sm_rx, g_off_rx, pin);
    jbdev_tx_program_init(pio, g_sm_tx, g_off_tx, pin);

    dma_channel_config c = dma_channel_get_default_config((uint)g_dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, g_sm_tx, true));
    dma_channel_configure((uint)g_dma, &c, &pio->txf[g_sm_tx], g_reply, 0, false);

    pio_sm_run(pio, g_sm_tx, true);
    pio_sm_run(pio, g_sm_rx, true);
    return true;
}

void jbdev_deinit(void) {
    if (!g_pio) { return; }
    pio_sm_run(g_pio, g_sm_rx, false);
    pio_sm_run(g_pio, g_sm_tx, false);
    if (g_dma >= 0) { dma_channel_unclaim((uint)g_dma); g_dma = -1; }
    pio_res_release(&g_res);
    g_pio = NULL;
}

void __not_in_flash_func(jbdev_resync)(void) {
    pio_sm_run(g_pio, g_sm_rx, false);
    pio_sm_clear_fifos(g_pio, g_sm_rx);
    pio_sm_restart(g_pio, g_sm_rx);
    // pio_sm_restart() does not promise to reset the PC, so re-enter explicitly.
    pio_sm_exec(g_pio, g_sm_rx, pio_encode_jmp(g_off_rx + jbdev_rx_offset_frame_end));
    pio_interrupt_clear(g_pio, RX_IRQ);
    pio_sm_run(g_pio, g_sm_rx, true);
    g_st.resyncs++;
}

jbdev_result_t __not_in_flash_func(jbdev_wait_opcode)(uint8_t *opcode, uint32_t timeout_us) {
    uint32_t t0 = timer_hw->timerawl;
    while (pio_sm_is_rx_fifo_empty(g_pio, g_sm_rx)) {
        if (timer_hw->timerawl - t0 > timeout_us) { return JBDEV_NONE; }
    }

    *opcode = (uint8_t)(pio_sm_get(g_pio, g_sm_rx) & 0xffU);
    // IRQ 4 fires in every inter-frame gap; only from here on does it mean truncation.
    pio_interrupt_clear(g_pio, RX_IRQ);
    g_st.frames++;
    return JBDEV_OK;
}

jbdev_result_t __not_in_flash_func(jbdev_read_request)(uint8_t *buf, unsigned n) {
    for (unsigned i = 0; i < n; i++) {
        uint32_t t0 = timer_hw->timerawl;
        while (pio_sm_is_rx_fifo_empty(g_pio, g_sm_rx)) {
            if (pio_interrupt_get(g_pio, RX_IRQ)) { g_st.truncated++; return JBDEV_TRUNCATED; }
            if (timer_hw->timerawl - t0 > JBDEV_BYTE_TIMEOUT_US) {
                g_st.truncated++;
                return JBDEV_TRUNCATED;
            }
        }
        buf[i] = (uint8_t)(pio_sm_get(g_pio, g_sm_rx) & 0xffU);
    }
    return JBDEV_OK;
}

void __not_in_flash_func(jbdev_stage_reply)(const uint8_t *bytes, unsigned n) {
    if (n > JBDEV_MAX_REPLY) { n = JBDEV_MAX_REPLY; }
    for (unsigned i = 0; i < n; i++) {
        g_reply[i] = (uint32_t)bytes[i] << 24; // MSB-justified
    }
    g_reply_n = n;
    dma_channel_set_read_addr((uint)g_dma, g_reply, false);
    dma_channel_set_trans_count((uint)g_dma, n, false);
}

// The turnaround is console-referenced: a 3 us high inside a '1' outlasts the ~2 us we owe, so no
// delay can stand in for landing on the stop bit's rising edge.
static bool __not_in_flash_func(wait_stop_bit)(void) {
    uint32_t t0 = timer_hw->timerawl;
    // gpio_get: GP32+ is not in sio_hw->gpio_in.
    while (!gpio_get(g_pin)) { // tail of the last bit's low
        if (timer_hw->timerawl - t0 > JBDEV_STOP_TIMEOUT_US) { return false; }
    }
    while (gpio_get(g_pin)) { // stop bit's falling edge
        if (timer_hw->timerawl - t0 > JBDEV_STOP_TIMEOUT_US) { return false; }
    }
    while (!gpio_get(g_pin)) { // stop bit's rising edge
        if (timer_hw->timerawl - t0 > JBDEV_STOP_TIMEOUT_US) { return false; }
    }
    return true;
}

jbdev_result_t __not_in_flash_func(jbdev_await_stop)(void) {
    // Silence RX first, or it samples our own transmission and every later frame is offset a bit.
    pio_sm_run(g_pio, g_sm_rx, false);

    if (!wait_stop_bit()) { g_st.no_stop++; return JBDEV_NO_STOP; }
    return JBDEV_OK;
}

void __not_in_flash_func(jbdev_send_reply)(void) {
    dma_channel_start((uint)g_dma);     // one store; TX unstalls 2.0 us later
    g_st.replies++;
}

jbdev_result_t __not_in_flash_func(jbdev_wait_sent)(void) {
    uint32_t budget = (uint32_t)g_reply_n * 40U + 200U; // 32 us/byte plus slack
    uint32_t t0 = timer_hw->timerawl;

    while (dma_channel_is_busy((uint)g_dma)) {
        if (timer_hw->timerawl - t0 > budget) { goto late; }
    }
    while (!pio_sm_is_tx_fifo_empty(g_pio, g_sm_tx)) {
        if (timer_hw->timerawl - t0 > budget) { goto late; }
    }
    while (pio_sm_get_pc(g_pio, g_sm_tx) != (uint8_t)(g_off_tx + jbdev_tx_offset_idle)) {
        if (timer_hw->timerawl - t0 > budget) { goto late; }
    }
    return JBDEV_OK;
late:
    g_st.short_sends++;                 // cannot be fixed here; a nonzero count is the evidence
    return JBDEV_TRUNCATED;
}

const jbdev_stats_t *jbdev_stats(void) { return &g_st; }
