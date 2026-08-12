#!/usr/bin/env python3
# Copyright 2026 thpoll83
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prove that a refactor changed no generated code.

Compares two ELFs function by function. Instruction operands that encode an
*address* are normalised away, so a change that only moves objects around in
the link (different SRC order, a file moved between rules.mk levels, a renamed
translation unit) compares equal, while any change to what the code actually
*does* shows up as a differing body.

This is the check to run for a pure code-motion change — extracting a header,
moving a file, deleting dead code, splitting a translation unit. It is strictly
stronger than comparing `arm-none-eabi-size` output, which is blind to two
functions swapping instructions at constant size, and more usable than
`cmp`-ing .text, which reports a difference for every relocation.

Usage:
    # capture a baseline before the refactor
    qmk compile -kb polykybd/split72 -km default
    cp .build/polykybd_split72_default.elf /tmp/base.elf

    # ...refactor, rebuild, then:
    tools/compare_codegen.py /tmp/base.elf .build/polykybd_split72_default.elf

Exit status is 0 when the generated code is equivalent, 1 otherwise, so it can
gate a commit.

What a difference here means, in order of likelihood:
  * the refactor was not behaviour-preserving after all;
  * a size/alignment-sensitive constant moved (EECONFIG_USER_DATA_SIZE, a
    static buffer, a struct member) and the compiler folded it differently;
  * the two ELFs were built with different -e options / feature flags, which
    this script cannot detect for you.

Note the deliberate limits: .data / .rodata *contents* are not compared (use
`arm-none-eabi-nm -S` for that, and see --data-symbols below), and a build that
differs only in a compile-time constant baked into an immediate WILL be
reported as differing - which is correct, that is a behaviour change.
"""

from __future__ import annotations

import argparse
import collections
import re
import shutil
import subprocess
import sys

# `  1002a4c:\t4b0b      \tldr\tr3, [pc, #44]\t@ (1002a7c <foo+0x30>)`
_INSN = re.compile(r"^\s*[0-9a-f]+:\s+([0-9a-f ]+)\t(.*)$")
_LABEL = re.compile(r"^[0-9a-f]+ <(.+)>:$")
# Any 4-8 digit hex token is an address in this ISA's disassembly; shorter
# immediates (#44, 0x7f) are real operands and are deliberately kept.
_ADDR = re.compile(r"\b(?:0x)?[0-9a-f]{4,8}\b")
_SYMREF = re.compile(r"<[^>]*>")


def _objdump(elf: str, tool: str) -> str:
    try:
        res = subprocess.run(
            [tool, "-d", elf], capture_output=True, text=True, check=True
        )
    except FileNotFoundError:
        sys.exit(f"error: {tool} not found on PATH")
    except subprocess.CalledProcessError as exc:
        sys.exit(f"error: {tool} -d {elf} failed:\n{exc.stderr.strip()}")
    return res.stdout


def functions(elf: str, tool: str) -> "collections.OrderedDict[str, list[str]]":
    """Map function name -> address-normalised instruction list."""
    out: "collections.OrderedDict[str, list[str]]" = collections.OrderedDict()
    current = None
    for line in _objdump(elf, tool).splitlines():
        label = _LABEL.match(line)
        if label:
            current = label.group(1)
            out[current] = []
            continue
        if current is None:
            continue
        insn = _INSN.match(line)
        if not insn:
            # A blank line ends the current function body.
            if not line.strip():
                current = None
            continue
        text = _ADDR.sub("ADDR", insn.group(2))
        # `<foo+0x30>` and `<bar>` are annotations on the address just
        # normalised; keeping them would re-introduce the link order.
        text = _SYMREF.sub("<SYM>", text)
        out[current].append(text)
    return out


def data_symbols(elf: str, tool: str) -> "collections.Counter[tuple[str, int]]":
    """Multiset of (name, size) for .data symbols.

    A multiset, not a dict: file-scope statics in different translation units
    share a name (this tree has FreeSansBold24pt7b three times and CSWTCH.5
    twice), so keying by name alone collapses them and can hide a size change
    in one behind its namesake.
    """
    nm = shutil.which(tool.replace("objdump", "nm")) or "nm"
    res = subprocess.run([nm, "-S", elf], capture_output=True, text=True)
    syms: "collections.Counter[tuple[str, int]]" = collections.Counter()
    for line in res.stdout.splitlines():
        parts = line.split()
        if len(parts) == 4 and parts[2] in ("D", "d"):
            syms[(parts[3], int(parts[1], 16))] += 1
    return syms


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Compare two ELFs for generated-code equivalence."
    )
    ap.add_argument("baseline", help="ELF built before the change")
    ap.add_argument("candidate", help="ELF built after the change")
    ap.add_argument(
        "--objdump",
        default="arm-none-eabi-objdump",
        help="disassembler to use (default: arm-none-eabi-objdump)",
    )
    ap.add_argument(
        "--data-symbols",
        action="store_true",
        help="also compare .data symbol names and sizes",
    )
    ap.add_argument(
        "--show",
        type=int,
        default=5,
        help="how many differing bodies to print in full (default 5)",
    )
    args = ap.parse_args()

    base = functions(args.baseline, args.objdump)
    cand = functions(args.candidate, args.objdump)

    only_base = sorted(set(base) - set(cand))
    only_cand = sorted(set(cand) - set(base))
    differing = [k for k in base if k in cand and base[k] != cand[k]]

    print(f"functions: baseline={len(base)} candidate={len(cand)}")
    ok = True

    if only_base:
        ok = False
        print(f"removed ({len(only_base)}): {', '.join(only_base[:20])}")
    if only_cand:
        ok = False
        print(f"added   ({len(only_cand)}): {', '.join(only_cand[:20])}")

    if differing:
        ok = False
        print(f"differing bodies: {len(differing)}")
        for name in differing[: args.show]:
            print(f"\n--- {name} ({len(base[name])} -> {len(cand[name])} insns)")
            import difflib

            for line in list(
                difflib.unified_diff(base[name], cand[name], lineterm="", n=2)
            )[:40]:
                print(f"    {line}")
    else:
        print("differing bodies: 0")

    if args.data_symbols:
        db = data_symbols(args.baseline, args.objdump)
        dc = data_symbols(args.candidate, args.objdump)
        if db != dc:
            ok = False
            for key in sorted(set(db) | set(dc)):
                if db.get(key, 0) != dc.get(key, 0):
                    name, size = key
                    print(
                        f".data {name} (size {size}): "
                        f"count {db.get(key, 0)} -> {dc.get(key, 0)}"
                    )
        else:
            count = sum(db.values())
            total = sum(size * n for (_, size), n in db.items())
            print(f".data symbols: {count} identical ({total} bytes)")

    print("\nRESULT:", "equivalent ✅" if ok else "DIFFERENT ❌")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
