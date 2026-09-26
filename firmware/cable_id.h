// cable_id.h — the adapter ID ladder on CTRL_ID (GP41 / ADC1): R24 10k to +3V3, R25 100R, R_id to
// GND in the shell. Each adapter is wired for one console, so a known cable picks the driver.
#ifndef CABLE_ID_H
#define CABLE_ID_H

#include <stdint.h>
#include <stdbool.h>

enum {
    ADAPT_NONE    = 0,      // open — also how a broken ID wire reads, which is the safe direction
    ADAPT_1       = 1,      // 33k .. ADAPT_9 = 0R
    ADAPT_9       = 9,
    ADAPT_UNKNOWN = 0xff,   // in no band: corroded, half-inserted, or a cable we do not know
};

enum { ADAPT_CAPTURE = 1, ADAPT_NES = 2, ADAPT_DC = 3, ADAPT_SNES = 4, ADAPT_GENESIS = 6, ADAPT_N64 = 7 };

// The PROTO_* to run with this cable in. sel is the raw CFG_PROTO_SEL byte: kept if the cable is
// wired for it, if it is an explicit Off (0x00), or if the slot names no console.
uint8_t cable_id_proto(uint8_t slot, uint8_t sel);

#define CABLE_ID_TICK_MS 100    // ~100x the divider's 1 ms RC, so every sample is settled

void    cable_id_init(void);

// One sample-classify-debounce pass from core0 at CABLE_ID_TICK_MS. The slot latches after 3
// agreeing samples, except ADAPT_NONE, which latches at once so an unplug tears the driver down.
uint8_t cable_id_tick(void);

uint8_t  cable_id_slot(void);
uint16_t cable_id_mv(void);     // last averaged reading, for telemetry and bench calibration

// cable_id_report.c: at most once a second, samples and writes "ctrl_id=<mV>mV" to UART1 (J4)
// whatever stdio is routed to. Every cart image that does not print cable_id_mv() calls it.
void cable_id_report(void);

// Pure, host-testable: mV -> slot, and the debounce step cable_id_tick() feeds.
uint8_t cable_id_classify(uint16_t mv);
uint8_t cable_id_step(uint16_t mv);
void    cable_id_reset(void);

#endif
