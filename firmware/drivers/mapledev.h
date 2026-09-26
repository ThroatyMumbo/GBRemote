// mapledev.h — device-side Maple transport, protocol-agnostic: decodes frames off the two lines and
// clocks replies back. Core1 with interrupts masked; every function is __not_in_flash_func.
#ifndef MAPLEDEV_H
#define MAPLEDEV_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/pio.h"
#include "maple_proto.h"

// A frame staged for transmission: the sync pulse count, the bit-pair count, then the payload.
// Held as words because that is what the TX DMA feeds the SM.
typedef struct {
    uint32_t w[MAPLE_MAX_WORDS + 2];
    unsigned nw, nbytes;
} mapledev_pkt_t;

typedef struct {
    // bad_ev: a decode error once locked; desync: one before. frame_timeout: a frame outran the
    // hard cap and the decoder was resynced.
    uint32_t frames, replies, bad_ev, bad_frame, desync, overruns, no_idle, resyncs, frame_timeout;
    // Wire time of the longest reply, against the 5.184 us/byte the bit rate implies.
    uint32_t tx_last, tx_max, tx_bytes_max;
    // Reply done -> receiver armed again: the only window that can lose the next request.
    uint32_t rearm_last, rearm_max;
    // The sampler pushed into a full FIFO and lost samples; the hardware's flag, not inferred.
    uint32_t rx_stall;
    // Longest unattended stretch; the 8-word FIFO holds ~16 us of samples.
    uint32_t gap_max;
    // Stalls during a drain: the decoder itself fell behind the sample rate.
    uint32_t rx_stall_drain;
    // Decoder end event -> first reply bit, the console-referenced turnaround.
    uint32_t turn_min, turn_max, turn_last;
} mapledev_stats_t;

// Claims 4 SMs, 3 programs and 1 DMA channel. pio_set_gpio_base(pio, 16) must already have run,
// and the two pins must be consecutive with pin_a (SDCKA) lower.
bool mapledev_init(PIO pio, uint pin_a, uint pin_b);
void mapledev_deinit(void);

// Pack a struct-order frame, checksum included, into a staged packet.
void mapledev_stage(mapledev_pkt_t *pkt, const uint8_t *frame, unsigned n);

// Rewrite the staged frame word's address bytes for the port we were addressed on. The checksum
// is unaffected: both bytes take the same port bits, so they cancel in the XOR.
void mapledev_set_port(mapledev_pkt_t *pkt, uint8_t port);

// Drain the sampler until a whole checksum-valid frame arrives or timeout_us elapses. On true,
// frame holds the request in struct order with the checksum already dropped.
bool mapledev_poll(uint8_t *frame, unsigned *len, uint32_t timeout_us);

// Silences RX (or it decodes our own reply), rearms the SM, waits for the console to release the
// bus, then starts the DMA. False: the bus never went idle and nothing was driven.
bool mapledev_send(const mapledev_pkt_t *pkt);
bool mapledev_wait_sent(const mapledev_pkt_t *pkt);

// Back to a known-idle receiver from any state, including mid-frame. Idempotent.
void mapledev_resync(void);

const mapledev_stats_t *mapledev_stats(void);

// Sample-FIFO occupancy right now. Anything but 0 on an idle bus means the receiver is being fed
// while nothing is happening, and a frame's start pattern arrives into a full FIFO.
unsigned mapledev_rx_level(void);

#endif
