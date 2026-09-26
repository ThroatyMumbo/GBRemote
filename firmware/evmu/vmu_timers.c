// Base timer, Timer 0 and Timer 1, from libevmu's evmu_timers.c (MIT, Falco Girgis). The quartz source
// converts with an exact rational remainder, so intermediates stay under 2^32 with no 64-bit divide.
#include "vmu_internal.h"

#define BT_COUNTER_MASK 0x3fffu
#define OSC_QUARTZ_HZ 32768u
#define OSC_RC_HZ     879236u
#define OSC_CF_HZ     6000000u

static unsigned consume_delay_(unsigned *delay, unsigned ticks) {
    unsigned skipped = *delay < ticks ? *delay : ticks;
    *delay -= skipped;
    return ticks - skipped;
}

static unsigned int0_rate_(uint8_t btcr) { return (btcr & BTCR_INT0_CYCLE) ? 0x40U : 0x4000U; }

static unsigned int1_rate_(uint8_t btcr) {
    switch (btcr & (BTCR_INT0_CYCLE | BTCR_INT1_CYCLE)) {
    case 0x00:
    case 0x80: return 0x20U;
    case 0x10:
    case 0x90: return 0x80U;
    case 0x20: return 0x200U;
    case 0x30: return 0x800U;
    case 0xa0: return 0x2U;
    case 0xb0: return 0x8U;
    default: return 0U;
    }
}

static unsigned bt_elapsed_(vmu_t *v, unsigned cc) {
    switch ((sfrc_(v, SFR_ISL) >> 4) & 3U) {
    case 1:
        return consume_delay_(&v->bt.start_delay, cc);
    case 3: {
        v->bt.start_delay = 0;
        uint32_t n = v->bt.rem + cc;
        unsigned scale = (unsigned)v->t0.tscale;
        v->bt.rem = n % scale;
        return n / scale;
    }
    default: {
        v->bt.start_delay = 0;
        uint8_t ocr = sfrc_(v, SFR_OCR);
        uint32_t hz;
        switch ((ocr >> 4) & 3U) {
        case 1: case 3: hz = OSC_CF_HZ; break;
        case 2:         hz = OSC_QUARTZ_HZ; break;
        default:        hz = OSC_RC_HZ; break;
        }
        uint32_t n = v->bt.rem + cc * OSC_QUARTZ_HZ * ((ocr & OCR_OCR7) ? 6U : 12U);
        v->bt.rem = n % hz;
        return n / hz;
    }
    }
}

static unsigned advance_(unsigned *counter, unsigned ticks, unsigned modulus, unsigned reload) {
    unsigned start = *counter % modulus;
    if (!ticks) { *counter = start; return 0; }
    unsigned to_ovf = modulus - start;
    if (ticks < to_ovf) { *counter = start + ticks; return 0; }
    ticks -= to_ovf;
    unsigned period = modulus - reload;
    unsigned n = 1U + ticks / period;
    *counter = reload + ticks % period;
    return n;
}

static unsigned advance8_(int *counter, unsigned ticks, uint8_t reload) {
    unsigned val = (unsigned)(*counter & 0xff);
    unsigned n = advance_(&val, ticks, 0x100U, reload);
    *counter = (int)val;
    return n;
}

static unsigned advance16_(int *tl, int *th, unsigned ticks, uint8_t rl, uint8_t rh) {
    unsigned val = ((unsigned)(*th & 0xff) << 8) | (unsigned)(*tl & 0xff);
    unsigned n = advance_(&val, ticks, 0x10000U, ((unsigned)rh << 8) | rl);
    *tl = (int)(val & 0xff);
    *th = (int)((val >> 8) & 0xff);
    return n;
}

static void base_timer_(vmu_t *v, unsigned cc) {
    uint8_t btcr = sfrc_(v, SFR_BTCR);
    unsigned elapsed = bt_elapsed_(v, cc);
    if (!(btcr & BTCR_OP_CTRL) || !elapsed) { return; }
    unsigned cur = v->bt.counter;
    unsigned next = cur + elapsed;
    unsigned r0 = int0_rate_(btcr);
    unsigned r1 = int1_rate_(btcr);
    if (r1 && cur / r1 < next / r1) {
        *sfr_(v, SFR_BTCR) |= BTCR_INT1_SRC;
        if (btcr & BTCR_INT1_EN) { vmu_irq_raise(v, IRQ_INT3_TBASE); }
    }
    if (cur / r0 < next / r0) {
        *sfr_(v, SFR_BTCR) |= BTCR_INT0_SRC;
        if (btcr & BTCR_INT0_EN) { vmu_irq_raise(v, IRQ_INT3_TBASE); }
    }
    v->bt.counter = (uint16_t)(next & BT_COUNTER_MASK);
}

static void timer0_(vmu_t *v, unsigned cc) {
    uint8_t *cnt = sfr_(v, SFR_T0CNT);
    if (!(*cnt & (T0CNT_P0HRUN | T0CNT_P0LRUN))) { return; }
    unsigned gated = consume_delay_(&v->t0.start_delay, cc);
    int c0 = 0;
    v->t0.tbase += (int)gated;
    while (v->t0.tbase >= v->t0.tscale) {
        c0++;
        v->t0.tbase -= v->t0.tscale;
    }
    if (!c0) { return; }
    const uint8_t both16 = T0CNT_P0LONG | T0CNT_P0LRUN | T0CNT_P0HRUN;
    if ((*cnt & both16) == both16) {
        if (advance16_(&v->t0.tl, &v->t0.th, (unsigned)c0, sfrc_(v, SFR_T0LR), sfrc_(v, SFR_T0HR))) {
            *cnt |= T0CNT_P0HOVF | T0CNT_T0LOVF;
            if (*cnt & T0CNT_T0HIE) { vmu_irq_raise(v, IRQ_T0H); }
        }
        return;
    }
    if (*cnt & T0CNT_P0LRUN) {
        if (advance8_(&v->t0.tl, (unsigned)c0, sfrc_(v, SFR_T0LR))) {
            *cnt |= T0CNT_T0LOVF;
            if (*cnt & T0CNT_T0LIE) { vmu_irq_raise(v, IRQ_INT2_T0L); }
        }
    }
    if (*cnt & T0CNT_P0HRUN) {
        if (advance8_(&v->t0.th, (unsigned)c0, sfrc_(v, SFR_T0HR))) {
            *cnt |= T0CNT_P0HOVF;
            if (*cnt & T0CNT_T0HIE) { vmu_irq_raise(v, IRQ_T0H); }
        }
    }
}

static void timer1_(vmu_t *v, unsigned cc) {
    uint8_t *cnt = sfr_(v, SFR_T1CNT);
    unsigned cy = consume_delay_(&v->t1.start_delay, cc);
    if (!cy || !(*cnt & (T1CNT_T1HRUN | T1CNT_T1LRUN))) { return; }
    const uint8_t both16 = T1CNT_T1LONG | T1CNT_T1HRUN | T1CNT_T1LRUN;
    if ((*cnt & both16) == both16) {
        unsigned low = advance8_(&v->t1.tl, cy, sfrc_(v, SFR_T1LR));
        if (advance8_(&v->t1.th, low, sfrc_(v, SFR_T1HR))) {
            *cnt |= T1CNT_T1HOVF;
            if (*cnt & T1CNT_T1HIE) { vmu_irq_raise(v, IRQ_T1); }
        }
        if (low) { *cnt |= T1CNT_T1LOVF; }
        return;
    }
    if (*cnt & T1CNT_T1LRUN) {
        if (advance8_(&v->t1.tl, cy, sfrc_(v, SFR_T1LR))) {
            *cnt |= T1CNT_T1LOVF;
            if (*cnt & T1CNT_T1LIE) { vmu_irq_raise(v, IRQ_T1); }
        }
    }
    if (*cnt & T1CNT_T1HRUN) {
        if (advance8_(&v->t1.th, cy, sfrc_(v, SFR_T1HR))) {
            *cnt |= T1CNT_T1HOVF;
            if (*cnt & T1CNT_T1HIE) { vmu_irq_raise(v, IRQ_T1); }
        }
    }
}

void vmu_timers_update(vmu_t *v, unsigned cc) {
    base_timer_(v, cc);
    timer0_(v, cc);
    timer1_(v, cc);
}
