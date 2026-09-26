// capenc.h — wire format v1, delta-RLE bus events for the port egress; byte-identical to
// demos/capture/tools/capenc.py. No SDK dependencies, so the host test builds it too.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CAPENC_PAYLOAD_MAX 1024
#define CAPENC_HDR         16
#define CAPENC_PKT_MAX     (CAPENC_HDR + CAPENC_PAYLOAD_MAX + 4)

#define CAPENC_F_RESET (1u << 0)   // start of trace / loop wrap
#define CAPENC_F_RST   (1u << 1)   // /RST level for every event in the packet

typedef void (*capenc_sink_t)(const uint8_t *pkt, size_t len, void *ctx);

typedef struct {
    uint8_t  pkt[CAPENC_PKT_MAX];
    size_t   len;                  // payload bytes staged so far (pkt[CAPENC_HDR..CAPENC_HDR+len))
    uint32_t seq;
    int32_t  prev_addr;            // -1 = packet start, so the next record is absolute
    bool     pkt_rst;
    bool     first;                // next packet carries F_RESET

    bool     have;                 // a record is pending, waiting to see if its run extends
    uint16_t p_addr;
    uint8_t  p_strobes;
    uint8_t  p_data;
    bool     p_has_data;
    bool     p_rst;
    uint32_t p_run;

    uint32_t dropped;              // events lost since the last packet: a live source cannot pause
    capenc_sink_t sink;
    void    *ctx;
} capenc_t;

// Rejoins the /WR-latched stream with the CLK-relative one, which pace off different DREQs. Same
// rule as capfile.merge(): a write word claims the next main-stream event at its address.
#define CAPMRG_Q 16

typedef struct {
    uint32_t q[CAPMRG_Q];
    uint8_t  head, count;
    uint32_t lost;                 // write words evicted because the queue was full
} capmrg_t;

void     capmrg_init(capmrg_t *m);
void     capmrg_push_wr(capmrg_t *m, uint32_t w);   // one word from the /WR-latched SM
uint32_t capmrg_apply(capmrg_t *m, uint32_t main_word);

void capenc_init(capenc_t *e, capenc_sink_t sink, void *ctx);

// Feed one raw capture word (GPn -> bit n, as capture.pio samples it).
void capenc_word(capenc_t *e, uint32_t w);

// Same output, batched: prefer it on the target, where per-event calls blow the cycle budget.
void capenc_words(capenc_t *e, const uint32_t *ws, size_t n);

// Emit any pending record and close the open packet. Call at end of trace or before a loop wrap.
void capenc_flush(capenc_t *e);

// Record n events lost to a capture-side overrun; reported in the next packet header.
void capenc_drop(capenc_t *e, uint32_t n);

uint32_t capenc_crc32(uint32_t crc, const uint8_t *p, size_t n);
