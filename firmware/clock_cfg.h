// clock_cfg.h — the operating point, set by cs_set_operating_point(); the emulator's shim matches it.
#pragma once

#ifndef SYSCLK_KHZ
#define SYSCLK_KHZ 200000
#endif
#ifndef VREG_MV
#define VREG_MV 1150
#endif

// POWMAN_BOD resets at 0.946 V out of reset, so it must come down before any rail below that.
#ifndef BOD_MV
#define BOD_MV 860
#endif
