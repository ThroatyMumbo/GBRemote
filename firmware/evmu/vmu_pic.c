// Interrupt controller. Transcribed from libevmu's evmu_pic.c (MIT, Falco Girgis).
#include "vmu_internal.h"

static const uint16_t isr_addr_[IRQ_COUNT] = {
    0x00, 0x03, 0x0b, 0x13, 0x1b, 0x23, 0x2b, 0x33, 0x3b, 0x43, 0x4b, 0x4f, 0x52, 0x55, 0x5a, 0x5d
};

void vmu_irq_raise(vmu_t *v, unsigned irq) { v->int_req |= (uint16_t)(1U << irq); }

unsigned vmu_irq_depth(const vmu_t *v) {
    unsigned depth = 0;
    for (int p = PRIO_HIGHEST; p >= PRIO_LOW; p--) {
        if (v->int_stack[p]) { depth++; }
    }
    return depth;
}

static uint16_t enabled_by_priority_(const vmu_t *v, int priority) {
    uint8_t ie = sfrc_(v, SFR_IE);
    uint8_t ip = sfrc_(v, SFR_IP);
    uint16_t mask = 0;
#define FROM_IP(bit, irq) ((uint16_t)((!!(ip & (bit)) ^ !(priority)) << (irq)))
    if (priority == PRIO_HIGHEST) {
        if (!(ie & IE_IE0)) { mask |= 1U << IRQ_INT0; }
        if (!(ie & IE_IE1) && !(ie & IE_IE0)) { mask |= 1U << IRQ_INT1; }
    } else if (priority == PRIO_HIGH || (ie & IE_IE7)) {
        mask |= FROM_IP(IP_P3,   IRQ_P3);
        mask |= FROM_IP(IP_SIO1, IRQ_SIO1);
        mask |= FROM_IP(IP_SIO0, IRQ_SIO0);
        mask |= FROM_IP(IP_T1,   IRQ_T1);
        mask |= FROM_IP(IP_T0H,  IRQ_T0H);
        mask |= FROM_IP(IP_INT3, IRQ_INT3_TBASE);
        mask |= FROM_IP(IP_INT2, IRQ_INT2_T0L);
        if (priority == PRIO_LOW) {
            if (ie & IE_IE0) { mask |= (1U << IRQ_INT0) | (1U << IRQ_INT1); }
            if (ie & IE_IE1) { mask |= 1U << IRQ_INT1; }
        }
    }
#undef FROM_IP
    return mask;
}

static uint16_t active_(const vmu_t *v) {
    return (uint16_t)(v->int_stack[0] | v->int_stack[1] | v->int_stack[2]);
}

bool vmu_pic_reti(vmu_t *v) {
    uint16_t r = (uint16_t)(vmu_pop(v) << 8);
    r |= vmu_pop(v);
    v->pc = r;
    v->process_this_instr = false;
    for (int p = PRIO_HIGHEST; p >= PRIO_LOW; p--) {
        if (v->int_stack[p]) {
            v->prev_int_priority = (uint8_t)p;
            v->int_stack[p] = 0;
            return true;
        }
    }
    return false;
}

static bool check_(vmu_t *v, int p) {
    uint16_t pmask = enabled_by_priority_(v, p);
    for (unsigned i = 0; i < IRQ_COUNT; i++) {
        uint16_t bit = (uint16_t)(1U << i);
        if (!(pmask & bit & v->int_req)) { continue; }
        v->int_req &= (uint16_t)~bit;
        v->int_stack[p] = bit;
        vmu_push(v, (uint8_t)(v->pc & 0xff));
        vmu_push(v, (uint8_t)(v->pc >> 8));
        vmu_write(v, SFR_PCON, (uint8_t)(vmu_read(v, SFR_PCON) & (uint8_t)~PCON_HALT));
        v->pc = isr_addr_[i];
        v->irqs++;
        return true;
    }
    return false;
}

bool vmu_pic_update(vmu_t *v) {
    if (!v->process_this_instr) { v->process_this_instr = true; return false; }
    if (!v->int_req) {
        return false; // nothing pending: the scans below cannot accept anything
    }
    if (!active_(v)) {
        for (int p = (int)v->prev_int_priority - 1; p >= PRIO_LOW; p--) {
            if (check_(v, p)) { return true; }
        }
        for (int p = PRIO_HIGH; p >= (int)v->prev_int_priority; p--) {
            if (check_(v, p)) { return true; }
        }
    } else {
        for (int p = PRIO_HIGHEST; p >= PRIO_LOW; p--) {
            if (v->int_stack[p]) { break; }
            if (check_(v, p)) { return true; }
        }
    }
    return false;
}
