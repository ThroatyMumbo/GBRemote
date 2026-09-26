#include "gendev.h"
#include "gendev.pio.h"
#include "gen_proto.h"
#include "pio_run.h"
#include "devcommon.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

_Static_assert(PIO_WORDS(gen_dpad) + PIO_WORDS(gen_tltr) <= PIO2_SHARED_WORDS,
               "gendev outgrew PIO2's budget with the CYW43 gSPI resident");

static pio_res_t g_res;
static PIO   g_pio;
static uint  g_sm_d, g_sm_t, g_off_d, g_off_t, g_pin_th, g_pin_tl, g_pin_d0;
static bool  g_open_drain;
static uint32_t g_reset_us = GEN_RESET_US_DEFAULT;

// Open drain is a patched copy of the push-pull program: every `out pins` retargeted at PINDIRS.
static uint16_t g_ins_d[16], g_ins_t[8];
static struct pio_program g_prog_d, g_prog_t;

static const struct pio_program *drive_patch(const struct pio_program *src, uint16_t *buf,
                                             struct pio_program *dst, uint n_out) {
    if (!g_open_drain) { return src; }
    uint16_t from = (uint16_t)pio_encode_out(pio_pins, n_out);
    uint16_t to   = (uint16_t)pio_encode_out(pio_pindirs, n_out);
    for (uint i = 0; i < (uint)src->length; i++) {
        buf[i] = src->instructions[i] == from ? to : src->instructions[i];
    }
    *dst = *src;
    dst->instructions = buf;
    return dst;
}

// gen_proto emits a pressed mask: push-pull inverts it to wire levels, open drain uses it as pindirs.
static inline uint32_t wire_word(uint32_t pressed) {
    return g_open_drain ? pressed : ~pressed;
}

// g_next_reset is a timerawl deadline.
static uint32_t g_next_reset;
static bool     g_th_last;
static uint32_t g_last_d, g_last_t;     // newest report, so a rewind can re-prime from it

// Rewind period while TH stays parked; longer than a frame, so a reading console never reaches it.
#define GEN_REARM_US 20000u

static gendev_stats_t g_st;

bool gendev_init(PIO pio, uint pin_tl, uint pin_th, uint pin_d0) {
    g_pio = NULL;
    g_st = (gendev_stats_t){ 0 };
    g_last_d = wire_word(0);    // all lines released, so a rewind before the first put is inert
    g_last_t = wire_word(0);

    // One IN base and one JMP pin reach TH from both programs; each OUT window must be its own.
    if (pin_th >= pin_d0 && pin_th < pin_d0 + 4) { return false; }
    if (pin_th >= pin_tl && pin_th < pin_tl + 2) { return false; }
    if (pin_tl + 2 > pin_d0 && pin_d0 + 4 > pin_tl) { return false; }

    const struct pio_program *progs[] = {
        drive_patch(&gen_dpad_program, g_ins_d, &g_prog_d, 4),
        drive_patch(&gen_tltr_program, g_ins_t, &g_prog_t, 2),
    };
    if (!pio_res_claim(&g_res, pio, progs, 2, 2)) { return false; }

    g_pio = pio;
    g_off_d = g_res.off[0]; g_off_t = g_res.off[1];
    g_sm_d  = g_res.sm[0];  g_sm_t  = g_res.sm[1];
    g_pin_th = pin_th;
    g_pin_tl = pin_tl;
    g_pin_d0 = pin_d0;

    gen_dpad_program_init(pio, g_sm_d, g_off_d, pin_d0, pin_th, !g_open_drain);
    gen_tltr_program_init(pio, g_sm_t, g_off_t, pin_tl, pin_th, !g_open_drain);

    g_th_last = gpio_get(pin_th);
    g_next_reset = timer_hw->timerawl + g_reset_us;

    pio_sm_run(pio, g_sm_d, true);
    pio_sm_run(pio, g_sm_t, true);
    return true;
}

void gendev_deinit(void) {
    if (!g_pio) { return; }
    pio_sm_run(g_pio, g_sm_d, false);
    pio_sm_run(g_pio, g_sm_t, false);
    // Push-pull leaves six lines as outputs; hand them back Hi-Z before the next driver claims them.
    pio_sm_set_pindirs_with_mask64(g_pio, g_sm_d, 0, 0xfULL << g_pin_d0);
    pio_sm_set_pindirs_with_mask64(g_pio, g_sm_t, 0, 0x3ULL << g_pin_tl);
    pio_res_release(&g_res);
    gendev_set_pullup(false);
    g_pio = NULL;
}

void gendev_set_reset_us(uint32_t us) { g_reset_us = us ? us : GEN_RESET_US_DEFAULT; }

// Must be set before gendev_init(): it decides which program is loaded and how the pins are primed.
void gendev_set_open_drain(bool on) { g_open_drain = on; }

void gendev_set_pullup(bool on) {
    if (!g_pio) { return; }
    for (uint i = 0; i < 6; i++) {
        uint p = i < 4 ? g_pin_d0 + i : g_pin_tl + (i - 4);
        if (on) {
            gpio_pull_up(p);
        } else {
            gpio_disable_pulls(p);
        }
    }
}

void __not_in_flash_func(gendev_put)(uint32_t dpad_word, uint32_t tltr_word) {
    if (!g_pio) { return; }
    dpad_word = wire_word(dpad_word);
    tltr_word = wire_word(tltr_word);
    g_last_d = dpad_word;
    g_last_t = tltr_word;
    // A full FIFO means the SM is mid-poll holding its word; drop, the next call lands in the gap.
    if (pio_sm_is_tx_fifo_full(g_pio, g_sm_d)) {
        g_st.drops++;
    } else {
        pio_sm_put(g_pio, g_sm_d, dpad_word);
    }
    if (!pio_sm_is_tx_fifo_full(g_pio, g_sm_t)) { pio_sm_put(g_pio, g_sm_t, tltr_word); }
}

// Re-prime the FIFO: pio_sm_restart() leaves X stale, and an empty FIFO would serve it.
static void __not_in_flash_func(sm_rewind)(uint sm, uint off, uint32_t word) {
    pio_sm_run(g_pio, sm, false);
    pio_sm_clear_fifos(g_pio, sm);
    pio_sm_restart(g_pio, sm);
    pio_sm_exec(g_pio, sm, pio_encode_jmp(off));
    pio_sm_put(g_pio, sm, word);
    pio_sm_run(g_pio, sm, true);
}

// gen_tltr is rewound too: parked at `wait 1 pin 0` under a low TH, it would freeze TL/TR.
static void __not_in_flash_func(gendev_resync)(void) {
    sm_rewind(g_sm_d, g_off_d + gen_dpad_offset_idle, g_last_d);
    sm_rewind(g_sm_t, g_off_t + gen_tltr_offset_idle, g_last_t);
    g_st.resyncs++;
}

// The counter reset fires on either TH level, armed off the last TH edge so it never lands inside a
// read; that edge is also the poll signal, since a PIO irq would fire on every rewind.
bool __not_in_flash_func(gendev_service)(uint32_t timeout_us) {
    if (!g_pio) { return false; }
    uint32_t t0 = timer_hw->timerawl;
    for (;;) {
        uint32_t now = timer_hw->timerawl;
        bool th = gpio_get(g_pin_th);
        if (th != g_th_last) {
            g_th_last = th;
            g_next_reset = now + g_reset_us;
            g_st.polls++;
            return true;
        }
        if ((int32_t)(now - g_next_reset) >= 0) {
            gendev_resync();
            g_next_reset = now + GEN_REARM_US;
        }
        if (now - t0 > timeout_us) { return false; }
    }
}

const gendev_stats_t *gendev_stats(void) { return &g_st; }
