// vmu_store.h — the flash side of the emulated VMU, core0 only: the one file that reads the card
// through XIP. Drivers include vmu_stage.h instead; tools/run.sh enforces that by grep.
#ifndef VMU_STORE_H
#define VMU_STORE_H

#include <stdint.h>
#include <stdbool.h>

// Loads the newer of the two slots into the card, or formats it if neither holds one, and
// publishes the clock the slot was saved with. Before the console leaves reset.
void vmu_store_init(void);

// At most one sector erased and programmed per call: an erase holds core0 for up to 400 ms (W25Q128
// max) while write_capture's FIFO is 8 words deep.
void vmu_store_poll(uint32_t now_ms);

bool vmu_store_busy(void);
void vmu_store_force(void);         // bench 'v': flush now, idle timer and wear floor skipped
void vmu_store_report(void);        // bench 'v': both slots, and what the live card hashes to

#endif
