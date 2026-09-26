// cart_mbc.h — MBC5 over cartserve.c with every bank resident: bank_sel + a DMA ring mount the
// selected bank ~200 ns after the $2000 write, with no core in the path.
#pragma once
#include <stdint.h>

// banks[0] and banks[1] must be cs_bank(0) and cs_bank(1); each is 16 KB, aligned and permuted.
// After cs_start(), before the /RST release. bank_sel ignores A12: never select via $3000.
void cs_mbc_start(uint8_t *const *banks, unsigned n);

// The MBC5 reset state, bank 1 high. Only while the console is held in /RST.
void cs_mbc_reset(void);

// The bank the high window serves now, or -1 if the base matches none.
int cs_mbc_bank(void);

// Point every table entry for `bank` at buf. Never moves the live base: the next select mounts it.
void cs_mbc_remap(unsigned bank, const uint8_t *buf);
