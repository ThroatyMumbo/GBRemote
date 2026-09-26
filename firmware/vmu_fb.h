// vmu_fb.h — core0's half of the VMU screen: core1's frame formatted into a served bank. Double
// buffered by bank flip, not cs_poke_bank().
#ifndef VMU_FB_H
#define VMU_FB_H

#include <stdint.h>
#include <stdbool.h>

// In bank 1, where the menu ROM links nothing: every byte of it in the built image is $FF filler,
// and tools/run.sh asserts it. Below $8000, as write_capture's A15 gate needs.
#define VMU_FB_ADDR 0x4000

void vmu_fb_init(void);

// The two high banks the flip alternates. Only $4000-$45FF is rewritten here; anything above it
// must go into both, or the next flip serves the stale copy. padart.c is the other writer.
#define VMU_FB_BANKS 2
uint8_t *vmu_fb_bank(unsigned i);

// Bounded work per call: one strip of the frame, then the flip. Returns the sequence to publish
// once a whole frame has been mounted, or 0.
uint8_t vmu_fb_poll(void);

void vmu_fb_test_toggle(void);      // bench 'l': an animated pattern with no console attached
bool vmu_fb_test_active(void);

#endif
