#include "mbox.h"

uint8_t mbox_cfg(uint8_t idx)
{
    return *(const volatile uint8_t *)(CFG_MIRROR_BASE + (uint16_t)idx);
}

void mbox_set_cfg(uint8_t idx, uint8_t val)
{
    MBOX_CFG_IDX = idx;
    MBOX_CFG_VAL = val;
}

void mbox_commit(void)
{
    MBOX_COMMIT = 1;
}
