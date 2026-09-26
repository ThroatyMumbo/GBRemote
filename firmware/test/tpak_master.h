// tpak_master.h — a Transfer Pak master driving n64_build_reply(), transcribed from libdragon's
// joypad_accessory.c and tpak.c with its own MBC walk, so it cannot share gbcart_emu.c's mistakes.
#ifndef TPAK_MASTER_H
#define TPAK_MASTER_H

#include "n64_proto.h"

// Its own copy on purpose, so a wrong device constant cannot agree with itself.
enum { TM_MBC_NONE, TM_MBC1, TM_MBC2, TM_MBC3, TM_MBC5 };
#define TM_MBC2_RAM_LEN 0x0200u

#define TM_BLOCK 32
#define TM_RETRY_LIMIT 2        // libdragon's JOYPAD_ACCESSORY_RETRY_LIMIT

typedef enum {
    TM_OK = 0,
    TM_ERR_NO_PAK,              // inverted data CRC
    TM_ERR_CRC,                 // wrong data CRC, every retry exhausted
    TM_ERR_NO_CART,
    TM_ERR_NOT_READY,
    TM_ERR_ALIGN,
} tm_result_t;

typedef struct {
    const n64_pak_t *pak;
    uint8_t  status;            // the identify status byte, threaded through as in/out
    unsigned retries;           // cumulative CRC retries, so a test can assert backpressure ran
} tm_t;

void tm_init(tm_t *m, const n64_pak_t *pak);

// One 0x02 / 0x03 accessory transaction, built and checked exactly as a console does.
tm_result_t tm_read (tm_t *m, uint16_t addr, uint8_t buf[TM_BLOCK]);
tm_result_t tm_write(tm_t *m, uint16_t addr, const uint8_t buf[TM_BLOCK]);

// libdragon's accessory probe. Returns what the console would conclude.
typedef enum { TM_ACC_NONE, TM_ACC_CPAK, TM_ACC_RUMBLE, TM_ACC_BIO,
               TM_ACC_TRANSFER, TM_ACC_SNAP } tm_accessory_t;
tm_accessory_t tm_detect(tm_t *m);

// libdragon tpak.c.
tm_result_t tm_tpak_power (tm_t *m, bool on);
tm_result_t tm_tpak_access(tm_t *m, bool on);
tm_result_t tm_tpak_bank  (tm_t *m, uint8_t bank);
tm_result_t tm_tpak_status(tm_t *m, uint8_t *out);
tm_result_t tm_tpak_init  (tm_t *m);

// Game Boy address space, both ends 32-byte aligned.
tm_result_t tm_gb_read (tm_t *m, uint16_t gbaddr, uint8_t *buf, uint32_t len);
tm_result_t tm_gb_write(tm_t *m, uint16_t gbaddr, const uint8_t *buf, uint32_t len);
tm_result_t tm_gb_write_reg(tm_t *m, uint16_t gbaddr, uint8_t value);

// The cartridge walk, which is what proves the MBC decode.
typedef struct {
    char     title[17];
    uint8_t  cart_type, cgb_flag, rom_size_code, ram_size_code, header_cksum;
    uint8_t  mbc;
    uint32_t rom_bytes, ram_bytes;
    uint16_t rom_banks;
    uint8_t  ram_banks;
    bool     has_rtc, header_ok;
} tm_cart_t;

tm_result_t tm_cart_header(tm_t *m, tm_cart_t *c);
tm_result_t tm_cart_set_ram_bank(tm_t *m, const tm_cart_t *c, uint8_t bank);
tm_result_t tm_cart_ram_enable(tm_t *m, bool on);
tm_result_t tm_cart_read_rom(tm_t *m, const tm_cart_t *c, uint8_t *out, uint32_t cap);
tm_result_t tm_cart_read_sram(tm_t *m, const tm_cart_t *c, uint8_t *out, uint32_t cap);

#endif
