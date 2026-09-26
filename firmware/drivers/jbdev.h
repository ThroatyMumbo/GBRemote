// jbdev.h — device-side single-wire Joybus transport, meaning-agnostic. core1, interrupts masked.
#ifndef JBDEV_H
#define JBDEV_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/pio.h"

#define JBDEV_MAX_REPLY       33    // 32-byte accessory block + its CRC
#define JBDEV_STOP_TIMEOUT_US 12    // give up hunting for the console's stop bit
#define JBDEV_BYTE_TIMEOUT_US 40    // a byte is 32 us on the wire

typedef enum {
    JBDEV_OK = 0,
    JBDEV_NONE,         // nothing on the wire inside the caller's timeout
    JBDEV_TRUNCATED,    // the line went idle mid-frame (the RX SM's own detector)
    JBDEV_NO_STOP,      // no stop bit appeared; nothing was driven
} jbdev_result_t;

typedef struct {
    uint32_t frames, replies, truncated, no_stop, short_sends, resyncs;
} jbdev_stats_t;

// Claims 2 SMs, 2 programs and 1 DMA channel; needs pio_set_gpio_base(pio, 16).
bool jbdev_init(PIO pio, uint pin);
void jbdev_deinit(void);

// Spin until the first byte of a frame arrives or timeout_us elapses.
jbdev_result_t jbdev_wait_opcode(uint8_t *opcode, uint32_t timeout_us);

// Read the remaining n bytes of the frame already in progress. Per-byte bounded.
jbdev_result_t jbdev_read_request(uint8_t *buf, unsigned n);

// Program the TX DMA without starting it.
void jbdev_stage_reply(const uint8_t *bytes, unsigned n);

// Silence RX and catch the console's stop bit; call it the instant the request is in, then build
// the reply. Anything before jbdev_send_reply() is turnaround.
jbdev_result_t jbdev_await_stop(void);

// Arm the staged DMA. One store separates it from the wire.
void jbdev_send_reply(void);

// Bounded wait for the reply to leave the wire, then verify the SM is back at idle.
jbdev_result_t jbdev_wait_sent(void);

// Return the transport to idle from any state, including a hung wait. Idempotent.
void jbdev_resync(void);

const jbdev_stats_t *jbdev_stats(void);

#endif
