// Internal bus, SFR side effects, XRAM, WRAM, stack, program memory and Port 3 buttons, from libevmu's
// evmu_ram.c, evmu_wram.c, evmu_gamepad.c and evmu_flash.c (MIT, Falco Girgis; LICENSE-libevmu).
#include "vmu_internal.h"

#define OCR_READBACK_HIGH 0x4c

static inline uint8_t *slot_(vmu_t *v, uint16_t addr) {
    addr &= 0x1ff;
    return &v->seg[addr >> 7][addr & 0x7f];
}

static inline uint16_t wram_addr_(const vmu_t *v) {
    return (uint16_t)(((sfrc_(v, SFR_VRMAD2) & 1U) << 8) | sfrc_(v, SFR_VRMAD1));
}

static inline void wram_autoinc_(vmu_t *v) {
    if (!(sfrc_(v, SFR_VSEL) & VSEL_INCE)) { return; }
    if (!++*sfr_(v, SFR_VRMAD1)) { *sfr_(v, SFR_VRMAD2) ^= 1; }
}

static inline uint8_t parity_(uint8_t n) {
    n ^= n >> 4; n ^= n >> 2; n ^= n >> 1;
    return n & 1;
}

uint16_t vmu_indirect_addr(vmu_t *v, unsigned mode) {
    mode &= 3;
    uint16_t a = vmu_read(v, (uint16_t)(mode | ((vmu_view(v, SFR_PSW) & (PSW_IRBK0 | PSW_IRBK1)) >> 1)));
    return (uint16_t)(a | ((mode & 2U) << 7));
}

uint8_t vmu_p3_value(const vmu_t *v) {
    return (uint8_t)(sfrc_(v, SFR_P3DDR) | (uint8_t)~sfrc_(v, SFR_P3) | (uint8_t)~v->p3_pressed);
}

// Read-modify-write instructions see the latch, not the pins.
uint8_t vmu_read_latch(vmu_t *v, uint16_t addr) {
    switch (addr) {
    case SFR_T1L: case SFR_T1H: case SFR_P1: case SFR_P3: case SFR_P7:
        return *slot_(v, addr);
    default:
        return vmu_read(v, addr);
    }
}

uint8_t vmu_read(vmu_t *v, uint16_t addr) {
    if (addr < 0x100) {
        return v->seg[addr >> 7][addr & 0x7f]; // plain RAM, no side effects
    }
    switch (addr) {
    case SFR_P1DDR: case SFR_P1FCR: case SFR_P3DDR: case SFR_MCR: case SFR_VCCR:
        return 0xff;
    case SFR_VTRBF: {
        uint8_t val = v->wram[wram_addr_(v)];
        wram_autoinc_(v);
        return val;
    }
    case SFR_T0L:    return (uint8_t)v->t0.tl;
    case SFR_T0H:    return (uint8_t)v->t0.th;
    case SFR_T1L:    return (uint8_t)v->t1.tl;
    case SFR_T1H:    return (uint8_t)v->t1.th;
    case SFR_VRMAD2: return (uint8_t)(0xfe | (sfrc_(v, SFR_VRMAD2) & 1));
    case SFR_P1:     return 0;
    case SFR_P3:     return vmu_p3_value(v);
    case SFR_P7:     return (uint8_t)(0xf0 | sfrc_(v, SFR_P7));
    case SFR_OCR:    return (uint8_t)(OCR_READBACK_HIGH | sfrc_(v, SFR_OCR));
    default:         return *slot_(v, addr);
    }
}

// Like vmu_read but without the VTRBF auto-increment; used by ST and by debuggers.
uint8_t vmu_view(vmu_t *v, uint16_t addr) {
    if (addr == SFR_VTRBF) { return v->wram[wram_addr_(v)]; }
    return vmu_read(v, addr);
}

void vmu_write(vmu_t *v, uint16_t addr, uint8_t val) {
    if (addr < 0x100) { v->seg[addr >> 7][addr & 0x7f] = val; return; }
    switch (addr) {
    case SFR_ACC:
        if (sfrc_(v, SFR_ACC) != val) { v->sfr[1] = (uint8_t)((v->sfr[1] & 0xfe) | parity_(val)); }
        break;
    case SFR_VTRBF:
        v->wram[wram_addr_(v)] = val;
        wram_autoinc_(v);
        return;
    case SFR_VRMAD2:
        val &= 1;
        break;
    case SFR_EXT: {
        bool to_flash = val & 1;
        if (v->ext_is_flash != to_flash) {
            if (vmu_ext_read(v, v->pc) == VMU_OP_JMPF) {
                v->pc =
                    (uint16_t)((vmu_ext_read(v, v->pc + 1U) << 8) | vmu_ext_read(v, v->pc + 2U));
            }
            v->ext_is_flash = to_flash;
        }
        break;
    }
    case SFR_XBNK:
        if (sfrc_(v, SFR_XBNK) != val) {
            if (val > 2) { return; }
            v->seg[3] = v->xram[val];
        }
        break;
    case SFR_PSW:
        if ((sfrc_(v, SFR_PSW) ^ val) & PSW_RAMBK0) {
            unsigned bank = (val & PSW_RAMBK0) >> 1;
            v->seg[0] = v->ram[bank];
            v->seg[1] = &v->ram[bank][128];
        }
        break;
    case SFR_T0PRR:
        v->t0.tscale = 256 - val;
        v->t0.tbase  = 0;
        v->bt.rem    = 0;
        break;
    case SFR_T0CNT: {
        uint8_t prev = sfrc_(v, SFR_T0CNT) & (T0CNT_P0LRUN | T0CNT_P0HRUN);
        uint8_t next = val & (T0CNT_P0LRUN | T0CNT_P0HRUN);
        if (!prev && next) {
            v->t0.start_delay = 1;
        } else if (!next) {
            v->t0.start_delay = 0;
        }
        if (!(val & T0CNT_P0LRUN)) { v->t0.tl = sfrc_(v, SFR_T0LR); }
        if (!(val & T0CNT_P0HRUN)) { v->t0.th = sfrc_(v, SFR_T0HR); }
        break;
    }
    case SFR_T0LR: case SFR_T0L:
        if (!(sfrc_(v, SFR_T0CNT) & T0CNT_P0LRUN)) { v->t0.tl = val; }
        break;
    case SFR_T0HR: case SFR_T0H:
        if (!(sfrc_(v, SFR_T0CNT) & T0CNT_P0HRUN)) { v->t0.th = val; }
        break;
    case SFR_T1CNT: {
        uint8_t prev = sfrc_(v, SFR_T1CNT) & (T1CNT_T1LRUN | T1CNT_T1HRUN);
        uint8_t next = val & (T1CNT_T1LRUN | T1CNT_T1HRUN);
        if (!prev && next) {
            v->t1.start_delay = 1;
        } else if (!next) {
            v->t1.start_delay = 0;
        }
        if (!(val & T1CNT_T1LRUN)) { v->t1.tl = sfrc_(v, SFR_T1LR); }
        if (!(val & T1CNT_T1HRUN)) { v->t1.th = sfrc_(v, SFR_T1HR); }
        break;
    }
    case SFR_T1LR:
        if (!(sfrc_(v, SFR_T1CNT) & T1CNT_T1LRUN)) { v->t1.tl = val; }
        break;
    case SFR_T1HR:
        if (!(sfrc_(v, SFR_T1CNT) & T1CNT_T1HRUN)) { v->t1.th = val; }
        break;
    case SFR_BTCR: {
        bool was = sfrc_(v, SFR_BTCR) & BTCR_OP_CTRL;
        bool now = val & BTCR_OP_CTRL;
        if (!was && now) {
            v->bt.start_delay = v->cc;
        } else if (!now) {
            v->bt.start_delay = 0;
        }
        if (!now) { v->bt.counter = 0; }
        break;
    }
    case SFR_ISL: case SFR_OCR:
        v->bt.rem = 0;
        break;
    default:
        break;
    }

    if (addr >= XRAM_BASE && addr <= XRAM_END && (sfrc_(v, SFR_VCCR) & VCCR_VCCR6)) { return; }
    uint8_t prev = *slot_(v, addr);
    *slot_(v, addr) = val;

    // Screen power and HOLD blank the panel at once, not on the next refresh (HOLD stops those).
    if ((addr == SFR_VCCR && ((prev ^ val) & VCCR_VCCR7)) ||
        (addr == SFR_PCON && ((prev ^ val) & PCON_HOLD))) {
        vmu_lcd_sample(v);
    }
}

uint8_t vmu_ext_read(const vmu_t *v, uint32_t addr) {
    if (v->ext_is_flash) { return addr < VMU_FLASH_SIZE ? v->flash[addr] : 0; }
    return (v->rom && addr < VMU_ROM_SIZE) ? v->rom[addr] : 0;
}

void vmu_ext_write(vmu_t *v, uint32_t addr, uint8_t val) {
    if (!v->ext_is_flash || addr >= VMU_FLASH_SIZE) { return; }
    if (v->flash[addr] != val) { v->flash_writes++; }
    v->flash[addr] = val;
}

void vmu_push(vmu_t *v, uint8_t val) {
    uint8_t *sp = sfr_(v, SFR_SP);
    v->ram[0][++*sp] = val;
}

uint8_t vmu_pop(vmu_t *v) {
    uint8_t *sp = sfr_(v, SFR_SP);
    return v->ram[0][(*sp)--];
}

int vmu_stack_depth(const vmu_t *v) { return (int)sfrc_(v, SFR_SP) - 0x7f; }

uint8_t vmu_stack_at(const vmu_t *v, unsigned depth) {
    return v->ram[0][(uint8_t)(sfrc_(v, SFR_SP) - depth)];
}

void vmu_set_buttons(vmu_t *v, uint8_t pressed) { v->p3_pressed = pressed; }

// A pressed button with P32INT set leaves HOLD; if the wake came out of sleep_ex, treat it as
// MODE and restart the game (see vmu_bios.c).
void vmu_gamepad_poll(vmu_t *v) {
    if (!(uint8_t)~sfrc_(v, SFR_P3DDR)) { return; }
    uint8_t p3    = vmu_p3_value(v);
    uint8_t p3int = sfrc_(v, SFR_P3INT);
    if (!(uint8_t)~p3) { return; }
    if (!(p3int & P3INT_P32INT)) { return; }
    vmu_write(v, SFR_P3INT, (uint8_t)(p3int | P3INT_P31INT));
    vmu_write(v, SFR_PCON, (uint8_t)(sfrc_(v, SFR_PCON) & (uint8_t)~PCON_HOLD));
    if (p3int & P3INT_P30INT) { vmu_irq_raise(v, IRQ_P3); }
    if (v->sleeping) {
        v->sleeping = false;
        vmu_reset(v);
        v->exited = true;
    }
}
