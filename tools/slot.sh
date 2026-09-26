#!/bin/bash
# Load a Game Boy ROM into one of the cart's eight Transfer Pak slots over USB, with the cart in BOOTSEL.
#   tools/slot.sh <slot 1-8> <game.gb> [label]
set -euo pipefail

[ $# -ge 2 ] || { echo "usage: $0 <slot 1-8> <game.gb> [label]" >&2; exit 1; }
slot="$1" gb="$2"
command -v picotool >/dev/null || { echo "no picotool on PATH" >&2; exit 1; }
[ -f "$gb" ] || { echo "no such ROM: $gb" >&2; exit 1; }

here="$(cd "$(dirname "$0")" && pwd)"
work_dir="$(mktemp -d)"
trap 'rm -rf "$work_dir"' EXIT

# Read the directory back first: mkslot.py merges into it, so the other slots survive.
picotool save -r 0x10080000 0x10081000 "$work_dir/dir_in.bin" -t bin
python3 "$here/mkslot.py" "$slot" "$gb" --outdir "$work_dir" --dir-in "$work_dir/dir_in.bin" ${3:+--name "$3"}

# The ROM arena starts at flash 0x100000, 2 MB per slot, mapped at XIP 0x10000000.
rom_addr=$(printf '0x%x' $((0x10100000 + (slot - 1) * 0x200000)))
picotool load "$work_dir/slot$slot.bin" -t bin -o "$rom_addr"
picotool load "$work_dir/slotdir.bin" -t bin -o 0x10080000
picotool reboot -a
