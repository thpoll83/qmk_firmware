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
import difflib
import itertools
import re
import shutil
import subprocess
import sys

# `  1002a4c:\t4b0b      \tldr\tr3, [pc, #44]\t@ (1002a7c <foo+0x30>)`
_INSN = re.compile(r"^\s*[0-9a-f]+:\s+([0-9a-f ]+)\t(.*)$")
_LABEL = re.compile(r"^[0-9a-f]+ <(.+)>:$")
# A bare 4-8 digit hex token is an address in this ISA's disassembly; shorter
# ones (0x7f) are real operands and are deliberately kept.
#
# The leading `#-?` group is what makes this safe: an ARM disassembly writes every
# IMMEDIATE with a `#` sigil (`movw r0, #0x1234`) and every address without one, so
# capturing the sigil is enough to tell the two apart. Without it the pattern also
# rewrote wide immediates, and `#0x1234` and `#0x5678` both normalised to `#ADDR` —
# i.e. exactly the "a size/alignment-sensitive constant moved and the compiler folded
# it differently" case this script exists to catch would have compared EQUAL, against
# the contract stated in the module docstring. Caught in review of this file
# (CodeRabbit, PR #205); `_addr_sub` keeps such an operand verbatim.
_ADDR = re.compile(r"(#-?)?\b(?:0x)?[0-9a-f]{4,8}\b")
_SYMREF = re.compile(r"<[^>]*>")


def _addr_sub(text: str) -> str:
    """Normalise bare address literals, leaving `#`-sigil immediates alone."""
    return _ADDR.sub(lambda m: m.group(0) if m.group(1) else "ADDR", text)


def _run_tool(argv: "list[str]") -> str:
    """Run a binutils tool and return stdout, or exit with a diagnostic.

    Every invocation goes through here so a failure can never be mistaken for
    empty output. That matters more than usual: this script's whole job is to
    answer "did anything change", and a tool that silently produced nothing would
    make both sides compare equal and report "equivalent" — the one wrong answer
    a verification tool must never give.

    argv is built from a hard-coded flag plus a tool name and path this script's
    own CLI supplied; it is passed as a LIST with the default shell=False, so
    there is no shell to inject through. shlex.quote would be actively wrong here:
    it escapes for a shell *string*, and applied to an argv element it corrupts
    the value.
    """
    try:
        # nosemgrep: python.lang.security.audit.dangerous-subprocess-use-audit
        res = subprocess.run(argv, capture_output=True, text=True, check=True)
    except FileNotFoundError:
        sys.exit(f"error: {argv[0]} not found on PATH")
    except subprocess.CalledProcessError as exc:
        sys.exit(f"error: {' '.join(argv)} failed:\n{(exc.stderr or '').strip()}")
    return res.stdout


def _objdump(elf: str, tool: str) -> str:
    return _run_tool([tool, "-d", elf])


def functions(elf: str, tool: str) -> "collections.OrderedDict[str, list[list[str]]]":
    """Map function name -> EVERY address-normalised body emitted under that name.

    A list of bodies, not one body: ELF allows duplicate local symbol names, and
    this tree really has them — `port_lock`, `port_unlock`, `chThdGetSelfX`,
    `flash_cs_force` and `flash_enable_xip_via_boot2` are each emitted in several
    translation units, five duplicated labels per image. Keying by name alone kept
    only the last one, so a change confined to an earlier body compared equal and
    the script reported "equivalent". Found in review of this file's first version.
    """
    out: "collections.OrderedDict[str, list[list[str]]]" = collections.OrderedDict()
    body: "list[str]" = []
    current = None

    def flush() -> None:
        if current is not None:
            out.setdefault(current, []).append(body)

    for line in _objdump(elf, tool).splitlines():
        label = _LABEL.match(line)
        if label:
            flush()
            current = label.group(1)
            body = []
            continue
        if current is None:
            continue
        insn = _INSN.match(line)
        if not insn:
            # A blank line ends the current function body.
            if not line.strip():
                flush()
                current = None
            continue
        text = _addr_sub(insn.group(2))
        # `<foo+0x30>` and `<bar>` are annotations on the address just
        # normalised; keeping them would re-introduce the link order.
        text = _SYMREF.sub("<SYM>", text)
        body.append(text)
    flush()
    return out


def body_multiset(bodies: "list[list[str]]") -> "collections.Counter[tuple[str, ...]]":
    """The bodies emitted under one name, as an unordered multiset.

    Comparison must not depend on the ORDER the duplicate bodies were emitted in.
    `functions()` returns them in link order, and link order is precisely what this
    script's main use case perturbs — moving a file between rules.mk levels, or
    reordering SRC, can swap two same-named local symbols without changing a single
    instruction. Comparing the ordered lists reported DIFFERENT for that, i.e. the
    tool cried wolf on exactly the class of change it exists to clear.

    A multiset and not a set: {A, A} vs {A, B} must still differ, so the counts
    carry. This is the other half of the duplicate-symbol fix — storing every body
    (rather than overwriting) stopped the tool MISSING a real change; comparing them
    unordered stops it INVENTING one.
    """
    return collections.Counter(tuple(b) for b in bodies)


def data_symbols(elf: str, tool: str) -> "collections.Counter[tuple[str, int]]":
    """Multiset of (name, size) for .data symbols.

    A multiset, not a dict: file-scope statics in different translation units
    share a name (this tree has FreeSansBold24pt7b three times and CSWTCH.5
    twice), so keying by name alone collapses them and can hide a size change
    in one behind its namesake.
    """
    nm = shutil.which(tool.replace("objdump", "nm")) or "nm"
    # Via _run_tool so a failing/absent nm exits loudly. Previously this parsed
    # whatever stdout came back regardless of exit status, so two failed runs
    # yielded two empty counters and the script reported ".data symbols identical".
    syms: "collections.Counter[tuple[str, int]]" = collections.Counter()
    for line in _run_tool([nm, "-S", elf]).splitlines():
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
    differing = [
        k
        for k in base
        if k in cand and body_multiset(base[k]) != body_multiset(cand[k])
    ]

    n_base = sum(len(v) for v in base.values())
    n_cand = sum(len(v) for v in cand.values())
    dupes = sum(len(v) - 1 for v in base.values() if len(v) > 1)
    print(f"functions: baseline={n_base} candidate={n_cand}"
          f" ({len(base)} distinct names, {dupes} duplicate label(s))")
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
        import difflib

        for name in differing[: args.show]:
            # Report from the multiset difference, not by pairing the lists
            # positionally: bodies that appear on both sides are matched and
            # dropped by the subtraction, so what is left is only the genuinely
            # unmatched ones. Pairing by index would instead diff a body against
            # whichever unrelated namesake happened to land at the same position.
            mb, mc = body_multiset(base[name]), body_multiset(cand[name])
            gone = list((mb - mc).elements())
            new = list((mc - mb).elements())
            if len(base[name]) != len(cand[name]):
                print(f"\n--- {name}: emitted {len(base[name])} time(s)"
                      f" -> {len(cand[name])}")
            for idx, (b, c) in enumerate(itertools.zip_longest(gone, new, fillvalue=())):
                where = name if max(len(gone), len(new)) == 1 else f"{name} [{idx + 1}]"
                print(f"\n--- {where} ({len(b)} -> {len(c)} insns)")
                for line in list(difflib.unified_diff(list(b), list(c), lineterm="", n=2))[:40]:
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
