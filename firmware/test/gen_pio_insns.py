#!/usr/bin/env python3
"""Emit a host-compilable C header of PIO instruction words from pioasm's JSON output.
The c-sdk output is unusable on the host: its `% c-sdk` block pulls in pico-sdk headers."""
import json
import subprocess
import sys


def main():
    if len(sys.argv) != 4:
        sys.exit("usage: gen_pio_insns.py <pioasm> <input.pio> <output.h>")
    pioasm, src, out = sys.argv[1:]

    doc = json.loads(subprocess.run([pioasm, "-o", "json", src],
                                    capture_output=True, check=True, text=True).stdout)

    lines = ["// generated from %s — do not edit" % src, "#include <stdint.h>", ""]
    for prog in doc["programs"]:
        name = prog["name"]
        up = name.upper()
        words = ", ".join("0x%04x" % int(i["hex"], 16) for i in prog["instructions"])
        lines += [
            "static const uint16_t %s_insns[] = { %s };" % (name, words),
            "#define %s_WRAP_TARGET %du" % (up, prog["wrapTarget"]),
            "#define %s_WRAP %du" % (up, prog["wrap"]),
        ]
        # A model that decodes delay fields has to know how wide the side-set steals them, and an
        # entry point that is not the wrap target has to be named rather than assumed.
        ss = prog.get("sideset", {})
        lines += [
            "#define %s_SIDESET %du" % (up, ss.get("size", 0)),
            "#define %s_SIDESET_OPT %d" % (up, 1 if ss.get("optional") else 0),
        ]
        for label, addr in sorted(prog.get("publicLabels", {}).items()):
            lines.append("#define %s_%s %du" % (up, label.upper(), addr))
        lines.append("")

    with open(out, "w") as f:
        f.write("\n".join(lines))


if __name__ == "__main__":
    main()
