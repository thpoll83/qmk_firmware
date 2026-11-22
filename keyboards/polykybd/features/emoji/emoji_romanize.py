#!/usr/bin/env python3
"""Write emoji keywords as the keys that type them, for languages whose words are
not typed letter by letter (used by emoji_dict.py extract --input).

    romaji   Japanese: ja-JP's letter keys show Latin letters, so kana keywords are
             written in romaji (スマイル -> sumairu). A keyword with kanji has no
             reading here and is dropped; the English words cover those emoji.
    pinyin   Chinese on Latin keys (zh-CN, zh-HK): 笑脸 -> xiaolian.
    zhuyin   Chinese on zh-TW's Zhuyin keys: 笑脸 -> ㄒㄧㄠㄌㄧㄢ.
    cangjie  Chinese on Latin keys typed in Cangjie (zh-HK): 笑 -> hhk.

Pinyin and Cangjie come from Unicode's Unihan database (kMandarin, kCangjie; the
first reading of a character with several). Tones are not typed and are dropped.
A keyword with a character that has no reading is dropped.
"""
from __future__ import annotations

import unicodedata
from pathlib import Path

METHODS = ("romaji", "pinyin", "zhuyin", "cangjie")

# ── kana -> romaji ─────────────────────────────────────────────────────────

_KANA = {
    "あ": "a", "い": "i", "う": "u", "え": "e", "お": "o",
    "か": "ka", "き": "ki", "く": "ku", "け": "ke", "こ": "ko",
    "さ": "sa", "し": "shi", "す": "su", "せ": "se", "そ": "so",
    "た": "ta", "ち": "chi", "つ": "tsu", "て": "te", "と": "to",
    "な": "na", "に": "ni", "ぬ": "nu", "ね": "ne", "の": "no",
    "は": "ha", "ひ": "hi", "ふ": "fu", "へ": "he", "ほ": "ho",
    "ま": "ma", "み": "mi", "む": "mu", "め": "me", "も": "mo",
    "や": "ya", "ゆ": "yu", "よ": "yo",
    "ら": "ra", "り": "ri", "る": "ru", "れ": "re", "ろ": "ro",
    "わ": "wa", "ゐ": "i", "ゑ": "e", "を": "o", "ん": "n",
    "が": "ga", "ぎ": "gi", "ぐ": "gu", "げ": "ge", "ご": "go",
    "ざ": "za", "じ": "ji", "ず": "zu", "ぜ": "ze", "ぞ": "zo",
    "だ": "da", "ぢ": "ji", "づ": "zu", "で": "de", "ど": "do",
    "ば": "ba", "び": "bi", "ぶ": "bu", "べ": "be", "ぼ": "bo",
    "ぱ": "pa", "ぴ": "pi", "ぷ": "pu", "ぺ": "pe", "ぽ": "po",
    "ゔ": "vu",
    "ぁ": "a", "ぃ": "i", "ぅ": "u", "ぇ": "e", "ぉ": "o", "ゎ": "wa", "ゕ": "ka", "ゖ": "ke",
}
# Two kana that are typed as one syllable: a small ya/yu/yo after an i-row kana, and
# the katakana extensions (ティ, ファ).
_PAIRS = {
    "きゃ": "kya", "きゅ": "kyu", "きょ": "kyo", "しゃ": "sha", "しゅ": "shu", "しょ": "sho",
    "ちゃ": "cha", "ちゅ": "chu", "ちょ": "cho", "にゃ": "nya", "にゅ": "nyu", "にょ": "nyo",
    "ひゃ": "hya", "ひゅ": "hyu", "ひょ": "hyo", "みゃ": "mya", "みゅ": "myu", "みょ": "myo",
    "りゃ": "rya", "りゅ": "ryu", "りょ": "ryo", "ぎゃ": "gya", "ぎゅ": "gyu", "ぎょ": "gyo",
    "じゃ": "ja", "じゅ": "ju", "じょ": "jo", "びゃ": "bya", "びゅ": "byu", "びょ": "byo",
    "ぴゃ": "pya", "ぴゅ": "pyu", "ぴょ": "pyo", "ぢゃ": "ja", "ぢゅ": "ju", "ぢょ": "jo",
    "てぃ": "ti", "でぃ": "di", "とぅ": "tu", "どぅ": "du", "ふぁ": "fa", "ふぃ": "fi",
    "ふぇ": "fe", "ふぉ": "fo", "うぃ": "wi", "うぇ": "we", "うぉ": "wo", "ゔぁ": "va",
    "ゔぃ": "vi", "ゔぇ": "ve", "ゔぉ": "vo", "しぇ": "she", "じぇ": "je", "ちぇ": "che",
    "つぁ": "tsa", "つぃ": "tsi", "つぇ": "tse", "つぉ": "tso", "いぇ": "ye", "てゅ": "tyu",
    "でゅ": "dyu", "ふゅ": "fyu",
}


def _hiragana(text: str) -> str:
    return "".join(chr(ord(c) - 0x60) if 0x30A1 <= ord(c) <= 0x30F6 else c for c in text)


def is_kana(text: str) -> bool:
    return bool(text) and all(0x3041 <= ord(c) <= 0x30FF or c in " ー・" for c in text)


def kana_to_romaji(text: str) -> str | None:
    """Kana in Hepburn romaji as an IME takes it, or None when the text has a
    character that is not kana (kanji, Latin). The long-vowel mark ー is dropped
    (コーヒー -> kohi), a small tsu doubles the next consonant (きって -> kitte)."""
    if not is_kana(text):
        return None
    h = _hiragana(text).replace("・", " ")
    out, i, double = [], 0, False
    while i < len(h):
        ch = h[i]
        if ch in " ":
            out.append(" "); i += 1; continue
        if ch == "ー":
            i += 1; continue
        if ch in "っッ":
            double = True; i += 1; continue
        syl = _PAIRS.get(h[i:i + 2])
        if syl:
            i += 2
        else:
            syl = _KANA.get(ch)
            i += 1
            if syl is None:
                return None
        if double and syl[0] not in "aiueon":
            syl = syl[0] + syl
        double = False
        out.append(syl)
    return "".join(out).strip() or None


# ── Unihan ─────────────────────────────────────────────────────────────────

class Unihan:
    """kMandarin and kCangjie from the Unihan text files (Unihan.zip)."""

    def __init__(self, directory: Path):
        self.pinyin: dict[str, str] = {}
        self.cangjie: dict[str, str] = {}
        for name, field, table in (("Unihan_Readings.txt", "kMandarin", self.pinyin),
                                   ("Unihan_DictionaryLikeData.txt", "kCangjie", self.cangjie)):
            for line in (Path(directory) / name).read_text(encoding="utf-8").splitlines():
                if line.startswith("#") or f"\t{field}\t" not in line:
                    continue
                cp, _f, value = line.split("\t", 2)
                table[chr(int(cp[2:], 16))] = value.split()[0]


def _toneless(syllable: str) -> str:
    """xiào -> xiao, lǜ -> lü: the tone marks off, the ü kept."""
    out = []
    for ch in unicodedata.normalize("NFD", syllable.lower()):
        if ch == "̈":                       # the diaeresis of ü stays
            out.append(ch)
        elif not unicodedata.combining(ch):
            out.append(ch)
    return unicodedata.normalize("NFC", "".join(out))


_ZY_INITIAL = {"b": "ㄅ", "p": "ㄆ", "m": "ㄇ", "f": "ㄈ", "d": "ㄉ", "t": "ㄊ", "n": "ㄋ", "l": "ㄌ",
               "g": "ㄍ", "k": "ㄎ", "h": "ㄏ", "j": "ㄐ", "q": "ㄑ", "x": "ㄒ", "zh": "ㄓ", "ch": "ㄔ",
               "sh": "ㄕ", "r": "ㄖ", "z": "ㄗ", "c": "ㄘ", "s": "ㄙ"}
_ZY_FINAL = {"a": "ㄚ", "o": "ㄛ", "e": "ㄜ", "ê": "ㄝ", "ai": "ㄞ", "ei": "ㄟ", "ao": "ㄠ", "ou": "ㄡ",
             "an": "ㄢ", "en": "ㄣ", "ang": "ㄤ", "eng": "ㄥ", "er": "ㄦ", "i": "ㄧ", "u": "ㄨ", "ü": "ㄩ",
             "ia": "ㄧㄚ", "ie": "ㄧㄝ", "iao": "ㄧㄠ", "iu": "ㄧㄡ", "ian": "ㄧㄢ", "in": "ㄧㄣ",
             "iang": "ㄧㄤ", "ing": "ㄧㄥ", "iong": "ㄩㄥ", "ua": "ㄨㄚ", "uo": "ㄨㄛ", "uai": "ㄨㄞ",
             "ui": "ㄨㄟ", "uan": "ㄨㄢ", "un": "ㄨㄣ", "uang": "ㄨㄤ", "ong": "ㄨㄥ", "ueng": "ㄨㄥ",
             "üe": "ㄩㄝ", "üan": "ㄩㄢ", "ün": "ㄩㄣ", "io": "ㄧㄛ"}
_ZY_Y = {"yi": "i", "ya": "ia", "ye": "ie", "yao": "iao", "you": "iu", "yan": "ian", "yin": "in",
         "yang": "iang", "ying": "ing", "yong": "iong", "yu": "ü", "yue": "üe", "yuan": "üan",
         "yun": "ün", "wu": "u", "wa": "ua", "wo": "uo", "wai": "uai", "wei": "ui", "wan": "uan",
         "wen": "un", "wang": "uang", "weng": "ueng", "yo": "io"}


def pinyin_to_zhuyin(syllable: str) -> str | None:
    """One toneless pinyin syllable as Zhuyin keys: xiao -> ㄒㄧㄠ."""
    s = syllable
    if s in ("zhi", "chi", "shi", "ri", "zi", "ci", "si"):
        return _ZY_INITIAL[s[:-1]]
    if s in _ZY_Y:
        return _ZY_FINAL.get(_ZY_Y[s])
    if s in ("m", "n", "ng", "hm", "hng"):
        return None
    for ini in ("zh", "ch", "sh"):
        if s.startswith(ini):
            init, rest = ini, s[2:]
            break
    else:
        init, rest = s[0], s[1:]
    if init not in _ZY_INITIAL:
        return _ZY_FINAL.get(s)
    if init in "jqx" and rest.startswith("u"):
        rest = "ü" + rest[1:]
    final = _ZY_FINAL.get(rest)
    return _ZY_INITIAL[init] + final if final else None


def han_to(method: str, text: str, unihan: Unihan) -> str | None:
    """A Chinese keyword as the keys that type it, or None when a character has no
    reading. Latin letters and digits pass through; spaces separate words."""
    out = []
    for ch in text:
        if ch.isspace():
            out.append(" ")
            continue
        if ch.isascii():
            out.append(ch)
            continue
        if method == "cangjie":
            code = unihan.cangjie.get(ch)
            if not code:
                return None
            out.append(code.lower())
            continue
        py = unihan.pinyin.get(ch)
        if not py:
            return None
        py = _toneless(py)
        if method == "pinyin":
            out.append(py)
        else:
            zy = pinyin_to_zhuyin(py)
            if zy is None:
                return None
            out.append(zy)
    return "".join(out).strip() or None


def convert(method: str, text: str, unihan: Unihan | None) -> str | None:
    if method == "romaji":
        return kana_to_romaji(text)
    if unihan is None:
        raise SystemExit(f"--input {method} needs --unihan <directory with the Unihan text files>")
    return han_to(method, text, unihan)
