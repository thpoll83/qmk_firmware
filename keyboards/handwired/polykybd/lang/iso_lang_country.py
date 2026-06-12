"""FROZEN single source of truth: ISO 639-1 language + ISO 3166-1 alpha-2
country code <-> 1-byte index tables for the PolyKybd GET_LANG_LIST payload.

The keyboard's language list is transmitted as one (lang_idx, country_idx) byte
pair per language instead of the 4 ASCII chars (cmd GET_LANG_LIST_PACKED). Each
index is the position of the code in the tuples below.

Generated once from the `iso-codes` package (json), then FROZEN. The index of a
code is APPEND-ONLY: never reorder, never delete; new ISO codes get appended at
the next free index. The host decoder and the firmware encoder must agree on
these exact indices, so this file is the shared source both are generated from.

Initial assignment (2026-06-10): alphabetical, for determinism only.
ISO 639-1: 184 codes (idx 0..183); private langs start at 184.
ISO 3166-1 alpha-2: 249 codes (idx 0..248).
"""

_LANG = (
    "aa ab ae af ak am an ar as av ay az ba be bg bh bi bm bn bo br bs ca ce ch co cr cs cu cv cy da de dv dz ee el en eo es et eu fa ff fi fj fo fr fy ga gd gl gn gu gv ha he hi ho hr ht hu hy hz ia id ie ig ii ik io is it iu ja jv ka kg ki kj kk kl km kn ko kr ks ku kv kw ky la lb lg li ln lo lt lu lv mg mh mi mk ml mn mr ms mt my na nb nd ne ng nl nn no nr nv ny oc oj om or os pa pi pl ps pt qu rm rn ro ru rw sa sc sd se sg si sk sl sm sn so sq sr ss st su sv sw ta te tg th ti tk tl tn to tr ts tt tw ty ug uk ur uz ve vi vo wa wo xh yi yo za zh zu"
).split()

# Private-use language pseudo-codes with no ISO 639-1 entry, appended above the
# standard block. hw = Hawaiian (ISO 639-2/3 'haw' cannot fit the 2-char field).
# nh = Nahuatl (ISO 639-3 'nah'), ck = Cherokee (ISO 639-2/3 'chr') — both stored
# verbatim in the 2-char lang field.
PRIVATE_LANGS = ["hw", "nh", "ck"]

_COUNTRY = (
    "AD AE AF AG AI AL AM AO AQ AR AS AT AU AW AX AZ BA BB BD BE BF BG BH BI BJ BL BM BN BO BQ BR BS BT BV BW BY BZ CA CC CD CF CG CH CI CK CL CM CN CO CR CU CV CW CX CY CZ DE DJ DK DM DO DZ EC EE EG EH ER ES ET FI FJ FK FM FO FR GA GB GD GE GF GG GH GI GL GM GN GP GQ GR GS GT GU GW GY HK HM HN HR HT HU ID IE IL IM IN IO IQ IR IS IT JE JM JO JP KE KG KH KI KM KN KP KR KW KY KZ LA LB LC LI LK LR LS LT LU LV LY MA MC MD ME MF MG MH MK ML MM MN MO MP MQ MR MS MT MU MV MW MX MY MZ NA NC NE NF NG NI NL NO NP NR NU NZ OM PA PE PF PG PH PK PL PM PN PR PS PT PW PY QA RE RO RS RU RW SA SB SC SD SE SG SH SI SJ SK SL SM SN SO SR SS ST SV SX SY SZ TC TD TF TG TH TJ TK TL TM TN TO TR TT TV TW TZ UA UG UM US UY UZ VA VC VE VG VI VN VU WF WS YE YT ZA ZM ZW"
).split()

LANG_CODES = _LANG + PRIVATE_LANGS
COUNTRY_CODES = _COUNTRY

_LANG_IDX = {c: i for i, c in enumerate(LANG_CODES)}
_COUNTRY_IDX = {c: i for i, c in enumerate(COUNTRY_CODES)}

assert len(LANG_CODES) <= 256 and len(COUNTRY_CODES) <= 256, "index must fit one byte"


def encode_pair(code: str) -> bytes:
    """4-char 'llCC' (lang lower + country UPPER) -> 2 bytes (lang_idx, country_idx)."""
    return bytes((_LANG_IDX[code[:2]], _COUNTRY_IDX[code[2:]]))


def decode_pair(lang_idx: int, country_idx: int) -> str:
    """Inverse of encode_pair: 2 indices -> 4-char 'llCC' code."""
    return LANG_CODES[lang_idx] + COUNTRY_CODES[country_idx]


def encode_packed(codes) -> bytes:
    """Encode a list of 4-char codes as: 1 count byte + 2 index bytes per code."""
    out = bytearray([len(codes)])
    for code in codes:
        out += encode_pair(code)
    return bytes(out)


def decode_packed(buf, on_skip=None) -> list:
    """Inverse of encode_packed. buf[0] = count, then count*(lang,country) pairs.

    An index pair that falls outside the known tables — e.g. a language a firmware
    newer than this frozen table reports — is skipped instead of aborting the whole
    list, so one unknown code never costs you every other language. When given,
    on_skip(position, lang_idx, country_idx) is called for each skipped pair so the
    caller can log it.
    """
    count = buf[0]
    out = []
    for i in range(count):
        li, ci = buf[1 + 2 * i], buf[2 + 2 * i]
        if 0 <= li < len(LANG_CODES) and 0 <= ci < len(COUNTRY_CODES):
            out.append(LANG_CODES[li] + COUNTRY_CODES[ci])
        elif on_skip is not None:
            on_skip(i, li, ci)
    return out
