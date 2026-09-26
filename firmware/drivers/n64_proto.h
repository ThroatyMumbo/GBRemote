// n64_proto.h — the N64 controller command set as pure host-testable logic; the wire is jbdev.c.
#ifndef N64_PROTO_H
#define N64_PROTO_H

#include <stdint.h>
#include <stdbool.h>
#include "pad.h"

#define N64_BLOCK 32

enum {
    N64_CMD_IDENTIFY = 0x00,
    N64_CMD_POLL     = 0x01,
    N64_CMD_ACC_READ = 0x02,
    N64_CMD_ACC_WRITE= 0x03,
    N64_CMD_RESET    = 0xff,
};

// 16-bit button word in poll-response byte order: buttons[15:8] then buttons[7:0].
#define N64_BTN_A      (1u << 15)
#define N64_BTN_B      (1u << 14)
#define N64_BTN_Z      (1u << 13)
#define N64_BTN_START  (1u << 12)
#define N64_BTN_DUP    (1u << 11)
#define N64_BTN_DDOWN  (1u << 10)
#define N64_BTN_DLEFT  (1u << 9)
#define N64_BTN_DRIGHT (1u << 8)
#define N64_BTN_RESET  (1u << 7)    // the console's own L+R+START; a device must never assert it
#define N64_BTN_L      (1u << 5)
#define N64_BTN_R      (1u << 4)
#define N64_BTN_CUP    (1u << 3)
#define N64_BTN_CDOWN  (1u << 2)
#define N64_BTN_CLEFT  (1u << 1)
#define N64_BTN_CRIGHT (1u << 0)

// The low two bits are one field, not flags (libdragon joybus.h's accessory status).
// The console issues no 0x02/0x03 unless it reads ACC_PRESENT.
#define N64_ST_ACC_MASK        0x03
#define N64_ST_ACC_UNSUPPORTED 0x00
#define N64_ST_ACC_PRESENT     0x01
#define N64_ST_ACC_ABSENT      0x02
#define N64_ST_ACC_CHANGED     0x03
#define N64_ST_ADDR_CRC_ERR    0x04

typedef struct { uint16_t buttons; int8_t stick_x, stick_y; } n64_state_t;

// PAK_ABSENT answers with the inverted data CRC ("no pak"); PAK_RETRY with a merely wrong one, which
// masters retry, the only way to say "present but not ready".
typedef enum { PAK_OK = 0, PAK_ABSENT, PAK_RETRY } pak_result_t;

// The 0x02/0x03 extension point; NULL means no pak.
typedef struct {
    pak_result_t (*read) (uint16_t addr, uint8_t out[N64_BLOCK], void *ctx);
    pak_result_t (*write)(uint16_t addr, const uint8_t in[N64_BLOCK], void *ctx);
    void *ctx;
} n64_pak_t;

uint16_t n64_addr_crc(uint16_t addr);
uint8_t  n64_data_crc(const uint8_t data[N64_BLOCK]);

uint8_t n64_request_len(uint8_t opcode);    // 1/1/1/3/35; 0 == unknown, never reply

// Returns the reply length, 0 if the opcode has no reply. status is in/out.
uint8_t n64_build_reply(uint8_t opcode, const uint8_t *req, uint8_t req_len,
                        const n64_state_t *st, uint8_t *status,
                        const n64_pak_t *pak, uint8_t out[N64_BLOCK + 1]);

// Canonical pad -> N64 wire state. cardinal/diagonal are the stick magnitudes; dpad_digital routes
// the canonical d-pad to the N64 d-pad instead of the stick.
#define N64_STICK_CARDINAL 80   // unverified convention: a real stick never reaches 127,
#define N64_STICK_DIAGONAL 70   // and (80,80) is outside a real octagonal gate

void n64_from_pad(const pad_state_t *p, uint8_t cardinal, uint8_t diagonal,
                  bool dpad_digital, n64_state_t *out);

#endif
