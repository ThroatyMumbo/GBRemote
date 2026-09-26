#include "maple_proto.h"
#include <string.h>

#define DEVINFO_LEN 112
#define ALLINFO_LEN 192

// FuncData[0] for the HKT-7700, verbatim from MaplePad's maple.c: which buttons and axes exist.
#define FUNCDATA0_HKT7700 0x000f06feu

// Verified byte for byte against a real HKT-7700 off the bus.
static const maple_devid_t k_hkt7700 = {
    .func          = MAPLE_FUNC_CONTROLLER,
    .func_data     = { FUNCDATA0_HKT7700, 0, 0 },
    .area_code     = 0xff,
    .connector_dir = 0x00,
    .name          = "Dreamcast Controller",
    .license       = "Produced By or Under License From SEGA ENTERPRISES,LTD.",
    .free_status   = "Version 1.010,1998/09/28,315-6211-AB   ,Analog Module : The 4th Edition.5/8  +DF",
    .standby_power = 430,
    .max_power     = 500,
};

static void put_word(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void put_u16(uint8_t *p, uint16_t v) {       // native, not swapped — as the references leave it
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}

// Space padded, never NUL terminated: the padding is part of the wire format.
static void put_text(uint8_t *p, unsigned n, const char *s) {
    unsigned i = 0;
    while (i < n && s[i]) { p[i] = (uint8_t)s[i]; i++; }
    while (i < n) { p[i++] = ' '; }
}

uint8_t maple_checksum(const uint8_t *frame, unsigned n) {
    uint8_t x = 0;
    for (unsigned i = 0; i < n; i++) { x ^= frame[i]; }
    return x;
}

unsigned maple_pack_words(const uint8_t *frame, unsigned n, uint32_t *words) {
    unsigned k = n / 4U;
    for (unsigned w = 0; w < k; w++) {
        const uint8_t *p = frame + 4U * w; // little-endian load; the PIO reverses it again
        words[w] = (uint32_t)p[0] | ((uint32_t)p[1] << 8)
                 | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
    // The SM shifts MSB first and the bit-pair count cuts the last word short, so the checksum
    // goes in its top byte.
    if (n & 3U) {
        words[k] = (uint32_t)frame[n - 1U] << 24;
        return k + 1U;
    }
    return k;
}

unsigned maple_unpack(const uint8_t *wire, unsigned n, uint8_t *out) {
    if (n < 5U || (n & 3U) != 1U || n > MAPLE_MAX_FRAME) { return 0; }
    unsigned k = n / 4U;
    for (unsigned w = 0; w < k; w++) {          // each whole word arrives reversed
        out[4U * w + 0] = wire[4U * w + 3];
        out[4U * w + 1] = wire[4U * w + 2];
        out[4U * w + 2] = wire[4U * w + 1];
        out[4U * w + 3] = wire[4U * w + 0];
    }
    return k * 4U; // the checksum is dropped, already verified
}

void maple_cond_default(maple_cond_t *c) {
    c->buttons = 0xffffU;
    c->trig_r = c->trig_l = 0;
    c->joy_x = c->joy_y = c->joy_x2 = c->joy_y2 = MAPLE_AXIS_CENTER;
}

static uint8_t axis_to_dc(int v) {
    int r = MAPLE_AXIS_CENTER + v;
    if (r < 0) { r = 0; }
    if (r > 255) { r = 255; }
    return (uint8_t)r;
}

void maple_from_pad(const pad_state_t *p, bool dpad_analog, maple_cond_t *out) {
    maple_cond_default(out);

    uint32_t b = p->buttons;
    uint16_t m = 0;
    if (b & PAD_A) { m |= MAPLE_BTN_A; }
    if (b & PAD_B) { m |= MAPLE_BTN_B; }
    if (b & PAD_X) { m |= MAPLE_BTN_X; }
    if (b & PAD_Y) { m |= MAPLE_BTN_Y; }
    if (b & PAD_START) { m |= MAPLE_BTN_START; }

    // The Dreamcast has no digital shoulders, so L/R land on the analog triggers at full scale.
    if (b & (PAD_L | PAD_ZL)) { out->trig_l = 0xff; }
    if (b & (PAD_R | PAD_ZR)) { out->trig_r = 0xff; }

    int dx = pad_dir(b, PAD_LEFT, PAD_RIGHT);
    int dy = pad_dir(b, PAD_DOWN, PAD_UP);

    // An analog axis, when the map supplies one, wins over the digital d-pad.
    int ax = p->axis[AX_LX];
    int ay = p->axis[AX_LY];
    bool analog = (ax != 0 || ay != 0);

    if (analog) {
        out->joy_x = axis_to_dc(ax);
        out->joy_y = axis_to_dc(-ay);           // canonical +Y is up; the DC's is down
    } else if (dpad_analog) {
        out->joy_x = axis_to_dc(dx * MAPLE_AXIS_FULL);
        out->joy_y = axis_to_dc(-dy * MAPLE_AXIS_FULL);
    } else {
        if (dx > 0) {
            m |= MAPLE_BTN_RIGHT;
        } else if (dx < 0) {
            m |= MAPLE_BTN_LEFT;
        }
        if (dy > 0) {
            m |= MAPLE_BTN_UP;
        } else if (dy < 0) {
            m |= MAPLE_BTN_DOWN;
        }
    }

    out->buttons = (uint16_t)~m;                // active low
}

static unsigned build_header(uint8_t *out, uint8_t cmd, uint8_t src, uint8_t port, unsigned nwords) {
    out[0] = cmd;
    out[1] = (uint8_t)(MAPLE_ADDR_DC | port);
    out[2] = (uint8_t)(src | port);
    out[3] = (uint8_t)nwords;
    return 4;
}

static unsigned build_devinfo(uint8_t *p, bool all, const maple_devid_t *id) {
    memset(p, 0, all ? ALLINFO_LEN : DEVINFO_LEN);
    put_word(p +  0, id->func);
    put_word(p +  4, id->func_data[0]);
    put_word(p +  8, id->func_data[1]);
    put_word(p + 12, id->func_data[2]);
    p[16] = id->area_code;
    p[17] = id->connector_dir;
    put_text(p + 18, 30, id->name);
    put_text(p + 48, 60, id->license);
    put_u16(p + 108, id->standby_power);
    put_u16(p + 110, id->max_power);
    if (all) { put_text(p + 112, 80, id->free_status); }
    return all ? ALLINFO_LEN : DEVINFO_LEN;
}

static unsigned build_cond(uint8_t *p, const maple_cond_t *c) {
    put_word(p + 0, MAPLE_FUNC_CONTROLLER);
    p[4]  = (uint8_t)c->buttons;
    p[5]  = (uint8_t)(c->buttons >> 8);
    p[6]  = c->trig_r;
    p[7]  = c->trig_l;
    p[8]  = c->joy_x;
    p[9]  = c->joy_y;
    p[10] = c->joy_x2;
    p[11] = c->joy_y2;
    return 12;
}

// The request carries at least n data words, by its own count and by what actually arrived.
static bool has_words(const uint8_t *req, unsigned req_len, unsigned n) {
    return maple_nwords(req) >= n && req_len >= 4U + 4U * n;
}

// What every unit answers the same way. False: not one of these, *len untouched.
static bool common_command(const maple_unit_t *u, const uint8_t *req, uint8_t *payload,
                           uint8_t *resp, int *len) {
    switch (maple_cmd(req)) {
    case MAPLE_CMD_RESET_DEVICE:
        *resp = MAPLE_CMD_RESP_ACK;
        *len = 0;
        return true;
    case MAPLE_CMD_DEVICE_REQUEST:
        *resp = MAPLE_CMD_RESP_DEVICE_STATUS;
        *len = (int)build_devinfo(payload, false, u->id);
        return true;
    case MAPLE_CMD_ALL_STATUS_REQUEST:
        *resp = MAPLE_CMD_RESP_ALL_STATUS;
        *len = (int)build_devinfo(payload, true, u->id);
        return true;
    default:
        return false;
    }
}

static int controller_command(const maple_unit_t *u, const uint8_t *req, unsigned req_len,
                              uint8_t *payload, uint8_t *resp) {
    int len;
    if (common_command(u, req, payload, resp, &len)) { return len; }

    switch (maple_cmd(req)) {
    case MAPLE_CMD_GET_CONDITION:
        if (!has_words(req, req_len, 1)) { return MAPLE_REPLY_NONE; }
        // A function we do not have is refused, not ignored, as a real HKT-7700 does; silence
        // here would be a device no console has ever seen.
        if (maple_data_word(req, 0) != u->id->func) {
            *resp = MAPLE_RESP_UNKNOWN_FUNC;
            return 0;
        }
        *resp = MAPLE_CMD_RESP_DATA_XFER;
        return (int)build_cond(payload, (const maple_cond_t *)u->state);

    default:
        // A real controller answers BADCMD to everything else, GET_MEDIA_INFO for its own function
        // included: command-unknown is decided before function-unknown.
        *resp = MAPLE_RESP_UNKNOWN_CMD;
        return 0;
    }
}

void maple_controller_unit(maple_unit_t *u, maple_cond_t *cond) {
    u->addr    = MAPLE_ADDR_CONTROLLER;
    u->id      = &k_hkt7700;
    u->command = controller_command;
    u->state   = cond;
}

// Every byte below was read off a real Visual Memory.
static const maple_devid_t k_visual_memory = {
    .func = MAPLE_FUNC_VMU,
    .func_data = {0x7e7e3f40U, 0x00051000U, 0x000f4100U}, // clock, LCD, storage
    .area_code = 0xff,
    .connector_dir = 0x00,
    .name = "Visual Memory",
    .license = "Produced By or Under License From SEGA ENTERPRISES,LTD.",
    .free_status =
        "Version 1.005,1999/04/28,315-6124-07,SEGA Visual Memory System BIOS Produced by ",
    .standby_power = 124,
    .max_power = 130,
};

// 7 words, not MaplePad's padded 8: the root block's geometry at +0x40, where a real VMU keeps it,
// so the reply and the filesystem cannot drift apart.
static unsigned build_minfo_card(const maple_vmu_t *st, uint8_t *p) {
    put_word(p + 0, MAPLE_FUNC_MEMCARD);
    memcpy(p + 4, st->card + VMU_ROOT_BLOCK * MAPLE_VMU_BLOCK + VMU_ROOT_GEOM, 24);
    return 28;
}

static unsigned build_minfo_lcd(uint8_t *p) {
    put_word(p + 0, MAPLE_FUNC_LCD);
    p[4] = 47;                                  // 48 dots across
    p[5] = 31;                                  // 32 down
    p[6] = 0x10;                                // 1 bit/dot, contrast 0
    p[7] = 0x02;                                // measured; MaplePad leaves this zero
    return 8;
}

// Hinnant's civil_from_days, era-based and 32-bit throughout: no RTC on this board, so the date
// is a second count the console sets and we tick.
static void civil_from_days(uint32_t z, uint16_t *y, uint8_t *m, uint8_t *d) {
    z += 719468U; // re-base on 0000-03-01
    uint32_t era = z / 146097U;
    uint32_t doe = z - era * 146097U;
    uint32_t yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    uint32_t doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    uint32_t mp = (5U * doy + 2U) / 153U;
    uint32_t mm = mp < 10U ? mp + 3U : mp - 9U;
    *d = (uint8_t)(doy - (153U * mp + 2U) / 5U + 1U);
    *m = (uint8_t)mm;
    *y = (uint16_t)(yoe + era * 400U + (mm <= 2U));
}

static uint32_t days_from_civil(uint16_t y, uint8_t m, uint8_t d) {
    uint32_t yy = (uint32_t)y - (m <= 2U);
    uint32_t era = yy / 400U;
    uint32_t yoe = yy - era * 400U;
    uint32_t doy = (153U * (m > 2U ? m - 3U : m + 9U) + 2U) / 5U + d - 1U;
    return era * 146097U + yoe * 365U + yoe / 4U - yoe / 100U + doy - 719468U;
}

void maple_date_from_secs(uint32_t secs, uint16_t *y, uint8_t *mo, uint8_t *d,
                          uint8_t *h, uint8_t *mi, uint8_t *s) {
    uint32_t tod = secs % 86400U;

    civil_from_days(secs / 86400U, y, mo, d);
    *h = (uint8_t)(tod / 3600U);
    *mi = (uint8_t)((tod / 60U) % 60U);
    *s = (uint8_t)(tod % 60U);
}

// The survey marks a clock read volatile, so only the echoed function code is compared against it.
// Plain binary, not BCD; the weekday byte read 0 on both captures of a running card.
static unsigned build_clock_read(const maple_vmu_t *st, uint8_t *p) {
    uint32_t secs = st->clock_secs + (st->now_ms - st->clock_base_ms) / 1000U;
    uint16_t y;
    uint8_t mo;
    uint8_t d;
    uint8_t h;
    uint8_t mi;
    uint8_t s;

    maple_date_from_secs(secs, &y, &mo, &d, &h, &mi, &s);
    put_word(p + 0, MAPLE_FUNC_CLOCK);
    put_u16(p + 4, y);
    p[6] = mo;
    p[7] = d;
    p[8] = h;
    p[9] = mi;
    p[10] = s;
    p[11] = 0;
    return 12;
}

// What the Dreamcast BIOS sets at power-up, same field order as the read. The refusal code for an
// impossible date is ours, not measured — a real card was never asked for one.
static bool vmu_clock_write(maple_vmu_t *st, const uint8_t *src, unsigned n) {
    uint16_t y = (uint16_t)(src[0] | (src[1] << 8));

    if (n != 8 || y < 1970U || y > 2105U || src[2] < 1U || src[2] > 12U || src[3] < 1U ||
        src[3] > 31U || src[4] > 23U || src[5] > 59U || src[6] > 59U) {
        return false;
    }

    st->clock_secs =
        days_from_civil(y, src[2], src[3]) * 86400U + src[4] * 3600U + src[5] * 60U + src[6];
    st->clock_base_ms = st->now_ms;
    return true;
}

#ifdef BENCH_PROTO
static unsigned g_read_cap;
void maple_vmu_read_cap(unsigned bytes) { g_read_cap = bytes & ~3u; }
#endif

// bits 15:0 block, 23:16 phase, 31:24 partition — the layout vmu_block_read() builds.
static int vmu_block(const maple_vmu_t *st, uint32_t blkid, uint8_t *p, uint8_t *resp) {
    unsigned block = blkid & 0xffffU;

    if (((blkid >> 16) & 0xffU) != 0) { // a 512-byte card has one phase
        *resp = MAPLE_RESP_FILE_ERR;
        put_word(p, 2);
        return 4;
    }
    if (block >= MAPLE_VMU_BLOCKS || !st->card) {
        *resp = MAPLE_RESP_FILE_ERR;
        put_word(p, 4);
        return 4;
    }
    unsigned n = MAPLE_VMU_BLOCK;
#ifdef BENCH_PROTO
    if (g_read_cap) n = g_read_cap;
#endif
    *resp = MAPLE_CMD_RESP_DATA_XFER;
    put_word(p + 0, MAPLE_FUNC_MEMCARD);
    put_word(p + 4, blkid);
    memcpy(p + 8, st->card + block * MAPLE_VMU_BLOCK, n);
    return 8 + (int)n;
}

// A block is read whole but written in four 128-byte phases, the phase picking the quarter.
// The LCD takes a whole frame.
#define VMU_WRITE_PHASE 128u

static int vmu_write(maple_vmu_t *st, const uint8_t *req, unsigned req_len,
                     uint32_t func, uint8_t *resp) {
    uint32_t blkid = maple_data_word(req, 1);
    unsigned block = blkid & 0xffffU;
    unsigned phase = (blkid >> 16) & 0xffU;
    unsigned n = (maple_nwords(req) - 2U) * 4U;
    const uint8_t *src = req + 12;

    if (req_len < 12U + n) { return MAPLE_REPLY_NONE; }

    // A whole frame or nothing: KOS sends all 192 bytes in one write, and half a frame on the
    // screen is worse than a dropped one. The ACK is unconditional either way.
    if (func == MAPLE_FUNC_LCD) {
        if (st->on_lcd && n == MAPLE_VMU_LCD_BYTES) { st->on_lcd(st->ctx, src, n); }
        *resp = MAPLE_CMD_RESP_ACK;
        return 0;
    }
    if (func == MAPLE_FUNC_CLOCK) {
        *resp = vmu_clock_write(st, src, n) ? MAPLE_CMD_RESP_ACK : MAPLE_RESP_FILE_ERR;
        return 0;
    }
    if (func != MAPLE_FUNC_MEMCARD) { *resp = MAPLE_RESP_UNKNOWN_FUNC; return 0; }

    if (block >= MAPLE_VMU_BLOCKS || !st->card
        || phase >= MAPLE_VMU_BLOCK / VMU_WRITE_PHASE
        || n > VMU_WRITE_PHASE) {
        *resp = MAPLE_RESP_FILE_ERR;
        return 0;
    }

    memcpy(st->card + block * MAPLE_VMU_BLOCK + phase * VMU_WRITE_PHASE, src, n);
    if (st->on_write) { st->on_write(st->ctx, block, st->now_ms); }
    *resp = MAPLE_CMD_RESP_ACK;
    return 0;
}

void maple_vmu_format(uint8_t *card) {
    uint8_t *root = card + VMU_ROOT_BLOCK * MAPLE_VMU_BLOCK;
    uint8_t *fat  = card + VMU_FAT_BLOCK * MAPLE_VMU_BLOCK;

    memset(card, 0, MAPLE_VMU_BLOCKS * MAPLE_VMU_BLOCK);

    memset(root, 0x55, 16);                     // the "formatted" marker
    root[0x10] = 0x01;                          // custom volume color, opaque white — as the real card
    root[0x11] = root[0x12] = root[0x13] = root[0x14] = 0xff;
    root[0x30] = 0x19; root[0x31] = 0x99;       // BCD format timestamp, 1999-04-28 00:00:00
    root[0x32] = 0x04; root[0x33] = 0x28;
    root[0x34] = 0x00; root[0x35] = 0x00; root[0x36] = 0x00; root[0x37] = 0x02;

    // The geometry, which GET_MEDIA_INFO hands back untouched.
    put_u16(root + VMU_ROOT_GEOM +  0, MAPLE_VMU_BLOCKS - 1);   // total
    put_u16(root + VMU_ROOT_GEOM +  2, 0);                      // partition
    put_u16(root + VMU_ROOT_GEOM +  4, VMU_ROOT_BLOCK);
    put_u16(root + VMU_ROOT_GEOM +  6, VMU_FAT_BLOCK);
    put_u16(root + VMU_ROOT_GEOM +  8, 1);                      // FAT is one block
    put_u16(root + VMU_ROOT_GEOM + 10, VMU_DIR_BLOCK);
    put_u16(root + VMU_ROOT_GEOM + 12, VMU_DIR_BLOCKS);
    put_u16(root + VMU_ROOT_GEOM + 14, 0);                      // icon shape
    put_u16(root + VMU_ROOT_GEOM + 16, VMU_USER_BLOCKS);
    put_u16(root + VMU_ROOT_GEOM + 18, 31);                     // save blocks
    root[VMU_ROOT_GEOM + 20] = 0; root[VMU_ROOT_GEOM + 21] = 0;
    root[VMU_ROOT_GEOM + 22] = 0x80; root[VMU_ROOT_GEOM + 23] = 0;

    for (unsigned i = 0; i < MAPLE_VMU_BLOCKS; i++) { put_u16(fat + 2 * i, VMU_FAT_UNUSED); }
    put_u16(fat + 2 * VMU_ROOT_BLOCK, VMU_FAT_LAST);
    put_u16(fat + 2 * VMU_FAT_BLOCK,  VMU_FAT_LAST);
    // The directory is one descending chain, 253 down to 241, so the FAT walk ends where it ends.
    for (unsigned b = VMU_DIR_BLOCK; b > VMU_DIR_BLOCK - VMU_DIR_BLOCKS + 1; b--) {
        put_u16(fat + 2 * b, (uint16_t)(b - 1));
    }
    put_u16(fat + 2 * (VMU_DIR_BLOCK - VMU_DIR_BLOCKS + 1), VMU_FAT_LAST);
}

static int vmu_command(const maple_unit_t *u, const uint8_t *req, unsigned req_len,
                       uint8_t *payload, uint8_t *resp) {
    maple_vmu_t *st = (maple_vmu_t *)u->state;
    uint32_t func;
    int len;
    if (common_command(u, req, payload, resp, &len)) { return len; }

    switch (maple_cmd(req)) {
    // A real card refuses a function it does not back with BADFUNC, where a controller — which
    // does not implement the command at all — answers BADCMD. Both measured.
    case MAPLE_CMD_GET_MEDIA_INFO:
        if (!has_words(req, req_len, 2)) { return MAPLE_REPLY_NONE; }
        func = maple_data_word(req, 0);
        if (func == MAPLE_FUNC_MEMCARD) { *resp = MAPLE_CMD_RESP_DATA_XFER; return (int)build_minfo_card(st, payload); }
        if (func == MAPLE_FUNC_LCD)     { *resp = MAPLE_CMD_RESP_DATA_XFER; return (int)build_minfo_lcd(payload); }
        *resp = MAPLE_RESP_UNKNOWN_FUNC;        // the clock has no media info
        return 0;

    case MAPLE_CMD_GET_CONDITION:
        if (!has_words(req, req_len, 1)) { return MAPLE_REPLY_NONE; }
        if (maple_data_word(req, 0) != MAPLE_FUNC_CLOCK) { *resp = MAPLE_RESP_UNKNOWN_FUNC; return 0; }
        *resp = MAPLE_CMD_RESP_DATA_XFER;
        put_word(payload + 0, MAPLE_FUNC_CLOCK);
        payload[4] = 0xff;                      // buttons, active low
        payload[5] = payload[6] = payload[7] = 0;
        return 8;

    case MAPLE_CMD_BLOCK_READ:
        if (!has_words(req, req_len, 2)) { return MAPLE_REPLY_NONE; }
        func = maple_data_word(req, 0);
        if (func == MAPLE_FUNC_MEMCARD) {
            return vmu_block(st, maple_data_word(req, 1), payload, resp);
        }
        if (func == MAPLE_FUNC_CLOCK)   { *resp = MAPLE_CMD_RESP_DATA_XFER; return (int)build_clock_read(st, payload); }
        *resp = MAPLE_RESP_UNKNOWN_FUNC;        // the LCD is written, never read
        return 0;

    case MAPLE_CMD_BLOCK_WRITE:
        if (!has_words(req, req_len, 2)) { return MAPLE_REPLY_NONE; }
        return vmu_write(st, req, req_len, maple_data_word(req, 0), resp);

    // The commit after a write. A real card is covering its flash here; ours is RAM, so the only
    // thing that matters is that the ACK comes back and the console stops waiting.
    case MAPLE_CMD_BLOCK_SYNC:
        if (!has_words(req, req_len, 1)) { return MAPLE_REPLY_NONE; }
        func = maple_data_word(req, 0);
        if (func != MAPLE_FUNC_MEMCARD && func != MAPLE_FUNC_LCD) { *resp = MAPLE_RESP_UNKNOWN_FUNC; return 0; }
        *resp = MAPLE_CMD_RESP_ACK;
        return 0;

    // The clock's buzzer. Accepted so a console that sets it is not left waiting; nothing sounds.
    case MAPLE_CMD_SET_CONDITION:
        if (!has_words(req, req_len, 1)) { return MAPLE_REPLY_NONE; }
        if (maple_data_word(req, 0) != MAPLE_FUNC_CLOCK) { *resp = MAPLE_RESP_UNKNOWN_FUNC; return 0; }
        *resp = MAPLE_CMD_RESP_ACK;
        return 0;

    default:
        *resp = MAPLE_RESP_UNKNOWN_CMD;
        return 0;
    }
}

void maple_vmu_unit(maple_unit_t *u, maple_vmu_t *st, uint8_t *card, uint8_t addr) {
    memset(st, 0, sizeof *st);
    st->card       = card;
    st->clock_secs = MAPLE_VMU_EPOCH;
    u->addr    = addr;
    u->id      = &k_visual_memory;
    u->command = vmu_command;
    u->state   = st;
}

void maple_bus_init(maple_bus_t *b) { memset(b, 0, sizeof *b); }

int maple_bus_find(const maple_bus_t *b, uint8_t dst) {
    uint8_t a = dst & MAPLE_PERIPH_MASK;
    for (unsigned i = 0; i < b->n; i++) {
        if (b->unit[i]->addr == a) { return (int)i; }
    }
    return -1;
}

bool maple_bus_add(maple_bus_t *b, const maple_unit_t *u) {
    if (b->n == MAPLE_MAX_UNITS || maple_bus_find(b, u->addr) >= 0) { return false; }
    b->unit[b->n++] = u;
    if (u->addr != MAPLE_ADDR_CONTROLLER) { b->sub_mask |= u->addr & MAPLE_SUB_MASK; }
    return true;
}

unsigned maple_build_reply(const maple_bus_t *b, const uint8_t *req, unsigned req_len,
                           uint8_t out[MAPLE_MAX_FRAME]) {
    const maple_unit_t *u;
    uint8_t port;
    uint8_t src;
    uint8_t resp = 0;
    int payload;
    int ui;
    unsigned n;

    if (req_len < 4) { return 0; }
    ui = maple_bus_find(b, maple_dst(req));
    if (ui < 0) { return 0; }
    u = b->unit[ui];

    payload = u->command(u, req, req_len, out + 4, &resp);
    if (payload < 0 || (payload & 3)) {
        return 0; // a payload is always whole words
    }

    // The echoed port bits land in both address bytes and cancel in the XOR, so a precomputed
    // checksum survives any port; the sub-device bits do not, so they go in before the sum.
    port = maple_src(req) & MAPLE_PORT_MASK;
    src  = u->addr;
    if (src == MAPLE_ADDR_CONTROLLER) { src |= b->sub_mask; }

    n = build_header(out, resp, src, port, (unsigned)payload / 4U);
    n += (unsigned)payload;
    out[n] = maple_checksum(out, n);
    return n + 1U;
}

// Bus states as the sampler reports them: bit0 = SDCKA, bit1 = SDCKB.
enum { S00 = 0, S01 = 1, S10 = 2, S11 = 3 };

// status byte: 0xff none, 0 start, 1 end, 2..9 data bit 0..7, |0x80 the bit is a one.
#define ST_NONE   0xffu
#define ST_START  0u
#define ST_END    1u
#define ST_PUSH0  2u
#define ST_PUSH7  (ST_PUSH0 + 7u)
#define ST_BITSET 0x80u

static uint8_t dfa_new(maple_rx_t *d, unsigned expect) {
    uint8_t s = d->nstates++;
    memset(d->next[s], 0xff, 4);
    d->status[s] = ST_NONE;
    d->next[s][expect] = s;                 // the sampler can repeat a level; staying is correct
    return s;
}

static uint8_t dfa_expect(maple_rx_t *d, uint8_t parent, unsigned expect) {
    uint8_t s = dfa_new(d, expect);
    d->next[parent][expect] = s;
    return s;
}

static uint8_t dfa_expect_st(maple_rx_t *d, uint8_t parent, unsigned expect, uint8_t st) {
    uint8_t s = dfa_expect(d, parent, expect);
    d->status[s] = st;
    return s;
}

static uint8_t dfa_expect2(maple_rx_t *d, uint8_t p1, uint8_t p2, unsigned expect) {
    uint8_t s = dfa_new(d, expect);
    d->next[p1][expect] = s;
    d->next[p2][expect] = s;
    return s;
}

void maple_rx_reset(maple_rx_t *r) {
    r->state = 0;
    r->byte = 0;
    r->xorsum = 0;
    r->framing = false;
    r->len = 0;
}

void maple_rx_init(maple_rx_t *r) {
    r->nstates = 0;

    // Start: SDCKA drops and stays low while SDCKB pulses four times, then both go high.
    uint8_t prev = dfa_new(r, S11);
    for (int i = 0; i < 4; i++) {
        prev = dfa_expect(r, prev, S10);
        prev = dfa_expect(r, prev, S00);
    }
    prev = dfa_expect(r, prev, S10);
    prev = dfa_expect_st(r, prev, S11, ST_START);

    // Data: one line is high between bits and acts as the clock. Going to 0b11 is a one, going to
    // 0b00 is a zero, and the roles then swap. Eight bits, MSB first, per byte.
    uint8_t option = prev;
    uint8_t possible_end = 0;
    uint8_t start_byte = r->nstates;
    for (int i = 0; i < 4; i++) {
        prev   = dfa_expect2(r, option, prev, S01);
        option = dfa_expect_st(r, prev, S11, (uint8_t)(ST_PUSH0 + i * 2) | ST_BITSET);
        prev   = dfa_expect_st(r, prev, S00, (uint8_t)(ST_PUSH0 + i * 2));
        if (i == 0) { possible_end = option; }

        prev   = dfa_expect2(r, option, prev, S10);
        option = dfa_expect_st(r, prev, S11, (uint8_t)(ST_PUSH0 + i * 2 + 1) | ST_BITSET);
        prev   = dfa_expect_st(r, prev, S00, (uint8_t)(ST_PUSH0 + i * 2 + 1));
        if (i == 3) {
            r->next[option][S01] = start_byte;
            r->next[prev][S01]   = start_byte;
        }
    }

    // End: SDCKB stays low while SDCKA pulses. The event fires two states early on purpose — the
    // data is all in by then, and waiting for the last edge would leave no time to reply.
    prev = dfa_expect(r, possible_end, S01);
    prev = dfa_expect(r, prev, S00);
    prev = dfa_expect_st(r, prev, S01, ST_END);
    prev = dfa_expect(r, prev, S00);
    prev = dfa_expect(r, prev, S01);
    r->next[prev][S11] = 0;

    maple_rx_reset(r);
}

maple_rx_ev_t maple_rx_feed(maple_rx_t *r, uint8_t sample) {
    uint8_t prev = r->state;
    uint8_t ns = r->next[prev][sample & 3U];
    if (ns == 0xffU) {
        maple_rx_reset(r);
        return MAPLE_RX_ERROR;
    }

    r->state = ns;
    if (ns == prev) {
        return MAPLE_RX_MORE; // no transition, so no data
    }

    uint8_t st = r->status[ns];
    if (st == ST_NONE) { return MAPLE_RX_MORE; }

    if (st & ST_BITSET) { r->byte |= (uint8_t)(1U << (7U - ((st & 0x7fU) - ST_PUSH0))); }

    switch (st & 0x7fU) {
    case ST_START:
        r->len = 0; r->byte = 0; r->xorsum = 0; r->framing = true;
        break;

    case ST_END: {
        if (!r->framing) { return MAPLE_RX_MORE; }
        r->framing = false;
        // A frame is whole words plus the checksum byte, and every byte including it XORs to 0.
        bool ok = r->xorsum == 0 && r->len >= 5U && (r->len & 3U) == 1U;
        return ok ? MAPLE_RX_FRAME : MAPLE_RX_ERROR;
    }

    case ST_PUSH7:
        if (!r->framing) { break; }
        if (r->len >= MAPLE_MAX_FRAME) { maple_rx_reset(r); return MAPLE_RX_ERROR; }
        r->buf[r->len++] = r->byte;
        r->xorsum ^= r->byte;
        r->byte = 0;
        break;

    default:
        break;
    }
    return MAPLE_RX_MORE;
}
