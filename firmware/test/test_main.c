// Host tests for everything that does not need silicon; each area lives in its own test_<area>.c.
#include "test.h"

int g_fail, g_run;

int main(void) {
    tests_pad();
    tests_n64();
    tests_maple();
    tests_vmu();
    tests_snes();
    tests_genesis();
    tests_capenc();
    tests_tpak();
    tests_rom();
    tests_serve();
    printf("%d checks, %d failed\n", g_run, g_fail);
    return g_fail != 0;
}
