// Emulated firmware entry points and the BIOS date/time area, from libevmu's evmu_rom.c (MIT, Falco
// Girgis). Unlike libevmu, MODE restarts the game and SLEEP holds until a button, then restarts.
#include "vmu_internal.h"

#define FAT_BLOCK      512u
#define FAT_ROOT_BLOCK 255u
#define FAT_DIR_BLOCK  253u
#define FAT_DIR_SIZE   13u
#define FAT_TYPE_GAME  0xcc

static uint8_t bcd_(int n) { return (uint8_t)(((n / 10) << 4) | (n % 10)); }

static bool leap_(int y) { return (y % 4 == 0) && (y % 100 != 0 || y % 400 == 0); }

static int month_days_(int y, int m) {
    if (m == 2) { return leap_(y) ? 29 : 28; }
    if (m > 7) { return (m & 1) ? 30 : 31; }
    return (m & 1) ? 31 : 30;
}

void vmu_set_datetime(vmu_t *v, int year, int month, int day, int hour, int min, int sec) {
    uint8_t *r = v->ram[0];
    r[SYS_YEAR_MSB_BCD] = bcd_(year / 100);
    r[SYS_YEAR_LSB_BCD] = bcd_(year % 100);
    r[SYS_MONTH_BCD]    = bcd_(month);
    r[SYS_DAY_BCD]      = bcd_(day);
    r[SYS_HOUR_BCD]     = bcd_(hour);
    r[SYS_MINUTE_BCD]   = bcd_(min);
    r[SYS_SEC_BCD]      = bcd_(sec);
    r[SYS_YEAR_MSB]     = (uint8_t)(year >> 8);
    r[SYS_YEAR_LSB]     = (uint8_t)year;
    r[SYS_MONTH]        = (uint8_t)month;
    r[SYS_DAY]          = (uint8_t)day;
    r[SYS_HOUR]         = (uint8_t)hour;
    r[SYS_MINUTE]       = (uint8_t)min;
    r[SYS_SEC]          = (uint8_t)sec;
    r[SYS_HALF_SEC]     = 0;
    r[SYS_LEAP_YEAR]    = leap_(year);
    r[SYS_DATE_SET]     = 0xff;
}

static void add_second_(vmu_t *v) {
    const uint8_t *r = v->ram[0];
    int year = (r[SYS_YEAR_MSB] << 8) | r[SYS_YEAR_LSB];
    int month = r[SYS_MONTH];
    int day = r[SYS_DAY];
    int hour = r[SYS_HOUR];
    int min = r[SYS_MINUTE];
    int sec = r[SYS_SEC] + 1;
    if (sec >= 60)  { sec = 0;  min++; }
    if (min >= 60)  { min = 0;  hour++; }
    if (hour >= 24) { hour = 0; day++; }
    if (month < 1 || month > 12) { month = 1; }
    if (day > month_days_(year, month)) { day = 1; month++; }
    if (month > 12) { month = 1; year++; }
    uint8_t half = r[SYS_HALF_SEC];
    vmu_set_datetime(v, year, month, day, hour, min, sec);
    v->ram[0][SYS_HALF_SEC] = half;
}

static uint16_t le16_(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool vmu_game_range(const vmu_t *v, uint32_t *base, uint32_t *end) {
    const uint8_t *root = &v->flash[FAT_ROOT_BLOCK * FAT_BLOCK];
    bool formatted = true;
    for (int i = 0; i < 16; i++) {
        if (root[i] != 0x55) { formatted = false; }
    }
    if (!formatted) {
        if (!v->bare_game_bytes) { return false; }
        *base = 0;
        *end  = (v->bare_game_bytes + FAT_BLOCK - 1) / FAT_BLOCK * FAT_BLOCK;
        return true;
    }
    unsigned dir = le16_(&root[0x4a]);
    unsigned dir_size = le16_(&root[0x4c]);
    if (!dir || dir > 255 || !dir_size) { dir = FAT_DIR_BLOCK; dir_size = FAT_DIR_SIZE; }
    for (unsigned b = 0; b < dir_size && dir >= b; b++) {
        const uint8_t *blk = &v->flash[(dir - b) * FAT_BLOCK];
        for (unsigned e = 0; e < FAT_BLOCK / 32; e++) {
            const uint8_t *ent = &blk[e * 32];
            if (ent[0] != FAT_TYPE_GAME) { continue; }
            *base = le16_(&ent[2]) * FAT_BLOCK;
            *end  = *base + le16_(&ent[24]) * FAT_BLOCK;
            return true;
        }
    }
    return false;
}

static uint32_t fm_addr_(const vmu_t *v) {
    return ((uint32_t)v->ram[1][0x7d] << 16 | (uint32_t)v->ram[1][0x7e] << 8 | v->ram[1][0x7f]) & 0x1ffff;
}

static void fm_wrt_(vmu_t *v) {
    uint32_t a = fm_addr_(v);
    uint32_t base;
    uint32_t end;
    if (!vmu_game_range(v, &base, &end) || a < base || a + FLASH_PRG_BYTES > end) {
        vmu_write(v, SFR_ACC, 0xff);
        return;
    }
    vmu_write(v, SFR_ACC, 0x00);
    for (uint32_t i = 0; i < FLASH_PRG_BYTES; i++) {
        uint8_t *dst = &v->flash[(a & ~0xffU) | ((a + i) & 0xff)];
        if (*dst != v->ram[1][0x80 + i]) { v->flash_writes++; }
        *dst = v->ram[1][0x80 + i];
    }
}

static void sleep_(vmu_t *v) {
    vmu_write(v, SFR_VCCR, (uint8_t)(sfrc_(v, SFR_VCCR) & (uint8_t)~VCCR_VCCR7));
    vmu_write(v, SFR_PCON, (uint8_t)(sfrc_(v, SFR_PCON) | PCON_HOLD));
    vmu_write(v, SFR_P3INT, (uint8_t)(sfrc_(v, SFR_P3INT) | P3INT_P32INT));
    v->sleeping = true;
}

// Returns the flash PC to resume at, or 0 to stay put (reset, sleep, unknown).
uint16_t vmu_bios_call(vmu_t *v, uint16_t pc) {
    switch (pc) {
    case 0:
        return 0;
    case BIOS_FM_WRT_EX:
        v->bios_calls[VMU_BIOS_FM_WRT_EX]++;
        fm_wrt_(v);
        return 0x105;
    case BIOS_FM_WRTA_EX:
        v->bios_calls[VMU_BIOS_FM_WRTA_EX]++;
        fm_wrt_(v);
        return 0x10b;
    case BIOS_FM_VRF_EX: {
        v->bios_calls[VMU_BIOS_FM_VRF_EX]++;
        uint32_t a = fm_addr_(v);
        uint8_t r = 0;
        for (uint32_t i = 0; i < FLASH_PRG_BYTES && r == 0; i++) {
            r = (uint8_t)(v->flash[(a & ~0xffU) | ((a + i) & 0xff)] ^ v->ram[1][0x80 + i]);
        }
        vmu_write(v, SFR_ACC, r);
        return 0x115;
    }
    case BIOS_FM_PRD_EX: {
        v->bios_calls[VMU_BIOS_FM_PRD_EX]++;
        uint32_t a = fm_addr_(v);
        for (uint32_t i = 0; i < FLASH_PRG_BYTES; i++) {
            v->ram[1][0x80 + i] = v->flash[(a & ~0xffU) | ((a + i) & 0xff)];
        }
        return 0x125;
    }
    case BIOS_TIMER_EX:
        v->bios_calls[VMU_BIOS_TIMER_EX]++;
        v->ram[0][SYS_HALF_SEC] ^= 1;
        if (!(v->ram[0][SYS_HALF_SEC] & 1)) { add_second_(v); }
        return 0x139;
    case BIOS_SLEEP_EX:
        v->bios_calls[VMU_BIOS_SLEEP_EX]++;
        sleep_(v);
        return 0;
    case BIOS_EXIT_EX:
        v->bios_calls[VMU_BIOS_EXIT_EX]++;
        vmu_reset(v);
        v->exited = true;
        return 0;
    default:
        v->bios_calls[VMU_BIOS_UNKNOWN]++;
        vmu_reset(v);
        v->exited = true;
        return 0;
    }
}
