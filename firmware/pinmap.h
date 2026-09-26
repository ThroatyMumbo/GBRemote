// pinmap.h — RP2350B (U1) GPIOs to cart edge J1 and controller port J3; check_sch.py diffs it
// against hardware/gen/expected_nets.txt. The cart bus stays at GP31 and below: one 32-bit mask.
#pragma once

#define PIN_RSTHOLD 0   // gate of the /RST hold FET        — output; HIGH = console held in reset
#define PIN_CLK   1     // GB CLK (timing reference)        — input
#define PIN_NWR   2     // GB /WR                           — input
#define PIN_NRD   3     // GB /RD  (read=low, write=high)   — input
#define PIN_NCS   4     // GB /CS                           — input (unused for ROM)
#define PIN_A14   6     // A14 — region gate (low/high bank)
#define PIN_A15   7     // A15 — internal-space gate ($8000+)
#define PIN_D0    8     // D0..D7 = GP8..GP15 (D0 = LSB)    — driven on reads
#define PIN_RST   16    // GB /reset                        — input; resets cart state
#define PIN_A0    18    // A0..A13 = GP18..GP31             — input

// wire_pos[i] = offset from PIN_A0 of the GPIO carrying A(i); the first suspect for a garbled logo.
// Banks are permuted at load; the mailbox paths un-permute via cs_wire_pos.
#define A0_13_WIRE_POS { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 }

// write_capture samples GP17 into bit 11, which nothing reads; the GP16/GP17 gap puts A0 at bit 12.

// Controller port CTRL0-7 = GP32-39, each behind 100R (R16-R23); reachable only from PIO2 at base 16.
#define PIN_CTRL0    32
#define PIN_CTRL_N   8
#define PIN_CTRL(n)  (PIN_CTRL0 + (n))

#define PIN_JOYBUS   PIN_CTRL0      // N64 Joybus data line

// Dreamcast Maple. Must be consecutive with SDCKA lower: the PIO reaches both with one
// `set pins,2`, and the decode reads them as bit0=SDCKA, bit1=SDCKB.
#define PIN_MAPLE_A  PIN_CTRL0      // DC connector pin 1
#define PIN_MAPLE_B  PIN_CTRL(1)    // DC connector pin 5

// SNES/NES. LATCH and CLK consecutive, LATCH lower: one IN base reaches both. DATA1 is the only
// driven line, open-drain.
#define PIN_SNES_DATA1  PIN_CTRL0   // SNES connector pin 4 / NES D0
#define PIN_SNES_DATA2  PIN_CTRL(1) // SNES pin 5 — fitted, outside ctrl_mask: Hi-Z reads as no joypad 3
#define PIN_SNES_LATCH  PIN_CTRL(2) // SNES pin 3
#define PIN_SNES_CLK    PIN_CTRL(3) // SNES pin 2
#define PIN_SNES_IOBIT  PIN_CTRL(4) // SNES pin 6 — fitted, outside ctrl_mask, reserved

// Genesis / Mega Drive DE-9. Seven signals, CTRL7 left free. Two adjacency rules, one per state
// machine: D0-D3 must be consecutive for `out pindirs,4` and TL/TR consecutive for `out pindirs,2`.
#define PIN_GEN_TL   PIN_CTRL0      // DB9 pin 6 — D4: B while TH high, A while low
#define PIN_GEN_TR   PIN_CTRL(1)    // DB9 pin 9 — D5: C while TH high, START while low
#define PIN_GEN_TH   PIN_CTRL(2)    // DB9 pin 7 — console output. `wait n pin 0` and `jmp pin`
#define PIN_GEN_D0   PIN_CTRL(3)    // D0-D3 = DB9 1-4: UP/DOWN/LEFT/RIGHT, or Z/Y/X/MODE on cycle 7

#define PIN_CTRL_ID  41             // cable ID divider: R24 10k to +3V3, R_id in the adapter shell
#define CTRL_ID_ADC  1              // ADC input 1 (RP2350B: ADC0..7 = GP40..47)

#define PIN_BLE_EN   17             // U6 load switch on the RM2's 3V3; R32 holds it off on a cold board

#define PIN_IR_TX    46             // IR_DRIVE: high turns Q on and lights the LED; R27 holds it off

// Also taken, no pin spare: GP5/GP40 UART1 (J4), GP42-45 RM2 gSPI, GP47 PSRAM CS (U3).
