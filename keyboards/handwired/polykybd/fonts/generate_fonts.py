#!/usr/bin/env python3
"""Generate the PolyKybd per-keycap pixel-font headers from fonts.yaml.

This replaces the old create_fonts.sh.  It reads fonts/fonts.yaml (the single
source of truth), invokes the `fontconvert` tool once per font entry, groups the
resulting GFXfont definitions into a small set of per-category headers in
base/fonts/generated/, and composes base/fonts/gfx_used_fonts.h with the
ALL_FONTS[] lookup table.

Usage:
    python3 fonts/generate_fonts.py [options]

Options:
    --fontconvert PATH   Path to the fontconvert binary (default: $FONTCONVERT
                         or `fontconvert` on PATH).
    --config PATH        Path to the YAML config (default: fonts/fonts.yaml next
                         to this script).
    --check              Generate in memory and diff against the committed
                         headers; exit non-zero on any drift (no files written).
                         Handy for CI to catch a stale checkout of the headers.
    -q, --quiet          Only print warnings and errors.

Font files must be present first (run fonts/dl-fonts.sh).  See fonts/README.md.
"""
from __future__ import annotations
import argparse, os, re, shutil, subprocess, sys
from pathlib import Path

try:
    import yaml
except ImportError:
    sys.exit("PyYAML is required:  pip install pyyaml")

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fontpack  # noqa: E402  (sibling module: PlyF font-pack serializer/validator)

# Committed structural manifest for the external-flash font pack (the build
# artifact that ships to the host). The manifest pins the pack's font order and
# ranges so a checkout is verifiable and step-3 firmware can derive its index map.
PACK_MANIFEST = "fontpack.manifest.json"

# Order in which a font entry's fields map onto fontconvert flags.  Flag order
# does not affect the rendered bytes (fontconvert parses options order-free); it
# only shapes the provenance comment fontconvert prints on the first line.
FIELD_FLAGS = [
    ("size", "-s"), ("variant", "-v"), ("grayscale", "-g"), ("normalize", "-N"),
    ("render_height", "-r"),
    ("yadvance", "-Y"), ("xshift", "-X"), ("max_width", "-W"), ("weight", "-w"), ("dither", "-D"), ("exposure", "-e"),
    ("contrast", "-c"), ("gamma", "-G"), ("saturation", "-B"), ("sharpness", "-U"),
    ("outline", "-O"), ("invert", "-I"), ("edge", "-E"),
    ("offset", "-o"), ("neg_offset", "-n"), ("bits", "-b"),
]
GFXFONT_RE = re.compile(r"const\s+GFXfont\s+(\w+)\s+PROGMEM", re.M)


def log(msg: str, quiet: bool = False) -> None:
    if not quiet:
        print(msg)


def resolve(entry: dict, categories: dict) -> dict:
    """Merge a font entry on top of its category defaults."""
    cat = entry.get("category")
    if cat not in categories:
        sys.exit(f"font entry references unknown category {cat!r}: {entry}")
    merged = dict(categories[cat].get("defaults", {}))
    merged.update({k: v for k, v in entry.items() if k != "category"})
    return merged


def build_argv(fc: str, e: dict, sources: dict, root: Path) -> list[str]:
    src = e.get("source")
    if src not in sources:
        sys.exit(f"font entry references unknown source {src!r}: {e}")
    argv = [fc, "-f", str(root / sources[src])]
    for field, flag in FIELD_FLAGS:
        val = e.get(field)
        if val in (None, False):                 # absent / disabled (also 0)
            continue
        if val is True:                          # bare boolean flag (-g/-N/-I/-E/…)
            argv.append(flag)
        else:                                    # flag with a value
            argv += [flag, str(val)]
    argv += [str(a) for a in e.get("extra_args", [])]
    if e.get("sequence"):
        argv += ["-S", str(e["sequence"])]       # sequence mode ignores ranges
    else:
        for pair in e.get("ranges", []):
            if isinstance(pair, (list, tuple)):
                argv += [str(pair[0]), str(pair[1])]
            else:                                 # single value: last cp, first=0x20
                argv.append(str(pair))
    return argv


def render(fc: str, entries, sources, categories, root: Path, quiet: bool):
    """Run fontconvert for each entry. Returns (cat_blocks, ordered_symbols)."""
    if not (shutil.which(fc) or Path(fc).exists()):
        sys.exit(f"fontconvert not found: {fc!r}\n"
                 f"Build it (see ../AdafruitGFX/fontconvert/README.md) and pass "
                 f"--fontconvert PATH or set $FONTCONVERT.")
    cat_blocks: dict[str, list[str]] = {c: [] for c in categories}
    symbols: list[str] = []
    for i, entry in enumerate(entries):
        e = resolve(entry, categories)
        argv = build_argv(fc, e, sources, root)
        res = subprocess.run(argv, capture_output=True, text=True)
        if res.returncode != 0:
            sys.exit(f"fontconvert failed for entry {i} ({e.get('variant')}):\n"
                     f"  cmd: {' '.join(argv)}\n{res.stderr}")
        out = res.stdout
        m = GFXFONT_RE.search(out)
        if not m:
            sys.exit(f"no GFXfont symbol in fontconvert output for entry {i} "
                     f"({e.get('variant')})")
        symbols.append(m.group(1))
        cat_blocks[entry["category"]].append(out.rstrip() + "\n")
        log(f"  [{i+1:>2}/{len(entries)}] {e.get('variant'):<16} -> {m.group(1)}", quiet)
    return cat_blocks, symbols


def compose_category(blocks: list[str]) -> str:
    return "#pragma once\n\n" + "\n".join(blocks)


def compose_index(index: dict, categories: dict, cat_blocks: dict, symbols: list[str]) -> str:
    lines = ["#pragma once", ""]
    for cat, meta in categories.items():
        if cat_blocks.get(cat):                  # only include non-empty categories
            lines.append(f'#include "generated/{meta["output"]}"')
    lines.append("")
    for inc in index.get("extra_includes", []):
        lines.append(f'#include "{inc}"')
    lines += ["", "const GFXfont* const ALL_FONTS [] = {"]
    for sym in index.get("prepend_fonts", []):
        lines.append(f"  &{sym},")
    for sym in symbols:
        lines.append(f"  &{sym},")
    lines += ["};", "", "#define ALL_FONT_SIZE (sizeof(ALL_FONTS) / sizeof(GFXfont*))", ""]
    return "\n".join(lines)


def main() -> None:
    here = Path(__file__).resolve().parent
    root = here.parent                            # the keyboard directory

    ap = argparse.ArgumentParser(description="Generate PolyKybd pixel-font headers from fonts.yaml")
    ap.add_argument("--fontconvert", default=os.environ.get("FONTCONVERT", "fontconvert"))
    ap.add_argument("--config", default=str(here / "fonts.yaml"))
    ap.add_argument("--check", action="store_true",
                    help="diff against committed headers, write nothing, fail on drift")
    ap.add_argument("--only", metavar="CATEGORY",
                    help="regenerate only this category's header (skips the ALL_FONTS "
                         "index and the stale-header cleanup, so the other category "
                         "headers and their source fonts are left untouched)")
    ap.add_argument("--emit-pack", metavar="PATH", default="",
                    help="also write the binary font pack (PlyF) to PATH (the "
                         "release artifact flashed to the keyboard). The manifest "
                         "is always (re)written next to the generated headers.")
    ap.add_argument("--content-version", type=int, default=0,
                    help="content_version stamped into the emitted pack header")
    ap.add_argument("-q", "--quiet", action="store_true")
    args = ap.parse_args()

    cfg = yaml.safe_load(Path(args.config).read_text())
    sources, categories = cfg["sources"], cfg["categories"]
    index, entries = cfg["index"], cfg["fonts"]
    if args.only:
        if args.only not in categories:
            sys.exit(f"--only: unknown category {args.only!r}")
        entries = [e for e in entries if e.get("category") == args.only]
        if not entries:
            sys.exit(f"--only: no font entries in category {args.only!r}")

    log(f"Generating {len(entries)} fonts via {args.fontconvert} ...", args.quiet)
    cat_blocks, symbols = render(args.fontconvert, entries, sources, categories,
                                 root, args.quiet)

    gen_dir = root / "base" / "fonts" / "generated"
    outputs: dict[Path, str] = {}
    for cat, meta in categories.items():
        if cat_blocks.get(cat):
            outputs[gen_dir / meta["output"]] = compose_category(cat_blocks[cat])
    pack_data = None
    if not args.only:                             # --only never rewrites the ALL_FONTS index
        index_text = compose_index(index, categories, cat_blocks, symbols)
        outputs[root / "base" / "fonts" / index["output"]] = index_text
        # Derive the font-pack manifest from the freshly composed (in-memory)
        # headers so it stays consistent with them under --check.
        cat_texts = {c: compose_category(b) for c, b in cat_blocks.items() if b}
        pack_data, manifest = fontpack.manifest_from_texts(
            index_text, cat_texts, cfg, content_version=args.content_version)
        outputs[gen_dir / PACK_MANIFEST] = fontpack.manifest_json(pack_data, manifest)

    if args.check:
        drift = False
        for path, content in outputs.items():
            old = path.read_text() if path.exists() else None
            if old != content:
                drift = True
                print(f"DRIFT: {path.relative_to(root)}")
        # flag any stale per-font headers left over from the old pipeline
        for stale in sorted(gen_dir.glob("*.h")):
            if stale not in outputs:
                drift = True
                print(f"STALE: {stale.relative_to(root)} (not produced by config)")
        if drift:
            sys.exit("font headers are out of date; run fonts/generate_fonts.py")
        log("OK: generated headers match the committed ones.", args.quiet)
        return

    gen_dir.mkdir(parents=True, exist_ok=True)
    # remove leftover per-font headers from the old shell pipeline (full runs only;
    # --only regenerates a single header and must not touch the other categories)
    if not args.only:
        keep = {p.name for p in outputs if p.parent == gen_dir}
        for stale in gen_dir.glob("*.h"):
            if stale.name not in keep:
                stale.unlink()
    for path, content in outputs.items():
        path.write_text(content)
        log(f"wrote {path.relative_to(root)}", args.quiet)
    if args.emit_pack and pack_data is not None:
        Path(args.emit_pack).write_bytes(pack_data)
        log(f"wrote {args.emit_pack} ({len(pack_data):,} B font pack)", args.quiet)
    log(f"Done: {len(symbols)} fonts in ALL_FONTS "
        f"(+{len(index.get('prepend_fonts', []))} prepended).", args.quiet)


if __name__ == "__main__":
    main()
