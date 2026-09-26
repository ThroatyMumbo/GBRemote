// SFR map and bit masks shared by the core's translation units. Values from libevmu's
// evmu_address_space.h / evmu_sfr.h (MIT, Falco Girgis).
#pragma once
#include "vmu_core.h"

#define SFR_ACC    0x100
#define SFR_PSW    0x101
#define SFR_B      0x102
#define SFR_C      0x103
#define SFR_TRL    0x104
#define SFR_TRH    0x105
#define SFR_SP     0x106
#define SFR_PCON   0x107
#define SFR_IE     0x108
#define SFR_IP     0x109
#define SFR_EXT    0x10d
#define SFR_OCR    0x10e
#define SFR_T0CNT  0x110
#define SFR_T0PRR  0x111
#define SFR_T0L    0x112
#define SFR_T0LR   0x113
#define SFR_T0H    0x114
#define SFR_T0HR   0x115
#define SFR_T1CNT  0x118
#define SFR_T1LC   0x11a
#define SFR_T1L    0x11b
#define SFR_T1LR   0x11b
#define SFR_T1HC   0x11c
#define SFR_T1H    0x11d
#define SFR_T1HR   0x11d
#define SFR_MCR    0x120
#define SFR_STAD   0x122
#define SFR_CNR    0x123
#define SFR_TDR    0x124
#define SFR_XBNK   0x125
#define SFR_VCCR   0x127
#define SFR_SCON0  0x130
#define SFR_SBUF0  0x131
#define SFR_SBR    0x132
#define SFR_SCON1  0x134
#define SFR_SBUF1  0x135
#define SFR_P1     0x144
#define SFR_P1DDR  0x145
#define SFR_P1FCR  0x146
#define SFR_P3     0x14c
#define SFR_P3DDR  0x14d
#define SFR_P3INT  0x14e
#define SFR_FPR    0x154
#define SFR_P7     0x15c
#define SFR_I01CR  0x15d
#define SFR_I23CR  0x15e
#define SFR_ISL    0x15f
#define SFR_VSEL   0x163
#define SFR_VRMAD1 0x164
#define SFR_VRMAD2 0x165
#define SFR_VTRBF  0x166
#define SFR_VLREG  0x167
#define SFR_BTCR   0x17f

#define XRAM_BASE  0x180
#define XRAM_END   0x1fb
#define XRAM_ICN_FILE  0x181
#define XRAM_ICN_GAME  0x182
#define XRAM_ICN_CLOCK 0x183
#define XRAM_ICN_FLASH 0x184

#define PSW_CY     0x80
#define PSW_AC     0x40
#define PSW_IRBK1  0x10
#define PSW_IRBK0  0x08
#define PSW_OV     0x04
#define PSW_RAMBK0 0x02
#define PSW_P      0x01

#define PCON_HOLD  0x02
#define PCON_HALT  0x01

#define IE_IE7     0x80
#define IE_IE1     0x02
#define IE_IE0     0x01

#define T0CNT_T0LIE  0x01
#define T0CNT_T0LOVF 0x02
#define T0CNT_T0HIE  0x04
#define T0CNT_P0HOVF 0x08
#define T0CNT_P0LEXT 0x10
#define T0CNT_P0LONG 0x20
#define T0CNT_P0LRUN 0x40
#define T0CNT_P0HRUN 0x80

#define T1CNT_T1HRUN 0x80
#define T1CNT_T1LRUN 0x40
#define T1CNT_T1LONG 0x20
#define T1CNT_ELDT1C 0x10
#define T1CNT_T1HOVF 0x08
#define T1CNT_T1HIE  0x04
#define T1CNT_T1LOVF 0x02
#define T1CNT_T1LIE  0x01

#define OCR_OCR7   0x80
#define OCR_OCR5   0x20
#define OCR_OCR4   0x10
#define OCR_OCR1   0x02
#define OCR_OCR0   0x01

#define MCR_MCR4   0x10
#define MCR_MCR3   0x08
#define VCCR_VCCR7 0x80
#define VCCR_VCCR6 0x40

#define P3INT_P32INT 0x04
#define P3INT_P31INT 0x02
#define P3INT_P30INT 0x01
#define P7_P71       0x02
#define FPR_UNLOCK   0x02
#define FPR_ADDR     0x01
#define VSEL_INCE    0x10

#define BTCR_INT0_CYCLE 0x80
#define BTCR_OP_CTRL    0x40
#define BTCR_INT1_CYCLE 0x30
#define BTCR_INT1_SRC   0x08
#define BTCR_INT1_EN    0x04
#define BTCR_INT0_SRC   0x02
#define BTCR_INT0_EN    0x01

#define IP_P3   0x80
#define IP_SIO1 0x20
#define IP_SIO0 0x10
#define IP_T1   0x08
#define IP_T0H  0x04
#define IP_INT3 0x02
#define IP_INT2 0x01

enum {
    IRQ_RESET, IRQ_INT0, IRQ_INT1, IRQ_INT2_T0L, IRQ_INT3_TBASE, IRQ_T0H, IRQ_T1,
    IRQ_SIO0, IRQ_SIO1, IRQ_RFB, IRQ_P3, IRQ_11, IRQ_12, IRQ_13, IRQ_14, IRQ_15, IRQ_COUNT
};
enum { PRIO_LOW, PRIO_HIGH, PRIO_HIGHEST, PRIO_COUNT };

#define FLASH_PRG_BYTES 128
#define FLASH_PRG_STATE_COUNT 3

#define BIOS_FM_WRT_EX  0x100
#define BIOS_FM_WRTA_EX 0x108
#define BIOS_FM_VRF_EX  0x110
#define BIOS_FM_PRD_EX  0x120
#define BIOS_TIMER_EX   0x130
#define BIOS_SLEEP_EX   0x140
#define BIOS_EXIT_EX    0x1f0

// System variables in RAM bank 0.
#define SYS_YEAR_MSB_BCD 0x10
#define SYS_YEAR_LSB_BCD 0x11
#define SYS_MONTH_BCD    0x12
#define SYS_DAY_BCD      0x13
#define SYS_HOUR_BCD     0x14
#define SYS_MINUTE_BCD   0x15
#define SYS_SEC_BCD      0x16
#define SYS_YEAR_MSB     0x17
#define SYS_YEAR_LSB     0x18
#define SYS_MONTH        0x19
#define SYS_DAY          0x1a
#define SYS_HOUR         0x1b
#define SYS_MINUTE       0x1c
#define SYS_SEC          0x1d
#define SYS_HALF_SEC     0x1e
#define SYS_LEAP_YEAR    0x1f
#define SYS_DATE_SET     0x31

// The board has no RTC; the BIOS clock starts at the build date unless the build passes its own.
#ifndef VMU_BUILD_YEAR
#define VMU_BUILD_YEAR  2026
#define VMU_BUILD_MONTH 9
#define VMU_BUILD_DAY   1
#endif

static inline uint8_t *sfr_(vmu_t *v, uint16_t addr) { return &v->sfr[addr - 0x100]; }
static inline uint8_t  sfrc_(const vmu_t *v, uint16_t addr) { return v->sfr[addr - 0x100]; }

bool vmu_pic_reti(vmu_t *v);
