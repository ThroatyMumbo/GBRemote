// Flat, heap-free Dreamcast VMU (Sanyo LC8670) emulation core. Transcribed from libevmu
// (MIT, Falco Girgis); see LICENSE-libevmu. No platform headers, no malloc, no floating point.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "vmu_isa.h"

#define VMU_FLASH_SIZE   0x20000
#define VMU_FLASH_SLACK  4          // fetch reads up to 3 bytes past PC 0xffff
#define VMU_ROM_SIZE     0x10000
#define VMU_LCD_W        48
#define VMU_LCD_H        32
#define VMU_LCD_STRIDE   6
#define VMU_LCD_BYTES    (VMU_LCD_STRIDE * VMU_LCD_H)

// P3 button bits, 1 = pressed, in vmu_set_buttons().
#define VMU_BTN_UP    0x01
#define VMU_BTN_DOWN  0x02
#define VMU_BTN_LEFT  0x04
#define VMU_BTN_RIGHT 0x08
#define VMU_BTN_A     0x10
#define VMU_BTN_B     0x20
#define VMU_BTN_MODE  0x40
#define VMU_BTN_SLEEP 0x80

#define VMU_LCD_ICON_FILE  0x01
#define VMU_LCD_ICON_GAME  0x02
#define VMU_LCD_ICON_CLOCK 0x04
#define VMU_LCD_ICON_FLASH 0x08

enum { VMU_BIOS_FM_WRT_EX, VMU_BIOS_FM_WRTA_EX, VMU_BIOS_FM_VRF_EX, VMU_BIOS_FM_PRD_EX,
       VMU_BIOS_TIMER_EX, VMU_BIOS_SLEEP_EX, VMU_BIOS_EXIT_EX, VMU_BIOS_UNKNOWN, VMU_BIOS_COUNT };

typedef struct vmu {
    uint8_t  ram[2][256];
    uint8_t  sfr[128];
    uint8_t  xram[3][128];
    uint8_t  wram[512];
    uint8_t *flash;                 // VMU_FLASH_SIZE + VMU_FLASH_SLACK bytes, read-write
    const uint8_t *rom;             // VMU_ROM_SIZE bytes, or NULL for the emulated BIOS
    uint8_t *seg[4];                // 128-byte windows: RAM low, RAM high, SFR, current XRAM bank
    bool     ext_is_flash;          // EXT bit 0: program fetches come from flash, else ROM
    uint32_t bare_game_bytes;       // GAME extent when the flash image has no filesystem

    uint16_t    pc;
    uint8_t     cc;                 // cycles of the most recently fetched instruction
    uint8_t     opcode;
    vmu_instr_t cur;
    int64_t     deficit_ns;

    uint16_t int_req;
    uint16_t int_stack[3];
    bool     process_this_instr;
    uint8_t  prev_int_priority;

    struct { int tl, th, tbase, tscale; unsigned start_delay; } t0;
    struct { int tl, th; unsigned start_delay; } t1;
    struct { uint16_t counter; uint32_t rem; unsigned start_delay; } bt;
    uint32_t prg_state;

    uint32_t lcd_ns;
    uint8_t  fb[VMU_LCD_BYTES];
    uint32_t fb_seq, fb_seq_seen;

    uint8_t  p3_pressed;
    bool     sleeping;
    bool     exited;

    uint64_t instructions, cycles;
    uint32_t irqs, unknown_ops;
    uint32_t flash_writes;          // bytes the game actually changed; drives the host's save policy
    uint32_t bios_calls[VMU_BIOS_COUNT];
} vmu_t;

void     vmu_init(vmu_t *v, uint8_t *flash, const uint8_t *rom);
void     vmu_reset(vmu_t *v);
void     vmu_set_datetime(vmu_t *v, int year, int month, int day, int hour, int min, int sec);
void     vmu_set_buttons(vmu_t *v, uint8_t pressed);
void     vmu_run_us(vmu_t *v, uint32_t us);
void     vmu_run_ns(vmu_t *v, uint64_t ns);
bool     vmu_lcd_frame(vmu_t *v, uint8_t out[VMU_LCD_BYTES]);
uint8_t  vmu_lcd_icons(const vmu_t *v);
uint32_t vmu_tcyc_ns(const vmu_t *v);

// Lower-level seams: the run loop is built from these, and the host tests drive them directly.
uint32_t vmu_step(vmu_t *v);
void     vmu_execute(vmu_t *v, const vmu_instr_t *in);
uint8_t  vmu_read(vmu_t *v, uint16_t addr);
uint8_t  vmu_read_latch(vmu_t *v, uint16_t addr);
uint8_t  vmu_view(vmu_t *v, uint16_t addr);
void     vmu_write(vmu_t *v, uint16_t addr, uint8_t val);
uint16_t vmu_indirect_addr(vmu_t *v, unsigned mode);
uint8_t  vmu_ext_read(const vmu_t *v, uint32_t addr);
void     vmu_ext_write(vmu_t *v, uint32_t addr, uint8_t val);
void     vmu_push(vmu_t *v, uint8_t val);
uint8_t  vmu_pop(vmu_t *v);
int      vmu_stack_depth(const vmu_t *v);
uint8_t  vmu_stack_at(const vmu_t *v, unsigned depth);
uint8_t  vmu_p3_value(const vmu_t *v);
void     vmu_gamepad_poll(vmu_t *v);
void     vmu_timers_update(vmu_t *v, unsigned cc);
bool     vmu_pic_update(vmu_t *v);
void     vmu_irq_raise(vmu_t *v, unsigned irq);
unsigned vmu_irq_depth(const vmu_t *v);
void     vmu_lcd_advance_ns(vmu_t *v, uint32_t ns);
void     vmu_lcd_sample(vmu_t *v);
uint16_t vmu_bios_call(vmu_t *v, uint16_t pc);
bool     vmu_game_range(const vmu_t *v, uint32_t *base, uint32_t *end);
