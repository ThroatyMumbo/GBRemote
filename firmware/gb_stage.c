#include "gb_stage.h"
#include "pad.h"
#include <string.h>

static gb_stage_t g_stage;

typedef struct { volatile uint32_t seq; gb_slot_info_t v; } slot_pub_t;
static slot_pub_t g_slot_pub;

gb_stage_t *gb_stage(void) { return &g_stage; }

void __not_in_flash_func(gb_feed_push)(void *stage, uint16_t off, const uint8_t blk[GB_FEED_BLOCK]) {
    gb_stage_t *s = stage;
    uint32_t h = s->feed_head;
    if (h - s->feed_tail >= GB_FEED_RING) { s->feed_drop++; return; }
    gb_feed_ent_t *e = &s->feed[h % GB_FEED_RING];
    e->off = off;
    memcpy(e->blk, blk, GB_FEED_BLOCK);
    __dmb();
    s->feed_head = h + 1U;
}

bool gb_feed_pop(uint16_t *off, uint8_t blk[GB_FEED_BLOCK]) {
    uint32_t t = g_stage.feed_tail;
    if (t == g_stage.feed_head) { return false; }
    __dmb();
    const gb_feed_ent_t *e = &g_stage.feed[t % GB_FEED_RING];
    *off = e->off;
    memcpy(blk, e->blk, GB_FEED_BLOCK);
    __dmb();
    g_stage.feed_tail = t + 1U;
    return true;
}

// Runs inside the Joybus turnaround; the caller detects a torn read (demos/stadium's BK_J_DATASEQ).
bool __not_in_flash_func(gb_win_pull)(void *stage, uint16_t off, uint8_t out[GB_FEED_BLOCK]) {
    gb_stage_t *s = stage;
    const uint8_t *w = s->win;
    if (!w || (uint32_t)off + GB_FEED_BLOCK > s->win_len) { return false; }
    memcpy(out, w + off, GB_FEED_BLOCK);
    return true;
}

void gb_win_publish(const uint8_t *buf, uint32_t len) {
    if (!buf) { g_stage.win = NULL; __dmb(); g_stage.win_len = 0; return; }
    g_stage.win_len = len;
    __dmb();
    g_stage.win = buf;
}

void gb_slot_info_publish(const gb_slot_info_t *v) {
    seq_write(&g_slot_pub.seq, &g_slot_pub.v, v, sizeof *v);
}

bool gb_slot_info(gb_slot_info_t *out) {
    return seq_read(&g_slot_pub.seq, out, &g_slot_pub.v, sizeof *out, NULL);
}
