#!/usr/bin/env python3
"""PolyKybd emoji search dictionary: extract, build, dump and query.

The emoji layer finds an emoji by keyword ("grin" -> grinning faces). The keyword
index is a data-only font-pack bundle (`emojidict`, flashed from the host like the
glyph bundles), so a localized dictionary can replace the default English one
without a firmware change. The firmware lookup mirrors `Dict.search()` below.

    extract  CLDR annotations XML -> emoji/dict_src/<lang>.tsv  (committed)
    build    dict_src/<lang>.tsv   -> <lang>.plyd               (the bundle)
    dump     size breakdown of a .plyd
    query    run the reference search on a .plyd

Only codepoints of the emoji picker (emoji_data.h) are indexed, since a search
result must be a key the picker can send. A localized dictionary uses the English
text for every codepoint its CLDR file does not cover (dingbats, superscripts).

Bundle layout (little-endian, every block 4-byte aligned)::

    PlyF header, 32 B   magic "PlyF", abi 2, flags FLAG_DATA, content_version =
                        dictionary revision, font_count 0, font_table_off 32,
                        total_size, crc32 over everything after the header
    dict header, 24 B   "EmjD", format u16, lang u16 (LANG_CODES index),
                        cp_count u16, word_count u16, cps_off u32,
                        words_off u32, strings_off u32  (offsets from bundle start)
    cps                 u32[cp_count]: the codepoints, in picker order
    words               u16[2 * (word_count + 1)]: per word {str_off, first posting}
    postings            u16[]: per word, the cps[] indices it names
    strings             the words, folded (see fold()), NUL-terminated, sorted

A word record is two u16: the string offset (from strings_off) and the index of
its first posting; word i's postings end where word i+1's begin (a sentinel
record closes the table). Bit 15 of a posting marks a word that appears in the
emoji's NAME (the CLDR "tts" text), which ranks above a word that is only a
keyword. The low 12 bits are the cps[] index.
"""
from __future__ import annotations

import argparse
import re
import struct
import sys
import unicodedata
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
POLY = HERE.parent
sys.path.insert(0, str(POLY / "lang"))
import iso_lang_country  # noqa: E402  (frozen, append-only index table)

CLDR_RELEASE = "release-48"
SRC_DIR = HERE / "dict_src"

PLYF_MAGIC = b"PlyF"
PLYF_ABI = 2
PLYF_HEADER_FMT = "<4sHHIIIIII"
PLYF_HEADER_SIZE = 32
FLAG_DATA = 0x0001            # data-only bundle: no fonts, payload after the header

DICT_MAGIC = b"EmjD"
DICT_FORMAT = 1
DICT_HEADER_FMT = "<4sHHHHIII"
DICT_HEADER_SIZE = 24

POST_NAME_BIT = 0x8000
POST_INDEX_MASK = 0x0FFF      # 4096 picker codepoints at most

SLOT_SIZE = 0x20000           # 128 KB emojidict slot

STOPWORDS = {
    "en": {"a", "an", "and", "at", "for", "in", "of", "on", "or", "the", "to", "with"},
    "de": {"am", "an", "auf", "das", "dem", "den", "der", "die", "ein", "eine", "einem",
           "einen", "einer", "im", "in", "mit", "mit", "oder", "und", "von", "vom", "zum", "zur"},
}

_FOLD_EXTRA = {"ß": "ss", "æ": "ae", "ø": "o", "œ": "oe", "ł": "l", "đ": "d", "þ": "th"}


def fold(text: str) -> str:
    """Lowercase and remove Latin, Greek and Cyrillic diacritics, so "Lächeln" and
    "lacheln" are one word.
    The firmware folds the typed query the same way before it searches."""
    text = text.lower()
    text = "".join(_FOLD_EXTRA.get(ch, ch) for ch in text)
    text = unicodedata.normalize("NFKD", text)
    out = []
    for ch in text:
        # A mark is a diacritic only on a Latin, Greek or Cyrillic letter. In the
        # Indic scripts the same Unicode class holds the vowel signs, which are
        # part of the word.
        if unicodedata.combining(ch) and out and ord(out[-1]) < 0x0530:
            continue
        out.append(ch)
    return unicodedata.normalize("NFC", "".join(out))


def tokens(text: str, lang: str) -> list[str]:
    stop = STOPWORDS.get(lang, set())
    out = []
    for tok in re.split(r"[^\w]+", fold(text)):
        tok = tok.strip("_")
        if len(tok) < 2 or tok in stop or tok.isdigit():
            continue
        out.append(tok)
    return out


def picker_codepoints() -> list[int]:
    """The emoji picker's codepoints, in category order, without duplicates."""
    src = (HERE / "emoji_data.h").read_text(encoding="utf-8")
    src = re.sub(r"/\*.*?\*/|//[^\n]*", "", src, flags=re.S)
    seen, out = set(), []
    for block in re.findall(r"static const uint32_t emj_cat\d+\[\]\s*=\s*\{(.*?)\};", src, re.S):
        for hx in re.findall(r"0x([0-9A-Fa-f]+)", block):
            cp = int(hx, 16)
            if cp not in seen:
                seen.add(cp)
                out.append(cp)
    return out


# ───────────────────────────── extract ─────────────────────────────

def _read_cldr(path: Path) -> dict[str, tuple[str, list[str]]]:
    out: dict[str, tuple[str, list[str]]] = {}
    for el in ET.parse(path).getroot().iter("annotation"):
        cp = el.get("cp")
        text = (el.text or "").strip()
        if not cp or not text or text == "↑↑↑":
            continue
        name, kws = out.get(cp, ("", []))
        if el.get("type") == "tts":
            name = text
        else:
            kws = [k.strip() for k in text.split("|") if k.strip()]
        out[cp] = (name, kws)
    return out


def cmd_extract(args: argparse.Namespace) -> int:
    lang = args.lang
    data: dict[str, tuple[str, list[str]]] = {}
    for xml in args.xml:                      # later files fill gaps only
        for cp, (name, kws) in _read_cldr(Path(xml)).items():
            old_name, old_kws = data.get(cp, ("", []))
            data[cp] = (old_name or name, old_kws or kws)
    rows, missing = [], []
    for cp in picker_codepoints():
        ch = chr(cp)
        name, kws = data.get(ch) or data.get(ch + "️") or ("", [])
        if not name and lang == "en":         # Unicode name for what CLDR lacks
            name = unicodedata.name(ch, "").lower()
        if not name and not kws:
            missing.append(cp)
            continue
        rows.append(f"{cp:X}\t{name}\t{' | '.join(kws)}")
    SRC_DIR.mkdir(exist_ok=True)
    out = SRC_DIR / f"{lang}.tsv"
    header = (f"# emoji search keywords, {lang}, from Unicode CLDR {CLDR_RELEASE} "
              f"(common/annotations, common/annotationsDerived). Unicode License v3, see LICENSE.\n"
              f"# codepoint\tname\tkeywords\n")
    out.write_text(header + "\n".join(rows) + "\n", encoding="utf-8")
    print(f"{out}: {len(rows)} codepoints, {len(missing)} without any text")
    if missing and args.verbose:
        print(" ".join(f"U+{c:04X}" for c in missing))
    return 0


# ───────────────────────────── build ─────────────────────────────

def _align4(buf: bytearray) -> None:
    buf.extend(b"\0" * (-len(buf) % 4))


def _read_tsv(path: Path) -> dict[int, tuple[str, list[str]]]:
    rows = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        hx, name, kws = (line.split("\t") + ["", ""])[:3]
        rows[int(hx, 16)] = (name, [k.strip() for k in kws.split("|") if k.strip()])
    return rows


def build(lang: str, revision: int) -> bytes:
    src = SRC_DIR / f"{lang}.tsv"
    picker = picker_codepoints()
    order = {cp: i for i, cp in enumerate(picker)}
    rows = _read_tsv(src)
    if lang != "en":                       # English text for what the language lacks
        for cp, row in _read_tsv(SRC_DIR / "en.tsv").items():
            rows.setdefault(cp, row)
    entries = sorted(((cp, name, kws) for cp, (name, kws) in rows.items() if cp in order),
                     key=lambda e: order[e[0]])
    cps = [e[0] for e in entries]
    if len(cps) > POST_INDEX_MASK + 1:
        raise SystemExit(f"{len(cps)} codepoints do not fit the 12-bit posting index")

    posts: dict[str, dict[int, int]] = {}
    for idx, (_cp, name, kws) in enumerate(entries):
        name_toks = set(tokens(name, lang))
        for tok in name_toks | {t for k in kws for t in tokens(k, lang)}:
            flag = POST_NAME_BIT if tok in name_toks else 0
            posts.setdefault(tok, {})[idx] = posts.get(tok, {}).get(idx, 0) | flag

    words = sorted(posts, key=lambda w: w.encode("utf-8"))
    strings = bytearray()
    str_offs = []
    for w in words:
        str_offs.append(len(strings))
        strings += w.encode("utf-8") + b"\0"
    postings: list[int] = []
    post_starts = []
    for w in words:
        post_starts.append(len(postings))
        # name hits first, then picker order: the order the search shows them in
        for idx, flag in sorted(posts[w].items(), key=lambda kv: (not kv[1], kv[0])):
            postings.append(idx | flag)
    post_starts.append(len(postings))
    str_offs.append(len(strings))

    if len(strings) > 0xFFFF or len(postings) > 0xFFFF:
        raise SystemExit(f"{lang}: {len(strings)} B of words or {len(postings)} postings "
                         "do not fit the 16-bit word record")
    lang_idx = iso_lang_country.LANG_CODES.index(lang)
    body = bytearray(DICT_HEADER_SIZE)
    cps_off = PLYF_HEADER_SIZE + len(body)
    body += struct.pack(f"<{len(cps)}I", *cps)
    _align4(body)
    words_off = PLYF_HEADER_SIZE + len(body)
    for so, ps in zip(str_offs, post_starts):
        body += struct.pack("<HH", so, ps)
    body += struct.pack(f"<{len(postings)}H", *postings)
    _align4(body)
    strings_off = PLYF_HEADER_SIZE + len(body)
    body += strings
    _align4(body)
    body[:DICT_HEADER_SIZE] = struct.pack(DICT_HEADER_FMT, DICT_MAGIC, DICT_FORMAT, lang_idx,
                                          len(cps), len(words), cps_off, words_off, strings_off)
    total = PLYF_HEADER_SIZE + len(body)
    if total > SLOT_SIZE:
        raise SystemExit(f"{lang}: {total} B does not fit the {SLOT_SIZE} B emojidict slot")
    header = struct.pack(PLYF_HEADER_FMT, PLYF_MAGIC, PLYF_ABI, FLAG_DATA, revision, 0,
                         PLYF_HEADER_SIZE, total, zlib.crc32(bytes(body)) & 0xFFFFFFFF, 0)
    return header + bytes(body)


def cmd_build(args: argparse.Namespace) -> int:
    blob = build(args.lang, args.revision)
    out = Path(args.out or f"{args.lang}.plyd")
    out.write_bytes(blob)
    print(f"{out}: {len(blob)} B ({len(blob) * 100 // SLOT_SIZE}% of the 128 KB slot)")
    return 0


# ───────────────────────────── read + search ─────────────────────────────

class Dict:
    """Reads a .plyd and runs the search the firmware runs."""

    def __init__(self, blob: bytes):
        (magic, abi, flags, self.revision, font_count, table_off, total, crc,
         _r) = struct.unpack_from(PLYF_HEADER_FMT, blob, 0)
        if magic != PLYF_MAGIC or abi != PLYF_ABI or not flags & FLAG_DATA or font_count:
            raise ValueError("not a data-only PlyF bundle")
        if total != len(blob) or zlib.crc32(blob[PLYF_HEADER_SIZE:]) & 0xFFFFFFFF != crc:
            raise ValueError("size or CRC mismatch")
        (dm, fmt, self.lang_idx, ncp, nword, cps_off, words_off,
         strings_off) = struct.unpack_from(DICT_HEADER_FMT, blob, PLYF_HEADER_SIZE)
        if dm != DICT_MAGIC or fmt != DICT_FORMAT:
            raise ValueError("not an EmjD dictionary")
        self.lang = iso_lang_country.LANG_CODES[self.lang_idx]
        self.cps = struct.unpack_from(f"<{ncp}I", blob, cps_off)
        recs = struct.unpack_from(f"<{2 * (nword + 1)}H", blob, words_off)
        post_off = words_off + 4 * (nword + 1)
        self.sizes = {"header": PLYF_HEADER_SIZE + DICT_HEADER_SIZE, "cps": 4 * ncp,
                      "words": 4 * (nword + 1), "postings": 2 * recs[-1],
                      "strings": len(blob) - strings_off, "total": len(blob)}
        self.words, self.posts = [], []
        for i in range(nword):
            so, ps = recs[2 * i], recs[2 * i + 1]
            end = blob.index(b"\0", strings_off + so)
            self.words.append(blob[strings_off + so:end])
            self.posts.append(struct.unpack_from(f"<{recs[2 * i + 3] - ps}H", blob, post_off + 2 * ps))

    def _lower_bound(self, key: bytes) -> int:
        lo, hi = 0, len(self.words)
        while lo < hi:
            mid = (lo + hi) // 2
            if self.words[mid] < key:
                lo = mid + 1
            else:
                hi = mid
        return lo

    def _prefix_words(self, key: bytes) -> list[int]:
        i, out = self._lower_bound(key), []
        while i < len(self.words) and self.words[i].startswith(key):
            out.append(i)
            i += 1
        return out

    def _typo_words(self, key: bytes) -> list[int]:
        """Words within one edit (insert, delete, substitute, swap) of the query,
        compared against the word's first len(query)+1 bytes so a typo inside a
        prefix still matches ("grni" -> "grin...")."""
        out = []
        for i, w in enumerate(self.words):
            if _within_one(key, w[:len(key)]) or _within_one(key, w[:len(key) + 1]):
                out.append(i)
        return out

    def search(self, query: str) -> tuple[list[int], bool]:
        """Codepoints for `query`, best first, and whether the typo pass ran.
        Each query token must match (AND); a token matches a word it is a prefix of.
        Rank: exact word in the name, exact keyword, prefix in the name, prefix
        keyword; picker order inside each rank. The firmware produces the same
        order in four passes over the postings with one 'already shown' bitmap."""
        toks = [t.encode("utf-8") for t in tokens(query, self.lang)] or [fold(query).encode()]
        typo = False
        per_tok = []
        for t in toks:
            hits = self._prefix_words(t)
            if not hits:
                hits, typo = self._typo_words(t), True
            per_tok.append((t, hits))
        allowed = None
        for _t, hits in per_tok[1:]:
            s = {p & POST_INDEX_MASK for wi in hits for p in self.posts[wi]}
            allowed = s if allowed is None else allowed & s
        first, hits = per_tok[0]
        best: dict[int, int] = {}
        for wi in hits:
            exact = self.words[wi] == first
            for p in self.posts[wi]:
                idx = p & POST_INDEX_MASK
                if allowed is not None and idx not in allowed:
                    continue
                rank = (0 if exact else 2) + (0 if p & POST_NAME_BIT else 1)
                best[idx] = min(best.get(idx, 9), rank)
        ordered = sorted(best, key=lambda i: (best[i], i))
        return [self.cps[i] for i in ordered], typo


def _within_one(a: bytes, b: bytes) -> bool:
    if a == b:
        return True
    la, lb = len(a), len(b)
    if abs(la - lb) > 1:
        return False
    i = 0
    while i < min(la, lb) and a[i] == b[i]:
        i += 1
    if la == lb:
        if a[i + 1:] == b[i + 1:]:
            return True                                   # substitution
        return i + 1 < la and a[i] == b[i + 1] and a[i + 1] == b[i] and a[i + 2:] == b[i + 2:]
    return a[i + 1:] == b[i:] if la > lb else a[i:] == b[i + 1:]


def cmd_dump(args: argparse.Namespace) -> int:
    d = Dict(Path(args.file).read_bytes())
    print(f"lang {d.lang} (index {d.lang_idx}), revision {d.revision}, "
          f"{len(d.cps)} codepoints, {len(d.words)} words")
    for k, v in d.sizes.items():
        print(f"  {k:9} {v:7} B")
    print(f"  slot use {d.sizes['total'] * 100 / SLOT_SIZE:.0f}% of {SLOT_SIZE // 1024} KB")
    return 0


def cmd_query(args: argparse.Namespace) -> int:
    d = Dict(Path(args.file).read_bytes())
    for q in args.query:
        cps, typo = d.search(q)
        shown = "".join(chr(c) for c in cps[:args.limit])
        more = f" (+{len(cps) - args.limit})" if len(cps) > args.limit else ""
        print(f"{q!r:14} {len(cps):3}{' typo' if typo else '     '}  {shown}{more}")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("extract", help="CLDR XML -> dict_src/<lang>.tsv")
    p.add_argument("lang")
    p.add_argument("xml", nargs="+", help="annotations/<lang>.xml, then annotationsDerived/<lang>.xml")
    p.add_argument("-v", "--verbose", action="store_true")
    p.set_defaults(fn=cmd_extract)
    p = sub.add_parser("build", help="dict_src/<lang>.tsv -> .plyd")
    p.add_argument("lang")
    p.add_argument("--revision", type=int, default=1)
    p.add_argument("-o", "--out")
    p.set_defaults(fn=cmd_build)
    p = sub.add_parser("dump", help="size breakdown of a .plyd")
    p.add_argument("file")
    p.set_defaults(fn=cmd_dump)
    p = sub.add_parser("query", help="run the reference search")
    p.add_argument("file")
    p.add_argument("query", nargs="+")
    p.add_argument("--limit", type=int, default=24)
    p.set_defaults(fn=cmd_query)
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
