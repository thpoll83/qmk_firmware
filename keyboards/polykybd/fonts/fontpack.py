#!/usr/bin/env python3
"""PolyKybd "PlyF" font-pack format: parse / serialize / validate / dump.

The font pack is the bulk glyph-bitmap data (emoji, symbols, CJK, Indic,
Arabic, …) split out of the firmware image into the external-flash resource
region, so the firmware shrinks and fonts update independently over HID. A small
"functional minimum" (IconsFont + the `latin` category + status-OLED fonts)
stays compiled into the firmware so the keyboard works with no pack present.

This module is the build-side counterpart of base/fontpack.h. It:
  * parses the committed base/fonts/generated/*.h GFXfont headers (no fontconvert
    or TTF sources needed — works straight off a checkout),
  * serializes the pack subset into a position-independent "PlyF" binary,
  * validates / dumps a pack binary (the offline validator), and
  * is importable by generate_fonts.py (build_pack_from_tree / write_pack).

Format (little-endian, 4-byte aligned, all internal refs are pack-base-relative
byte offsets):

    header 32 B  : magic "PlyF", abi_version, flags, content_version,
                   font_count, font_table_off, total_size,
                   crc32 (over everything after the header), reserved
    font table   : font_count × 20 B {bitmap_off, glyph_off, first, last,
                   yAdvance(i16), reserved}
    glyph arrays : per font, GFXglyph[last-first+1], 8 B each
                   {bitmapOffset(u16), w, h, xAdvance, xOffset, yOffset (i8)}
    bitmap blobs : per font, concatenated 1-bit glyph bitmaps

See base/fontpack.h for the matching C structs and the firmware-side contract.
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import sys
import zlib
from dataclasses import dataclass, field
from pathlib import Path

MAGIC = b"PlyF"
ABI_VERSION = 2             # 2: column-native (OLED page) glyph bitmaps (see _glyph_len)
HEADER_SIZE = 32
FONT_REC_SIZE = 20
GLYPH_REC_SIZE = 8

HEADER_FMT = "<4sHHIIIII"   # magic, abi, flags, content_ver, count, table_off, total, crc, (reserved appended)
FONT_FMT = "<IIIIhH"         # bitmap_off, glyph_off, first, last, yAdvance, reserved
GLYPH_FMT = "<Hbbbbbx"       # bitmapOffset, w, h, xAdvance, xOffset, yOffset, pad


def _glyph_len(w: int, h: int) -> int:
    """Per-glyph bitmap byte count for the COLUMN-NATIVE (OLED page) layout (ABI 2):
    cb = (h+7)//8 page-bytes per column × w columns. (ABI 1 was row-major (w*h+7)//8.)
    The generated GFX headers are column-native, so this is the slice length into a
    parsed font's flat `bitmaps` blob."""
    return w * ((h + 7) // 8)


# ─────────────────────────── parsing the C headers ──────────────────────────

@dataclass
class ParsedFont:
    symbol: str
    first: int
    last: int
    yadvance: int
    glyphs: list[tuple[int, int, int, int, int, int]]  # (bmpOff, w, h, xAdv, xOff, yOff)
    bitmaps: bytes

    def expected_glyph_count(self) -> int:
        return self.last - self.first + 1


_COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
_HEX_RE = re.compile(r"0x[0-9A-Fa-f]+|-?\d+")


def _strip_comments(text: str) -> str:
    return _COMMENT_RE.sub("", text)


def _parse_int(tok: str) -> int:
    return int(tok, 16) if tok.lower().startswith("0x") else int(tok, 10)


def _canonical_offsets(symbol: str, rows: list[tuple[int, ...]]) -> list[tuple[int, ...]]:
    """Rewrite each glyph's bitmapOffset to the running cumulative bitmap length.

    ⚠️ This exists so the serialised pack does not depend on a COSMETIC choice in
    the header.  A GAP record (w == h == 0) has no bitmap, so its offset is dead --
    the renderer skips the glyph on `w == 0 && h == 0` and never reads it -- and
    the two fontconvert emitters disagree about what to put there: the older one
    wrote the running length, the current one writes 0.  Parsing verbatim let that
    difference reach the .plyf bytes, so simply reformatting the headers changed 4
    shipped bundles (same size, ~535 single-byte diffs) and would have forced a
    reship plus a content_version bump for zero visual change.

    The running length is the canonical form: it is what the committed headers and
    prune_shadowed_glyphs both use, so this keeps existing bundles byte-stable.
    For a REAL glyph the running length equals its stored offset, which is
    asserted rather than assumed -- if it ever does not, the header is malformed
    and silently renumbering it would corrupt every glyph after it.
    """
    out, run = [], 0
    for i, g in enumerate(rows):
        off, w, h = g[0], g[1], g[2]
        if w and h:
            if off != run:
                raise ValueError(f"{symbol}Glyphs[{i}]: bitmapOffset {off} != "
                                 f"cumulative {run} — malformed header")
            out.append(g)
            run += w * ((h + 7) // 8)
        else:
            out.append((run,) + tuple(g[1:]))
    return out


def parse_gfx_header(text: str) -> dict[str, ParsedFont]:
    """Parse a generated GFXfont header into {symbol: ParsedFont}.

    Recognises the fontconvert output shape:
        const uint8_t  <Sym>Bitmaps[] PROGMEM = { 0x.., ... };
        const GFXglyph <Sym>Glyphs[]  PROGMEM = { { .. }, ... };
        const GFXfont  <Sym>          PROGMEM = { (uint8_t*)<Sym>Bitmaps,
                                                  (GFXglyph*)<Sym>Glyphs,
                                                  first, last, yAdvance };
    """
    clean = _strip_comments(text)
    bitmaps: dict[str, bytes] = {}
    glyphs: dict[str, list[tuple[int, ...]]] = {}

    for m in re.finditer(r"const\s+uint8_t\s+(\w+?)Bitmaps\s*\[\]\s*\w*\s*=\s*\{(.*?)\}\s*;",
                          clean, re.S):
        toks = _HEX_RE.findall(m.group(2))
        bitmaps[m.group(1)] = bytes(_parse_int(t) & 0xFF for t in toks)

    for m in re.finditer(r"const\s+GFXglyph\s+(\w+?)Glyphs\s*\[\]\s*\w*\s*=\s*\{(.*?)\}\s*;",
                          clean, re.S):
        body = m.group(2)
        rows = []
        for row in re.finditer(r"\{([^}]*)\}", body):
            vals = [_parse_int(t) for t in _HEX_RE.findall(row.group(1))]
            if len(vals) != 6:
                raise ValueError(f"{m.group(1)}Glyphs: expected 6 fields, got {vals}")
            rows.append(tuple(vals))
        glyphs[m.group(1)] = _canonical_offsets(m.group(1), rows)

    fonts: dict[str, ParsedFont] = {}
    for m in re.finditer(
        r"const\s+GFXfont\s+(\w+)\s*\w*\s*=\s*\{\s*\(uint8_t\s*\*\)\s*(\w+?)Bitmaps\s*,"
        r"\s*\(GFXglyph\s*\*\)\s*(\w+?)Glyphs\s*,(.*?)\}\s*;", clean, re.S):
        sym, bsym, gsym, tail = m.groups()
        nums = _HEX_RE.findall(tail)
        if len(nums) < 3:
            raise ValueError(f"GFXfont {sym}: cannot parse first/last/yAdvance from {tail!r}")
        first, last, yadv = (_parse_int(nums[0]), _parse_int(nums[1]), _parse_int(nums[2]))
        if bsym not in bitmaps or gsym not in glyphs:
            raise ValueError(f"GFXfont {sym}: missing {bsym}Bitmaps/{gsym}Glyphs")
        fonts[sym] = ParsedFont(sym, first, last, yadv, glyphs[gsym], bitmaps[bsym])
    return fonts


# ─────────────────────────── serialize / deserialize ────────────────────────

def serialize_pack(fonts: list[ParsedFont], content_version: int = 0,
                   abi_version: int = ABI_VERSION,
                   global_indices: list[int] | None = None) -> bytes:
    """Build a "PlyF" pack from an ordered list of fonts (order == priority).

    `global_indices`, when given, is a parallel list of each font's position in
    the full ALL_FONTS priority order; it is written into the per-font record's
    `reserved` u16 so a multi-bundle firmware can merge fonts from several packs
    back into global priority order. Omitted (single-pack path) → reserved = 0,
    which is what every legacy reader already ignores (byte-identical output)."""
    n = len(fonts)
    if global_indices is not None and len(global_indices) != n:
        raise ValueError("global_indices length must match fonts")
    table_off = HEADER_SIZE

    # Layout: header, font table, all glyph arrays, all bitmap blobs.
    glyph_blocks, bitmap_blocks = [], []
    glyphs_base = table_off + n * FONT_REC_SIZE
    cur = glyphs_base
    glyph_offs = []
    for f in fonts:
        glyph_offs.append(cur)
        blob = b"".join(struct.pack(GLYPH_FMT, g[0], g[1], g[2], g[3], g[4], g[5])
                        for g in f.glyphs)
        glyph_blocks.append(blob)
        cur += len(blob)
        cur = (cur + 3) & ~3                      # 4-align next block

    bitmap_offs = []
    for f in fonts:
        bitmap_offs.append(cur)
        bitmap_blocks.append(f.bitmaps)
        cur += len(f.bitmaps)
        cur = (cur + 3) & ~3

    total_size = cur

    table = b"".join(
        struct.pack(FONT_FMT, bitmap_offs[i], glyph_offs[i], f.first, f.last, f.yadvance,
                    (global_indices[i] if global_indices is not None else 0))
        for i, f in enumerate(fonts))

    body = bytearray(total_size - HEADER_SIZE)
    def place(off: int, blob: bytes):
        body[off - HEADER_SIZE: off - HEADER_SIZE + len(blob)] = blob
    place(table_off, table)
    for off, blob in zip(glyph_offs, glyph_blocks):
        place(off, blob)
    for off, blob in zip(bitmap_offs, bitmap_blocks):
        place(off, blob)

    crc = zlib.crc32(bytes(body)) & 0xFFFFFFFF
    header = struct.pack(HEADER_FMT + "I", MAGIC, abi_version, 0, content_version,
                         n, table_off, total_size, crc, 0)
    return bytes(header) + bytes(body)


@dataclass
class PackFont:
    first: int
    last: int
    yadvance: int
    glyph_off: int
    bitmap_off: int


@dataclass
class Pack:
    abi_version: int
    content_version: int
    font_count: int
    total_size: int
    crc32: int
    crc_ok: bool
    fonts: list[PackFont] = field(default_factory=list)


def parse_pack(data: bytes) -> Pack:
    if len(data) < HEADER_SIZE:
        raise ValueError("pack shorter than header")
    (magic, abi, _flags, content_ver, count, table_off, total, crc, _res) = struct.unpack(
        HEADER_FMT + "I", data[:HEADER_SIZE])
    if magic != MAGIC:
        raise ValueError(f"bad magic {magic!r} (expected {MAGIC!r})")
    if total != len(data):
        raise ValueError(f"total_size {total} != actual {len(data)}")
    crc_ok = (zlib.crc32(data[HEADER_SIZE:]) & 0xFFFFFFFF) == crc
    fonts = []
    for i in range(count):
        base = table_off + i * FONT_REC_SIZE
        boff, goff, first, last, yadv, _ = struct.unpack(
            FONT_FMT, data[base:base + FONT_REC_SIZE])
        fonts.append(PackFont(first, last, yadv, goff, boff))
    return Pack(abi, content_ver, count, total, crc, crc_ok, fonts)


# ─────────────────────────── validation ─────────────────────────────────────

def validate_pack(data: bytes, expected_manifest: list[dict] | None = None,
                  abi_version: int = ABI_VERSION) -> list[str]:
    """Return a list of problems (empty == valid)."""
    problems: list[str] = []
    try:
        pack = parse_pack(data)
    except Exception as e:                                   # noqa: BLE001
        return [f"unparseable: {e}"]

    if pack.abi_version != abi_version:
        problems.append(f"abi_version {pack.abi_version} != expected {abi_version}")
    if not pack.crc_ok:
        problems.append("crc32 mismatch (corrupt pack)")

    for i, f in enumerate(pack.fonts):
        if f.last < f.first:
            problems.append(f"font {i}: last 0x{f.last:X} < first 0x{f.first:X}")
        gcount = f.last - f.first + 1
        gend = f.glyph_off + gcount * GLYPH_REC_SIZE
        if not (HEADER_SIZE <= f.glyph_off <= pack.total_size and gend <= pack.total_size):
            problems.append(f"font {i}: glyph block [{f.glyph_off}:{gend}] out of bounds")
        if not (HEADER_SIZE <= f.bitmap_off <= pack.total_size):
            problems.append(f"font {i}: bitmap_off {f.bitmap_off} out of bounds")

    if expected_manifest is not None:
        if len(expected_manifest) != pack.font_count:
            problems.append(f"font_count {pack.font_count} != manifest "
                            f"{len(expected_manifest)}")
        else:
            for i, (exp, got) in enumerate(zip(expected_manifest, pack.fonts)):
                if exp["first"] != got.first or exp["last"] != got.last:
                    problems.append(
                        f"font {i} ({exp.get('symbol','?')}): range "
                        f"0x{got.first:X}-0x{got.last:X} != manifest "
                        f"0x{exp['first']:X}-0x{exp['last']:X}")
    return problems


# ─────────────────────────── tree → pack (used by generate_fonts.py) ─────────

def _load_yaml(path: Path) -> dict:
    try:
        import yaml
    except ImportError:                                     # pragma: no cover
        sys.exit("PyYAML is required:  pip install pyyaml")
    return yaml.safe_load(path.read_text())


def resident_symbols(cfg: dict, fonts_dir: Path) -> set[str]:
    """Symbols that stay compiled into the firmware (NOT in the pack)."""
    idx = cfg.get("index", {})
    res = set(idx.get("prepend_fonts", []))      # IconsFont etc.
    res |= set(idx.get("resident_fonts", []))    # UI-chrome fonts in packed cats
    cats = cfg["categories"]
    for cat, meta in cats.items():
        if meta.get("resident"):
            hdr = fonts_dir / "generated" / meta["output"]
            if hdr.exists():
                res |= set(parse_gfx_header(hdr.read_text()).keys())
    return res


def all_fonts_order(fonts_dir: Path) -> list[str]:
    """The full font priority order (resident + pack).

    The firmware's gfx_used_fonts.h only compiles RESIDENT_FONTS[], so the full
    order lives in the committed generated/all_fonts_order.json. Fall back to a
    legacy ALL_FONTS[] table in gfx_used_fonts.h if the JSON is absent.
    """
    order_json = fonts_dir / "generated" / "all_fonts_order.json"
    if order_json.exists():
        return json.loads(order_json.read_text())["order"]
    idx = (fonts_dir / "gfx_used_fonts.h").read_text()
    body = idx.split("ALL_FONTS")[1]
    return re.findall(r"&(\w+)\s*,", body.split("};")[0])


def build_pack(order: list[str], resident: set[str], parsed: dict[str, ParsedFont],
               content_version: int = 0):
    """Core builder: (pack_bytes, manifest) from an ALL_FONTS order + parsed fonts.

    The pack contains every ALL_FONTS entry that is NOT resident, in ALL_FONTS
    order (so within-pack overlap precedence is preserved). The manifest lists
    those fonts (symbol, first, last) for the offline validator and the
    firmware-side index map.
    """
    pack_fonts: list[ParsedFont] = []
    manifest: list[dict] = []
    for gi, sym in enumerate(order):
        if sym in resident:
            continue
        if sym not in parsed:
            raise ValueError(f"ALL_FONTS entry {sym!r} not found in generated headers")
        pf = parsed[sym]
        if len(pf.glyphs) != pf.expected_glyph_count():
            raise ValueError(
                f"{sym}: {len(pf.glyphs)} glyphs != last-first+1="
                f"{pf.expected_glyph_count()} (0x{pf.first:X}-0x{pf.last:X})")
        pack_fonts.append(pf)
        manifest.append({"all_fonts_index": gi, "symbol": sym,
                         "first": pf.first, "last": pf.last})

    data = serialize_pack(pack_fonts, content_version=content_version)
    return data, manifest


# ─────────────────────── shadowed-glyph dedupe (dead weight) ─────────────────
#
# Front-to-back precedence means the firmware only ever draws a codepoint from the
# lowest-gidx font that has a glyph for it (fontpack_assemble: resident set first,
# then the pack fonts by their stored gidx). Any glyph in a *later* font that is
# byte-identical to that winner never renders — it is pure dead weight in flash.
# `prune_shadowed_glyphs()` empties those copies before the bundles are serialized;
# because only never-drawn duplicates are removed, the assembled render is provably
# unchanged (asserted below). Only PACK (non-resident) fonts are touched — resident
# fonts are compiled in, not part of a bundle.

def _glyph_key(pf: ParsedFont, i: int):
    """A byte-identity key for glyph `i` (metrics + exact bitmap), or None if empty."""
    o, w, h, xa, xo, yo = pf.glyphs[i]
    if w == 0 or h == 0:
        return None
    n = _glyph_len(w, h)
    return (w, h, xa, xo, yo, bytes(pf.bitmaps[o:o + n]))


def _repack_font_without(pf: ParsedFont, empty_idxs: set[int]) -> ParsedFont:
    """Copy of `pf` with the glyphs in `empty_idxs` emptied (width/height 0) and the
    bitmap blob repacked so the removed bytes are reclaimed and every surviving
    glyph's bitmapOffset is recomputed. Font count and [first,last] are unchanged
    (empty glyphs keep their slot), so no gidx / index shifts."""
    new_glyphs: list[tuple[int, int, int, int, int, int]] = []
    blob = bytearray()
    for i, (o, w, h, xa, xo, yo) in enumerate(pf.glyphs):
        if i in empty_idxs:
            new_glyphs.append((len(blob), 0, 0, 0, 0, 0))       # drop the bitmap
        elif w == 0 or h == 0:
            new_glyphs.append((len(blob), w, h, xa, xo, yo))    # already empty
        else:
            new_glyphs.append((len(blob), w, h, xa, xo, yo))
            blob += pf.bitmaps[o:o + _glyph_len(w, h)]
    return ParsedFont(pf.symbol, pf.first, pf.last, pf.yadvance, new_glyphs, bytes(blob))


def _assembly_seq(order: list[str], resident: set[str]) -> list[str]:
    """Symbol order the firmware assembles in: the resident set first, then the pack
    fonts in ALL_FONTS priority (mirrors fontpack_assemble)."""
    return [s for s in order if s in resident] + [s for s in order if s not in resident]


def _assembled(order: list[str], resident: set[str],
               parsed: dict[str, ParsedFont]) -> dict[int, tuple[str, tuple]]:
    """{codepoint: (winning_symbol, glyph_key)} — what the keyboard actually draws."""
    out: dict[int, tuple[str, tuple]] = {}
    for sym in _assembly_seq(order, resident):
        pf = parsed.get(sym)
        if pf is None:
            continue
        for i in range(len(pf.glyphs)):
            k = _glyph_key(pf, i)
            if k is not None:
                out.setdefault(pf.first + i, (sym, k))
    return out


def prune_shadowed_glyphs(order: list[str], resident: set[str],
                          parsed: dict[str, ParsedFont]) -> list[tuple[str, int, int]]:
    """Empty every pack glyph that is byte-identical to, and shadowed by, the font
    that actually renders that codepoint. Mutates `parsed` in place; returns
    ``[(symbol, glyphs_emptied, bytes_reclaimed)]`` sorted by bytes desc.

    A zero-visual-change flash optimisation: only never-drawn duplicates are removed,
    and the assembled render is asserted unchanged."""
    before = _assembled(order, resident, parsed)
    to_empty: dict[str, set[int]] = {}
    for sym in order:
        if sym in resident or sym not in parsed:
            continue
        pf = parsed[sym]
        for i in range(len(pf.glyphs)):
            k = _glyph_key(pf, i)
            if k is None:
                continue
            win = before.get(pf.first + i)
            if win is not None and win[0] != sym and win[1] == k:   # shadow == winner
                to_empty.setdefault(sym, set()).add(i)

    saved: list[tuple[str, int, int]] = []
    for sym, idxs in to_empty.items():
        pf = parsed[sym]
        b = sum(_glyph_len(pf.glyphs[i][1], pf.glyphs[i][2]) for i in idxs)
        parsed[sym] = _repack_font_without(pf, idxs)
        saved.append((sym, len(idxs), b))

    if _assembled(order, resident, parsed) != before:
        raise AssertionError("prune_shadowed_glyphs changed the assembled render — "
                             "refusing to emit a pack that draws differently")
    saved.sort(key=lambda t: -t[2])
    return saved


def extra_pack_fonts(cfg: dict, fonts_dir: Path) -> dict[str, ParsedFont]:
    """Fonts appended to the pack from standalone headers (index.pack_extra_fonts)
    — e.g. the language-layer flags, which are generated outside fonts.yaml."""
    out: dict[str, ParsedFont] = {}
    for e in cfg.get("index", {}).get("pack_extra_fonts", []):
        hdr = fonts_dir / e["header"]
        if hdr.exists():
            out.update(parse_gfx_header(hdr.read_text()))
    return out


def extra_pack_symbols(cfg: dict) -> list[str]:
    return [e["symbol"] for e in cfg.get("index", {}).get("pack_extra_fonts", [])]


def build_pack_from_tree(fonts_dir: Path, cfg: dict, content_version: int = 0):
    """build_pack() sourced from the committed headers on disk."""
    order = all_fonts_order(fonts_dir)
    resident = resident_symbols(cfg, fonts_dir)
    parsed: dict[str, ParsedFont] = {}
    for hdr in (fonts_dir / "generated").glob("*.h"):
        parsed.update(parse_gfx_header(hdr.read_text()))
    parsed.update(extra_pack_fonts(cfg, fonts_dir))
    return build_pack(order, resident, parsed, content_version)


def manifest_from_texts(order: list[str], category_texts: dict[str, str],
                        cfg: dict, fonts_dir: Path, content_version: int = 0,
                        dedupe: bool = True):
    """build_pack() sourced from in-memory header text (for generate_fonts --check).

    `order` is the full font priority order (resident + pack); `category_texts`
    maps category name -> that category's header text. `pack_extra_fonts` (e.g.
    flags) are read from their standalone headers under `fonts_dir`. Mirrors
    build_pack_from_tree so the manifest stays consistent with a freshly
    regenerated header set.
    """
    idx = cfg.get("index", {})
    resident = set(idx.get("prepend_fonts", [])) | set(idx.get("resident_fonts", []))
    parsed: dict[str, ParsedFont] = {}
    cats = cfg["categories"]
    for cat, text in category_texts.items():
        fonts = parse_gfx_header(text)
        parsed.update(fonts)
        if cats.get(cat, {}).get("resident"):
            resident |= set(fonts.keys())
    parsed.update(extra_pack_fonts(cfg, fonts_dir))
    # Prune before measuring, so the manifest describes the pack that is actually
    # built. The bundle path always pruned; this one did not, which is why the
    # committed total_size (post-dedupe) and the emitted one (unpruned) disagreed
    # on every run -- recorded as "already stale" instead of being fixed.
    if dedupe:
        prune_shadowed_glyphs(order, resident, parsed)
    return build_pack(order, resident, parsed, content_version)


# ─────────────────────────── bundles (split pack) ───────────────────────────
#
# The single pack above is also emitted as N independent "bundles" — one PlyF per
# script family — laid out in fixed, sector-aligned slots inside the 2 MB fontpack
# window, behind a small directory. Each bundle is a normal abi-1 PlyF; the only
# extra is that every font record carries its global ALL_FONTS index (the spare
# `reserved` u16), so the firmware merges the fonts of all present bundles back
# into global priority order. Grouping + slot sizes live in fonts.yaml `bundles:`.

SECTOR_SIZE = 0x1000   # 4 KB flash sector — slot offsets/sizes are sector-aligned

# Reserved high gidx band for `pack_extra` fonts (currently just the LangFlags
# pack font in the `flags` bundle). The stored `reserved` u16 is ONLY a sort key
# for the firmware's merge (fontpack_assemble insertion-sorts pack fonts by it);
# a pack_extra font like the flags glyph lives in disjoint PUA (0xE000+) and
# overlaps nothing, so its exact rank never changes a lookup — it just needs to
# sort last. Pinning it into a fixed high band (instead of its dense position at
# the END of ALL_FONTS) means appending a hint font at the tail of fonts.yaml no
# longer shifts the trailing pack_extra's index, so only the EDITED bundle's
# .plyf changes — flags.plyf stays byte-stable and needs no reship/version bump.
# See CLAUDE.md "Font pack" → "gidx is a sort key, not a dense position".
PACK_EXTRA_GIDX_BASE = 0xF000


def compute_bundle_layout(cfg: dict) -> dict:
    """Resolve fonts.yaml `bundles:` into concrete slot offsets/sizes.

    Returns {layout_version, region, dir_offset, dir_size, sector, slots:[{id,
    index, offset, size}]}. Offsets are relative to the fontpack region base
    (FW_RESOURCE_OFFSET). The last slot may use slot_kb: rest to claim the tail."""
    b = cfg["bundles"]
    region = int(b["region_kb"]) * 1024
    dir_size = int(b.get("dir_sectors", 1)) * SECTOR_SIZE
    slots, off = [], dir_size
    lst = b["list"]
    for i, e in enumerate(lst):
        if e.get("slot_kb") == "rest":
            size = region - off
        else:
            size = int(e["slot_kb"]) * 1024
        if size <= 0 or size % SECTOR_SIZE:
            raise ValueError(f"bundle {e['id']!r}: slot size {size} not a positive "
                             f"multiple of {SECTOR_SIZE}")
        slots.append({"id": e["id"], "index": i, "offset": off, "size": size})
        off += size
    if off > region:
        raise ValueError(f"bundle slots ({off} B) exceed the {region} B fontpack region")
    return {"layout_version": int(b["layout_version"]), "region": region,
            "dir_offset": 0, "dir_size": dir_size, "sector": SECTOR_SIZE, "slots": slots}


def symbol_categories_from_texts(category_texts: dict[str, str]) -> dict[str, str]:
    """{font symbol -> category name} parsed from per-category header text."""
    out: dict[str, str] = {}
    for cat, text in category_texts.items():
        for sym in parse_gfx_header(text):
            out[sym] = cat
    return out


def symbol_categories_from_tree(fonts_dir: Path, cfg: dict) -> dict[str, str]:
    gen = fonts_dir / "generated"
    texts = {cat: (gen / meta["output"]).read_text()
             for cat, meta in cfg["categories"].items()
             if (gen / meta["output"]).exists()}
    return symbol_categories_from_texts(texts)


def build_bundles(order: list[str], resident: set[str], parsed: dict[str, ParsedFont],
                  sym2cat: dict[str, str], cfg: dict,
                  content_versions: dict[str, int] | None = None):
    """Split the pack into per-family bundles.

    Returns (bundles, layout) where each bundle is a dict {id, index, data,
    content_version, slot, fonts}. A font joins a bundle by its `pack_extra`
    symbol (flags) or by its category's bundle membership; every non-resident
    ALL_FONTS entry must map to exactly one bundle, and each bundle must fit its
    reserved slot — both are hard errors (the build-time overflow guard)."""
    layout = compute_bundle_layout(cfg)
    lst = cfg["bundles"]["list"]
    cat2bi, sym2bi = {}, {}
    for i, e in enumerate(lst):
        for c in e.get("categories", []):
            cat2bi[c] = i
        for s in e.get("pack_extra", []):
            sym2bi[s] = i

    members: list[list[tuple[int, ParsedFont]]] = [[] for _ in lst]
    extra_n = 0
    for gi, sym in enumerate(order):
        if sym in resident:
            continue
        if sym not in parsed:
            raise ValueError(f"ALL_FONTS entry {sym!r} not found in generated headers")
        if sym in sym2bi:
            bi = sym2bi[sym]
            # pack_extra (trailing, disjoint) → pinned high band, not dense `gi`,
            # so tail appends don't shift it (see PACK_EXTRA_GIDX_BASE above).
            stored = PACK_EXTRA_GIDX_BASE + extra_n
            # `reserved`/gidx is a u16; fail the build loudly rather than wrap if the
            # reserved band ever fills (and keep it clear of a real font's dense gi).
            if stored > 0xFFFF:
                raise ValueError(
                    f"pack_extra gidx overflow: {stored:#06x} > 0xFFFF — too many "
                    f"pack_extra fonts for the band at {PACK_EXTRA_GIDX_BASE:#06x}")
            extra_n += 1
        else:
            cat = sym2cat.get(sym)
            if cat not in cat2bi:
                raise ValueError(f"font {sym!r} (category {cat!r}) maps to no bundle "
                                 f"— add its category to a fonts.yaml bundle")
            bi = cat2bi[cat]
            stored = gi
        members[bi].append((stored, parsed[sym]))

    cvers = content_versions or {}
    bundles = []
    for i, e in enumerate(lst):
        mem = members[i]
        for _, pf in mem:
            if len(pf.glyphs) != pf.expected_glyph_count():
                raise ValueError(
                    f"{pf.symbol}: {len(pf.glyphs)} glyphs != last-first+1="
                    f"{pf.expected_glyph_count()}")
        cver = int(cvers.get(e["id"], 0))
        data = serialize_pack([pf for _, pf in mem], content_version=cver,
                              global_indices=[gi for gi, _ in mem])
        slot = layout["slots"][i]
        if len(data) > slot["size"]:
            raise ValueError(f"bundle {e['id']!r}: {len(data)} B exceeds its "
                             f"{slot['size']} B slot — enlarge slot_kb in fonts.yaml")
        bundles.append({
            "id": e["id"], "index": i, "data": data, "content_version": cver, "slot": slot,
            "fonts": [{"all_fonts_index": gi, "symbol": pf.symbol,
                       "first": pf.first, "last": pf.last} for gi, pf in mem]})
    return bundles, layout


def bundles_manifest_json(bundles: list[dict], layout: dict) -> str:
    """Deterministic per-bundle ABI contract (no volatile content_version)."""
    doc = {"abi_version": ABI_VERSION, "layout_version": layout["layout_version"],
           "region_size": layout["region"], "dir_offset": layout["dir_offset"],
           "dir_size": layout["dir_size"], "bundle_count": len(bundles),
           "bundles": [{"id": b["id"], "index": b["index"],
                        "slot_offset": b["slot"]["offset"], "slot_size": b["slot"]["size"],
                        "total_size": len(b["data"]), "font_count": len(b["fonts"]),
                        "fonts": b["fonts"]} for b in bundles]}
    return json.dumps(doc, indent=2) + "\n"


def bundle_layout_header(bundles: list[dict], layout: dict) -> str:
    """C header shared by firmware + host: region, directory, per-bundle slots."""
    lines = [
        "// AUTO-GENERATED by fonts/generate_fonts.py — do not edit.",
        "// Font-pack bundle flash layout (slots are relative to FW_RESOURCE_OFFSET).",
        "#pragma once", "",
        f"#define FONTPACK_LAYOUT_VERSION {layout['layout_version']}u",
        f"#define FONTPACK_REGION_SIZE    0x{layout['region']:X}UL",
        f"#define FONTPACK_DIR_OFFSET     0x{layout['dir_offset']:X}UL",
        f"#define FONTPACK_DIR_SIZE       0x{layout['dir_size']:X}UL",
        f"#define FONTPACK_SECTOR_SIZE    0x{layout['sector']:X}UL",
        f"#define FONTPACK_BUNDLE_COUNT   {len(bundles)}u", "",
        "// X(id, index, slot_offset, slot_size)",
        "#define FONTPACK_BUNDLE_LIST \\"]
    for b in bundles:
        s = b["slot"]
        lines.append(f"    X({b['id']}, {b['index']}, 0x{s['offset']:X}UL, "
                     f"0x{s['size']:X}UL) \\")
    lines[-1] = lines[-1].rstrip(" \\")     # drop trailing continuation
    lines.append("")
    return "\n".join(lines) + "\n"


def manifest_json(data: bytes, manifest: list[dict]) -> str:
    """Deterministic manifest JSON (no volatile content_version).

    The manifest is the committed ABI contract: it pins the pack's font order and
    each font's codepoint range + structural sizes, so a checkout can be verified
    without rebuilding from TTFs, and step-3 firmware can derive its index map.
    """
    doc = {"abi_version": ABI_VERSION, "font_count": len(manifest),
           "total_size": len(data), "fonts": manifest}
    return json.dumps(doc, indent=2) + "\n"


def canonical_manifest(fonts_dir: Path, cfg: dict) -> tuple[str, bytes]:
    data, manifest = build_pack_from_tree(fonts_dir, cfg, content_version=0)
    return manifest_json(data, manifest), data


def write_pack(fonts_dir: Path, cfg: dict, out_bin: Path | None, out_manifest: Path,
               content_version: int = 0) -> tuple[int, int]:
    text, data = canonical_manifest(fonts_dir, cfg)
    out_manifest.write_text(text)
    if out_bin is not None:
        # content_version only lives in the binary header, never the manifest.
        body, _ = build_pack_from_tree(fonts_dir, cfg, content_version)
        out_bin.write_bytes(body)
    return len(data), json.loads(text)["font_count"]


# ─────────────────────────── CLI ────────────────────────────────────────────

def _human(n: int) -> str:
    return f"{n:,} B (~{n // 1024} KB)"


def _cmd_build(args) -> int:
    fonts_dir = Path(args.fonts_dir)
    cfg_path = Path(args.config) if args.config else Path(__file__).resolve().parent / "fonts.yaml"
    cfg = _load_yaml(cfg_path)
    out_bin = Path(args.out)
    out_manifest = Path(args.manifest) if args.manifest else out_bin.with_suffix(".manifest.json")
    size, count = write_pack(fonts_dir, cfg, out_bin, out_manifest, args.content_version)
    print(f"wrote {out_bin}  ({count} fonts, {_human(size)})")
    print(f"wrote {out_manifest}")
    return 0


def _cmd_validate(args) -> int:
    data = Path(args.pack).read_bytes()
    manifest = None
    if args.manifest:
        manifest = json.loads(Path(args.manifest).read_text())["fonts"]
    problems = validate_pack(data, expected_manifest=manifest)
    if problems:
        print(f"INVALID: {args.pack}")
        for p in problems:
            print(f"  - {p}")
        return 1
    pack = parse_pack(data)
    print(f"OK: {args.pack}  abi={pack.abi_version} content={pack.content_version} "
          f"fonts={pack.font_count} size={_human(pack.total_size)} crc=ok")
    return 0


def _cmd_dump(args) -> int:
    pack = parse_pack(Path(args.pack).read_bytes())
    print(f"abi={pack.abi_version} content={pack.content_version} "
          f"fonts={pack.font_count} total={_human(pack.total_size)} "
          f"crc={'ok' if pack.crc_ok else 'BAD'}")
    for i, f in enumerate(pack.fonts):
        gcount = f.last - f.first + 1
        print(f"  [{i:>3}] 0x{f.first:05X}-0x{f.last:05X} ({gcount:>4} glyphs) "
              f"yAdv={f.yadvance:>3} glyph@{f.glyph_off} bitmap@{f.bitmap_off}")
    return 0


def _cmd_selftest(args) -> int:
    # Round-trip: parse a synthetic header, serialize, parse back, compare.
    hdr = """
    const uint8_t Foo_Bitmaps[] PROGMEM = { 0x12, 0x34, 0xFF, 0x00 };
    const GFXglyph Foo_Glyphs[] PROGMEM = {
      {   0,  3,  4,  5,  -1,  -7 },   // a
      {   2,  6,  8,  9,   0,  -3 } }; // b
    const GFXfont Foo PROGMEM = {
      (uint8_t  *)Foo_Bitmaps, (GFXglyph *)Foo_Glyphs, 0x41, 0x42, 12 };
    """
    fonts = parse_gfx_header(hdr)
    assert set(fonts) == {"Foo"}, fonts
    f = fonts["Foo"]
    assert (f.first, f.last, f.yadvance) == (0x41, 0x42, 12)
    assert f.glyphs[0] == (0, 3, 4, 5, -1, -7)
    assert f.bitmaps == bytes([0x12, 0x34, 0xFF, 0x00])
    data = serialize_pack([f], content_version=7)
    problems = validate_pack(data)
    assert not problems, problems
    pack = parse_pack(data)
    assert pack.content_version == 7 and pack.font_count == 1 and pack.crc_ok
    pf = pack.fonts[0]
    assert (pf.first, pf.last, pf.yadvance) == (0x41, 0x42, 12)
    # bit-flip → crc must fail
    bad = bytearray(data); bad[-1] ^= 0xFF
    assert not parse_pack(bytes(bad)).crc_ok

    # dedupe: a lower-priority font that draws 0x41 byte-identically to the winner
    # gets that glyph emptied (its bytes reclaimed); its unique 0x42 stays; the
    # winner and the assembled render are untouched.
    A = ParsedFont("A", 0x41, 0x41, 12, [(0, 3, 4, 5, 0, 0)], b"\xAB\xC0")
    B = ParsedFont("B", 0x41, 0x42, 12,
                   [(0, 3, 4, 5, 0, 0), (2, 2, 2, 3, 0, 0)], b"\xAB\xC0\xF0")
    parsed = {"A": A, "B": B}
    before = _assembled(["A", "B"], set(), parsed)
    saved = prune_shadowed_glyphs(["A", "B"], set(), parsed)
    assert saved == [("B", 1, 2)], saved
    assert parsed["A"].bitmaps == b"\xAB\xC0"                 # winner untouched
    assert parsed["B"].glyphs[0][1:3] == (0, 0)              # 0x41 emptied
    assert parsed["B"].glyphs[1][1:3] == (2, 2)              # 0x42 kept
    assert parsed["B"].bitmaps == b"\xF0"                    # only 0x42's bytes remain
    assert _assembled(["A", "B"], set(), parsed) == before   # render unchanged
    print("selftest OK")
    return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="PolyKybd PlyF font-pack tool")
    sub = ap.add_subparsers(dest="cmd", required=True)

    here = Path(__file__).resolve().parent
    default_fonts_dir = here.parent / "base" / "fonts"

    b = sub.add_parser("build", help="build a pack from the committed headers")
    b.add_argument("--fonts-dir", default=str(default_fonts_dir))
    b.add_argument("--config", default="")
    b.add_argument("--out", required=True)
    b.add_argument("--manifest", default="")
    b.add_argument("--content-version", type=int, default=0)
    b.set_defaults(func=_cmd_build)

    v = sub.add_parser("validate", help="validate a pack binary (offline validator)")
    v.add_argument("pack")
    v.add_argument("--manifest", default="", help="cross-check font order/ranges")
    v.set_defaults(func=_cmd_validate)

    d = sub.add_parser("dump", help="list the fonts in a pack")
    d.add_argument("pack")
    d.set_defaults(func=_cmd_dump)

    s = sub.add_parser("selftest", help="round-trip self test (no inputs needed)")
    s.set_defaults(func=_cmd_selftest)

    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except BrokenPipeError:                 # piping `dump` into head/less
        sys.exit(0)
