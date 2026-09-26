#include "n64_proto.h"
#include <string.h>

uint16_t n64_addr_crc(uint16_t addr) {
    static const uint16_t xor_table[16] = {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x15, 0x1F, 0x0B,
        0x16, 0x19, 0x07, 0x0E,
        0x1C, 0x0D, 0x1A, 0x01
    };
    uint16_t crc = 0;
    addr &= 0xFFE0;
    for (int i = 15; i >= 5; i--) {
        if ((addr >> i) & 1) { crc ^= xor_table[i]; }
    }
    return addr | (crc & 0x1F);
}

uint8_t n64_data_crc(const uint8_t data[N64_BLOCK]) {
    unsigned crc = 0;
    for (int i = 0; i < N64_BLOCK; i++) {
        unsigned x = crc ^ data[i];
        crc = 0;
        if (x & 0x80) { crc ^= 0x89; }
        if (x & 0x40) { crc ^= 0x86; }
        if (x & 0x20) { crc ^= 0x43; }
        if (x & 0x10) { crc ^= 0xE3; }
        if (x & 0x08) { crc ^= 0xB3; }
        if (x & 0x04) { crc ^= 0x9B; }
        if (x & 0x02) { crc ^= 0x8F; }
        if (x & 0x01) { crc ^= 0x85; }
    }
    return (uint8_t)crc;
}

uint8_t n64_request_len(uint8_t opcode) {
    switch (opcode) {
    case N64_CMD_IDENTIFY:
    case N64_CMD_POLL:
    case N64_CMD_RESET:     return 1;
    case N64_CMD_ACC_READ:  return 3;
    case N64_CMD_ACC_WRITE: return 35;
    default:                return 0;
    }
}

uint8_t n64_build_reply(uint8_t opcode, const uint8_t *req, uint8_t req_len,
                        const n64_state_t *st, uint8_t *status,
                        const n64_pak_t *pak, uint8_t out[N64_BLOCK + 1]) {
    switch (opcode) {
    // A real controller answers 0xFF exactly like 0x00: it does not raise CHANGED.
    case N64_CMD_RESET:
    case N64_CMD_IDENTIFY:
        out[0] = 0x05;                      // controller
        out[1] = 0x00;
        out[2] = *status;
        // Both self-clear once reported: CHANGED decays to PRESENT, a CRC error is reported once.
        if ((*status & N64_ST_ACC_MASK) == N64_ST_ACC_CHANGED) {
            *status = (uint8_t)((*status & ~N64_ST_ACC_MASK) | N64_ST_ACC_PRESENT);
        }
        *status &= (uint8_t)~N64_ST_ADDR_CRC_ERR;
        return 3;

    case N64_CMD_POLL:
        out[0] = (uint8_t)(st->buttons >> 8);
        out[1] = (uint8_t)(st->buttons & 0xff);
        out[2] = (uint8_t)st->stick_x;
        out[3] = (uint8_t)st->stick_y;
        return 4;

    case N64_CMD_ACC_READ: {
        if (req_len < 3) { return 0; }
        uint16_t a = (uint16_t)((req[1] << 8) | req[2]);
        bool ok = (n64_addr_crc(a) == a);
        if (!ok) { *status |= N64_ST_ADDR_CRC_ERR; }
        // Not memset first: the pak overwrites all 32 bytes, and this runs in the turnaround.
        if (pak && pak->read && ok) {
            pak_result_t r = pak->read((uint16_t)(a & 0xFFE0), out, pak->ctx);
            if (r == PAK_OK) {
                out[N64_BLOCK] = n64_data_crc(out);
                return N64_BLOCK + 1;
            }
            memset(out, 0, N64_BLOCK);
            if (r == PAK_RETRY) {
                out[N64_BLOCK] = (uint8_t)(n64_data_crc(out) ^ 0x01);   // wrong, not inverted
                return N64_BLOCK + 1;
            }
        } else {
            memset(out, 0, N64_BLOCK);
        }
        out[N64_BLOCK] = (uint8_t)~n64_data_crc(out);   // inverted CRC == "no accessory"
        return N64_BLOCK + 1;
    }

    case N64_CMD_ACC_WRITE: {
        if (req_len < 35) { return 0; }
        uint16_t a = (uint16_t)((req[1] << 8) | req[2]);
        bool ok = (n64_addr_crc(a) == a);
        if (!ok) { *status |= N64_ST_ADDR_CRC_ERR; }
        uint8_t crc = n64_data_crc(&req[3]);
        pak_result_t r = PAK_ABSENT;
        if (pak && pak->write && ok) { r = pak->write((uint16_t)(a & 0xFFE0), &req[3], pak->ctx); }
        if (r == PAK_OK) {
            out[0] = crc;
        } else if (r == PAK_RETRY) {
            out[0] = (uint8_t)(crc ^ 0x01);
        } else {
            out[0] = (uint8_t)~crc;
        }
        return 1;
    }

    default:
        return 0;
    }
}

void n64_from_pad(const pad_state_t *p, uint8_t cardinal, uint8_t diagonal,
                  bool dpad_digital, n64_state_t *out) {
    uint32_t b = p->buttons;
    uint16_t n = 0;

    if (b & PAD_A) { n |= N64_BTN_A; }
    if (b & PAD_B) { n |= N64_BTN_B; }
    if (b & PAD_START) { n |= N64_BTN_START; }
    if (b & PAD_L) { n |= N64_BTN_L; }
    if (b & PAD_R) { n |= N64_BTN_R; }
    if (b & PAD_C_UP) { n |= N64_BTN_CUP; }
    if (b & PAD_C_DOWN) { n |= N64_BTN_CDOWN; }
    if (b & PAD_C_LEFT) { n |= N64_BTN_CLEFT; }
    if (b & PAD_C_RIGHT) { n |= N64_BTN_CRIGHT; }
    // SELECT is otherwise dead weight and Z is the button a Game Boy pad has no home for.
    if (b & (PAD_SELECT | PAD_ZL | PAD_ZR)) { n |= N64_BTN_Z; }

    int dx = pad_dir(b, PAD_LEFT, PAD_RIGHT);
    int dy = pad_dir(b, PAD_DOWN, PAD_UP);
    if (p->axis[AX_LX] > 0) {
        dx = 1;
    } else if (p->axis[AX_LX] < 0) {
        dx = -1;
    }
    if (p->axis[AX_LY] > 0) {
        dy = 1;
    } else if (p->axis[AX_LY] < 0) {
        dy = -1;
    }

    if (dpad_digital) {
        if (dx > 0) {
            n |= N64_BTN_DRIGHT;
        } else if (dx < 0) {
            n |= N64_BTN_DLEFT;
        }
        if (dy > 0) {
            n |= N64_BTN_DUP;
        } else if (dy < 0) {
            n |= N64_BTN_DDOWN;
        }
        out->stick_x = out->stick_y = 0;
    } else {
        // Most N64 games poll only the stick; a d-pad-only pad walks into walls.
        int mag = (dx && dy) ? diagonal : cardinal;
        out->stick_x = (int8_t)(dx * mag);
        out->stick_y = (int8_t)(dy * mag);
    }

    out->buttons = n & (uint16_t)~N64_BTN_RESET;
}
