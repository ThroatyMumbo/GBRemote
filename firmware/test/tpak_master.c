#include "tpak_master.h"
#include <string.h>

#define ADDR_LABEL   0x0000
#define ADDR_PROBE   0x8000
#define ADDR_BANK    0xA000
#define ADDR_STATUS  0xB000
#define ADDR_DATA    0xC000
#define BANK_SIZE    0x4000

#define PROBE_ABSENT   0x00
#define PROBE_RUMBLE   0x80
#define PROBE_BIO      0x81
#define PROBE_TPAK_ON  0x84
#define PROBE_SNAP     0x85
#define PROBE_TPAK_OFF 0xFE

#define ST_ACCESS  0x01
#define ST_BOOTING 0x04
#define ST_NO_CART 0x40

void tm_init(tm_t *m, const n64_pak_t *pak) {
    memset(m, 0, sizeof *m);
    m->pak    = pak;
    m->status = N64_ST_ACC_PRESENT;
}

// libdragon joybus_accessory.c: OK when the CRCs match, NO_PAK when the reply's is the bitwise
// inverse, BAD_CRC otherwise. Only the third case is retried.
static tm_result_t classify(uint8_t expect, uint8_t got) {
    if (got == expect)                    return TM_OK;
    if (got == (uint8_t)(expect ^ 0xFF))  return TM_ERR_NO_PAK;
    return TM_ERR_CRC;
}

static tm_result_t read_once(tm_t *m, uint16_t addr, uint8_t buf[TM_BLOCK]) {
    uint8_t req[3], out[N64_BLOCK + 1];
    uint16_t a = n64_addr_crc(addr);
    n64_state_t st = { 0, 0, 0 };

    req[0] = N64_CMD_ACC_READ;
    req[1] = (uint8_t)(a >> 8);
    req[2] = (uint8_t)a;
    if (n64_build_reply(N64_CMD_ACC_READ, req, 3, &st, &m->status, m->pak, out) != N64_BLOCK + 1)
        return TM_ERR_NO_PAK;

    tm_result_t r = classify(n64_data_crc(out), out[N64_BLOCK]);
    if (r == TM_OK) memcpy(buf, out, TM_BLOCK);
    return r;
}

tm_result_t tm_read(tm_t *m, uint16_t addr, uint8_t buf[TM_BLOCK]) {
    tm_result_t r = TM_ERR_CRC;
    for (unsigned i = 0; i <= TM_RETRY_LIMIT; i++) {
        r = read_once(m, addr, buf);
        if (r != TM_ERR_CRC) return r;
        m->retries++;
    }
    return r;
}

static tm_result_t write_once(tm_t *m, uint16_t addr, const uint8_t buf[TM_BLOCK]) {
    uint8_t req[35], out[N64_BLOCK + 1];
    uint16_t a = n64_addr_crc(addr);
    n64_state_t st = { 0, 0, 0 };

    req[0] = N64_CMD_ACC_WRITE;
    req[1] = (uint8_t)(a >> 8);
    req[2] = (uint8_t)a;
    memcpy(&req[3], buf, TM_BLOCK);
    if (n64_build_reply(N64_CMD_ACC_WRITE, req, 35, &st, &m->status, m->pak, out) != 1)
        return TM_ERR_NO_PAK;

    return classify(n64_data_crc(buf), out[0]);
}

tm_result_t tm_write(tm_t *m, uint16_t addr, const uint8_t buf[TM_BLOCK]) {
    tm_result_t r = TM_ERR_CRC;
    for (unsigned i = 0; i <= TM_RETRY_LIMIT; i++) {
        r = write_once(m, addr, buf);
        if (r != TM_ERR_CRC) return r;
        m->retries++;
    }
    return r;
}

static tm_result_t set_value(tm_t *m, uint16_t addr, uint8_t v) {
    uint8_t block[TM_BLOCK];
    memset(block, v, sizeof block);
    return tm_write(m, addr, block);
}

static uint8_t probe(tm_t *m, uint8_t write_val) {
    uint8_t block[TM_BLOCK];
    if (set_value(m, ADDR_PROBE, write_val) != TM_OK) return PROBE_ABSENT;
    if (tm_read(m, ADDR_PROBE, block) != TM_OK)       return PROBE_ABSENT;
    return block[0];
}

// libdragon's joypad_accessory_detect_async() and its callbacks, in order. The Controller Pak step
// comes first and is the one an echoing $0000 would fail.
tm_accessory_t tm_detect(tm_t *m) {
    uint8_t backup[TM_BLOCK], pattern[TM_BLOCK], back[TM_BLOCK];

    if (set_value(m, ADDR_PROBE, PROBE_TPAK_OFF) != TM_OK) return TM_ACC_NONE;
    if (set_value(m, ADDR_PROBE, 0x00) != TM_OK)           return TM_ACC_NONE;

    if (tm_read(m, ADDR_LABEL, backup) == TM_OK) {
        for (unsigned i = 0; i < TM_BLOCK; i++) pattern[i] = (uint8_t)i;
        if (tm_write(m, ADDR_LABEL, pattern) == TM_OK &&
            tm_read (m, ADDR_LABEL, back)    == TM_OK &&
            memcmp(back, pattern, TM_BLOCK) == 0) {
            tm_write(m, ADDR_LABEL, backup);
            return TM_ACC_CPAK;
        }
    }

    uint8_t v = probe(m, PROBE_RUMBLE);
    if (v == PROBE_RUMBLE) return TM_ACC_RUMBLE;
    if (v == PROBE_BIO)    return TM_ACC_BIO;

    if (probe(m, PROBE_TPAK_ON) == PROBE_TPAK_ON) {
        set_value(m, ADDR_PROBE, PROBE_TPAK_OFF);
        return TM_ACC_TRANSFER;
    }

    if (probe(m, PROBE_SNAP) == PROBE_SNAP) return TM_ACC_SNAP;
    return TM_ACC_NONE;
}

tm_result_t tm_tpak_power (tm_t *m, bool on) { return set_value(m, ADDR_PROBE, on ? PROBE_TPAK_ON : PROBE_TPAK_OFF); }
tm_result_t tm_tpak_access(tm_t *m, bool on) { return set_value(m, ADDR_STATUS, on ? 1 : 0); }
tm_result_t tm_tpak_bank  (tm_t *m, uint8_t b) { return set_value(m, ADDR_BANK, b); }

tm_result_t tm_tpak_status(tm_t *m, uint8_t *out) {
    uint8_t block[TM_BLOCK];
    tm_result_t r = tm_read(m, ADDR_STATUS, block);
    if (r == TM_OK && out) *out = block[0];
    return r;
}

tm_result_t tm_tpak_init(tm_t *m) {
    uint8_t block[TM_BLOCK], status = 0;
    tm_result_t r;

    if ((r = tm_tpak_power(m, true)) != TM_OK) return r;
    if ((r = tm_read(m, ADDR_PROBE, block)) != TM_OK) return r;
    if (block[0] != PROBE_TPAK_ON) return TM_ERR_NO_PAK;

    if ((r = tm_tpak_access(m, true)) != TM_OK) return r;

    // Poll for BOOTING to clear, though libdragon does not wait at all: the device has to satisfy
    // the stricter of the two.
    for (int i = 0; i < 50; i++) {
        if ((r = tm_tpak_status(m, &status)) != TM_OK) return r;
        if (!(status & ST_BOOTING)) break;
    }
    if (status & ST_BOOTING)  return TM_ERR_NOT_READY;
    if (status & ST_NO_CART)  return TM_ERR_NO_CART;
    if (!(status & ST_ACCESS)) return TM_ERR_NOT_READY;
    return TM_OK;
}

tm_result_t tm_gb_read(tm_t *m, uint16_t gbaddr, uint8_t *buf, uint32_t len) {
    if ((gbaddr | len) & (TM_BLOCK - 1)) return TM_ERR_ALIGN;

    uint32_t addr = gbaddr, end = addr + len;
    int bank = (int)(addr / BANK_SIZE);
    uint16_t pak = (uint16_t)(ADDR_DATA + (addr % BANK_SIZE));

    tm_result_t r = tm_tpak_bank(m, (uint8_t)bank);
    if (r != TM_OK) return r;

    while (addr < end) {
        if ((int)(addr / BANK_SIZE) > bank) {
            bank = (int)(addr / BANK_SIZE);
            if ((r = tm_tpak_bank(m, (uint8_t)bank)) != TM_OK) return r;
            pak = ADDR_DATA;
        }
        if ((r = tm_read(m, pak, buf)) != TM_OK) return r;
        addr += TM_BLOCK; buf += TM_BLOCK; pak += TM_BLOCK;
    }
    return TM_OK;
}

tm_result_t tm_gb_write(tm_t *m, uint16_t gbaddr, const uint8_t *buf, uint32_t len) {
    if ((gbaddr | len) & (TM_BLOCK - 1)) return TM_ERR_ALIGN;

    uint32_t addr = gbaddr, end = addr + len;
    int bank = (int)(addr / BANK_SIZE);
    uint16_t pak = (uint16_t)(ADDR_DATA + (addr % BANK_SIZE));

    tm_result_t r = tm_tpak_bank(m, (uint8_t)bank);
    if (r != TM_OK) return r;

    while (addr < end) {
        if ((int)(addr / BANK_SIZE) > bank) {
            bank = (int)(addr / BANK_SIZE);
            if ((r = tm_tpak_bank(m, (uint8_t)bank)) != TM_OK) return r;
            pak = ADDR_DATA;
        }
        if ((r = tm_write(m, pak, buf)) != TM_OK) return r;
        addr += TM_BLOCK; buf += TM_BLOCK; pak += TM_BLOCK;
    }
    return TM_OK;
}

// MBC registers mirror across their whole region, so 32 copies == one write.
tm_result_t tm_gb_write_reg(tm_t *m, uint16_t gbaddr, uint8_t value) {
    uint8_t block[TM_BLOCK];
    memset(block, value, sizeof block);
    return tm_gb_write(m, gbaddr, block, TM_BLOCK);
}

// Same table as tools/mkslot.py's mbc_from_cart_type.
static uint8_t tm_mbc_from_cart_type(uint8_t t) {
    if (t == 0x00 || t == 0x08 || t == 0x09) return TM_MBC_NONE;
    if (t <= 0x03) return TM_MBC1;
    if (t <= 0x06) return TM_MBC2;
    if (t >= 0x0F && t <= 0x13) return TM_MBC3;
    if (t >= 0x19 && t <= 0x1E) return TM_MBC5;
    return TM_MBC5;
}

static uint32_t rom_bytes_from_code(uint8_t c) {
    if (c <= 0x08) return (32u * 1024u) << c;
    switch (c) {
    case 0x52: return 72u * BANK_SIZE;
    case 0x53: return 80u * BANK_SIZE;
    case 0x54: return 96u * BANK_SIZE;
    default:   return 32u * 1024u;
    }
}

static uint32_t ram_bytes_from_code(uint8_t code, uint8_t mbc) {
    if (mbc == TM_MBC2) return TM_MBC2_RAM_LEN;
    switch (code) {
    case 0x01: return 2u * 1024u;
    case 0x02: return 8u * 1024u;
    case 0x03: return 32u * 1024u;
    case 0x04: return 128u * 1024u;
    case 0x05: return 64u * 1024u;
    default:   return 0;
    }
}

tm_result_t tm_cart_header(tm_t *m, tm_cart_t *c) {
    uint8_t h[0x60];
    tm_result_t r = tm_gb_read(m, 0x0100, h, sizeof h);
    if (r != TM_OK) return r;

    memset(c, 0, sizeof *c);
    for (int i = 0; i < 16; i++) {
        uint8_t ch = h[0x34 + i];
        c->title[i] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : ' ';
    }
    c->title[16] = 0;

    c->cgb_flag      = h[0x43];
    c->cart_type     = h[0x47];
    c->rom_size_code = h[0x48];
    c->ram_size_code = h[0x49];
    c->header_cksum  = h[0x4D];

    c->mbc       = tm_mbc_from_cart_type(c->cart_type);
    c->has_rtc   = (c->cart_type == 0x0F || c->cart_type == 0x10);
    c->rom_bytes = rom_bytes_from_code(c->rom_size_code);
    c->rom_banks = (uint16_t)(c->rom_bytes / BANK_SIZE);
    c->ram_bytes = ram_bytes_from_code(c->ram_size_code, c->mbc);
    c->ram_banks = c->ram_bytes ? (uint8_t)((c->ram_bytes + 0x2000u - 1) / 0x2000u) : 0;

    uint8_t sum = 0;
    for (int i = 0x34; i <= 0x4C; i++) sum = (uint8_t)(sum - h[i] - 1);
    c->header_ok = (sum == c->header_cksum);
    return TM_OK;
}

tm_result_t tm_cart_ram_enable(tm_t *m, bool on) {
    return tm_gb_write_reg(m, 0x0000, on ? 0x0A : 0x00);
}

// Including the MBC1 mode reset and the MBC2 A8 rule.
static tm_result_t tm_cart_set_rom_bank(tm_t *m, const tm_cart_t *c, uint16_t bank) {
    tm_result_t r;
    switch (c->mbc) {
    case TM_MBC_NONE:
        return TM_OK;
    case TM_MBC2:
        return tm_gb_write_reg(m, 0x2100, bank & 0x0F);
    case TM_MBC1:
        if ((r = tm_gb_write_reg(m, 0x6000, 0x00)) != TM_OK) return r;
        if ((r = tm_gb_write_reg(m, 0x4000, (bank >> 5) & 0x03)) != TM_OK) return r;
        return tm_gb_write_reg(m, 0x2000, bank & 0x1F);
    case TM_MBC3:
        return tm_gb_write_reg(m, 0x2000, bank & 0x7F);
    case TM_MBC5:
        if ((r = tm_gb_write_reg(m, 0x2000, bank & 0xFF)) != TM_OK) return r;
        return tm_gb_write_reg(m, 0x3000, (bank >> 8) & 0x01);
    }
    return TM_OK;
}

tm_result_t tm_cart_set_ram_bank(tm_t *m, const tm_cart_t *c, uint8_t bank) {
    tm_result_t r;
    switch (c->mbc) {
    case TM_MBC_NONE:
    case TM_MBC2:
        return TM_OK;
    case TM_MBC1:
        if ((r = tm_gb_write_reg(m, 0x6000, 0x01)) != TM_OK) return r;
        return tm_gb_write_reg(m, 0x4000, bank & 0x03);
    default:
        return tm_gb_write_reg(m, 0x4000, bank);
    }
}

tm_result_t tm_cart_read_rom(tm_t *m, const tm_cart_t *c, uint8_t *out, uint32_t cap) {
    tm_result_t r;
    for (uint16_t bank = 0; bank < c->rom_banks; bank++) {
        uint32_t off = (uint32_t)bank * BANK_SIZE;
        if (off + BANK_SIZE > cap) return TM_ERR_ALIGN;
        uint16_t win = bank ? BANK_SIZE : 0x0000;
        if (bank && (r = tm_cart_set_rom_bank(m, c, bank)) != TM_OK) return r;
        if ((r = tm_gb_read(m, win, out + off, BANK_SIZE)) != TM_OK) return r;
    }
    return TM_OK;
}

tm_result_t tm_cart_read_sram(tm_t *m, const tm_cart_t *c, uint8_t *out, uint32_t cap) {
    tm_result_t r;
    uint32_t bank_len = (c->mbc == TM_MBC2) ? TM_MBC2_RAM_LEN : 0x2000u;
    uint32_t remaining = c->ram_bytes;

    if (!c->ram_banks || !remaining) return TM_OK;
    if (remaining > cap) return TM_ERR_ALIGN;

    if ((r = tm_cart_ram_enable(m, true)) != TM_OK) return r;

    for (uint8_t b = 0; b < c->ram_banks && remaining; b++) {
        if ((r = tm_cart_set_ram_bank(m, c, b)) != TM_OK) goto done;
        uint32_t take = remaining < bank_len ? remaining : bank_len;
        if ((r = tm_gb_read(m, 0xA000, out + (uint32_t)b * bank_len, take)) != TM_OK) goto done;
        if (c->mbc == TM_MBC2)
            for (uint32_t i = 0; i < take; i++) out[(uint32_t)b * bank_len + i] &= 0x0F;
        remaining -= take;
    }
done:
    tm_cart_ram_enable(m, false);
    return r;
}
