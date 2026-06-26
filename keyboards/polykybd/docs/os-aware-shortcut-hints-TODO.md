# TODO: OS-aware shortcut-preview hints — pack glyphs (wave B)

Status: **deferred — needs the `fontconvert` toolchain (AdafruitGFX), which was not
available in the session that wrote this.** The OS-aware *correctness* pass (wave A)
already merged via PR #95 (`keycode_to_disp_overlay` editing/window-management icons
now follow the active OS; redo/lock/minimize/fullscreen fixed). This doc is the spec
for **wave B**: six additional OS-aware hints whose glyphs must live in the **font
pack** (NOT resident).

The glyphs here were prototyped as hand-drawn resident `IconsFont` entries and
verified to render at 72×40, then **reverted** because the maintainer wants them
non-resident (in the pack). Pick this up in a session where `fontconvert` is
buildable.

---

## 1. The six actions, per OS

Display-only hints drawn on the physical key that produces the native chord for the
active OS (`get_local_state()->active_os`). **No new keycodes.** Each is an entry in
`keycode_to_disp_overlay()` (`poly_keymap.c`).

| Action | Glyph | Windows | macOS | Linux |
|--------|-------|---------|-------|-------|
| Word left | ⇠ `ICON_WORD_LEFT` | Ctrl+← | Option+← | Ctrl+← |
| Word right | ⇢ `ICON_WORD_RIGHT` | Ctrl+→ | Option+→ | Ctrl+→ |
| Launcher / search | ☰ `ICON_LAUNCHER` | Win+S | Cmd+Space | Super+S |
| App switcher | 🗔 `ICON_APP_SWITCH` | Alt+Tab | Cmd+Tab | Alt+Tab |
| Window switcher | 🖽 `ICON_WINDOW_SWITCH` | Win+Tab | **Cmd+`** | Super+Tab |
| Close / quit | ⛝ `ICON_CLOSE` | Ctrl+W · Alt+F4 | Cmd+W · Cmd+Q | Ctrl+W · Alt+F4 |

Also (uses existing `ARROWS_LEFTSTOP`/`ARROWS_RIGHTSTOP`, no new glyph):
- Line start / end — macOS only: **Cmd+← / Cmd+→** (Home/End are dedicated keys on
  Win/Linux). NOTE: macOS Cmd+← / → therefore collides with "word left/right" — on
  macOS word-nav is **Option+arrows**, line-nav is **Cmd+arrows**, so they live on
  different modifiers and do not conflict.

**macOS window switcher = Cmd+`** (backtick / `KC_GRV`) — cycles windows of the
current app. (Mission Control / Exposé use Ctrl+↑ / Ctrl+↓, which are not clean
single-Cmd combos, so Cmd+` is the chosen hint.)

---

## 2. Glyph codepoints + art

All six map to a real Unicode codepoint so `named_glyphs.h` + `keycode_to_disp_overlay`
reference them like the existing pack hints (e.g. `CLIPBOARD_COPY = U"\x1F4CB"`):

| Define | Codepoint | Char | Source |
|--------|-----------|------|--------|
| `ICON_WORD_LEFT`     | `U+21E0` | ⇠ | NotoSansSymbols (v1, arrows) — **hand-draw preferred** (see below) |
| `ICON_WORD_RIGHT`    | `U+21E2` | ⇢ | "" |
| `ICON_LAUNCHER`      | `U+2630` | ☰ | NotoSansSymbols2 (renders cleanly — could just add the range) |
| `ICON_APP_SWITCH`    | `U+1F5D4` | 🗔 | **hand-draw** (Noto's is a faint empty window) |
| `ICON_WINDOW_SWITCH` | `U+1F5BD` | 🖽 | **hand-draw** (Noto's is too busy at 72×40) |
| `ICON_CLOSE`         | `U+26DD` | ⛝ | NotoSansSymbols2 (renders cleanly) |

⚠️ Probing showed: **`U+21E0`/`U+21E2` are tofu boxes in NotoSansSymbols2** (the v2
font shipped in `fonts/`), and **`U+1F5D4`/`U+1F5BD` render poorly** at this size.
`U+2630` and `U+26DD` are already present in the current pack and render fine. So a
straight `fonts.yaml` range addition only gives you two of the six in good shape.

### Recommended approach: a tiny hand-authored pack font

Because four of the six don't come out well from the Noto TTFs, author a small
dedicated GFXfont header (like `gfx_icons.h` but **not** resident — listed under a
pack bundle in `fonts.yaml`/`bundles`), containing the six glyphs at their real
codepoints. The approved hand-drawn 1-bit designs are reproduced by this script
(run with the repo `.venv` that has Pillow):

```python
# hint_glyphs.py — approved hand-drawn designs (1-bit), verified at 72x40 via
# PolyKybdHost/tools/oled_preview.py. Heights chosen so the Tab-key hints clear
# the narrow ARROWS_TAB base legend.
def _grid(w,h): return [["."]*w for _ in range(h)]
def _rows(g): return ["".join(r) for r in g]
def _hline(g,x0,x1,y,t=1):
    for yy in range(y,y+t):
        for x in range(x0,x1+1):
            if 0<=yy<len(g) and 0<=x<len(g[0]): g[yy][x]="#"
def _vline(g,x,y0,y1,t=1):
    for xx in range(x,x+t):
        for y in range(y0,y1+1):
            if 0<=y<len(g) and 0<=xx<len(g[0]): g[y][xx]="#"
def _rect(g,x0,y0,x1,y1,t=2):
    _hline(g,x0,x1,y0,t); _hline(g,x0,x1,y1-t+1,t); _vline(g,x0,y0,y1,t); _vline(g,x1-t+1,y0,y1,t)
def _fill(g,x0,y0,x1,y1):
    for y in range(y0,y1+1):
        for x in range(x0,x1+1):
            if 0<=y<len(g) and 0<=x<len(g[0]): g[y][x]="#"
def _diag(g,x0,y0,x1,y1,t=2):
    n=max(abs(x1-x0),abs(y1-y0))
    for i in range(n+1):
        x=round(x0+(x1-x0)*i/n); y=round(y0+(y1-y0)*i/n)
        for dx in range(t):
            for dy in range(t):
                if 0<=y+dy<len(g) and 0<=x+dx<len(g[0]): g[y+dy][x+dx]="#"

def word_left():            # 26x13 dashed left arrow
    W,H=26,13; g=_grid(W,H); my=H//2
    _diag(g,1,my,8,1); _diag(g,1,my,8,H-2)
    x=8
    while x<W-1: _hline(g,x,min(x+2,W-1),my-1,2); x+=5
    return _rows(g)
def word_right():
    return ["".join(reversed(r)) for r in word_left()]
def launcher():             # 28x20 hamburger (3 bars)
    W,H=28,20; g=_grid(W,H)
    for y in (1,9,17): _fill(g,0,y,W-1,y+3)
    return _rows(g)
def app_switch():           # 24x20 two overlapping windows
    W,H=24,20; g=_grid(W,H)
    _rect(g,6,0,23,13,2); _fill(g,6,0,23,2)
    for y in range(4,20):
        for x in range(0,18): g[y][x]="."
    _rect(g,1,5,17,19,2); _fill(g,1,5,17,7)
    return _rows(g)
def window_switch():        # 22x20 2x2 tiled frame
    W,H=22,20; g=_grid(W,H)
    _rect(g,1,1,20,18,2); _vline(g,10,1,18,2); _hline(g,1,20,9,2)
    return _rows(g)
def close_box():            # 26x26 squared saltire (box + X)
    W,H=26,26; g=_grid(W,H)
    _rect(g,1,1,24,24,2); _diag(g,3,3,22,22,2); _diag(g,22,3,3,22,2)
    return _rows(g)
```

Pack each row-grid to a GFXglyph the same way `gfx_icons.h` is built (MSB-first,
continuous-bit-packed, byte-padded per glyph; `xOffset = 0` so the leading spaces in
the hint strings position it; `yOffset = (40 - h)//2 - 23` to vertically centre at
baseline 23). The integrator that did this for the resident prototype is preserved in
the session scratchpad (`integrate_hints.py`) — adapt it to emit a standalone pack
header instead of appending to `IconsFont`.

Alternatively, if you'd rather use Noto for ⇠/⇢/☰/⛝ (so only 🗔/🖽 are hand-drawn),
run `fonts/dl-fonts.sh` to fetch **NotoSansSymbols (v1)** for the dashed arrows
(v2 lacks them) and add the four codepoints as `fonts.yaml` ranges in a pack bundle.

---

## 3. fontpack wiring

1. Add the new font (hand-authored header or `fonts.yaml` ranges) to a **pack bundle**
   — `symbol` is the natural home for these (it already holds the misc-symbol BMP
   glyphs). Keep it **non-resident** (do NOT add to `index.resident_fonts`).
2. Regenerate with the pinned `fontconvert` (only needed if you take the `fonts.yaml`
   route; the hand-authored header needs no fontconvert):
   `FONTCONVERT=/tmp/fontconvert_pinned python3 fonts/generate_fonts.py`
3. Rebuild the bundles + bump the `symbol` bundle `content_version` so hosts re-flash:
   `python3 fonts/generate_fonts.py --emit-bundles <dir> --bundle-version symbol=<N+1>`
4. Reship the bundle: copy the new `symbol.plyf` + updated `bundles.json` into
   `PolyKybdHost/polyhost/res/fontpack/` (the host auto-flashes stale bundles on
   connect; see qmk CLAUDE.md "Font pack").
5. `generate_fonts.py --check` must pass.

These hints render only when the pack is present (same as the existing copy/paste
hints) — that's the intended, non-resident behaviour.

---

## 4. `named_glyphs.h`

```c
#define ICON_WORD_LEFT      U"\x21E0"
#define ICON_WORD_RIGHT     U"\x21E2"
#define ICON_LAUNCHER       U"\x2630"
#define ICON_APP_SWITCH     U"\x1F5D4"
#define ICON_WINDOW_SWITCH  U"\x1F5BD"
#define ICON_CLOSE          U"\x26DD"
```

---

## 5. `keycode_to_disp_overlay()` edits (poly_keymap.c)

Build on the wave-A structure already in tree (apple / non-apple split). Leading
spaces position each icon to the right of the key's base legend; the values below
were tuned against `oled_preview.py` (Tab uses the narrow `ARROWS_TAB` base, so 4
spaces clear it).

**Apple branch** — add an Option(Alt)+arrows block (word nav) and extend the `cmd`
switch:

```c
        // (top of the apple branch, after cmd/ctrl are computed)
        if ((local_mods & MOD_MASK_ALT) != 0 && !cmd) {
            switch(keycode) {
                case KC_LEFT:  return U"    " ICON_WORD_LEFT;
                case KC_RIGHT: return U"    " ICON_WORD_RIGHT;
                default: break;
            }
        }
        // ... inside the existing `if (cmd) { switch(keycode) ... }`:
                case KC_TAB:   return U"    " ICON_APP_SWITCH;    // Cmd+Tab
                case KC_SPACE: return U"   "  ICON_LAUNCHER;      // Cmd+Space (Spotlight)
                case KC_W:     return U"    " ICON_CLOSE;         // Cmd+W
                case KC_Q:     return U"    " ICON_CLOSE;         // Cmd+Q
                case KC_GRV:   return U"    " ICON_WINDOW_SWITCH; // Cmd+` window switcher
                case KC_LEFT:  return U"    " ARROWS_LEFTSTOP;    // Cmd+Left  line start
                case KC_RIGHT: return U"    " ARROWS_RIGHTSTOP;   // Cmd+Right line end
```

**Non-apple branch** — extend the Ctrl switch, add an Alt branch, extend the wm
(GUI) switch:

```c
        // ... inside the `if (ctrl) { switch(keycode) ... }`:
            case KC_LEFT:  return U"    " ICON_WORD_LEFT;   // Ctrl+Left
            case KC_RIGHT: return U"    " ICON_WORD_RIGHT;  // Ctrl+Right
            case KC_W:     return U"    " ICON_CLOSE;       // Ctrl+W
    } else if ((local_mods & MOD_MASK_ALT) != 0) {
        switch(keycode) {
            case KC_TAB: return U"    " ICON_APP_SWITCH;    // Alt+Tab
            case KC_F4:  return U"    " ICON_CLOSE;         // Alt+F4
            default: break;
        }
    } else if (wm_held) {
        // ... existing D/L/P/UP/DOWN, plus:
            case KC_TAB: return U"    " ICON_WINDOW_SWITCH; // Win/Super+Tab
            case KC_S:   return U"   "  ICON_LAUNCHER;      // Win+S search
```

---

## 6. Verify

For each glyph and each in-key hint, render through `PolyKybdHost/tools/oled_preview.py`
(`gfx_font.load_all_fonts` + `oled_preview.Renderer.draw` at `BUFFER_X=28`,
`BASELINE=23`) per OS — confirm no overlap and no edge-clipping (yellow pixels in
`oled_to_rgb`). The Tab-key hints are the tight case; the app/window-switch glyphs
were sized compact (≤24 px wide) specifically to clear the `ARROWS_TAB` base legend.

Then build `split72:default`, objcopy the `.bin`, flash + a font-pack sync, and
eyeball on hardware.
