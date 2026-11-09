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
text for every codepoint its CLDR file does not cover (dingbats, superscripts), and
also indexes every codepoint's English words as keywords, so "smile" works in a
German dictionary; the language's own name words still rank first.

Bundle layout (little-endian, every block 4-byte aligned)::

    PlyF header, 32 B   magic "PlyF", abi 2, flags FLAG_DATA, content_version =
                        dictionary revision, font_count 0, font_table_off 32,
                        total_size, crc32 over everything after the header
    dict header, 32 B   "EmjD", format u16, lang u16 (LANG_CODES index),
                        cp_count u16, word_count u16, cps_off u32,
                        words_off u32, strings_off u32, alpha_count u16,
                        fold_count u16, alpha_off u32, fold_off u32,
                        stop_count u16, variant u16 (see variant_code()), stop_off u32
                        (offsets from bundle start)
    alphabet            u32[alpha_count]: the letter for byte code 1, 2, ...
    fold table          u32[fold_count]: (code << 24) | codepoint, sorted by
                        codepoint: every letter a user can type, upper and lower
                        case, and the byte code of its base letter
    stop words          stop_count coded words, each NUL-terminated: the query
                        drops these, as the index never contains them
    cps                 u32[cp_count]: the codepoints, in picker order
    words               u16[2 * (word_count + 1)]: per word {str_off, first posting}
    postings            u16[]: per word, the cps[] indices it names
    strings             the words, folded (see fold()), 6 bits per letter
                        (pack_word()), each ended by code 0 and padded to a byte

Words are stored one byte per letter in every script. The alphabet lists the
letters the dictionary uses, in codepoint order; letter k is stored as byte k+1,
and 0 ends a word. Russian needs 32 codes plus the Latin letters of the English
fallback text. The firmware maps each typed letter through the fold table with a
binary search, so Ä, ä and a give the same byte and the keyboard needs no Unicode
data of its own. A letter the table does not list becomes 0xFF, which no word
contains, so it matches only through the typo pass. Byte order equals codepoint order, so the
word table stays sorted for the binary search.

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
import emoji_romanize    # noqa: E402  (keywords as the keys that type them)

CLDR_RELEASE = "release-48"
SRC_DIR = HERE / "dict_src"

PLYF_MAGIC = b"PlyF"
PLYF_ABI = 2
PLYF_HEADER_FMT = "<4sHHIIIIII"
PLYF_HEADER_SIZE = 32
FLAG_DATA = 0x0001            # data-only bundle: no fonts, payload after the header

DICT_MAGIC = b"EmjD"
DICT_FORMAT = 2                # 2: 6-bit letter codes, typeable words only
DICT_HEADER_FMT = "<4sHHHHIIIHHIIHHI"
DICT_HEADER_SIZE = 44
ALPHA_MAX = 63                # 6-bit codes 1..63; 0 ends a word; 0xFF = typed letter not in the alphabet
LETTER_BITS = 6
WORD_MAX = 64                 # letters in one word (EMJDICT_WORD_MAX); longer words are dropped
CODE_UNKNOWN = 0xFF

POST_NAME_BIT = 0x8000
POST_INDEX_MASK = 0x0FFF      # 4096 picker codepoints at most

SLOT_SIZE = 0x20000           # 128 KB emojidict slot
# A localized dictionary indexes every English name word, and the English
# keywords that name at least this many emoji ("smile", "happy", "love"); the
# rarer keywords would not fit the slot for every language.
EN_KEYWORD_MIN_EMOJI = 3
QUERY_MAX = 16                # typed characters the firmware keeps (EMJDICT_QUERY_MAX)
MAX_WORDS = 4                 # query words the firmware searches (EMJDICT_MAX_WORDS)

STOPWORDS = {
    "en": {"a", "an", "and", "at", "for", "in", "of", "on", "or", "the", "to", "with"},
    "de": {"am", "an", "auf", "das", "dem", "den", "der", "die", "ein", "eine", "einem",
           "einen", "einer", "im", "in", "mit", "mit", "oder", "und", "von", "vom", "zum", "zur"},
}

# Letters with no Unicode decomposition, and their base letter.
# Hangul: a syllable is typed as its jamo on the 2-set layout (ko-KR's letter keys
# show ㅂ ㅈ ㄷ ...). A compound vowel or final is two keys; a Shift double (ㄲ ㅃ
# ㅒ ㅖ) folds to its base key, like every other letter folds to its base letter.
_HANGUL_L = "ㄱㄲㄴㄷㄸㄹㅁㅂㅃㅅㅆㅇㅈㅉㅊㅋㅌㅍㅎ"
_HANGUL_V = ["ㅏ", "ㅐ", "ㅑ", "ㅒ", "ㅓ", "ㅔ", "ㅕ", "ㅖ", "ㅗ", "ㅗㅏ", "ㅗㅐ", "ㅗㅣ", "ㅛ", "ㅜ",
             "ㅜㅓ", "ㅜㅔ", "ㅜㅣ", "ㅠ", "ㅡ", "ㅡㅣ", "ㅣ"]
_HANGUL_T = ["", "ㄱ", "ㄲ", "ㄱㅅ", "ㄴ", "ㄴㅈ", "ㄴㅎ", "ㄷ", "ㄹ", "ㄹㄱ", "ㄹㅁ", "ㄹㅂ", "ㄹㅅ", "ㄹㅌ",
             "ㄹㅍ", "ㄹㅎ", "ㅁ", "ㅂ", "ㅂㅅ", "ㅅ", "ㅆ", "ㅇ", "ㅈ", "ㅊ", "ㅋ", "ㅌ", "ㅍ", "ㅎ"]
# The ko-KR keys show CONJOINING jamo (lang_lut.c: HANGEUL_MIEUM is U+1106): the
# initial form for every consonant, the medial form for every vowel. A word is
# stored in those, so a typed key matches it.
_TO_CONJOINING = dict(zip(_HANGUL_L, (chr(0x1100 + i) for i in range(19))))
_TO_CONJOINING.update(zip("ㅏㅐㅑㅒㅓㅔㅕㅖㅗㅘㅙㅚㅛㅜㅝㅞㅟㅠㅡㅢㅣ", (chr(0x1161 + i) for i in range(21))))


def hangul_keys(text: str) -> str:
    """Hangul syllables as the jamo keys that type them, in the conjoining forms
    the ko-KR keys show: 한 -> 하ᄂ (ㅎ ㅏ ㄴ)."""
    out = []
    for ch in text:
        o = ord(ch) - 0xAC00
        if 0 <= o < 11172:
            jamo = _HANGUL_L[o // 588] + _HANGUL_V[(o % 588) // 28] + _HANGUL_T[o % 28]
            out.append("".join(_TO_CONJOINING[j] for j in jamo))
        else:
            out.append(ch)
    return "".join(out)


_FOLD_BASE = {"ß": "s", "æ": "a", "œ": "o", "ø": "o", "ł": "l", "đ": "d", "ð": "d",
              "þ": "t", "ı": "i", "ħ": "h", "ŧ": "t", "ŋ": "n", "ĸ": "k", "ſ": "s",
              "ς": "σ",
              "ㄲ": "ㄱ", "ㄸ": "ㄷ", "ㅃ": "ㅂ", "ㅆ": "ㅅ", "ㅉ": "ㅈ", "ㅒ": "ㅐ", "ㅖ": "ㅔ",
              "\u1101": "\u1100", "\u1104": "\u1103", "\u1108": "\u1107", "\u110A": "\u1109",
              "\u110D": "\u110C", "\u1164": "\u1162", "\u1168": "\u1166"}

FOLD_SCRIPT_END = 0x0530      # Latin, Greek, Cyrillic: the scripts whose letters fold


def fold_letter(ch: str) -> str:
    """The base letter of one letter, always exactly one character: ä, á, â -> a,
    ß -> s, й -> и, ά -> α. Two typed letters are two stored letters, so the
    firmware folds a query letter by letter with the bundle's fold table.
    Letters of other scripts are only lowercased: in the Indic scripts the vowel
    signs are separate letters and stay. A wrong extra match costs nothing, since
    the user picks the emoji from the results."""
    low = ch.lower()
    if len(low) != 1:                 # İ lowercases to i + combining dot
        low = low[0]
    if low in _FOLD_BASE:
        return _FOLD_BASE[low]
    if ord(low) >= FOLD_SCRIPT_END:
        return low
    if low in _FOLD_BASE:
        return _FOLD_BASE[low]
    base = unicodedata.normalize("NFKD", low)[:1] or low
    return _FOLD_BASE.get(base, base)


def fold(text: str) -> str:
    """Fold every letter of `text` to its base letter (see fold_letter())."""
    text = hangul_keys(unicodedata.normalize("NFC", text))
    return "".join(fold_letter(ch) for ch in text if not 0x0300 <= ord(ch) <= 0x036F)


def fold_table(code: dict[str, int]) -> list[tuple[int, int]]:
    """(codepoint, byte code) for every letter that folds into the alphabet: the
    alphabet itself, its upper case, and every Latin, Greek or Cyrillic letter
    whose base letter is in it. Sorted by codepoint."""
    cands = {ord(ch) for ch in code} | {ord(ch.upper()) for ch in code if len(ch.upper()) == 1}
    cands |= {c for c in range(0x41, FOLD_SCRIPT_END) if chr(c).isalpha()}
    cands |= {ord(c) for c in _FOLD_BASE}   # the Hangul Shift doubles, among others
    return sorted((c, code[fold_letter(chr(c))]) for c in cands if fold_letter(chr(c)) in code)


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


def parse_id(did: str) -> tuple[str, str, str]:
    """A dictionary id `lang[-CC][@method]` -> (lang, country, input method).
    Two dictionaries of one language differ by country and input method, so the
    language table needs no new entry: zh-CN@pinyin, zh-TW@zhuyin, zh-HK@cangjie."""
    rest, _, method = did.partition("@")
    lang, _, country = rest.partition("-")
    if method and method not in emoji_romanize.METHODS:
        raise SystemExit(f"unknown input method {method!r}; one of {', '.join(emoji_romanize.METHODS)}")
    return lang, country.upper(), method


def variant_code(country: str, method: str) -> int:
    """The dictionary header's variant field: (country index + 1) << 8 | method
    number (both 0 = none). Country indices are iso_lang_country.COUNTRY_CODES."""
    c = iso_lang_country.COUNTRY_CODES.index(country) + 1 if country else 0
    m = emoji_romanize.METHODS.index(method) + 1 if method else 0
    return (c << 8) | m


def cmd_extract(args: argparse.Namespace) -> int:
    did = args.lang
    lang, _country, method = parse_id(did)
    unihan = emoji_romanize.Unihan(Path(args.unihan)) if args.unihan else None
    data: dict[str, tuple[str, list[str]]] = {}
    for xml in args.xml:                      # later files fill gaps only
        for cp, (name, kws) in _read_cldr(Path(xml)).items():
            old_name, old_kws = data.get(cp, ("", []))
            data[cp] = (old_name or name, old_kws or kws)
    if method:
        conv = {}
        for cp, (name, kws) in data.items():
            name = emoji_romanize.convert(method, name, unihan) or ""
            kws = [k for k in (emoji_romanize.convert(method, k, unihan) for k in kws) if k]
            if name or kws:
                conv[cp] = (name, kws)
        data = conv
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
    out = SRC_DIR / f"{did}.tsv"
    how = (f", written as {method} keys (emoji_romanize.py"
           + (", Unihan kMandarin/kCangjie" if method != "romaji" else "") + ")") if method else ""
    header = (f"# emoji search keywords, {did}, from Unicode CLDR {CLDR_RELEASE} "
              f"(common/annotations, common/annotationsDerived){how}. Unicode License v3, see LICENSE.\n"
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


_LAYOUTS: dict = {}


def _is_typeable(ch: str) -> bool:
    """A legend a search query can contain: a letter, or a symbol used as a key
    name (zh-HK's Kangxi radicals); not a digit, punctuation or a space."""
    return unicodedata.category(ch)[0] == "L" or unicodedata.category(ch) == "So"


def _layouts() -> dict[str, dict[str, str]]:
    """Per keyboard layout (xx-YY): the folded first character of the unshifted
    legend of every key in lang/lang_lut.c (en-US where a layout inherits), by
    keycode name. Read from the firmware's own table, so a dictionary holds only
    words that layout can type."""
    if _LAYOUTS:
        return _LAYOUTS
    macros: dict[str, str] = {}
    for h in (POLY / "lang").glob("*.h"):
        for m in re.finditer(r"#define\s+(\w+)\s+([^\n]*)", h.read_text(encoding="utf-8")):
            macros.setdefault(m.group(1), m.group(2))

    def unescape(q: str) -> str:
        q = re.sub(r"\\U([0-9A-Fa-f]{8})", lambda m: chr(int(m.group(1), 16)), q)
        q = re.sub(r"\\u([0-9A-Fa-f]{4})", lambda m: chr(int(m.group(1), 16)), q)
        q = re.sub(r"\\x([0-9A-Fa-f]+)", lambda m: chr(int(m.group(1), 16)), q)
        simple = {"f": "\f", "v": "\v", "r": "\r", "n": "\n", "t": "\t", "a": "\a", "b": "\b",
                  "\\": "\\", '"': '"', "'": "'"}
        return re.sub(r"\\([fvrntab\\\"'])", lambda m: simple[m.group(1)], q)

    def resolve(text: str, depth: int = 0) -> str:
        out = ""
        for lit_, name in re.findall(r'U"((?:[^"\\]|\\.)*)"|([A-Za-z_]\w*)', text):
            if lit_ or not name:
                out += unescape(lit_)
            elif name in macros and depth < 8:
                out += resolve(macros[name], depth + 1)
        return out

    src = (POLY / "lang" / "lang_lut.c").read_text(encoding="utf-8")
    start = src.index("]]]*/", src.index("lang_plane [ALPHA")) + 5
    body = src[start:src.index("//[[[end]]]", start)]
    blocks = re.split(r"\{/\*\s*(KC_\w+):\s*\*/", body)
    raw: dict[str, dict[str, str | None]] = {}
    for i in range(1, len(blocks), 2):
        for m in re.finditer(r"/\*\s*([a-z]{2}-[A-Z]{2})\s*\*/\s*(.*)", blocks[i + 1]):
            cell = re.split(r",(?=(?:[^\"]*\"[^\"]*\")*[^\"]*$)", m.group(2))[0].strip()
            raw.setdefault(m.group(1), {})[blocks[i]] = None if cell == "NULL" else resolve(cell)
    if "en-US" not in raw:
        raise SystemExit("could not read the layouts from lang/lang_lut.c")
    for layout, keys in raw.items():
        legends = {}
        for kc, txt in keys.items():
            if kc in _NOT_TYPED:
                continue
            txt = txt if txt is not None else raw["en-US"].get(kc)
            ch = next((c for c in (txt or "") if ord(c) >= 0x20), None)
            if ch and _is_typeable(ch):
                legends[kc] = fold_letter(ch)
        _LAYOUTS[layout] = legends
    return _LAYOUTS


# Control keys, whose legends ("Esc", the Tab arrow) are not letters. The number
# row counts: its digits are not letters, and a letter on it (zh-TW's ㄅ ㄉ ㄓ) is
# moved to a free key in search mode (emoji_layer.c build_slot_letters()).
_NOT_TYPED = {"KC_ENTER", "KC_ESCAPE", "KC_BACKSPACE", "KC_TAB", "KC_SPACE"}


def _matching_layouts(lang: str, country: str) -> list[str]:
    out = [l for l in _layouts() if l.split("-")[0] == lang and (not country or l.split("-")[1] == country)]
    if not out:
        raise SystemExit(f"no keyboard layout for {lang}{'-' + country if country else ''}")
    return out


def key_remap(lang: str, country: str) -> dict[str, str]:
    """For a converted dictionary (romaji, pinyin, Cangjie), each Latin letter as
    the legend on the key that types it on the target layout: zh-HK's keys show
    Cangjie radicals, so a Cangjie or pinyin `a` is stored as ⽇. Empty where the
    layout shows the Latin letters itself."""
    lay = _layouts()[_matching_layouts(lang, country)[0]]
    remap = {}
    for c in "abcdefghijklmnopqrstuvwxyz":
        got = lay.get(f"KC_{c.upper()}")
        if got and got != c:
            remap[c] = got
    return remap


def typeable_letters(lang: str, country: str) -> set[str]:
    """Letters the layouts of this language (and country, when given) can type."""
    out: set[str] = set()
    for layout in _matching_layouts(lang, country):
        out |= set(_layouts()[layout].values())
    return out


def pack_word(codes: list[int]) -> bytes:
    """6-bit codes, least significant bits first, then the end code 0, padded to a
    whole byte."""
    acc = nbits = 0
    out = bytearray()
    for c in codes + [0]:
        acc |= c << nbits
        nbits += LETTER_BITS
        while nbits >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nbits -= 8
    if nbits:
        out.append(acc & 0xFF)
    return bytes(out)


def unpack_word(data: bytes, start: int, end: int) -> bytes:
    """The codes of one packed word, up to its end code."""
    out = bytearray()
    acc = nbits = 0
    for b in data[start:end]:
        acc |= b << nbits
        nbits += 8
        while nbits >= LETTER_BITS:
            c = acc & ((1 << LETTER_BITS) - 1)
            acc >>= LETTER_BITS
            nbits -= LETTER_BITS
            if c == 0:
                return bytes(out)
            out.append(c)
    return bytes(out)


def build(did: str, revision: int) -> bytes:
    lang, country, method = parse_id(did)
    src = SRC_DIR / f"{did}.tsv"
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

    def add(tok: str, idx: int, flag: int) -> None:
        posts.setdefault(tok, {})[idx] = posts.get(tok, {}).get(idx, 0) | flag

    remap = key_remap(lang, country) if method else {}
    own = {cp for cp in _read_tsv(src)}

    def own_tokens(text: str) -> list[str]:
        return ["".join(remap.get(ch, ch) for ch in t) for t in tokens(text, lang)]

    for idx, (cp, name, kws) in enumerate(entries):
        tok = own_tokens if cp in own else (lambda text: tokens(text, lang))
        name_toks = set(tok(name))
        for t in name_toks | {t for k in kws for t in tok(k)}:
            add(t, idx, POST_NAME_BIT if t in name_toks else 0)
    if lang != "en":
        # The English words too, as keywords: a German speaker may type "smile".
        # Without the name bit, so the language's own name words rank above them.
        en = _read_tsv(SRC_DIR / "en.tsv")
        kw_count: dict[str, int] = {}
        for _n, kws in en.values():
            for tok in {t for k in kws for t in tokens(k, "en")}:
                kw_count[tok] = kw_count.get(tok, 0) + 1
        for idx, (cp, _name, _kws) in enumerate(entries):
            en_name, en_kws = en.get(cp, ("", []))
            common = {t for k in en_kws for t in tokens(k, "en")
                      if kw_count[t] >= EN_KEYWORD_MIN_EMOJI}
            for tok in set(tokens(en_name, "en")) | common:
                add(tok, idx, 0)

    # Only words this language's keyboard can type in search mode: a Russian layout
    # types no Latin letters, so the English words go; a German one keeps them.
    typeable = typeable_letters(lang, country)
    posts = {w: p for w, p in posts.items() if len(w) <= WORD_MAX and all(ch in typeable for ch in w)}
    alphabet = sorted({ch for w in posts for ch in w})
    if len(alphabet) > ALPHA_MAX:
        raise SystemExit(f"{did}: {len(alphabet)} letters do not fit {LETTER_BITS}-bit codes")
    code = {ch: i + 1 for i, ch in enumerate(alphabet)}
    words = sorted(posts)                  # codepoint order == code order
    strings = bytearray()
    str_offs = []
    for w in words:
        str_offs.append(len(strings))
        strings += pack_word([code[ch] for ch in w])
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
    folds = fold_table(code)
    body = bytearray(DICT_HEADER_SIZE)
    alpha_off = PLYF_HEADER_SIZE + len(body)
    body += struct.pack(f"<{len(alphabet)}I", *(ord(ch) for ch in alphabet))
    fold_off = PLYF_HEADER_SIZE + len(body)
    body += struct.pack(f"<{len(folds)}I", *((c << 24) | cp for cp, c in folds))
    stops = sorted({fold(w) for w in STOPWORDS.get(lang, set())} |
                   ({fold(w) for w in STOPWORDS["en"]} if lang != "en" else set()))
    stops = [w for w in stops if all(ch in code for ch in w)]
    stop_off = PLYF_HEADER_SIZE + len(body)
    for w in stops:
        body += bytes(code[ch] for ch in w) + b"\0"
    _align4(body)
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
                                          len(cps), len(words), cps_off, words_off, strings_off,
                                          len(alphabet), len(folds), alpha_off, fold_off,
                                          len(stops), variant_code(country, method), stop_off)
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
        (dm, fmt, self.lang_idx, ncp, nword, cps_off, words_off, strings_off,
         nalpha, nfold, alpha_off, fold_off, nstop, variant,
         stop_off) = struct.unpack_from(DICT_HEADER_FMT, blob, PLYF_HEADER_SIZE)
        if dm != DICT_MAGIC or fmt != DICT_FORMAT:
            raise ValueError("not an EmjD dictionary")
        self.lang = iso_lang_country.LANG_CODES[self.lang_idx]
        self.country = iso_lang_country.COUNTRY_CODES[(variant >> 8) - 1] if variant >> 8 else ""
        self.method = emoji_romanize.METHODS[(variant & 0xFF) - 1] if variant & 0xFF else ""
        self.alphabet = [chr(c) for c in struct.unpack_from(f"<{nalpha}I", blob, alpha_off)]
        self.fold_map = {e & 0xFFFFFF: e >> 24
                         for e in struct.unpack_from(f"<{nfold}I", blob, fold_off)}
        self.stops = set()
        o = stop_off
        for _ in range(nstop):
            end = blob.index(b"\0", o)
            self.stops.add(blob[o:end])
            o = end + 1
        self.cps = struct.unpack_from(f"<{ncp}I", blob, cps_off)
        recs = struct.unpack_from(f"<{2 * (nword + 1)}H", blob, words_off)
        post_off = words_off + 4 * (nword + 1)
        self.sizes = {"header": PLYF_HEADER_SIZE + DICT_HEADER_SIZE, "alphabet": 4 * nalpha,
                      "fold": 4 * nfold,
                      "cps": 4 * ncp,
                      "words": 4 * (nword + 1), "postings": 2 * recs[-1],
                      "strings": len(blob) - strings_off, "total": len(blob)}
        self.words, self.posts = [], []
        for i in range(nword):
            so, ps = recs[2 * i], recs[2 * i + 1]
            self.words.append(unpack_word(blob, strings_off + so, strings_off + recs[2 * i + 2]))
            self.posts.append(struct.unpack_from(f"<{recs[2 * i + 3] - ps}H", blob, post_off + 2 * ps))

    def encode(self, word: str) -> bytes:
        """Typed letters in the dictionary's byte codes, through the fold table,
        the way the firmware maps them."""
        return bytes(self.fold_map.get(ord(ch), CODE_UNKNOWN) for ch in word)

    def decode(self, codes: bytes) -> str:
        return "".join(self.alphabet[c - 1] for c in codes)

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
        Words are split at spaces. Each word must match (AND), and a word matches
        every dictionary word it is a prefix of. A query word with no match at all,
        even in the typo pass, is left out of the AND, and so is a word that would
        leave no result ("with" is a stop word and only typo-matches "witch").
        Rank: exact word in the name, exact keyword, prefix in the name, prefix
        keyword; picker order inside each rank. The firmware produces the same
        order in four passes over the postings with one 'already shown' bitmap."""
        # Drop stop words, except the last word: the user is still typing it, and
        # "a" is how "apple" starts.
        coded = [self.encode(w) for w in query[:QUERY_MAX].split()][:MAX_WORDS]
        toks = [t for t in coded[:-1] if t not in self.stops] + coded[-1:]
        typo = False
        per_tok = []
        for t in toks:
            hits = self._prefix_words(t)
            if not hits:
                hits, typo = self._typo_words(t), True
            if hits:
                per_tok.append((t, hits))
        if not per_tok:
            return [], typo
        allowed = None
        for _t, hits in per_tok[1:]:
            s = {p & POST_INDEX_MASK for wi in hits for p in self.posts[wi]}
            if allowed is not None:
                s &= allowed
            first_set = {p & POST_INDEX_MASK for wi in per_tok[0][1] for p in self.posts[wi]}
            if s & first_set:                 # a word that would empty the result is ignored
                allowed = s
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
          f"{len(d.cps)} codepoints, {len(d.words)} words, {len(d.alphabet)} letters")
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


# Queries the firmware suite (base/tests/emoji_dict_tests.cpp) compares against
# this reference: prefixes, exact words, typos, AND, stop words, accents, case.
VECTOR_QUERIES = {
    "en": ["grin", "smil", "smiel", "heart", "red heart", "RED HEART", "face with tears",
           "a", "red a", "rokcet", "cat", "tear", "thumbs", "café", "zzzz", "arrow",
           "flag", "o", "  red   heart ", "rαcket", "face with with with tears",
           "rocket heart"],
    "de": ["lachen", "Lächeln", "LÄCHELN", "lacheln", "rotes herz", "herz mit pfeil",
           "gesicht mit tränen", "Straße", "strasse", "katze", "star", "pfeil", "ö", "rakete herz",
           "smile", "happy", "red heart", "cat"],
}


def cmd_vectors(args: argparse.Namespace) -> int:
    """One line per query: query codepoints, typo flag, every result codepoint
    (all hex, comma-separated). Read by the firmware suite."""
    d = Dict(Path(args.file).read_bytes())
    for q in args.query or VECTOR_QUERIES.get(d.lang, []):
        cps, typo = d.search(q)
        print(",".join(f"{ord(c):X}" for c in q), int(typo),
              ",".join(f"{c:X}" for c in cps), sep="\t")
    return 0


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("extract", help="CLDR XML -> dict_src/<id>.tsv; id = lang[-CC][@romaji|pinyin|zhuyin|cangjie]")
    p.add_argument("lang")
    p.add_argument("xml", nargs="+", help="annotations/<lang>.xml, then annotationsDerived/<lang>.xml")
    p.add_argument("-v", "--verbose", action="store_true")
    p.add_argument("--unihan", help="directory with Unihan_Readings.txt and Unihan_DictionaryLikeData.txt "
                                    "(for @pinyin, @zhuyin, @cangjie)")
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
    p = sub.add_parser("vectors", help="reference results for the firmware suite")
    p.add_argument("file")
    p.add_argument("query", nargs="*")
    p.set_defaults(fn=cmd_vectors)
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
