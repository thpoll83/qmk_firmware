#!/usr/bin/env python3
"""Draw the split72 emoji layer in search mode, per base layout, as an HTML page.

It reads _EMJ and the base layers from split72's keymap.c and applies the
firmware's search-mode key rules (build_slot_letters() in emoji_layer.c: letters
on their own key, a lost letter moved to the nearest free slot on its half).
Used for the docs site's using/emoji-search picture. Keep it in step with
emoji_layer.c when those rules change.

    python3 render_search_keys.py [QUERY] [OUT.html]      # default "smil", preview.html
    PREVIEW_LANG=ru python3 render_search_keys.py         # Russian letters (ru only)

Then screenshot the page with Playwright at deviceScaleFactor 2 (viewport
1060 wide, full page) and scale it to 1400 px wide for the docs asset. It also
prints, per layout, the letters moved to a free key and any still lost.
"""
import html
import re
import sys
from pathlib import Path

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")   # Cyrillic output on a non-UTF-8 console

QMK = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(QMK / "emoji"))
import emoji_dict  # noqa: E402

QUERY = sys.argv[1] if len(sys.argv) > 1 else "smil"
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else Path("preview.html")

src = re.sub(r"/\*.*?\*/", "", (QMK / "split72/keymaps/default/keymap.c").read_text(encoding="utf-8"), flags=re.S)


def layer(name):
    m = re.search(r"\[" + name + r"\]\s*=\s*LAYOUT_left_right_stacked\((.*?)\n\s*\)", src, re.S)
    body = re.sub(r"//[^\n]*", "", m.group(1))
    toks, d, cur = [], 0, ""
    for ch in body:
        d += (ch == "(") - (ch == ")")
        if ch == "," and d == 0:
            toks.append(cur.strip()); cur = ""
        else:
            cur += ch
    if cur.strip():
        toks.append(cur.strip())
    return toks


# LAYOUT_left_right_stacked: left rows 7,7,8,8,7 then right rows 7,7,8,8,7.
ROWS = [7, 7, 8, 8, 7]
LEFT_COLS = [range(0, 7), range(0, 7), range(0, 8), range(0, 8), [0, 1, 2, 3, 5, 6, 7]]
RIGHT_COLS = [range(1, 8), range(1, 8), range(0, 8), range(0, 8), [0, 1, 2, 4, 5, 6, 7]]

_cfg = (QMK / "split72/keymaps/default/config.h").read_text(encoding="utf-8")
RESULT_OF_CAT = [int(x) for x in re.search(r"EMJ_SEARCH_RESULT_OF_CAT \{([^}]*)\}", _cfg).group(1).split(",")]
SPACE_MRU = {5, 6}
import os
LANG = os.environ.get("PREVIEW_LANG", "en")
ALIAS = {"DE_UDIA": "KC_LBRC", "DE_ODIA": "KC_SCLN", "DE_ADIA": "KC_QUOT", "DE_SS": "KC_MINS",
         "DE_Z": "KC_Y", "DE_Y": "KC_Z", "DE_PLUS": "KC_RBRC", "DE_MINS": "KC_SLSH", "DE_HASH": "KC_NUHS",
         "DE_ACUT": "KC_EQL", "DE_CIRC": "KC_GRV", "DE_GRV": "KC_EQL", "DE_LABK": "KC_NUBS",
         "KC_QUOTE": "KC_QUOT", "KC_GRAVE": "KC_GRV", "KC_COMMA": "KC_COMM", "KC_SLASH": "KC_SLSH",
         "KC_MINUS": "KC_MINS", "KC_EQUAL": "KC_EQL"}
LETTER = {f"KC_{c}": c.lower() for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ"}
if LANG == "ru":
    RU = dict(zip("QWERTYUIOPASDFGHJKLZXCVBNM", "йцукенгшщзфывапролдячсмить"))
    LETTER = {f"KC_{c}": v for c, v in RU.items()}
    LETTER.update({"KC_LBRC": "х", "KC_RBRC": "ъ", "KC_SCLN": "ж", "KC_QUOT": "э",
                   "KC_COMM": "б", "KC_DOT": "ю", "KC_GRV": "ё"})
# Matrix (row, col) of each LAYOUT_left_right_stacked argument, in order (split72).
MATRIX = [[0, 0], [0, 1], [0, 2], [0, 3], [0, 4], [0, 5], [0, 6], [1, 0], [1, 1], [1, 2], [1, 3], [1, 4], [1, 5], [1, 6], [2, 0], [2, 1], [2, 2], [2, 3], [2, 4], [2, 5], [2, 6], [3, 7], [3, 0], [3, 1], [3, 2], [3, 3], [3, 4], [3, 5], [3, 6], [4, 7], [4, 0], [4, 1], [4, 2], [4, 3], [4, 4], [4, 5], [4, 6], [5, 1], [5, 2], [5, 3], [5, 4], [5, 5], [5, 6], [5, 7], [6, 1], [6, 2], [6, 3], [6, 4], [6, 5], [6, 6], [6, 7], [8, 0], [7, 1], [7, 2], [7, 3], [7, 4], [7, 5], [7, 6], [7, 7], [9, 0], [8, 1], [8, 2], [8, 3], [8, 4], [8, 5], [8, 6], [8, 7], [9, 1], [9, 2], [9, 3], [9, 4], [9, 5], [9, 6], [9, 7]]

d = emoji_dict.Dict(emoji_dict.build("en", 1))
results = [chr(c) for c in d.search(QUERY)[0]]
total = len(results)
PG = f"1/{max(1, (total + 11) // 12)}"

emj = layer("_EMJ")
LAYOUTS = [("_L0", "Qwerty"), ("_L1", "Qwerty Stag!")]


def tap(k):
    m = re.match(r"^[LR](GUI|ALT|CTL|SFT)_T\((.*)\)$", k)
    return m.group(2) if m else k


def letter_of(k):
    return LETTER.get(ALIAS.get(k, k), "")


def roles(base):
    base = [tap(k) for k in base]
    assert len(emj) == len(base) == len(MATRIX), (len(emj), len(base), len(MATRIX))
    pairs = list(zip(emj, base))
    # the firmware's two passes, in matrix scan order
    order = sorted(range(len(pairs)), key=lambda i: tuple(MATRIX[i]))
    letter, free, reach, lost = {}, set(), set(), []
    for i in order:
        e, k = pairs[i]
        cp = letter_of(k)
        own = e.startswith("ESLOT")
        if own:
            if cp: letter[i] = cp; reach.add(cp)
            else: free.add(i)
        elif cp:
            lost.append(i)
    ANCHOR = {"KC_LBRC": ("KC_P", 1), "KC_RBRC": ("KC_P", 2), "KC_BSLS": ("KC_P", 3),
              "KC_SCLN": ("KC_L", 1), "KC_QUOT": ("KC_L", 2), "KC_NUHS": ("KC_L", 3),
              "KC_COMM": ("KC_M", 1), "KC_DOT": ("KC_M", 2), "KC_SLSH": ("KC_M", 3),
              "KC_GRV": ("KC_1", -1), "KC_MINS": ("KC_0", 1), "KC_EQL": ("KC_0", 2), "KC_NUBS": ("KC_Z", -1)}
    nb = {}
    for i in order:
        nb.setdefault(ALIAS.get(pairs[i][1], pairs[i][1]), MATRIX[i])
    moved = {}
    for i in lost:
        cp = letter_of(pairs[i][1])
        if cp in reach: continue
        r, c = MATRIX[i]
        side = r < 5
        an = ANCHOR.get(ALIAS.get(pairs[i][1], pairs[i][1]))
        if an and an[0] in nb and (nb[an[0]][0] < 5) == side:
            r, c = nb[an[0]][0], nb[an[0]][1] + an[1]
        cand = [j for j in free if (MATRIX[j][0] < 5) == side]
        if not cand: continue
        j = min(cand, key=lambda j: (abs(MATRIX[j][0] - r) + abs(MATRIX[j][1] - c), MATRIX[j]))
        letter[j] = cp; moved[j] = cp; free.discard(j); reach.add(cp)
    out = []
    for i, (e, k) in enumerate(pairs):
        if e == "KC_EMJ_MODE":
            out.append(("mode", "🥳", "TOGGLE"))
        elif e == "KC_EMJ_S_CLEAR":
            out.append(("clear", "Clear", ""))
        elif e == "KC_EMJ_PAGE_PREV":
            out.append(("page", "◀", PG))
        elif e == "KC_EMJ_PAGE_NEXT":
            out.append(("page", "▶", PG))
        elif e == "KC_EMJ_CLEAR":
            out.append(("recent", "Clr", "recents"))
        elif e == "KC_EMJ_S_BKSP":
            out.append(("bksp", "⌫", ""))
        elif e == "KC_EMJ_S_SPACE":
            out.append(("space", "␣", "space"))
        elif e.startswith("KC_EMJ_CAT("):
            r = RESULT_OF_CAT[int(e[11:-1])]
            out.append(("result", results[r] if r < total else "", f"#{r + 1}"))
        elif e.startswith("EMRU"):
            out.append(("recent", f"R{int(e[5:-1]) + 1}", "recent"))
        elif e.startswith("ESLOT"):
            if i in moved:
                out.append(("moved", moved[i], "moved"))
            elif i in letter:
                out.append(("letter", letter[i], ""))
            else:
                out.append(("blank", "", ""))
        elif e == "KC_BASE":
            out.append(("base", "Base", "exit"))
        else:
            out.append(("blank", "", ""))
    lost_left = [letter_of(pairs[i][1]) for i in lost if letter_of(pairs[i][1]) not in reach]
    return out, (sorted(moved.values()), lost_left)


def half(keys, cols):
    cells, i = [], 0
    for r, cs in enumerate(cols):
        for c in cs:
            kind, main, sub = keys[i]; i += 1
            cells.append(f'<div class="k {kind}" style="grid-row:{r + 1};grid-column:{c + 1}">'
                         f'<span class="m">{html.escape(main)}</span>'
                         + ('<span class="tg on"></span>' if sub == "TOGGLE" else f'<span class="s">{html.escape(sub)}</span>') + '</div>')
    return '<div class="half">' + "".join(cells) + "</div>"


boards = []
for lid, name in LAYOUTS:
    keys, (moved, still) = roles(layer(lid))
    nl = sum(ROWS)
    left, right = keys[:nl], keys[nl:]
    print(f"{name}: moved to a free key: {' '.join(moved) or 'none'}" + (f"; still lost: {' '.join(still)}" if still else ""))
    boards.append(f'<section><h2>{html.escape(name)}</h2>'
                  f'<div class="board">{half(left, LEFT_COLS)}{half(right, RIGHT_COLS)}</div></section>')

legend = "".join(f'<span class="k {k} lg"><span class="m">{m}</span></span> {t}' for k, m, t in [
    ("result", "😀", "result (number row)"), ("letter", "a", "letter"), ("moved", "х", "letter moved to a free key"), ("space", "␣", "space"),
    ("bksp", "⌫", "backspace"), ("page", "▶", "result page"), ("clear", "Clear", "clear query"),
    ("mode", "🥳", "search on/off"), ("recent", "R1", "recent emoji"), ("base", "Base", "leave the layer")])

page = f"""<!doctype html><meta charset="utf-8"><title>Emoji search keys</title>
<style>
:root {{ --bg:#14161a; --fg:#e8e8e8; --muted:#9aa0a6; --key:#24272d; }}
body {{ background:var(--bg); color:var(--fg); font:14px system-ui,sans-serif; margin:24px; }}
h1 {{ font-size:20px; margin:0 0 4px }} h2 {{ font-size:16px; margin:18px 0 2px }} p {{ margin:0 0 8px; color:var(--muted) }}
.board {{ display:flex; gap:56px }}
.half {{ display:grid; grid-template-columns:repeat(8,52px); grid-template-rows:repeat(5,44px); gap:4px }}
.k {{ background:var(--key); border-radius:6px; display:flex; flex-direction:column; align-items:center; justify-content:center; border:2px solid #333 }}
.k .m {{ font-size:19px; line-height:1.1 }} .k .s {{ font-size:9px; color:var(--muted) }}
.result {{ border-color:#d4a72c; background:#2f2a18 }} .letter {{ border-color:#4a8fd9 }} .space,.bksp {{ border-color:#3fb950; background:#18291c }}
.page {{ border-color:#bc8cff; background:#251d33 }} .clear {{ border-color:#f85149; background:#331a1a }} .clear .m {{ font-size:14px }}
.mode {{ border-color:#bc8cff }} .mode {{ flex-direction:row; gap:5px }} .mode .m {{ font-size:18px }} .tg {{ width:11px; height:22px; border:1.5px solid var(--fg); border-radius:7px; margin-top:3px; position:relative }} .tg::after {{ content:""; position:absolute; top:1.5px; width:7px; height:7px; border-radius:50%; background:var(--fg); left:1.5px; top:2px }} .recent {{ border-color:#555 }} .recent .m {{ font-size:13px; color:var(--muted) }}
.base .m {{ font-size:13px }} .moved {{ border-color:#4a8fd9; border-style:dashed; background:#16243a }} .blank {{ opacity:.35 }}
.legend {{ display:flex; flex-wrap:wrap; gap:6px 14px; align-items:center; margin:8px 0 4px }}
.lg {{ display:inline-flex; min-width:34px; padding:0 4px; height:28px; vertical-align:middle; flex-direction:column }} .lg .m {{ font-size:14px }}
</style>
<p>Query “{html.escape(QUERY)}”: {total} results, page 1 of {max(1, (total + 11) // 12)}.</p>
<div class="legend">{legend}</div>
{''.join(boards)}
"""
OUT.write_text(page, encoding="utf-8")
print(OUT, total)
