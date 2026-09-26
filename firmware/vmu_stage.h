// vmu_stage.h — the core0/core1 contract for the emulated VMU, gb_stage.h's idiom: no flash, no SDK,
// every field names its single writer. A static, not a pad_arena tenant: the writeback outlives detach.
#ifndef VMU_STAGE_H
#define VMU_STAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "drivers/maple_proto.h"

#define VMU_CARD_BYTES (MAPLE_VMU_BLOCKS * MAPLE_VMU_BLOCK)
#define VMU_SECTOR     4096u
#define VMU_SECTORS    (VMU_CARD_BYTES / VMU_SECTOR)     // 32, eight card blocks each

// vmu_run.c executes the card, and an LC8670 fetch reads up to 3 bytes past PC 0xffff. Nothing
// else ever looks at these bytes; the store writes VMU_CARD_BYTES.
#define VMU_CARD_SLACK 4u

typedef struct {
    // core1 is the only writer
    volatile uint32_t epoch;            // ++ on attach, ++ on detach; odd while a session is live
    volatile uint32_t writes;           // ++ per accepted memory-card write phase
    volatile uint32_t last_write_ms;    // core0's idle timer input
    volatile uint32_t flush_req;        // ++ in dc_deinit(); core1 does not wait for the ack
    volatile uint32_t clock_seq;        // seqlock over clock
    struct { uint32_t secs, base_ms; } clock;
    volatile uint32_t lcd_seq;          // seqlock over lcd[]
    volatile uint32_t lcd_frames;
    uint8_t           lcd[MAPLE_VMU_LCD_BYTES];

    // core0 is the only writer
    volatile uint32_t flush_ack;        // == flush_req once the detach writeback finished
    volatile uint32_t saves;
    volatile uint32_t saved_writes;     // the `writes` value the last completed flush covered
    volatile uint32_t boot_secs;        // the clock as the newest slot header left it

    // One block core0 wants on the card. core1 installs it between frames, so a console read can
    // never see half of it; core0 writing the card itself would tear inside the turnaround.
    volatile uint32_t poke_req;         // core0: ++ once poke_blk and poke_buf are set
    volatile uint32_t poke_ack;         // core1: = poke_req once installed
    volatile uint16_t poke_blk;
    uint8_t           poke_buf[MAPLE_VMU_BLOCK];
} vmu_stage_t;

vmu_stage_t *vmu_stage(void);
uint8_t     *vmu_card(void);            // VMU_CARD_BYTES, live for the life of the image

// core1, from the Maple turnaround. Which sector changed is not tracked: core0 compares the card
// against the slot it is writing, which is the same answer without a second place to get it wrong.
void vmu_mark(void *stage, uint32_t now_ms);
void vmu_clock_publish(void *stage, uint32_t secs, uint32_t base_ms);
void vmu_lcd_publish(void *stage, const uint8_t *frame, unsigned n);
void vmu_poke_take(void *stage, uint8_t *card, uint32_t now_ms);    // between frames, never inside one

// core0: stage one block. False while a previous one is still waiting.
bool vmu_poke_put(unsigned block, const uint8_t *data);
bool vmu_poke_idle(void);

// core0. Both retry a torn read; the clock answers 0 if it has never been published.
uint32_t vmu_clock_now(uint32_t now_ms);
uint32_t vmu_lcd_seq(void);         // cheap: skip the copy when there is no new frame
bool     vmu_lcd_take(uint8_t out[MAPLE_VMU_LCD_BYTES], uint32_t *seq);

#endif
