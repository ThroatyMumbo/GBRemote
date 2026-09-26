// padart.h — core0 stages the resident driver's picture at $4600 (padart_fmt.h), a bounded chunk
// per call and only when the resident proto moves.
#ifndef PADART_H
#define PADART_H

#include <stdint.h>

void padart_init(void);

// The sequence to publish at $0019 once the window is settled, or 0. Publish $001A from
// padart_proto() first: the sequence is what says the pair is whole.
uint8_t padart_poll(uint8_t active_proto);
uint8_t padart_proto(void);     // PROTO_* the window holds, 0 for "no picture for this driver"

#endif
