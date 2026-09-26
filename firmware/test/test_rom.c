// The served pages against the GB menu's own parsers: slots at $0400, map targets, controller art.
#include "test.h"
#include "config_map.h"
#include "gbcart_emu.h"
#include "romstore.h"
#include "slots.h"
#include "art.h"
#include "pad_mapinfo.h"
#include "mapinfo.h"
#include "generated/padart_blobs.h"

// The controller picture at $4600: packer against the GB menu's own parser, then the menu's
// incremental repaint against a full one.
volatile uint8_t g_art_win[ART_WINDOW];

// scr_emu.c's paint, reproduced: one cell per pass, a cursor into the dirty button's span, and
// shown[] tracking what the screen holds rather than what the cart last said.
static uint8_t a_map[18 * 32], a_shown[ART_MAX_BTN], a_live[3], a_seen[3];
static uint8_t a_pb, a_pj, a_on;

// Its own xorshift, so the sequence is fixed whatever else in the suite draws from a shared one.
static uint32_t art_rnd(void) {
    static uint32_t s = 0x13579bdfu;
    s ^= s << 13; s ^= s >> 17; s ^= s << 5;
    return s;
}

static void art_m_cell(uint8_t i) {
    a_map[((uint16_t)art_cell_y(i) << 5) + art_cell_x(i)] = art_cell_tile(i, a_live);
}

static void art_m_scan(void) {
    for (uint8_t b = 0; b < art_nbtn(); b++)
        if ((art_btn_down(b, a_live) ? 1 : 0) != a_shown[b]) {
            a_pb = b; a_pj = 0; a_on = 1; a_shown[b] = 0xff;
            return;
        }
    a_on = 0;
}

static void art_m_step(void) {
    uint8_t first = art_btn_first(a_pb), last = art_btn_last(a_pb);

    if ((uint8_t)(first + a_pj) >= last) {
        a_shown[a_pb] = art_btn_down(a_pb, a_live) ? 1 : 0;
        a_pj = 0;
        art_m_scan();
        return;
    }
    art_m_cell(art_btn_cell((uint8_t)(first + a_pj)));
    a_pj++;
}

static void art_m_pass(void) {
    if (memcmp(a_live, a_seen, 3)) { memcpy(a_seen, a_live, 3); art_m_scan(); }
    if (a_on) art_m_step();
}

static int art_m_settled(void) {
    for (uint8_t i = 0; i < art_ncells(); i++) {
        uint16_t at = ((uint16_t)art_cell_y(i) << 5) + art_cell_x(i);
        if (a_map[at] != art_cell_tile(i, a_live)) return 0;
    }
    return 1;
}

// The button count each pad must still carry: the one place that notices a button dropped from
// tools/padart/pads/.
static const struct { uint8_t proto; uint8_t nbtn; const char *name; } k_art_want[] = {
    { PROTO_N64,  14, "n64"  },
    { PROTO_SNES, 12, "snes" },
    { PROTO_NES,   8, "nes"  },
    { PROTO_VIS,  10, "vis"  },
};

static void test_art_blob(const padart_ent_t *e, uint8_t want_btn, const char *name) {
    uint8_t *w = (uint8_t *)g_art_win;
    unsigned stale = 0;

    CHECK(e->len <= ART_WINDOW, "the %s blob does not fit the window", name);
    memcpy(w, e->blob, e->len);
    CHECK(art_parse(), "the packed %s blob must parse", name);
    CHECK(art_nbtn() == want_btn, "%s: nbtn %u, want %u", name, art_nbtn(), want_btn);
    CHECK(art_ntiles() <= ART_MAX_TILES && art_npals() <= ART_MAX_PALS,
          "%s: tiles %u palettes %u", name, art_ntiles(), art_npals());
    CHECK(art_tiles_addr() % 16 == 0, "the tile block must be 16-aligned for HDMA2/HDMA4");

    // Every index the ROM feeds back into a VRAM write, over every state a cell can be in.
    for (uint8_t b = 0; b < art_nbtn(); b++)
        for (uint8_t j = art_btn_first(b); j < art_btn_last(b); j++)
            CHECK(art_btn_cell(j) < art_ncells(), "%s: button %u names cell %u of %u",
                  name, b, art_btn_cell(j), art_ncells());
    for (uint8_t i = 0; i < art_ncells(); i++) {
        CHECK(art_cell_x(i) < ART_COLS && art_cell_y(i) < ART_ROWS,
              "%s: cell %u at (%u,%u) is off screen", name, i, art_cell_x(i), art_cell_y(i));
        for (unsigned m = 0; m < 0x1000000u; m += 0x111111u) {
            uint8_t st[3] = { (uint8_t)m, (uint8_t)(m >> 8), (uint8_t)(m >> 16) };
            CHECK(art_cell_tile(i, st) < art_ntiles(), "%s: cell %u names tile %u of %u",
                  name, i, art_cell_tile(i, st), art_ntiles());
        }
    }

    // A truncated or foreign blob must be refused rather than half rendered.
    w[0] = 'X'; CHECK(!art_parse(), "%s: bad magic must not parse", name); w[0] = ART_MAGIC0;
    w[2]++;     CHECK(!art_parse(), "%s: a future version must not parse", name); w[2]--;
    w[ARTH_MAXK] = ART_MAX_K + 1;
    CHECK(!art_parse(), "%s: a fan-out past the mask lookup must not parse", name);
    w[ARTH_MAXK] = ART_MAX_K;
    w[ARTH_OFF_TILES] ^= 8;
    CHECK(!art_parse(), "%s: a misaligned tile block must not parse", name);
    w[ARTH_OFF_TILES] ^= 8;
    CHECK(art_parse(), "%s: the blob must parse again once restored", name);

    // A press shorter than its own span must not leave its drawn cells pressed; art_scan() marks
    // the span in flight for this.
    memset(a_live, 0, 3);
    memset(a_seen, 0, 3);
    memset(a_shown, 0, sizeof a_shown);
    a_pb = a_pj = a_on = 0;
    for (uint8_t i = 0; i < art_ncells(); i++) art_m_cell(i);

    for (unsigned t = 0; t < 4000; t++) {
        a_live[0] = (uint8_t)(art_rnd() & 0xff);      // the eight the Game Boy itself can send
        for (unsigned p = 0, n = 1 + (art_rnd() & 3); p < n; p++) art_m_pass();
        a_live[0] = 0;
        for (unsigned p = 0; p < 40; p++) art_m_pass();
        if (!art_m_settled()) {
            stale++;
            for (uint8_t i = 0; i < art_ncells(); i++) art_m_cell(i);    // resync so trials stay independent
        }
    }
    CHECK(stale == 0, "%s: %u of 4000 press/release pairs left a stale cell", name, stale);
}

static void test_art(void) {
    // An unstaged window is $FF filler, which must read as "no picture", never as garbage indices
    // that would reach a VRAM write.
    memset((uint8_t *)g_art_win, 0xff, ART_WINDOW);
    CHECK(!art_parse(), "an unstaged window must not parse");

    for (unsigned i = 0; i < sizeof k_padart / sizeof k_padart[0]; i++) {
        const char *name = NULL;
        uint8_t want = 0;
        for (unsigned j = 0; j < sizeof k_art_want / sizeof k_art_want[0]; j++)
            if (k_art_want[j].proto == k_padart[i].proto) {
                want = k_art_want[j].nbtn;
                name = k_art_want[j].name;
            }
        CHECK(name != NULL, "proto %u has art but no row in k_art_want[]", k_padart[i].proto);
        test_art_blob(&k_padart[i], want, name);
    }
}

// One map per proto, and a controller switch brings that proto's map with it.
static void test_map_slots(void) {
    uint8_t seen[CFG_LEN] = { 0 };
    for (unsigned p = 0; p < 256; p++) {
        uint8_t o = cfg_map_off((uint8_t)p);
        if (!o) continue;
        CHECK(o >= CFG_MAP && o + CFG_MAP_LEN <= CFG_MAP_END, "proto %u map at %02x", p, o);
        for (unsigned i = 0; i < CFG_MAP_LEN; i++) {
            CHECK(!seen[o + i], "proto %u map overlaps another at %02x", p, o + i);
            seen[o + i] = 1;
        }
    }
    CHECK(cfg_map_off(PROTO_N64) == CFG_MAP, "N64 keeps the original CFG_MAP");
    CHECK(cfg_map_off(PROTO_VIS) != 0, "every published proto needs a map slot");
    CHECK(cfg_map_off(PROTO_NONE) == 0, "Off has no map");
    CHECK(cfg_class_of(CFG_MAP_END - 1) == CFG_CLASS_MAP, "the last map byte is a map write");
    CHECK(cfg_class_of(CFG_MAP_END) == CFG_CLASS_NONE, "past the maps is reserved");

    uint8_t cfg[CFG_LEN];
    pad_map_t m;
    memset(cfg, 0xff, sizeof cfg);
    cfg[CFG_PROTO_SEL] = PROTO_N64;
    cfg[cfg_map_off(PROTO_N64) + 4] = ACT_PAD(PAD_B);
    cfg[cfg_map_off(PROTO_SNES) + 5] = ACT_NONE;
    map_compile(cfg, &m);
    CHECK(m.action[4] == ACT_PAD(PAD_B) && m.action[5] == ACT_PAD(PAD_B), "N64 map: GB A -> B");
    cfg[CFG_PROTO_SEL] = PROTO_SNES;
    map_compile(cfg, &m);
    CHECK(m.action[4] == ACT_PAD(PAD_A) && m.action[5] == ACT_NONE, "SNES keeps its own map");
}

// The $0500 page, encoder against the GB menu's own parser.
volatile uint8_t g_mp_page[MP_PAGE_LEN];

static void test_mapinfo_page(void) {
    static const pad_target_t t64[] = {
        { ACT_PAD(PAD_A), "A" }, { ACT_PAD(PAD_SELECT), "Z" }, { ACT_AXIS(AX_LX, 1), "Stick R" },
        { ACT_PAD(PAD_C_RIGHT), "C-Right-too-long" },
    };
    const pad_driver_t off = { .name = "none", .proto = PROTO_NONE };
    const pad_driver_t n64 = { .name = "n64", .proto = PROTO_N64, PAD_TARGETS(t64) };
    const pad_driver_t bare = { .name = "snes", .proto = PROTO_SNES };
    const pad_driver_t *tab[] = { &off, &n64, &bare };
    uint8_t *page = (uint8_t *)g_mp_page;
    char s[MP_LABEL_MAX];

    memset(page, 0xff, MP_PAGE_LEN);
    CHECK(!mp_find(PROTO_N64), "erased flash must not parse as a map page");

    CHECK(pad_mapinfo_build(page, tab, 3) == 2, "Off is not published");
    CHECK(page[PMIH_NLABELS] == 11, "labels deduped: %u", page[PMIH_NLABELS]);
    CHECK(mp_find(PROTO_N64) && mp_map_off() == CFG_MAP && mp_n() == 4, "N64 entry");
    mp_label(1, s);
    CHECK(!strcmp(s, "Z") && mp_action(1) == ACT_PAD(PAD_SELECT), "label trimmed: '%s'", s);
    mp_label(3, s);
    CHECK(!strcmp(s, "C-Right"), "label cut at PMI_LABEL_LEN: '%s'", s);
    CHECK(mp_index_of(ACT_AXIS(AX_LX, 1)) == 2 && mp_index_of(ACT_NONE) == 0xff, "index_of");
    CHECK(mp_find(PROTO_SNES) && mp_n() == 8 && mp_map_off() == cfg_map_off(PROTO_SNES),
          "a driver with no table gets the eight GB buttons");
    mp_label(4, s);
    CHECK(!strcmp(s, "A") && mp_action(4) == ACT_PAD(PAD_A), "GB button 4 is A: '%s'", s);
    CHECK(!mp_find(PROTO_GENESIS), "an absent proto is not found");

    // Overflow stops at a whole driver, and what was written still parses.
    static pad_target_t many[40];
    static char names[40][8];
    static pad_driver_t big[16];
    const pad_driver_t *bt[16];
    for (unsigned d = 0; d < 16; d++) {
        big[d] = (pad_driver_t){ .name = "x", .proto = (uint8_t)(d + 1), .targets = many, .ntargets = 40 };
        bt[d] = &big[d];
    }
    for (unsigned i = 0; i < 40; i++) {
        snprintf(names[i], sizeof names[i], "T%u", i);
        many[i] = (pad_target_t){ (uint8_t)i, names[i] };
    }
    unsigned k = pad_mapinfo_build(page, bt, 16);
    CHECK(k > 0 && k < 16, "a full page truncates: %u", k);
    CHECK(mp_find((uint8_t)k) && mp_n() == 40 && !mp_find((uint8_t)(k + 1)), "truncated page parses");
}

// The $0400 page, encoder against the GB menu's own parser. A mismatch here is a blank Transfer
// Pak menu on a real console and nothing else to see, so it is worth driving both real sides.
volatile uint8_t g_slot_page[SLOT_PAGE_LEN];

static void test_slot_page(void) {
    gb_slot_hdr_t h[GB_SLOTS];
    const gb_slot_hdr_t *p[GB_SLOTS];
    char name[SLOT_NAME_MAX];
    uint8_t *page = (uint8_t *)g_slot_page;

    memset(h, 0, sizeof h);
    for (unsigned i = 0; i < GB_SLOTS; i++) p[i] = NULL;

    // An unprogrammed page is $FF filler, which must read as "no slot page", not as garbage rows.
    memset(page, 0xff, SLOT_PAGE_LEN);
    CHECK(!slot_ok(), "erased flash must not parse as a slot page");
    CHECK(slot_count() == 0, "erased page count %u", slot_count());

    // Populate slots 2, 3 and 7 — deliberately not 1..3, because entry i is not slot i.
    h[1] = (gb_slot_hdr_t){ .magic = GB_SLOT_MAGIC, .rom_len = 0x100000, .ram_len = 32768,
                            .rom_banks = 64, .mbc = MBC3, .has_rtc = 1, .cgb_flag = 0x80 };
    memcpy(h[1].title, "POKEMON CRYSTAL", 15);
    h[2] = (gb_slot_hdr_t){ .magic = GB_SLOT_MAGIC, .rom_len = 0x8000, .ram_len = 0,
                            .rom_banks = 2, .mbc = MBC_NONE, .cgb_flag = 0x00 };
    memcpy(h[2].title, "TETRIS", 6);
    h[6] = (gb_slot_hdr_t){ .magic = GB_SLOT_MAGIC, .rom_len = 0x40000, .ram_len = 512,
                            .rom_banks = 16, .mbc = MBC2, .cgb_flag = 0x00 };
    memcpy(h[6].title, "KID ICARUS", 10);
    p[1] = &h[1]; p[2] = &h[2]; p[6] = &h[6];

    slot_page_build(page, p);
    CHECK(slot_ok(), "built page must parse");
    CHECK(slot_count() == 3, "count %u, want 3", slot_count());

    CHECK(slot_index(0) == 2 && slot_index(1) == 3 && slot_index(2) == 7,
          "slot indices %u %u %u", slot_index(0), slot_index(1), slot_index(2));

    slot_name(0, name);
    CHECK(strcmp(name, "POKEMON CRYSTAL") == 0, "title 0 '%s'", name);
    CHECK(slot_mbc(0) == MBC3 && slot_banks(0) == 64 && slot_ram_kb(0) == 32,
          "entry 0 mbc %u banks %u ram %u", slot_mbc(0), slot_banks(0), slot_ram_kb(0));
    CHECK((slot_flags(0) & (SLOTF_PRESENT | SLOTF_RTC | SLOTF_CGB | SLOTF_SAVE))
              == (SLOTF_PRESENT | SLOTF_RTC | SLOTF_CGB | SLOTF_SAVE),
          "entry 0 flags %02x", slot_flags(0));

    slot_name(1, name);
    CHECK(strcmp(name, "TETRIS") == 0, "title 1 '%s'", name);
    CHECK(!(slot_flags(1) & (SLOTF_SAVE | SLOTF_RTC | SLOTF_CGB)), "entry 1 flags %02x",
          slot_flags(1));
    CHECK(slot_banks(1) == 2, "entry 1 banks %u", slot_banks(1));

    // MBC2's 512 bytes round to 0 KB, so SLOTF_SAVE is the only thing that says a save exists.
    CHECK(slot_ram_kb(2) == 0 && (slot_flags(2) & SLOTF_SAVE), "MBC2 save flag %02x",
          slot_flags(2));

    // A full 16-char title has no room for a terminator in the header; it must still come back
    // whole and NUL-terminated.
    memset(h[1].title, 0, sizeof h[1].title);
    memcpy(h[1].title, "ABCDEFGHIJKLMNOP", 16);
    slot_page_build(page, p);
    slot_name(0, name);
    CHECK(strcmp(name, "ABCDEFGHIJKLMNOP") == 0, "16-char title came back '%s'", name);

    // Every slot populated must still fit the page, and the count must not exceed what it holds.
    for (unsigned i = 0; i < GB_SLOTS; i++) { h[i] = h[2]; p[i] = &h[i]; }
    slot_page_build(page, p);
    CHECK(slot_count() == GB_SLOTS, "full page count %u", slot_count());
    CHECK(slot_index(GB_SLOTS - 1) == GB_SLOTS, "last slot index %u", slot_index(GB_SLOTS - 1));

    // A count byte lying about how many entries the page holds must be clamped, not believed.
    page[3] = 0xff;
    CHECK(slot_count() <= (SLOT_PAGE_LEN - SLOT_HDR_LEN) / SLOT_ENTRY_STRIDE,
          "a lying count byte was believed: %u", slot_count());

    // Bytes tools/mkslot.py emitted for rom/controller.gb, pinning its struct format to the C side:
    //   python3 tools/mkslot.py 1 rom/controller.gb --outdir <dir> && xxd -l 64 <dir>/slotdir.bin
    static const uint8_t golden[64] = {
        0x47,0x42,0x52,0x4d, 0x00,0x00,0x10,0x00, 0x00,0x80,0x00,0x00, 0x00,0x00,0x00,0x00,
        0x02,0x00, 0x00, 0x00, 0x80, 'G','B','C','O','N','T','R','O','L',0,0,
    };
    const gb_slot_hdr_t *g = (const gb_slot_hdr_t *)golden;
    CHECK(g->magic == GB_SLOT_MAGIC, "mkslot magic %08x", g->magic);
    CHECK(g->rom_off == GB_ROM_BASE, "mkslot rom_off %08x", g->rom_off);
    CHECK(g->rom_len == 0x8000 && g->ram_len == 0, "mkslot lengths %u/%u", g->rom_len, g->ram_len);
    CHECK(g->rom_banks == 2 && g->mbc == MBC_NONE && g->has_rtc == 0,
          "mkslot banks %u mbc %u rtc %u", g->rom_banks, g->mbc, g->has_rtc);
    CHECK(g->cgb_flag == 0x80, "mkslot cgb %02x", g->cgb_flag);
    CHECK(strcmp(g->title, "GBCONTROL") == 0, "mkslot title '%s'", g->title);
}

void tests_rom(void) {
    test_slot_page();
    test_map_slots();
    test_mapinfo_page();
    test_art();
}
