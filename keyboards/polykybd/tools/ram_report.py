#!/usr/bin/env python3
"""Print where an RP2040 firmware ELF's RAM goes, and how much is left.

    python3 keyboards/polykybd/tools/ram_report.py <elf> [<elf> ...] [--top N]

For each ELF: the RAM sections with their size, the free RAM left in each bank
(the linker gives main RAM's remainder to `.heap`, so its size IS the headroom),
and the N largest RAM symbols.

Why it exists: the v1.9.1 release failed to link the DOOM monolith
(`POLYKYBD_DOOM=yes`) 40 B over the 256 KB bank, and nothing had shown how
close it was. PR CI never built that flavour, and local builds use a different
gcc (13.2 vs CI's 15.2, which needs 400–500 B more RAM for the same source),
so the headroom a developer saw was not the headroom CI had. CI now prints this
for the release-shape builds, so the number is visible on every PR.

Uses only arm-none-eabi-objdump / -nm, so it runs inside the qmkfm/qmk_cli
container.
"""
import argparse
import subprocess
import sys

RAM_LO, RAM_HI = 0x20000000, 0x20042000
# RP2040 SRAM banks as the linker scripts name them.
BANKS = (
    ("main (ram0)", 0x20000000, 0x20040000),
    ("scratch X (ram4)", 0x20040000, 0x20041000),
    ("scratch Y (ram5)", 0x20041000, 0x20042000),
)


_OBJDUMP = ["arm-none-eabi-objdump", "-h"]
_NM = ["arm-none-eabi-nm", "-S", "--size-sort"]


def _run(argv, elf):
    # Audit: argv is one of the two literal lists above and elf is a path from
    # this script's own command line (CI passes build_pack.sh's output). The
    # list form runs with no shell, so the path is one argument whatever it holds.
    # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-audit
    return subprocess.run([*argv, elf], capture_output=True, text=True, check=True).stdout


def sections(elf):
    """[(name, size, vma)] of the sections that occupy RAM."""
    out = []
    for line in _run(_OBJDUMP, elf).splitlines():
        p = line.split()
        if len(p) >= 4 and p[0].isdigit():
            size, vma = int(p[2], 16), int(p[3], 16)
            if size and RAM_LO <= vma < RAM_HI:
                out.append((p[1], size, vma))
    return out


def symbols(elf):
    """[(size, type, name)] of the symbols that live in RAM, largest first."""
    out = []
    for line in _run(_NM, elf).splitlines():
        p = line.split()
        if len(p) == 4 and RAM_LO <= int(p[0], 16) < RAM_HI:
            out.append((int(p[1], 16), p[2], p[3]))
    return sorted(out, reverse=True)


def report(elf, top):
    secs = sections(elf)
    print(f"== {elf}")
    for name, size, vma in sorted(secs, key=lambda s: s[2]):
        print(f"   {name:16s} {size:7d} B  at 0x{vma:08x}")
    for bank, lo, hi in BANKS:
        used = sum(s for n, s, v in secs if lo <= v < hi and n != ".heap")
        print(f"   {bank:18s} used {used:7d} of {hi - lo:6d} B, free {hi - lo - used:6d} B")
    heap = next((s for n, s, _ in secs if n == ".heap"), 0)
    print(f"   headroom (.heap, main RAM left after statics): {heap} B")
    if top:
        print(f"   largest {top} RAM symbols (pools and stacks included):")
        for size, kind, name in symbols(elf)[:top]:
            print(f"   {size:9d} {kind} {name}")
    return heap


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("elf", nargs="+")
    ap.add_argument("--top", type=int, default=15, help="largest symbols to list (0: none)")
    args = ap.parse_args()
    for elf in args.elf:
        report(elf, args.top)
    return 0


if __name__ == "__main__":
    sys.exit(main())
