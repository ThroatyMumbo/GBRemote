// vmu_run.h — the card's GAME file, run on core0 from run_bus() only while the console is quiet, as a
// real VMU runs its game only out of the controller. The LC8670 core is firmware/evmu/.
#ifndef VMU_RUN_H
#define VMU_RUN_H

#include <stdint.h>
#include <stdbool.h>
#include "vmu_stage.h"

void vmu_run_init(void);            // after vmu_store_init(): it runs whatever the card holds

// The gate. Going active resets the core, because the console owned the card in between and may
// have written a different game into it — which is what a real card does when it is pulled out.
void vmu_run_gate(bool console_quiet, uint32_t now_ms);
bool vmu_run_active(void);

// gb_buttons is the GBDK J_* byte from $6000. Bounded: it spends at most EMU_BUDGET_US per call.
void vmu_run_poll(uint32_t now_ms, uint8_t gb_buttons);

// Upright, never the 180° the Maple path applies: this frame comes from the LCD controller in the
// orientation a VMU is read standing on its own, not the one it has inside a controller.
bool vmu_run_take_frame(uint8_t out[MAPLE_VMU_LCD_BYTES]);

void vmu_run_reset(uint32_t now_ms);
void vmu_run_report(void);

#endif
