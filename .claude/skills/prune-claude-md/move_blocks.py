#!/usr/bin/env python3
"""Cut blocks out of a CLAUDE.md, append each verbatim to a doc, leave a pointer.

Usage:  python3 move_blocks.py spec.json

spec.json is a list of entries, each:
  {
    "src":     "CLAUDE.md",           # file to cut from
    "start":   207,                   # 1-based first line of the block
    "end":     322,                   # 1-based line AFTER the block (null = EOF)
    "dst":     "docs/SUBSYSTEM.md",   # destination (appended if it exists)
    "title":   "# Subsystem",         # used only when dst does not exist; null to append
    "pointer": "## Subsystem\n\n..."  # replacement text left in src
  }

All entries must share one `src`. Cuts are applied bottom-up so the line numbers
you measured against the ORIGINAL file all stay valid. Every moved block is
asserted byte-identical in its destination before src is rewritten.
"""
import json
import pathlib
import sys

PROVENANCE = "Moved out of `CLAUDE.md`. Verbatim."


def main(spec_path: str) -> int:
    spec = json.loads(pathlib.Path(spec_path).read_text())
    if not spec:
        print("empty spec")
        return 1

    srcs = {e["src"] for e in spec}
    if len(srcs) != 1:
        print(f"all entries must share one src, got {sorted(srcs)}")
        return 1
    src = pathlib.Path(spec[0]["src"])
    lines = src.read_text().splitlines(keepends=True)

    for e in spec:
        e["_end"] = e["end"] if e.get("end") else len(lines) + 1
        if not 1 <= e["start"] < e["_end"] <= len(lines) + 1:
            print(f"bad range {e['start']}..{e['end']} for {e['dst']}")
            return 1

    order = sorted(spec, key=lambda e: e["start"])
    for a, b in zip(order, order[1:]):
        if b["start"] < a["_end"]:
            print(f"overlapping ranges: {a['dst']} and {b['dst']}")
            return 1

    moved = {id(e): "".join(lines[e["start"] - 1:e["_end"] - 1]) for e in spec}

    for e in reversed(order):                      # bottom-up keeps line numbers valid
        lines[e["start"] - 1:e["_end"] - 1] = [e["pointer"]]
    src.write_text("".join(lines))

    total = 0
    for e in order:
        block, dst = moved[id(e)], pathlib.Path(e["dst"])
        dst.parent.mkdir(parents=True, exist_ok=True)
        if dst.exists():
            head = dst.read_text().rstrip("\n") + "\n\n"
        else:
            head = f"{(e.get('title') or '# ' + dst.stem).rstrip()}\n\n{PROVENANCE}\n\n"
        dst.write_text(head + block)
        assert block in dst.read_text(), f"VERIFY FAILED: {e['dst']}"
        total += len(block)
        print(f"  {len(block):7d} B  line {e['start']:>5} -> {e['dst']}")

    print(f"\nmoved {total} B; {src} is now {len(src.read_text())} B")
    print("NOW RUN: the link re-base check and the losslessness diff (SKILL.md steps 4-6)")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
