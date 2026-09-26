#include "vmu_stage.h"
#include "pad.h"
#include <string.h>

static vmu_stage_t g_stage;
static uint8_t     g_card[VMU_CARD_BYTES + VMU_CARD_SLACK];

vmu_stage_t *vmu_stage(void) { return &g_stage; }
uint8_t     *vmu_card(void)  { return g_card; }

void __not_in_flash_func(vmu_mark)(void *stage, uint32_t now_ms) {
    vmu_stage_t *s = stage;
    s->last_write_ms = now_ms;
    s->writes++;
}

// Published on change only: the Dreamcast sets the date once at power-up, and core0 does the
// arithmetic from the base rather than being told the time.
void __not_in_flash_func(vmu_clock_publish)(void *stage, uint32_t secs, uint32_t base_ms) {
    vmu_stage_t *s = stage;
    const __typeof__(s->clock) c = { secs, base_ms };
    seq_write(&s->clock_seq, &s->clock, &c, sizeof c);
}

void __not_in_flash_func(vmu_lcd_publish)(void *stage, const uint8_t *frame, unsigned n) {
    vmu_stage_t *s = stage;
    if (n != MAPLE_VMU_LCD_BYTES) { return; }
    seq_write(&s->lcd_seq, s->lcd, frame, MAPLE_VMU_LCD_BYTES);
    s->lcd_frames++;
}

void __not_in_flash_func(vmu_poke_take)(void *stage, uint8_t *card, uint32_t now_ms) {
    vmu_stage_t *s = stage;
    uint32_t req = s->poke_req;
    if (req == s->poke_ack) { return; }
    __dmb();
    memcpy(card + (uint32_t)s->poke_blk * MAPLE_VMU_BLOCK, s->poke_buf, MAPLE_VMU_BLOCK);
    vmu_mark(s, now_ms);
    __dmb();
    s->poke_ack = req;
}

bool vmu_poke_idle(void) { return g_stage.poke_req == g_stage.poke_ack; }

bool vmu_poke_put(unsigned block, const uint8_t *data) {
    if (!vmu_poke_idle() || block >= MAPLE_VMU_BLOCKS) { return false; }
    g_stage.poke_blk = (uint16_t)block;
    memcpy(g_stage.poke_buf, data, MAPLE_VMU_BLOCK);
    __dmb();
    g_stage.poke_req++;
    return true;
}

uint32_t vmu_clock_now(uint32_t now_ms) {
    __typeof__(g_stage.clock) c;
    if (!seq_read(&g_stage.clock_seq, &c, &g_stage.clock, sizeof c, NULL) || !c.secs) { return 0; }
    return c.secs + (now_ms - c.base_ms) / 1000U;
}

uint32_t vmu_lcd_seq(void) { return g_stage.lcd_seq; }

bool vmu_lcd_take(uint8_t out[MAPLE_VMU_LCD_BYTES], uint32_t *seq) {
    return seq_read(&g_stage.lcd_seq, out, g_stage.lcd, MAPLE_VMU_LCD_BYTES, seq);
}
