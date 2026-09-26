// Pad-state tests: the GB->N64 mapping, the cable-ID ladder, the driver catalog, and Tandy VIS.
#include "test.h"
#include "config_map.h"
#include "cable_id.h"
#include "n64_proto.h"
#include "pad_catalog.h"
#include "vis_proto.h"

static void test_mapping(void) {
    uint8_t cfg[CFG_LEN];
    memset(cfg, 0xff, sizeof cfg);                  // wholly unprogrammed => identity map
    pad_map_t m;
    map_compile(cfg, &m);
    for (int i = 0; i < CFG_MAP_LEN; i++)
        CHECK(m.action[i] == pad_map_identity.action[i], "unprogrammed map bit %d", i);

    for (unsigned gb = 0; gb < 256; gb++) {
        pad_state_t p;
        n64_state_t n;
        map_apply(&m, (uint8_t)gb, &p);
        n64_from_pad(&p, N64_STICK_CARDINAL, N64_STICK_DIAGONAL, false, &n);

        CHECK(!(n.buttons & N64_BTN_RESET), "gb %02x asserted the console reset bit", gb);

        int sx = n.stick_x, sy = n.stick_y;
        if (sx < 0) sx = -sx;
        if (sy < 0) sy = -sy;
        CHECK(sx <= N64_STICK_CARDINAL && sy <= N64_STICK_CARDINAL,
              "gb %02x stick %d,%d exceeds cardinal", gb, n.stick_x, n.stick_y);
        if (sx && sy)
            CHECK(sx == N64_STICK_DIAGONAL && sy == N64_STICK_DIAGONAL,
                  "gb %02x diagonal %d,%d not on the gate", gb, n.stick_x, n.stick_y);
        else if (sx || sy)
            CHECK(sx + sy == N64_STICK_CARDINAL, "gb %02x cardinal %d,%d", gb, n.stick_x, n.stick_y);

        bool lr = (gb & PAD_LEFT) && (gb & PAD_RIGHT);
        bool ud = (gb & PAD_UP)   && (gb & PAD_DOWN);
        if (lr) CHECK(n.stick_x == 0, "gb %02x L+R did not cancel", gb);
        if (ud) CHECK(n.stick_y == 0, "gb %02x U+D did not cancel", gb);

        // SELECT is the one remap that is not the identity: it is the only home for Z.
        if (gb & PAD_SELECT) CHECK(n.buttons & N64_BTN_Z, "gb %02x SELECT did not reach Z", gb);
        if (gb & PAD_A)      CHECK(n.buttons & N64_BTN_A, "gb %02x A missing", gb);
        if (gb & PAD_B)      CHECK(n.buttons & N64_BTN_B, "gb %02x B missing", gb);
        if (gb & PAD_START)  CHECK(n.buttons & N64_BTN_START, "gb %02x START missing", gb);
    }

    // The d-pad drives the stick by default, and the N64 d-pad only when asked.
    pad_state_t p;
    n64_state_t n;
    map_apply(&m, PAD_UP, &p);
    n64_from_pad(&p, N64_STICK_CARDINAL, N64_STICK_DIAGONAL, false, &n);
    CHECK(n.stick_y == N64_STICK_CARDINAL && !(n.buttons & N64_BTN_DUP), "default UP -> stick");
    n64_from_pad(&p, N64_STICK_CARDINAL, N64_STICK_DIAGONAL, true, &n);
    CHECK(n.stick_y == 0 && (n.buttons & N64_BTN_DUP), "digital UP -> d-pad");
}

static void test_cable_id(void) {
    static const uint16_t nominal[10] = { 3300, 2534, 2272, 1658, 1198, 1070, 617, 327, 178, 33 };
    for (int i = 0; i < 10; i++)
        CHECK(cable_id_classify(nominal[i]) == (i == 0 ? ADAPT_NONE : (uint8_t)i),
              "nominal %u mV -> %u, want %d", nominal[i], cable_id_classify(nominal[i]), i);

    // Nothing at or above the open threshold may ever read as a cable — the guard band exists so
    // an absent or broken ID cannot alias onto a high-value slot.
    for (unsigned mv = 2917; mv <= 3300; mv++)
        CHECK(cable_id_classify((uint16_t)mv) == ADAPT_NONE, "%u mV classified as a cable", mv);

    // Midpoints between adjacent bands are in neither, and must say so rather than guess.
    for (int i = 1; i < 9; i++) {
        uint16_t mid = (uint16_t)((nominal[i] + nominal[i + 1]) / 2);
        CHECK(cable_id_classify(mid) == ADAPT_UNKNOWN,
              "midpoint %u mV -> %u, want UNKNOWN", mid, cable_id_classify(mid));
    }

    cable_id_reset();
    CHECK(cable_id_step(2534) == ADAPT_NONE, "latched on the first sample");
    CHECK(cable_id_step(2534) == ADAPT_NONE, "latched on the second sample");
    CHECK(cable_id_step(2534) == ADAPT_1,    "did not latch on the third");
    CHECK(cable_id_step(3300) == ADAPT_NONE, "unplug must latch immediately");

    CHECK(cable_id_proto(ADAPT_NES, PROTO_DREAMCAST) == PROTO_NES,       "NES cable, DC config");
    CHECK(cable_id_proto(ADAPT_NES, 0xff) == PROTO_NES,                  "NES cable, unset config");
    CHECK(cable_id_proto(ADAPT_NES, PROTO_NONE) == PROTO_NONE,           "explicit Off must stand");
    CHECK(cable_id_proto(ADAPT_SNES, PROTO_SNES_MOUSE) == PROTO_SNES_MOUSE, "mouse on the SNES cable");
    CHECK(cable_id_proto(ADAPT_SNES, PROTO_N64) == PROTO_SNES,           "SNES cable, N64 config");
    CHECK(cable_id_proto(ADAPT_DC, PROTO_GENESIS) == PROTO_DREAMCAST,    "DC cable, Genesis config");
    CHECK(cable_id_proto(ADAPT_GENESIS, PROTO_SNES) == PROTO_GENESIS,    "Genesis cable, SNES config");
    CHECK(cable_id_proto(ADAPT_N64, PROTO_SNES) == PROTO_N64,            "N64 cable, SNES config");
    CHECK(cable_id_proto(ADAPT_CAPTURE, PROTO_N64) == PROTO_N64,         "capture slot names no console");
    CHECK(cable_id_proto(ADAPT_SNES, PROTO_VIS) == PROTO_VIS,            "a cable must not override VIS");
    CHECK(cable_id_proto(ADAPT_N64, PROTO_VIS) == PROTO_VIS,             "a cable must not override VIS");
    CHECK(cable_id_proto(ADAPT_NONE, PROTO_DREAMCAST) == PROTO_DREAMCAST, "no cable defers to config");
    CHECK(cable_id_proto(ADAPT_UNKNOWN, PROTO_SNES) == PROTO_SNES,       "unknown cable defers to config");
}

// The $0300 driver catalog. Synthetic drivers, so no driver .c is linked and the vtable can stay
// NULL — pad_catalog_build() reads nothing but name/label/proto/res.
static const uint8_t *cat_at(const uint8_t *p, unsigned i) {
    return p + p[CATH_ENTRY_OFF] + i * p[CATH_STRIDE];
}

static void test_pad_catalog(void) {
    uint8_t page[CAT_PAGE_LEN];

    const pad_driver_t d_none = { .name = "none",  .proto = PROTO_NONE };
    const pad_driver_t d_n64  = { .name = "n64",   .label = "Nintendo 64", .proto = PROTO_N64,
                                  .res = { .ctrl_mask = 1, .pio_sms = 2, .pio_words = 19 } };
    const pad_driver_t d_dc   = { .name = "dreamcast", .label = "Dreamcast", .proto = PROTO_DREAMCAST,
                                  .res = { .ctrl_mask = 3, .pio_sms = 4, .pio_words = 32,
                                           .excl_ble = 1 } };
    const pad_driver_t d_bare = { .name = "bare",  .proto = PROTO_SNES,
                                  .res = { .ctrl_mask = 0x0d, .pio_sms = 1, .pio_words = 12,
                                           .logic_5v = 1, .needs_5v = 1 } };
    const pad_driver_t *const tab[] = { &d_none, &d_n64, &d_dc, &d_bare };

    unsigned n = pad_catalog_build(page, tab, 4);

    CHECK(n == 3, "built %u entries, want 3 (PROTO_NONE is not published)", n);
    CHECK(page[CATH_MAGIC0] == 'D' && page[CATH_MAGIC1] == 'R', "magic is wrong");
    CHECK(page[CATH_VERSION] == CAT_VERSION, "version %u", page[CATH_VERSION]);
    CHECK(page[CATH_COUNT] == 3, "count byte %u", page[CATH_COUNT]);
    CHECK(page[CATH_STRIDE] == CAT_ENTRY_STRIDE, "stride %u", page[CATH_STRIDE]);
    CHECK(page[CATH_NAME_OFF] == CAT_NAME_OFF, "name_off %u", page[CATH_NAME_OFF]);
    CHECK(page[CATH_ENTRY_OFF] == CAT_HDR_LEN, "entry_off %u", page[CATH_ENTRY_OFF]);

    // PROTO_NONE skipped, and what is left is dense from index 0.
    CHECK(cat_at(page, 0)[0] == PROTO_N64, "entry 0 proto %u", cat_at(page, 0)[0]);
    CHECK(cat_at(page, 1)[0] == PROTO_DREAMCAST, "entry 1 proto %u", cat_at(page, 1)[0]);
    CHECK(cat_at(page, 2)[0] == PROTO_SNES, "entry 2 proto %u", cat_at(page, 2)[0]);

    CHECK(strcmp((const char *)cat_at(page, 0) + CAT_NAME_OFF, "Nintendo 64") == 0,
          "entry 0 label '%s'", (const char *)cat_at(page, 0) + CAT_NAME_OFF);
    // label == NULL falls back to name, so a driver added without one still gets a usable row.
    CHECK(strcmp((const char *)cat_at(page, 2) + CAT_NAME_OFF, "bare") == 0,
          "entry 2 label '%s'", (const char *)cat_at(page, 2) + CAT_NAME_OFF);

    CHECK(cat_at(page, 0)[1] & CATF_SUPPORTED, "n64 should be supported");
    CHECK(cat_at(page, 1)[1] & CATF_EXCL_BLE, "dc should carry CATF_EXCL_BLE");
    // 32 words only fits because excl_ble spends the CYW43 reservation.
    CHECK(cat_at(page, 1)[1] & CATF_SUPPORTED, "dc should be supported via excl_ble");
    CHECK(cat_at(page, 2)[1] & (CATF_LOGIC_5V | CATF_NEEDS_5V), "bare should be 5 V both ways");
    CHECK(!(cat_at(page, 2)[1] & CATF_PINLESS), "bare has pins");

    const pad_driver_t d_all = { .name = "all8", .proto = PROTO_GENESIS,
                                 .res = { .ctrl_mask = 0xff } };
    // 32 words without excl_ble exceeds the 26 a driver may have.
    const pad_driver_t d_fat = { .name = "fat", .proto = PROTO_LOOPY,
                                 .res = { .ctrl_mask = 1, .pio_words = 30 } };
    const pad_driver_t d_pin = { .name = "pinless", .label = "Bluetooth HID", .proto = PROTO_BLE_HID };
    const pad_driver_t *const tab2[] = { &d_all, &d_fat, &d_pin };

    n = pad_catalog_build(page, tab2, 3);
    CHECK(n == 3, "built %u entries, want 3", n);
    CHECK(cat_at(page, 0)[1] & CATF_SUPPORTED, "all eight CTRL lines, CTRL7 included, must be claimable");
    CHECK(!(cat_at(page, 1)[1] & CATF_SUPPORTED), "over-budget must not be supported");
    CHECK(cat_at(page, 2)[1] & CATF_PINLESS, "a transport should be CATF_PINLESS");
    CHECK(strcmp((const char *)cat_at(page, 2) + CAT_NAME_OFF, "Bluetooth HID") == 0,
          "a 13-char label must survive whole");

    // A label longer than the field is truncated, and the NUL survives.
    const pad_driver_t d_long = { .name = "l", .label = "0123456789abcdefghij",
                                  .proto = PROTO_N64 };
    const pad_driver_t *const tab3[] = { &d_long };
    pad_catalog_build(page, tab3, 1);
    CHECK(strcmp((const char *)cat_at(page, 0) + CAT_NAME_OFF, "0123456789abc") == 0,
          "long label '%s'", (const char *)cat_at(page, 0) + CAT_NAME_OFF);

    // More drivers than the page holds: truncate at CAT_MAX_ENTRIES, never write past 256.
    // ASan is on, so an overrun fails loudly rather than silently.
    const pad_driver_t *big[32];
    for (unsigned i = 0; i < 32; i++) big[i] = &d_n64;
    n = pad_catalog_build(page, big, 32);
    CHECK(n == CAT_MAX_ENTRIES, "truncated to %u, want %u", n, CAT_MAX_ENTRIES);
    CHECK(page[CATH_COUNT] == CAT_MAX_ENTRIES, "count %u after truncation", page[CATH_COUNT]);

    // The guarantee the whole feature rests on: the catalog's verdict and drv_apply()'s gate are
    // the same function, so the menu can never offer a driver that init would then refuse.
    const pad_driver_t *const one[] = { &d_dc };
    pad_catalog_build(page, one, 1);
    CHECK(((cat_at(page, 0)[1] & CATF_SUPPORTED) != 0)
              == (pad_res_check(&d_dc.res) == PAD_OK),
          "CATF_SUPPORTED disagrees with pad_res_check()");
}

// Every frame a Tandy VIS controller sent, captured with demos/irlearn; the held set is bits 13-3.
static void test_vis_proto(void) {
    static const uint16_t captured[] = {0x0002, 0x400d, 0x4016, 0x4025, 0x4045, 0x4081, 0x4105,
        0x4201, 0x4401, 0x0801, 0x5001, 0x6001, 0x0126, 0x0031, 0x0062, 0x0222, 0x00a6, 0x4822,
        0x1022, 0x002a, 0x0422, 0x2022, 0x6125, 0x6032, 0x6052, 0x0019, 0x0051, 0x0146, 0x0286,
        0x0306, 0x0602, 0x4c02, 0x5802, 0x4906, 0x403e, 0x0925, 0x4705, 0x4136, 0x4935, 0x013d,
        0x0726};
    for (unsigned i = 0; i < sizeof captured / sizeof captured[0]; i++) {
        uint16_t got = vis_encode(captured[i] & VIS_HELD_MASK);
        CHECK(got == captured[i], "vis_encode(%04x) = %04x", captured[i] & VIS_HELD_MASK, got);
    }

    pad_state_t p;
    pad_zero(&p);
    p.buttons = PAD_A | PAD_SELECT | PAD_START | PAD_LEFT;
    CHECK(vis_held_from_pad(&p) == (VIS_BTN_A | VIS_BTN_4 | VIS_BTN_3 | VIS_BTN_LEFT),
          "SELECT is 4, START is 3: got %04x", vis_held_from_pad(&p));
    p.buttons = PAD_UP | PAD_DOWN | PAD_B;
    CHECK(vis_held_from_pad(&p) == VIS_BTN_B, "Down+Up must not be sent: got %04x",
          vis_held_from_pad(&p));
    p.buttons = PAD_X | PAD_Y;
    CHECK(vis_held_from_pad(&p) == (VIS_BTN_1 | VIS_BTN_2), "X/Y are 1/2: got %04x",
          vis_held_from_pad(&p));

    // A = 0100 0000 0010 0101; a 0 is mark-space, a 1 space-mark: the capture's +326 -608 +608.
    CHECK(vis_halves(0x4025) == 0x9aaaa699u, "halves(4025) = %08x", vis_halves(0x4025));
    CHECK(proto_pinless(PROTO_VIS) && !proto_pinless(PROTO_SNES) && !proto_pinless(0xff),
          "PROTO_VIS must be in the pinless range");
}

void tests_pad(void) {
    test_vis_proto();
    test_mapping();
    test_cable_id();
    test_pad_catalog();
}
