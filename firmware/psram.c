// psram.c — APS6404 bring-up on QMI CS1, ported from micropython's ports/rp2/rp2_psram.c.
// Runs from SRAM: flash XIP is unavailable while QMI direct mode is on.

#include "psram.h"
#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "hardware/structs/qmi.h"
#include "hardware/structs/xip_ctrl.h"
#include "hardware/regs/qmi.h"

size_t __no_inline_not_in_flash_func(psram_init)(uint cs_pin) {
    gpio_set_function(cs_pin, GPIO_FUNC_XIP_CS1);
    uint32_t intr_stash = save_and_disable_interrupts();

    // Detect: drop any prior QPI mode, then read the JEDEC ID (KGD + EID) in single-SPI.
    qmi_hw->direct_csr = 30 << QMI_DIRECT_CSR_CLKDIV_LSB | QMI_DIRECT_CSR_EN_BITS;
    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {}

    // 0xF5 (quad-width) = exit QPI, in case a previous run/boot left the PSRAM in QPI mode.
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    qmi_hw->direct_tx = QMI_DIRECT_TX_OE_BITS
                      | QMI_DIRECT_TX_IWIDTH_VALUE_Q << QMI_DIRECT_TX_IWIDTH_LSB | 0xf5;
    while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {}
    (void)qmi_hw->direct_rx;
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS);

    // 0x9F read-ID: 7 bytes out, KGD at index 5, EID at index 6.
    qmi_hw->direct_csr |= QMI_DIRECT_CSR_ASSERT_CS1N_BITS;
    uint8_t kgd = 0;
    uint8_t eid = 0;
    for (size_t i = 0; i < 7; i++) {
        qmi_hw->direct_tx = (i == 0) ? 0x9fU : 0xffU;
        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_TXEMPTY_BITS) == 0) {}
        while ((qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) != 0) {}
        if (i == 5) {
            kgd = (uint8_t)qmi_hw->direct_rx;
        } else if (i == 6) {
            eid = (uint8_t)qmi_hw->direct_rx;
        } else {
            (void)qmi_hw->direct_rx;
        }
    }
    qmi_hw->direct_csr &= ~(QMI_DIRECT_CSR_ASSERT_CS1N_BITS | QMI_DIRECT_CSR_EN_BITS);

    if (kgd != 0x5D) {                          // no known-good APS6404 on CS1
        restore_interrupts(intr_stash);
        return 0;
    }
    size_t psram_size = 1024 * 1024;            // size code lives in the EID
    uint8_t size_id = eid >> 5;
    if (eid == 0x26 || size_id == 2) {
        psram_size *= 8; // 8 MiB (U3, APS6404L-3SQR-SN)
    } else if (size_id == 0) {
        psram_size *= 2; // 2 MiB
    } else if (size_id == 1) {
        psram_size *= 4; // 4 MiB
    }

    // Memory-mapped quad access on M1; the SDK has only QMI_M0_* field names, and M1's layout matches.
    const int max_psram_freq = 133000000;       // APS6404 max SCK
    const int clock_hz = (int)clock_get_hz(clk_sys); // read before direct mode takes flash XIP away

    // Re-enter direct mode at clkdiv 10 and enable QPI mode (0x35) on the PSRAM.
    qmi_hw->direct_csr = 10 << QMI_DIRECT_CSR_CLKDIV_LSB
                       | QMI_DIRECT_CSR_EN_BITS | QMI_DIRECT_CSR_AUTO_CS1N_BITS;
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {}
    qmi_hw->direct_tx = QMI_DIRECT_TX_NOPUSH_BITS | 0x35U; // CMD_QPI_EN
    while (qmi_hw->direct_csr & QMI_DIRECT_CSR_BUSY_BITS) {}

    // Timing derived from the system clock (PSRAM SCK = clk_sys / divisor, divisor>=clk/133MHz).
    int divisor = (clock_hz + max_psram_freq - 1) / max_psram_freq;
    if (divisor == 1 && clock_hz > 100000000) { divisor = 2; }
    int rxdelay = divisor;
    if (clock_hz / divisor > 100000000) { rxdelay += 1; }
    const int clock_period_fs = (int)(1000000000000000LL / clock_hz);
    const int max_select   = (125 * 1000000) / clock_period_fs;               // CS max-low (tCEM ~8us)
    const int min_deselect = (18 * 1000000 + (clock_period_fs - 1)) / clock_period_fs - (divisor + 1) / 2;

    qmi_hw->m[1].timing = 1 << QMI_M1_TIMING_COOLDOWN_LSB
        | QMI_M1_TIMING_PAGEBREAK_VALUE_1024 << QMI_M1_TIMING_PAGEBREAK_LSB   // 1024-byte page (APS6404 wrap)
        | max_select   << QMI_M1_TIMING_MAX_SELECT_LSB
        | min_deselect << QMI_M1_TIMING_MIN_DESELECT_LSB
        | rxdelay      << QMI_M1_TIMING_RXDELAY_LSB
        | divisor      << QMI_M1_TIMING_CLKDIV_LSB;

    // Read = 0xEB quad fast-read, 6 dummy cycles, everything quad-width.
    qmi_hw->m[1].rfmt =
          QMI_M0_RFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_PREFIX_WIDTH_LSB
        | QMI_M0_RFMT_ADDR_WIDTH_VALUE_Q   << QMI_M0_RFMT_ADDR_WIDTH_LSB
        | QMI_M0_RFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_RFMT_SUFFIX_WIDTH_LSB
        | QMI_M0_RFMT_DUMMY_WIDTH_VALUE_Q  << QMI_M0_RFMT_DUMMY_WIDTH_LSB
        | QMI_M0_RFMT_DATA_WIDTH_VALUE_Q   << QMI_M0_RFMT_DATA_WIDTH_LSB
        | QMI_M0_RFMT_PREFIX_LEN_VALUE_8   << QMI_M0_RFMT_PREFIX_LEN_LSB
        | 6                                << QMI_M0_RFMT_DUMMY_LEN_LSB;
    qmi_hw->m[1].rcmd = 0xEB;

    // Write = 0x38 quad write, no dummy.
    qmi_hw->m[1].wfmt =
          QMI_M0_WFMT_PREFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_PREFIX_WIDTH_LSB
        | QMI_M0_WFMT_ADDR_WIDTH_VALUE_Q   << QMI_M0_WFMT_ADDR_WIDTH_LSB
        | QMI_M0_WFMT_SUFFIX_WIDTH_VALUE_Q << QMI_M0_WFMT_SUFFIX_WIDTH_LSB
        | QMI_M0_WFMT_DUMMY_WIDTH_VALUE_Q  << QMI_M0_WFMT_DUMMY_WIDTH_LSB
        | QMI_M0_WFMT_DATA_WIDTH_VALUE_Q   << QMI_M0_WFMT_DATA_WIDTH_LSB
        | QMI_M0_WFMT_PREFIX_LEN_VALUE_8   << QMI_M0_WFMT_PREFIX_LEN_LSB;
    qmi_hw->m[1].wcmd = 0x38;

    qmi_hw->direct_csr = 0;                                      // leave direct mode -> M1 is XIP now
    hw_set_bits(&xip_ctrl_hw->ctrl, XIP_CTRL_WRITABLE_M1_BITS);  // allow writes to the PSRAM window
    restore_interrupts(intr_stash);
    return psram_size;
}
