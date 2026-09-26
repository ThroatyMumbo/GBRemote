// Host tests for the Transfer Pak: detection, MBC decode, retry, feed/window, and a real image walk.
#include "test.h"
#include "n64_proto.h"
#include "gbcart_emu.h"
#include "tpak_proto.h"
#include "tpak_master.h"

// Transfer Pak. The pak callbacks run on core1 and may only see SRAM, so a ROM bank change is
// serviced here between transactions, exactly where run_bus() services it on core0.

typedef struct {
    tpak_t    t;
    gbcart_t  cart;
    n64_pak_t inner, pak;
    const uint8_t *rom;                 // stands in for the flash slot
    uint32_t  rom_len;
    uint32_t  refill_done;
    unsigned  refills;
    bool      auto_service;             // false: core0 never catches up, so PAK_RETRY is visible
    uint8_t   bank0[GB_ROM_WINDOW];
    uint8_t   window[GB_ROM_WINDOW];
    uint8_t   sram[GB_SRAM_MAX];
} tpak_host_t;

static void tph_service(tpak_host_t *h) {
    for (unsigned w = 0; w < 2; w++) {
        uint16_t b = gbcart_want_bank(&h->cart, w);
        if (h->cart.win_bank[w] == b) continue;
        uint32_t off = (uint32_t)b * GB_ROM_WINDOW;
        uint8_t *dst = w ? h->window : h->bank0;
        if (off + GB_ROM_WINDOW <= h->rom_len) memcpy(dst, h->rom + off, GB_ROM_WINDOW);
        else                                   memset(dst, 0xff, GB_ROM_WINDOW);
        h->cart.win_bank[w] = b;
        h->refills++;
    }
    h->refill_done = h->t.refill_req;
}

static pak_result_t tph_read(uint16_t a, uint8_t out[N64_BLOCK], void *ctx) {
    tpak_host_t *h = ctx;
    pak_result_t r = h->inner.read(a, out, &h->t);
    if (h->auto_service) tph_service(h);
    return r;
}

static pak_result_t tph_write(uint16_t a, const uint8_t in[N64_BLOCK], void *ctx) {
    tpak_host_t *h = ctx;
    pak_result_t r = h->inner.write(a, in, &h->t);
    if (h->auto_service) tph_service(h);
    return r;
}

static void tph_init(tpak_host_t *h, const uint8_t *rom, uint32_t rom_len,
                     uint8_t mbc, bool has_rtc, uint32_t ram_len) {
    memset(h, 0, sizeof *h);
    h->rom = rom;
    h->rom_len = rom_len;
    h->auto_service = true;

    gbcart_reset(&h->cart);
    h->cart.mbc       = mbc;
    h->cart.has_rtc   = has_rtc;
    h->cart.rom_banks = (uint16_t)(rom_len / GB_ROM_WINDOW);
    h->cart.ram_len   = ram_len;
    h->cart.win[0]    = h->bank0;
    h->cart.win[1]    = h->window;
    h->cart.sram      = h->sram;

    tph_service(h);                         // the session's initial stage, as core0 would
    tpak_init(&h->t, &h->cart);
    tpak_bind(&h->t, &h->inner);
    h->pak.read = tph_read; h->pak.write = tph_write; h->pak.ctx = h;
}

static uint8_t rom_byte(uint32_t off) {
    uint32_t x = off * 2654435761u;         // distinct per offset, so a banking slip is visible
    return (uint8_t)((x >> 24) ^ (off >> 14));
}

static void build_rom(uint8_t *rom, uint32_t len, uint8_t cart_type,
                      uint8_t rom_code, uint8_t ram_code, const char *title) {
    for (uint32_t i = 0; i < len; i++) rom[i] = rom_byte(i);
    memset(rom + 0x0134, 0, 0x1C);
    for (int i = 0; i < 16 && title[i]; i++) rom[0x0134 + i] = (uint8_t)title[i];
    rom[0x0143] = 0x80;
    rom[0x0147] = cart_type;
    rom[0x0148] = rom_code;
    rom[0x0149] = ram_code;
    uint8_t sum = 0;
    for (int i = 0x134; i <= 0x14C; i++) sum = (uint8_t)(sum - rom[i] - 1);
    rom[0x014D] = sum;
}

static void test_tpak_detect(void) {
    static uint8_t rom[0x8000];
    tpak_host_t h;
    tm_t m;
    uint8_t block[TM_BLOCK], st = 0;

    build_rom(rom, sizeof rom, 0x00, 0x00, 0x00, "DETECT");
    tph_init(&h, rom, sizeof rom, MBC_NONE, false, 0);
    tm_init(&m, &h.pak);

    // The whole point of the $0000 echo: the Controller Pak probe must not read its pattern back.
    CHECK(tm_detect(&m) == TM_ACC_TRANSFER, "console did not identify a Transfer Pak");

    // $8000 answers 0x84 only to a 0x84 write; every other probe value leaves us powered off.
    static const uint8_t probes[] = { 0xFE, 0x00, 0x80, 0x81, 0x85, 0x01 };
    for (unsigned i = 0; i < sizeof probes; i++) {
        memset(block, probes[i], sizeof block);
        CHECK(tm_write(&m, 0x8000, block) == TM_OK, "probe write %02x", probes[i]);
        CHECK(tm_read(&m, 0x8000, block) == TM_OK, "probe read %02x", probes[i]);
        CHECK(block[0] == 0x00, "probe %02x read back %02x, want 00", probes[i], block[0]);
    }

    CHECK(tm_tpak_init(&m) == TM_OK, "tpak_init");
    CHECK(tm_read(&m, 0x8000, block) == TM_OK && block[0] == 0x84, "powered probe");

    // A real pak with access on reads 89: RUNNING is a level, not a one-read latch.
    CHECK(tm_tpak_status(&m, &st) == TM_OK, "status");
    CHECK(st == 0x89, "status %02x, want 89", st);
    CHECK(tm_tpak_status(&m, &st) == TM_OK && st == 0x89, "RUNNING must hold; got %02x", st);

    // Access off latches WAS_RESET into byte 0 of the next $B000 read only; $3000 does not clear it.
    CHECK(tm_tpak_access(&m, false) == TM_OK, "access off");
    CHECK(tm_read(&m, 0x3000, block) == TM_OK && block[0] == 0x84 && block[31] == 0x84,
          "$3000 after access off %02x..%02x, want 84..84", block[0], block[31]);
    CHECK(tm_read(&m, 0xB000, block) == TM_OK && block[0] == 0x84 && block[1] == 0x80,
          "$B000 after access off %02x %02x, want 84 80", block[0], block[1]);
    CHECK(tm_tpak_status(&m, &st) == TM_OK && st == 0x80, "WAS_RESET did not clear; %02x", st);
    CHECK(tm_tpak_access(&m, true) == TM_OK, "access on");

    // The bank register keeps the low two bits, and a register takes the last byte of the block.
    for (unsigned b = 0; b < 256; b++) {
        memset(block, (uint8_t)b, sizeof block);
        CHECK(tm_write(&m, 0xA000, block) == TM_OK, "bank write %u", b);
        CHECK(tm_read(&m, 0xA000, block) == TM_OK, "bank read %u", b);
        CHECK(block[0] == (b & 3u), "bank %u read back %u", b, block[0]);
    }
    memset(block, 2, sizeof block); block[31] = 1;
    tm_write(&m, 0xA000, block);
    CHECK(tm_read(&m, 0xA000, block) == TM_OK && block[0] == 1, "bank took %u, want in[31]", block[0]);

    // Power does not touch access: a real pak comes back up still in access mode.
    CHECK(tm_tpak_power(&m, false) == TM_OK && tm_tpak_power(&m, true) == TM_OK, "power cycle");
    CHECK(tm_tpak_status(&m, &st) == TM_OK && st == 0x89, "after power cycle %02x, want 89", st);

    // Access mode 0 blanks the window without looking like a missing pak.
    CHECK(tm_tpak_bank(&m, 0) == TM_OK, "bank 0");
    CHECK(tm_tpak_access(&m, false) == TM_OK, "access off");
    CHECK(tm_read(&m, 0xC100, block) == TM_OK, "window read in mode 0");
    for (unsigned i = 0; i < TM_BLOCK; i++) CHECK(block[i] == 0, "mode-0 window byte %u", i);

    // Identify reports the 2-bit accessory field, and the console reads 0x01 as "pak present".
    uint8_t status = N64_ST_ACC_PRESENT, out[N64_BLOCK + 1];
    uint8_t req[1] = { N64_CMD_IDENTIFY };
    n64_state_t ns = { 0, 0, 0 };
    CHECK(n64_build_reply(N64_CMD_IDENTIFY, req, 1, &ns, &status, &h.pak, out) == 3, "identify");
    CHECK(out[0] == 0x05 && out[1] == 0x00, "identify id %02x %02x", out[0], out[1]);
    CHECK((out[2] & N64_ST_ACC_MASK) == N64_ST_ACC_PRESENT, "identify status %02x", out[2]);

    status = N64_ST_ACC_ABSENT;
    CHECK(n64_build_reply(N64_CMD_IDENTIFY, req, 1, &ns, &status, NULL, out) == 3, "identify");
    CHECK((out[2] & N64_ST_ACC_MASK) == N64_ST_ACC_ABSENT, "absent status %02x", out[2]);
    CHECK((status & N64_ST_ACC_MASK) == N64_ST_ACC_ABSENT, "absent must not decay");

    // An empty slot is a powered pak that reports no cartridge, not a missing accessory.
    tpak_host_t e;
    tph_init(&e, rom, sizeof rom, MBC_NONE, false, 0);
    e.t.cart = NULL;
    tm_init(&m, &e.pak);
    CHECK(tm_tpak_init(&m) == TM_ERR_NO_CART, "empty slot must report NO_CART");
}

static void tpak_walk_one(uint8_t cart_type, uint8_t rom_code, uint8_t ram_code,
                          uint8_t mbc, uint32_t ram_len, const char *title) {
    static uint8_t rom[512 * 1024];
    static uint8_t dump[512 * 1024];
    uint32_t rom_len = 32u * 1024u << rom_code;
    tpak_host_t h;
    tm_t m;
    tm_cart_t c;

    build_rom(rom, rom_len, cart_type, rom_code, ram_code, title);
    tph_init(&h, rom, rom_len, mbc, false, ram_len);
    tm_init(&m, &h.pak);

    CHECK(tm_detect(&m) == TM_ACC_TRANSFER, "%s: not a Transfer Pak", title);
    CHECK(tm_tpak_init(&m) == TM_OK, "%s: tpak_init", title);

    CHECK(tm_cart_header(&m, &c) == TM_OK, "%s: header read", title);
    CHECK(c.header_ok, "%s: header checksum %02x", title, c.header_cksum);
    CHECK(strncmp(c.title, title, strlen(title)) == 0, "%s: title read back '%s'", title, c.title);
    CHECK(c.mbc == mbc, "%s: mbc %u, want %u", title, c.mbc, mbc);
    CHECK(c.rom_banks == rom_len / GB_ROM_WINDOW, "%s: %u banks", title, c.rom_banks);

    memset(dump, 0, rom_len);
    CHECK(tm_cart_read_rom(&m, &c, dump, sizeof dump) == TM_OK, "%s: rom walk", title);
    uint32_t bad = 0, first = 0;
    for (uint32_t i = 0; i < rom_len; i++)
        if (dump[i] != rom[i]) { if (!bad) first = i; bad++; }
    CHECK(bad == 0, "%s: %u ROM bytes differ, first at %06x (bank %u)",
          title, bad, first, first / GB_ROM_WINDOW);

    if (!ram_len) return;

    // Write a pattern through the window, then let the master's own dump read it back.
    uint8_t pat[TM_BLOCK], back[GB_SRAM_MAX];
    uint32_t bank_len = (mbc == MBC2) ? MBC2_RAM_LEN : 0x2000u;
    uint8_t  banks    = (uint8_t)((ram_len + bank_len - 1) / bank_len);

    CHECK(tm_cart_ram_enable(&m, true) == TM_OK, "%s: ram enable", title);
    for (uint8_t b = 0; b < banks; b++) {
        CHECK(tm_cart_set_ram_bank(&m, &c, b) == TM_OK, "%s: ram bank %u", title, b);
        for (uint32_t off = 0; off < bank_len; off += TM_BLOCK) {
            for (unsigned i = 0; i < TM_BLOCK; i++)
                pat[i] = (uint8_t)(b * 17u + off + i);
            CHECK(tm_gb_write(&m, (uint16_t)(0xA000 + off), pat, TM_BLOCK) == TM_OK,
                  "%s: sram write b%u+%04x", title, b, off);
        }
    }
    CHECK(tm_cart_ram_enable(&m, false) == TM_OK, "%s: ram disable", title);

    memset(back, 0, sizeof back);
    CHECK(tm_cart_read_sram(&m, &c, back, sizeof back) == TM_OK, "%s: sram walk", title);
    bad = 0; first = 0;
    for (uint8_t b = 0; b < banks; b++)
        for (uint32_t off = 0; off < bank_len; off++) {
            uint8_t want = (uint8_t)(b * 17u + (off & ~31u) + (off & 31u));
            if (mbc == MBC2) want &= 0x0f;
            uint32_t idx = b * bank_len + off;
            if (back[idx] != want) { if (!bad) first = idx; bad++; }
        }
    CHECK(bad == 0, "%s: %u SRAM bytes differ, first at %05x", title, bad, first);
}

static void test_tpak_roundtrip(void) {
    tpak_walk_one(0x00, 0x00, 0x00, MBC_NONE, 0,            "ROMONLY");
    tpak_walk_one(0x03, 0x04, 0x03, MBC1,     32u * 1024u,  "MBC1");
    tpak_walk_one(0x06, 0x01, 0x00, MBC2,     MBC2_RAM_LEN, "MBC2");
    tpak_walk_one(0x13, 0x04, 0x03, MBC3,     32u * 1024u,  "MBC3");
    tpak_walk_one(0x1B, 0x04, 0x03, MBC5,     32u * 1024u,  "MBC5");
}

static void test_mbc(void) {
    static uint8_t rom[512 * 1024];
    uint8_t sram[GB_SRAM_MAX];
    gbcart_t c;
    bool ready;

    build_rom(rom, sizeof rom, 0x03, 0x04, 0x03, "MBCUNIT");

    // MBC1 cannot express $20/$40/$60 — each reads as the bank above. Needs a cart big enough to
    // have a bank $21 at all, or the range wrap hides the aliasing.
    gbcart_reset(&c);
    c.mbc = MBC1; c.rom_banks = 128; c.ram_len = 0; c.sram = sram;
    gbcart_write(&c, 0x6000, 0x00);
    gbcart_write(&c, 0x4000, 0x01);
    gbcart_write(&c, 0x2000, 0x00);
    CHECK(gbcart_want_bank(&c, 1) == 0x21, "MBC1 $20 aliases to %u", gbcart_want_bank(&c, 1));
    gbcart_write(&c, 0x2000, 0x05);
    CHECK(gbcart_want_bank(&c, 1) == 0x25, "MBC1 hi|lo == %u", gbcart_want_bank(&c, 1));

    // The one $4000 register is ROM high bits in mode 0 and RAM bank in mode 1.
    gbcart_reset(&c);
    c.mbc = MBC1; c.rom_banks = 128; c.ram_len = 32u * 1024u; c.sram = sram;
    memset(sram, 0, sizeof sram);
    gbcart_write(&c, 0x0000, 0x0A);
    gbcart_write(&c, 0x6000, 0x01);
    gbcart_write(&c, 0x4000, 0x02);
    gbcart_write(&c, 0xA000, 0x5A);
    CHECK(sram[2 * 0x2000] == 0x5A, "MBC1 mode-1 RAM bank 2 landed at %u", 0);
    gbcart_write(&c, 0x6000, 0x00);
    gbcart_write(&c, 0xA001, 0x3C);
    CHECK(sram[1] == 0x3C, "MBC1 mode-0 must use RAM bank 0");

    // MBC1 mode 1 moves GB $0000-$3FFF too, so the low window is staged, not fixed. tpak_master
    // never sees this — it forces mode 0 before every ROM read — but a retail game can.
    gbcart_reset(&c);
    c.mbc = MBC1; c.rom_banks = 128; c.ram_len = 0; c.sram = sram;
    CHECK(gbcart_want_bank(&c, 0) == 0, "MBC1 mode 0 low window must be bank 0");
    gbcart_write(&c, 0x4000, 0x02);
    CHECK(gbcart_want_bank(&c, 0) == 0, "mode 0 must leave the low window alone");
    gbcart_write(&c, 0x6000, 0x01);
    CHECK(gbcart_want_bank(&c, 0) == 0x40, "MBC1 mode 1 low window == %u",
          gbcart_want_bank(&c, 0));
    c.win[0] = rom; c.win_bank[0] = 0;                  // still holding bank 0
    gbcart_read(&c, 0x0100, &ready);
    CHECK(!ready, "a stale low window must report not-ready, not serve bank 0");
    c.win_bank[0] = 0x40;
    CHECK(gbcart_read(&c, 0x0100, &ready) == rom[0x0100] && ready, "low window after staging");
    gbcart_write(&c, 0x6000, 0x00);
    CHECK(gbcart_want_bank(&c, 0) == 0, "leaving mode 1 must restore bank 0");

    // MBC2: A8 high selects the bank register, A8 low the RAM enable; RAM is 512 nibbles.
    gbcart_reset(&c);
    c.mbc = MBC2; c.rom_banks = 16; c.ram_len = MBC2_RAM_LEN; c.sram = sram;
    gbcart_write(&c, 0x2100, 0x07);
    CHECK(gbcart_want_bank(&c, 1) == 7, "MBC2 bank via $2100 == %u", gbcart_want_bank(&c, 1));
    gbcart_write(&c, 0x2000, 0x03);
    CHECK(gbcart_want_bank(&c, 1) == 7, "MBC2 must ignore a bank write with A8 low");
    gbcart_write(&c, 0x0000, 0x0A);
    gbcart_write(&c, 0xA000, 0xF5);
    CHECK((gbcart_read(&c, 0xA000, &ready) & 0x0f) == 0x05, "MBC2 nibble");
    CHECK((gbcart_read(&c, 0xA000, &ready) & 0xf0) == 0xf0, "MBC2 high nibble must read 1s");
    CHECK(gbcart_read(&c, 0xA000 + MBC2_RAM_LEN, &ready) ==
          gbcart_read(&c, 0xA000, &ready), "MBC2 RAM must mirror");

    // MBC3: 7-bit bank, and the RTC latch is a 0->1 sequence on $6000.
    gbcart_reset(&c);
    c.mbc = MBC3; c.has_rtc = true; c.rom_banks = 128; c.ram_len = 32u * 1024u; c.sram = sram;
    gbcart_write(&c, 0x2000, 0xFF);
    CHECK(gbcart_want_bank(&c, 1) == 0x7F, "MBC3 bank masked to %u", gbcart_want_bank(&c, 1));
    gbcart_write(&c, 0x0000, 0x0A);
    c.rtc_live[0] = 42;
    gbcart_write(&c, 0x4000, 0x08);
    gbcart_write(&c, 0x6000, 0x00);
    gbcart_write(&c, 0x6000, 0x01);
    CHECK(gbcart_read(&c, 0xA000, &ready) == 42, "MBC3 RTC seconds after latch");
    c.rtc_live[0] = 9;
    CHECK(gbcart_read(&c, 0xA000, &ready) == 42, "MBC3 RTC must stay latched");
    gbcart_write(&c, 0x6000, 0x00);
    gbcart_write(&c, 0x6000, 0x01);
    CHECK(gbcart_read(&c, 0xA000, &ready) == 9, "MBC3 RTC re-latch");
    gbcart_write(&c, 0x4000, 0x00);
    CHECK(gbcart_read(&c, 0xA000, &ready) != 9 || sram[0] == 9, "MBC3 bank 0 must leave the RTC");

    // MBC5: 9 bits across two registers, and bank 0 is a legal selection.
    gbcart_reset(&c);
    c.mbc = MBC5; c.rom_banks = 512; c.ram_len = 0; c.sram = sram;
    gbcart_write(&c, 0x2000, 0x00);
    CHECK(gbcart_want_bank(&c, 1) == 0, "MBC5 must allow bank 0, got %u", gbcart_want_bank(&c, 1));
    gbcart_write(&c, 0x2000, 0x34);
    gbcart_write(&c, 0x3000, 0x01);
    CHECK(gbcart_want_bank(&c, 1) == 0x134, "MBC5 9-bit bank == %u", gbcart_want_bank(&c, 1));

    // The block forms the Transfer Pak calls must agree byte for byte with the per-byte path,
    // MBC2 nibble and stale-window veto included.
    gbcart_reset(&c);
    c.mbc = MBC3; c.rom_banks = 128; c.ram_len = 0x2000; c.sram = sram;
    c.win[0] = rom; c.win_bank[0] = 0;
    c.win[1] = rom + GB_ROM_WINDOW; c.win_bank[1] = 1;
    memset(sram, 0, sizeof sram);
    gbcart_write(&c, 0x0000, 0x0A);

    uint8_t blk[32], one[32];
    for (uint16_t base = 0x0000; base < 0xC000; base = (uint16_t)(base + 0x20)) {
        if (base >= 0x8000 && base < 0xA000) continue;
        bool ok = gbcart_read_block(&c, base, blk, 32);
        bool all = true;
        for (unsigned i = 0; i < 32; i++) {
            one[i] = gbcart_read(&c, (uint16_t)(base + i), &ready);
            if (!ready) all = false;
        }
        CHECK(ok == all, "block/byte readiness disagree at %04x", base);
        if (ok && all) CHECK(memcmp(blk, one, 32) == 0, "block/byte differ at %04x", base);
    }

    for (unsigned i = 0; i < 32; i++) blk[i] = (uint8_t)(0xA0 + i);
    gbcart_write_block(&c, 0xA040, blk, 32);
    CHECK(memcmp(sram + 0x40, blk, 32) == 0, "block write into SRAM");
    CHECK(c.save_dirty != 0, "an SRAM write must mark the save dirty");

    uint32_t was_dirty = c.save_dirty;
    gbcart_write_block(&c, 0x2000, blk, 32);            // an MBC register, not storage
    CHECK(gbcart_want_bank(&c, 1) == (uint16_t)((0xA0 + 31) & 0x7f), "block MBC write, last wins");
    CHECK(c.save_dirty == was_dirty, "an MBC register write must not dirty the save");

    c.win_bank[1] = 0xffff;                             // core0 has not staged the new bank
    CHECK(!gbcart_read_block(&c, 0x4000, blk, 32), "a stale window must veto the block read");

    // Cart RAM answers $FF until $0000 has been written $0A.
    gbcart_reset(&c);
    c.mbc = MBC3; c.rom_banks = 4; c.ram_len = 0x2000; c.sram = sram;
    memset(sram, 0x11, sizeof sram);
    CHECK(gbcart_read(&c, 0xA000, &ready) == 0xff, "disabled RAM must read ff");
    gbcart_write(&c, 0x0000, 0x0A);
    CHECK(gbcart_read(&c, 0xA000, &ready) == 0x11, "enabled RAM");
    gbcart_write(&c, 0x0000, 0x00);
    CHECK(gbcart_read(&c, 0xA000, &ready) == 0xff, "re-disabled RAM");

    // A real MBC3 (Crystal, 32 KB) reads $FF for RAM banks 4-7 rather than keeping the old bank.
    gbcart_reset(&c);
    c.mbc = MBC3; c.rom_banks = 128; c.ram_len = 32u * 1024u; c.sram = sram;
    memset(sram, 0x33, sizeof sram);
    gbcart_write(&c, 0x0000, 0x0A);
    gbcart_write(&c, 0x4000, 0x03);
    CHECK(gbcart_read(&c, 0xA000, &ready) == 0x33, "MBC3 RAM bank 3");
    for (uint8_t b = 4; b <= 7; b++) {
        gbcart_write(&c, 0x4000, b);
        CHECK(gbcart_read(&c, 0xA000, &ready) == 0xff, "MBC3 RAM bank %u must read ff", b);
        gbcart_write(&c, 0xA000, 0x44);
    }
    for (unsigned i = 0; i < sizeof sram; i++) CHECK(sram[i] == 0x33, "bank 4-7 write landed at %u", i);

    // /RST resets the registers and nothing else: the staged window stays valid.
    c.win[1] = rom + GB_ROM_WINDOW; c.win_bank[1] = 5;
    gbcart_write(&c, 0x2000, 0x05);
    gbcart_mbc_reset(&c);
    CHECK(gbcart_want_bank(&c, 1) == 1 && !c.ram_enabled, "mbc reset left bank %u", gbcart_want_bank(&c, 1));
    CHECK(c.win_bank[1] == 5, "mbc reset must not touch the staged window");
}

static void test_tpak_retry(void) {
    static uint8_t rom[128 * 1024];
    tpak_host_t h;
    tm_t m;
    uint8_t block[TM_BLOCK], out[N64_BLOCK + 1], req[3];
    uint8_t status = N64_ST_ACC_PRESENT;
    n64_state_t ns = { 0, 0, 0 };

    build_rom(rom, sizeof rom, 0x13, 0x03, 0x00, "RETRY");
    tph_init(&h, rom, sizeof rom, MBC3, false, 0);
    tm_init(&m, &h.pak);
    CHECK(tm_tpak_init(&m) == TM_OK, "tpak_init");

    // Stall core0, then move the bank. The window now holds the wrong 16 KB.
    h.auto_service = false;
    CHECK(tm_tpak_bank(&m, 0) == TM_OK, "bank 0");
    CHECK(tm_gb_write_reg(&m, 0x2000, 4) == TM_OK, "select ROM bank 4");
    CHECK(h.cart.win_bank[1] != gbcart_want_bank(&h.cart, 1), "the window must now be stale");

    CHECK(tm_tpak_bank(&m, 1) == TM_OK, "bank 1");
    uint16_t a = n64_addr_crc(0xC000);
    req[0] = N64_CMD_ACC_READ; req[1] = (uint8_t)(a >> 8); req[2] = (uint8_t)a;
    CHECK(n64_build_reply(N64_CMD_ACC_READ, req, 3, &ns, &status, &h.pak, out) == N64_BLOCK + 1,
          "stalled read length");
    uint8_t good = n64_data_crc(out);
    CHECK(out[N64_BLOCK] != good, "a stalled read must not answer a valid CRC");
    CHECK(out[N64_BLOCK] != (uint8_t)~good,
          "a stalled read must not answer the inverted CRC — that reads as pak removed");

    // The master retries; a retry that outlives the stall gets the right bytes.
    unsigned before = m.retries;
    CHECK(tm_read(&m, 0xC000, block) == TM_ERR_CRC, "a permanently stalled read must fail");
    CHECK(m.retries > before, "the master must have retried");

    h.auto_service = true;
    tph_service(&h);
    CHECK(tm_read(&m, 0xC000, block) == TM_OK, "read after the refill lands");
    CHECK(memcmp(block, rom + 4u * GB_ROM_WINDOW, TM_BLOCK) == 0, "wrong bank served");

    // Access off holds the cart in /RST: on a real pak bank 4 comes back as bank 1, refilled.
    unsigned refills = h.t.refill_req;
    CHECK(tm_tpak_access(&m, false) == TM_OK && tm_tpak_access(&m, true) == TM_OK, "access cycle");
    CHECK(h.t.refill_req == refills + 1, "the /RST bank move must request a refill");
    CHECK(tm_tpak_bank(&m, 1) == TM_OK && tm_read(&m, 0xC000, block) == TM_OK, "read after /RST");
    CHECK(memcmp(block, rom + 1u * GB_ROM_WINDOW, TM_BLOCK) == 0, "access cycle kept the MBC bank");

    // PAK_ABSENT still answers the inverted CRC the no-pak tests already assert.
    CHECK(n64_build_reply(N64_CMD_ACC_READ, req, 3, &ns, &status, NULL, out) == N64_BLOCK + 1,
          "no-pak read length");
    CHECK(out[N64_BLOCK] == 0xff, "no-pak CRC %02x, want ff", out[N64_BLOCK]);
}

typedef struct { unsigned n; uint16_t off; uint8_t blk[N64_BLOCK]; } feed_rec_t;

static void feed_rec(void *ctx, uint16_t off, const uint8_t blk[N64_BLOCK]) {
    feed_rec_t *r = ctx;
    r->n++;
    r->off = off;
    memcpy(r->blk, blk, N64_BLOCK);
}

static uint8_t g_win[0x2000];

static bool win_rd(void *ctx, uint16_t off, uint8_t out[N64_BLOCK]) {
    (void)ctx;
    if ((uint32_t)off + N64_BLOCK > sizeof g_win) return false;
    memcpy(out, g_win + off, N64_BLOCK);
    return true;
}

// Stadium's battle feed: GB $8000-$9FFF through the pak, which no cart decodes.
static void test_tpak_feed(void) {
    static uint8_t rom[128 * 1024];
    tpak_host_t h;
    tm_t m;
    feed_rec_t r = { 0 };
    uint8_t block[TM_BLOCK], pat[TM_BLOCK];

    build_rom(rom, sizeof rom, 0x13, 0x03, 0x03, "FEED");
    tph_init(&h, rom, sizeof rom, MBC3, false, 32u * 1024u);
    tpak_set_feed(&h.t, feed_rec, &r);
    tm_init(&m, &h.pak);
    CHECK(tm_tpak_init(&m) == TM_OK, "tpak_init");

    for (unsigned i = 0; i < TM_BLOCK; i++) pat[i] = (uint8_t)(0xA0 + i);
    uint32_t dirty = h.cart.save_dirty;
    CHECK(tm_gb_write(&m, 0x8000 + 5 * 32, pat, TM_BLOCK) == TM_OK, "feed write");
    CHECK(r.n == 1 && r.off == 5 * 32, "feed saw %u writes, off %04x", r.n, r.off);
    CHECK(memcmp(r.blk, pat, TM_BLOCK) == 0, "feed block differs");
    CHECK(tm_gb_write(&m, 0x9FE0, pat, TM_BLOCK) == TM_OK && r.n == 2 && r.off == 0x1FE0,
          "header write off %04x", r.off);
    CHECK(h.cart.save_dirty == dirty, "a feed write must never dirty the save");

    // Stadium powers the pak off before a battle; a bank write sent then is dropped.
    CHECK(tm_tpak_bank(&m, 1) == TM_OK, "bank 1");
    CHECK(tm_tpak_power(&m, false) == TM_OK, "power off");
    CHECK(tm_write(&m, 0xC040, pat) == TM_OK && r.n == 3 && r.off == 0x40,
          "unpowered window write must feed; n %u off %04x", r.n, r.off);

    // Powered, bank 1: pak $C000 is GB $4000, MBC3's RAM bank register, never the feed.
    CHECK(tm_tpak_power(&m, true) == TM_OK && tm_tpak_bank(&m, 1) == TM_OK, "power on, bank 1");
    memset(block, 0, sizeof block); block[31] = 3;
    CHECK(tm_write(&m, 0xC000, block) == TM_OK, "RAM bank select");
    CHECK(r.n == 3, "an MBC write reached the feed");
    CHECK(gbcart_ram_bank(&h.cart) == 3, "the MBC did not take RAM bank 3");

    // Reads of the feed range are the cart's, and a missing hook is a real pak again.
    CHECK(tm_tpak_bank(&m, 2) == TM_OK && tm_read(&m, 0xC000, block) == TM_OK, "feed-range read");
    CHECK(block[0] == 0xff && block[31] == 0xff, "VRAM read %02x, want ff", block[0]);
    tpak_set_feed(&h.t, NULL, NULL);
    CHECK(tm_write(&m, 0xC000, pat) == TM_OK && r.n == 3, "no hook, yet the write was fed");

    // The read half, tpak_set_window: the same window, answered instead of only listened to.
    for (unsigned i = 0; i < TM_BLOCK; i++) g_win[0x1F00 + i] = (uint8_t)(i ^ 0x5a);
    tpak_set_window(&h.t, win_rd, NULL);
    CHECK(tm_tpak_bank(&m, 2) == TM_OK && tm_read(&m, 0xDF00, block) == TM_OK, "window read");
    CHECK(memcmp(block, g_win + 0x1F00, TM_BLOCK) == 0, "the window hook did not answer");
    CHECK(tm_tpak_bank(&m, 0) == TM_OK && tm_read(&m, 0xC100, block) == TM_OK, "bank 0 read");
    CHECK(memcmp(block, rom + 0x100, TM_BLOCK) == 0, "the window leaked into bank 0");
    tpak_set_window(&h.t, NULL, NULL);
    CHECK(tm_tpak_bank(&m, 2) == TM_OK && tm_read(&m, 0xDF00, block) == TM_OK, "hook removed");
    CHECK(block[0] == 0xff, "no window hook, yet the read answered %02x", block[0]);

    // The backup case: no cartridge staged at all, which is the only state the channel runs in.
    tpak_host_t e;
    tph_init(&e, rom, sizeof rom, MBC3, false, 0);
    e.t.cart = NULL;
    r.n = 0;
    tpak_set_feed(&e.t, feed_rec, &r);
    tpak_set_window(&e.t, win_rd, NULL);
    tm_init(&m, &e.pak);
    CHECK(tm_tpak_init(&m) == TM_ERR_NO_CART, "an empty slot must still report NO_CART");
    CHECK(tm_gb_write(&m, 0x9F00, pat, TM_BLOCK) == TM_OK && r.n == 1 && r.off == 0x1F00,
          "no cart: window write n %u off %04x", r.n, r.off);
    memcpy(g_win + 0x1F00, pat, TM_BLOCK);
    CHECK(tm_gb_read(&m, 0x9F00, block, TM_BLOCK) == TM_OK, "no cart: window read");
    CHECK(memcmp(block, pat, TM_BLOCK) == 0, "no cart: the window did not round trip");
}

static void test_tpak_real_image(void) {
    static uint8_t rom[0x40000], dump[0x40000];     // cart_mbc.c's ceiling: 16 resident banks
    FILE *f = fopen("../../rom/controller.gb", "rb");
    if (!f) { printf("skip: rom/controller.gb not built\n"); return; }
    size_t n = fread(rom, 1, sizeof rom, f);
    fclose(f);
    CHECK(n >= 0x8000 && n % 0x4000 == 0, "controller.gb is %zu bytes", n);
    if (n < 0x8000 || n % 0x4000) return;

    tpak_host_t h;
    tm_t m;
    tm_cart_t c;
    tph_init(&h, rom, n, MBC5, false, 0);
    tm_init(&m, &h.pak);

    CHECK(tm_detect(&m) == TM_ACC_TRANSFER, "real image: not a Transfer Pak");
    CHECK(tm_tpak_init(&m) == TM_OK, "real image: tpak_init");
    CHECK(tm_cart_header(&m, &c) == TM_OK, "real image: header");
    CHECK(c.header_ok, "real image: header checksum");
    CHECK(strncmp(c.title, "GBCONTROL", 9) == 0, "real image: title '%s'", c.title);
    CHECK(c.mbc == MBC5 && c.rom_banks == n / 0x4000, "real image: %u banks, mbc %u",
          c.rom_banks, c.mbc);
    CHECK(tm_cart_read_rom(&m, &c, dump, n) == TM_OK, "real image: rom walk");
    CHECK(memcmp(dump, rom, n) == 0, "real image: dump differs from the file");
}

void tests_tpak(void) {
    test_tpak_detect();
    test_tpak_roundtrip();
    test_mbc();
    test_tpak_retry();
    test_tpak_feed();
    test_tpak_real_image();
}
