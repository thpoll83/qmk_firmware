# TODO: OS-aware shortcut-preview hints — pack glyphs (wave B)

Status: **deferred — needs the `fontconvert` toolchain (AdafruitGFX), which was not
available in the session that wrote this.** The OS-aware *correctness* pass (wave A)
already merged via PR #95 (`keycode_to_disp_overlay` editing/window-management icons
now follow the active OS; redo/lock/minimize/fullscreen fixed). This doc is the spec
for **wave B**: six additional OS-aware hints whose glyphs must live in the **font
pack** (NOT resident).

All six glyphs are sourced **straight from Noto** (no custom art) and go in the font
pack (NOT resident). They were briefly prototyped as hand-drawn resident `IconsFont`
entries and verified at 72×40, then **reverted** — the maintainer wants them
non-resident and is fine using the Noto glyphs directly. Pick this up in a session
where `fontconvert` is buildable.

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

All six are real Noto glyphs — **use them directly, no hand-drawing.** Map each to
its Unicode codepoint so `named_glyphs.h` + `keycode_to_disp_overlay` reference them
like the existing pack hints (e.g. `CLIPBOARD_COPY = U"\x1F4CB"`):

| Define | Codepoint | Char | Source font | Notes |
|--------|-----------|------|-------------|-------|
| `ICON_LAUNCHER`      | `U+2630` | ☰ | **NotoSansSymbols2** | renders clean |
| `ICON_CLOSE`         | `U+26DD` | ⛝ | **NotoSansSymbols2** | renders clean |
| `ICON_APP_SWITCH`    | `U+1F5D4` | 🗔 | **NotoSansSymbols2** | window + cursor; render at ~34 px |
| `ICON_WINDOW_SWITCH` | `U+1F5BD` | 🖽 | **NotoSansSymbols2** | tiled frame; render at ~34 px |
| `ICON_WORD_LEFT`     | `U+21E0` | ⇠ | **NotoSansSymbols v1** | base Arrows block — see fetch note |
| `ICON_WORD_RIGHT`    | `U+21E2` | ⇢ | **NotoSansSymbols v1** | "" |

⚠️ **The only availability gotcha is ⇠/⇢ (`U+21E0`/`U+21E2`).** They live in the base
Arrows block (`U+2190–21FF`), which is in **Noto Sans Symbols *v1*** — the repo's
`fonts/` ships only **Symbols *2***, where those codepoints are `.notdef` (tofu box).
Verified against the font cmap: `2630`, `26DD`, `1F5D4`, `1F5BD` are all in Symbols2;
`21E0`, `21E2` are not. So fetch v1 (next section) for the two dashed arrows; the
other four come straight from Symbols2.

🗔/🖽 are slightly thin/busy at small sizes (🗔 is a window outline with a tiny mouse
cursor; 🖽 has a decorative ragged border) but were chosen and accepted as-is — render
them around **34 px** tall (the preview sweet spot), not smaller.

### Approach: add the codepoints as `fonts.yaml` ranges (all-Noto)

No custom art. Add the six codepoints to a **pack bundle** as `fonts.yaml` entries and
regenerate with `fontconvert`:

- **From NotoSansSymbols2** (already a source in `fonts.yaml`): `2630`, `26DD`,
  `1F5D4`, `1F5BD`. The `symbol` bundle is the natural home (it already holds the
  misc-symbol BMP glyphs). A single small range entry (or a few singletons) covers
  them; pick a `render_height`/`size` that lands 🗔/🖽 near ~34 px at the keycap.
- **From NotoSansSymbols v1** for `21E0`/`21E2`: run `fonts/dl-fonts.sh` (it fetches
  the Noto sources) so the v1 Symbols TTF with the base Arrows block is present, add
  it as a font source in `fonts.yaml`, and add the two codepoints.

Keep every entry **non-resident** (do NOT add to `index.resident_fonts`).

> If a glyph still reads poorly on hardware at the chosen size, the fallback is a tiny
> hand-authored 1-bit GFXfont header (the resident prototype's designs + integrator
> live in this session's git history on the reverted `claude/os-aware-shortcut-hint-icons`
> commits) — but with the sizes above that shouldn't be needed.

---

## 3. fontpack wiring

1. Add the six codepoints as `fonts.yaml` ranges in the **`symbol`** pack bundle (it
   already holds the misc-symbol BMP glyphs). Four come from NotoSansSymbols2; add
   NotoSansSymbols v1 as a source for `21E0`/`21E2`. Keep them **non-resident** (do
   NOT add to `index.resident_fonts`).
2. Regenerate with the pinned `fontconvert`:
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
