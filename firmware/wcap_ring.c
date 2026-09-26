// wcap_ring.c — write_capture -> SRAM ring by DMA, for demos that stream
// through the mailbox. The product drains with pio_sm_get and does not link this.
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/address_mapped.h"
#include "cartserve.h"

#define WR_RING      1024
#define WR_RING_BITS 12                 // log2(WR_RING * 4)
static uint32_t __attribute__((aligned(4096))) g_wr_ring[WR_RING];
static int      g_wr_dma = -1, g_wr_rearm;
static uint32_t g_wr_count = WR_RING, g_wr_tail, g_wr_maxdepth;

void cs_wcap_ring_start(void) {
    dma_channel_config c;
    g_wr_dma   = dma_claim_unused_channel(true);
    g_wr_rearm = dma_claim_unused_channel(true);

    c = dma_channel_get_default_config(g_wr_dma);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, WR_RING_BITS);
    channel_config_set_dreq(&c, pio_get_dreq(pio1, CS_SM_WCAP, false));
    channel_config_set_chain_to(&c, g_wr_rearm);
    dma_channel_configure(g_wr_dma, &c, g_wr_ring, &pio1->rxf[CS_SM_WCAP], WR_RING, true);

    c = dma_channel_get_default_config(g_wr_rearm);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, false);
    dma_channel_configure(g_wr_rearm, &c,
                          hw_set_alias_untyped(&dma_hw->ch[g_wr_dma].al1_transfer_count_trig),
                          &g_wr_count, 1, false);
}

bool cs_wcap_ring_next(uint32_t *w) {
    uint32_t head = ((dma_hw->ch[g_wr_dma].write_addr - (uint32_t)g_wr_ring) >> 2) & (WR_RING - 1u);
    if (head == g_wr_tail) return false;
    uint32_t depth = (head - g_wr_tail) & (WR_RING - 1u);
    if (depth > g_wr_maxdepth) g_wr_maxdepth = depth;
    *w = g_wr_ring[g_wr_tail];
    g_wr_tail = (g_wr_tail + 1u) & (WR_RING - 1u);
    return true;
}

uint32_t cs_wcap_ring_maxdepth(void) { return g_wr_maxdepth; }
