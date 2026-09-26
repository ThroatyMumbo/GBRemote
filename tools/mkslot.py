#!/usr/bin/env python3
"""Pack a .gb image into the cart's flash slot store.

Emits two files for tools/slot.sh to program: the 4 KB slot directory and the ROM image itself.
The directory is read back and merged, so populating slot 2 does not erase slot 1.

Layout mirrors firmware/romstore.h; the offsets there are pinned by _Static_assert against the
struct format below.
"""
import argparse
import struct
import sys
from pathlib import Path

DIR_OFF, ROM_BASE, ROM_LIMIT = 0x080000, 0x100000, 0xEC0000
FLASH_LEN = 16 * 1024 * 1024                        # W25Q128JVSIQ, U2
SLOT_STRIDE = 2 * 1024 * 1024
SLOTS, HDR_LEN, DIR_LEN = 8, 64, 4096
MAGIC = 0x4D524247                                  # 'GBRM'
HDR_FMT = "<IIIIHBBB17s26x"                         # must stay 64 bytes
assert struct.calcsize(HDR_FMT) == HDR_LEN

MBC_NONE, MBC1, MBC2, MBC3, MBC5 = range(5)

# slot_page.c caps the name at SLOT_NAME_MAX - 1 and the header field is title[17].
NAME_MAX = 16

# The $0134 title is a build artifact ("PM_CRYSTAL"), so the slot screen gets a real name instead.
# Keyed by the parsed header title; every key here was read off a dump, never guessed. --name wins.
RETAIL_NAMES = {
    "PM_CRYSTAL":    "Pokemon Crystal",
    "POKEMON_GLD":   "Pokemon Gold",      # a CGB header ends the title at $013E, mid-"GLDAAUE"
    "POKEMON_SLV":   "Pokemon Silver",
    "POKEMON GREEN": "Pokemon Green",
    "POKEMON RED":   "Pokemon Red",
    "POKEMON YEL":   "Pokemon Yellow",
    "TETRIS":        "Tetris",
    "YUGIOUDS":      "Yu-Gi-Oh! DDS",
}


def display_name(title):
    """The label the GB slot screen shows: the table, else the header title title-cased."""
    if title in RETAIL_NAMES:
        return RETAIL_NAMES[title]
    return " ".join(w.capitalize() for w in title.replace("_", " ").split())


def mbc_from_cart_type(t):
    """The MBC type byte from the cartridge header's type code (header $0147)."""
    if t in (0x00, 0x08, 0x09):
        return MBC_NONE
    if t <= 0x03:
        return MBC1
    if t <= 0x06:
        return MBC2
    if 0x0F <= t <= 0x13:
        return MBC3
    if 0x19 <= t <= 0x1E:
        return MBC5
    return MBC5                                     # unknown: MBC5 decode is the most permissive


def ram_bytes(code, mbc):
    if mbc == MBC2:
        return 0x200                                # 512 x 4 bits, inside the MBC
    return {0x01: 2048, 0x02: 8192, 0x03: 32768, 0x04: 131072, 0x05: 65536}.get(code, 0)


def parse(rom):
    if len(rom) < 0x150:
        sys.exit("not a Game Boy image: shorter than the header")
    # $0134-$0143 on DMG; a CGB header ends the title at $013E, leaving $013F-$0142 the
    # manufacturer code and $0143 the CGB flag. Either way the first NUL ends it.
    raw = rom[0x134:0x13F] if rom[0x143] in (0x80, 0xC0) else rom[0x134:0x144]
    title = bytes(c if 0x20 <= c < 0x7F else 0x20 for c in raw.split(b"\0")[0]).decode()
    cart_type, rom_code, ram_code = rom[0x147], rom[0x148], rom[0x149]
    mbc = mbc_from_cart_type(cart_type)

    chk = 0
    for b in rom[0x134:0x14D]:
        chk = (chk - b - 1) & 0xFF
    if chk != rom[0x14D]:
        print(f"warning: header checksum is {rom[0x14D]:02x}, computed {chk:02x}", file=sys.stderr)

    declared = (32 * 1024) << rom_code if rom_code <= 0x08 else len(rom)
    if declared != len(rom):
        print(f"warning: header declares {declared} bytes, file is {len(rom)}", file=sys.stderr)
    if len(rom) % 0x4000:
        sys.exit(f"ROM is {len(rom)} bytes, not a multiple of 16 KB")

    ram = ram_bytes(ram_code, mbc)
    if ram > 0x8000:
        sys.exit(f"cart declares {ram} bytes of save RAM; the arena caps it at 32768")

    title = title.rstrip()
    return dict(title=title, label=display_name(title), mbc=mbc, cart_type=cart_type,
                cgb=rom[0x143], has_rtc=cart_type in (0x0F, 0x10), rom_len=len(rom),
                banks=len(rom) // 0x4000, ram_len=ram)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("slot", type=int, choices=range(1, SLOTS + 1))
    ap.add_argument("rom", type=Path)
    ap.add_argument("--outdir", type=Path, default=Path("firmware/build"))
    # Read-only: the merged directory goes to <outdir>/slotdir.bin, never back to this path.
    ap.add_argument("--dir-in", type=Path, help="existing directory image to merge from")
    ap.add_argument("--image", type=Path,
                    help="write into a whole flash image instead, at the offsets above")
    ap.add_argument("--name", help=f"slot label, overriding the table ({NAME_MAX} chars)")
    args = ap.parse_args()

    rom = args.rom.read_bytes()
    info = parse(rom)
    if args.name:
        info["label"] = args.name
    if len(info["label"]) > NAME_MAX:
        sys.exit(f"label {info['label']!r} is {len(info['label'])} chars; the slot screen shows {NAME_MAX}")

    rom_off = ROM_BASE + (args.slot - 1) * SLOT_STRIDE
    if rom_off + info["rom_len"] > ROM_LIMIT:
        sys.exit(f"slot {args.slot} at {rom_off:#x} + {info['rom_len']} overruns the ROM arena")

    table = bytearray(b"\xff" * DIR_LEN)
    src = args.dir_in
    # Silently starting fresh here would drop every other slot; the caller meant to merge.
    if src and not src.exists():
        sys.exit(f"--dir-in {src} does not exist; refusing to build a directory that drops other slots")
    if args.image and args.image.exists() and not src:
        table[:] = args.image.read_bytes()[DIR_OFF:DIR_OFF + DIR_LEN]
    elif src and src.exists():
        prev = src.read_bytes()
        table[:len(prev)] = prev[:DIR_LEN]

    at = (args.slot - 1) * HDR_LEN
    table[at:at + HDR_LEN] = struct.pack(
        HDR_FMT, MAGIC, rom_off, info["rom_len"], info["ram_len"], info["banks"],
        info["mbc"], int(info["has_rtc"]), info["cgb"], info["label"].encode()[:NAME_MAX])

    if args.image:
        img = bytearray(b"\xff" * FLASH_LEN)
        if args.image.exists():
            was = args.image.read_bytes()
            img[:len(was)] = was[:FLASH_LEN]
        img[DIR_OFF:DIR_OFF + DIR_LEN] = table
        img[rom_off:rom_off + info["rom_len"]] = rom
        args.image.parent.mkdir(parents=True, exist_ok=True)
        args.image.write_bytes(img)
    else:
        args.outdir.mkdir(parents=True, exist_ok=True)
        dir_out = args.outdir / "slotdir.bin"
        rom_out = args.outdir / f"slot{args.slot}.bin"
        dir_out.write_bytes(table)
        rom_out.write_bytes(rom)

    names = ["none", "mbc1", "mbc2", "mbc3", "mbc5"]
    print(f"slot {args.slot}: '{info['label']}' (header '{info['title']}') {names[info['mbc']]} "
          f"{info['rom_len'] // 1024} KB / {info['banks']} banks, "
          f"save {info['ram_len']} B{', rtc' if info['has_rtc'] else ''}")
    if args.image:
        print(f"  rom  -> {rom_off:#08x}")
        print(f"  dir  -> {DIR_OFF:#08x}")
        print(f"  into -> {args.image}")
    else:
        print(f"  rom  -> {rom_off:#08x}  {rom_out}")
        print(f"  dir  -> {DIR_OFF:#08x}  {dir_out}")


if __name__ == "__main__":
    main()
