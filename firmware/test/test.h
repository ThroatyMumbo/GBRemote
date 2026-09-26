// Shared by every test_<area>.c: the check counter and each area's entry point.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_fail, g_run;

#define CHECK(cond, ...) do { g_run++; if (!(cond)) { \
    g_fail++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); putchar('\n'); \
} } while (0)

void tests_pad(void);
void tests_n64(void);
void tests_maple(void);
void tests_vmu(void);
void tests_snes(void);
void tests_genesis(void);
void tests_capenc(void);
void tests_tpak(void);
void tests_rom(void);
void tests_serve(void);

unsigned maple_make_req_to(uint8_t *req, uint8_t cmd, uint8_t dst, uint8_t port,
                           const uint32_t *w, unsigned nw);
