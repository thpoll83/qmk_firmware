# The per-keycap display pipeline

Moved out of `CLAUDE.md` 2026-09-14. Verbatim.

### Display rendering pipeline
1. Host sends compressed bitmap → `fill_overlay.c` decompresses (optionally on core1) → `overlays[idx][360]`
2. On key event, `split72.c` selects the keycap via shift-register bitmask and calls `kdisp_invert()` for instant visual feedback
3. Active window change → host sends new overlay set → firmware swaps all 72 keycap images

**Per-keycap rendering gotchas (`base/disp_array.c`)** — learned the hard way:
- **How a legend is actually DRAWN — glyph resolution and the baseline align, the
  column-native bitmap layout, the plotter modes, the courtyard, the whole `HINT_*`
  display-list vocabulary, the three size tiers and the bbox walker — is
  [`keyboards/polykybd/LEGEND_RENDERING.md`](LEGEND_RENDERING.md).**
  Its sibling `LEGEND_LAYOUT.md` covers where the elements GO; that one is
  placement, this one is the drawing primitives. Read it before touching
  `base/disp_array.c`, `base/font_lookup.c`, `base/legend_plan.c` or
  `keycode_helper.c`. Four rules from it apply even if you never open it:
  - ⚠️ **Adding an op is TWO walkers, not one — THREE counting the host.** The draw
    dispatch (`disp_array.c`) and the measurement (`font_lookup.c`) must clear the
    same flags and skip the same arguments, or the bbox describes a legend the draw
    does not produce, and `plan_main_legend()` and `roll_idle_offset()` are then
    working from fiction. `PolyKybdHost`'s `oled_preview.py` + `SUPPORTED_OPS` is
    the third edit, and skipping it is silent: a refused op makes the layout editor
    fall back to the keycode TEXT, which looks exactly like the op not working.
  - ⚠️ **Nudge-run arithmetic is unverifiable by any test in this repo.** A 5-nudge
    lift transcribed as a 4-nudge macro pushed a descender two rows off the panel
    while `-Werror`, 52 bbox tests, cppcheck and `qmk lint --strict` were all green.
    Render every legend you touched through `PolyKybdHost/tools/oled_preview.py` and
    require **0** pixels outside the 72x40 window.
  - ⚠️ **Keep every glyph of one legend in ONE font.** `kdisp_write_gfx_char`
    baseline-aligns by `font->yAdvance - fonts[0]->yAdvance`, so a legend built from
    two faces sits on two baselines — `a»ñ` put its `a` 7 px high. Drawing a lone
    icon through a **single-font array** makes that adjustment 0, which is why the
    language flags use `{ &flag_font }`.
  - ⚠️ **The resident C1 icon band `0x80–0x9F` is FULL (32/32), and `0xA0+` is not
    an option** — it collides with printable Latin-1 and `IconsFont` is
    `g_all_fonts[0]`, so a custom icon parked there silently shadows the real glyph.
    The next resident icon has to go in the pack, or free a slot.
    `python3 tools/check_icon_slots.py` is the only thing that can answer "is this
    slot free?".
- ⚠️ **`render_key()` and `to_static_text()` are a PAIR — both must normalise the
  keycode the same way, or a key draws its chrome and NO legend.** `update_displays()`
  consults `render_key()` exactly when `to_static_text()` returned NULL, which is
  **every letter** (the language translation lives inside `render_key()`). So when
  `to_static_text()` unwrapped a mod-tap keycode and `render_key()` did not,
  `RSFT_T(KC_A)` (`0x3204`) fell through every branch there — `is_letter` is false for
  it and `translate_keycode()` has no row — and the keycap rendered its modifier badge
  in an otherwise **empty cell** (field, 2026-08-18). The unwrap had been in
  `to_static_text()` for years, one function away. This is the sibling of the already-
  documented "`render_key()` is only consulted when `to_static_text()` returns NULL"
  (Intl-remap section): that one is about a key with a legend *bypassing* `render_key`,
  this one is about the same seam producing *no* legend at all. Any future keycode
  rewriting (a second wrapper class, an alias) has to land in **both**.
- ⚠️ **A THIRD seam: `update_displays()` can carry a bespoke `else if (keycode ==
  X)` branch that makes the keycode's legend DEAD — grep for your keycode there
  before believing a legend edit does anything.** `KC_EDEN` had one: it drew its own
  hardcoded `U"Reset"` / `U"Eden"` through `mid_fonts`, so the `keycode_helper.c`
  case was never consulted for the awake keycap and **two successive commits edited a
  string that never rendered**. Worse, the branch sat *after* the `text != NULL` test,
  so the `KC_SETTINGS_MORE` gate's empty string routed straight into it and the key
  stayed visible while every other gated key hid (field, 2026-08-26). Both are gone —
  `HINT_MID` made the size expressible in an ordinary legend, so the special case
  could be **deleted** rather than gated. **Prefer that: a per-keycode branch there
  is a legend the rest of the pipeline cannot see.**
- ⚠️ **`get_local_layer()` is the SYNCED snapshot, not live state — never gate
  RENDERING on it.** It lags a layer change by up to one housekeeping pass, so a
  render landing inside that window reads the OLD layer. The `KC_SETTINGS_MORE` gate
  originally required `get_highest_layer(get_local_layer()->layer) == _SL`; a render
  in that window concluded the gate did not apply and drew the advanced keys, and
  `_SL` usually gets no later refresh to correct it. The clause was also redundant —
  every gated keycode is mapped exactly once, only on `_SL`, in BOTH keymaps — so it
  could never hide anything the keycode list did not, and could reveal what it
  existed to hide. Gate on the keycode and the synced *value*, not on the layer.
- ⚠️ **"Hidden" is TWO invariants — blank AND inert — and the gate covered only the
  drawing half for the two keycodes on that row that cannot be undone.**
  `process_record_user()` intercepts `QK_REBOOT` and `QK_BOOTLOADER` in its
  pressed-edge switch (the bootloader announce, and the reboot's bridged handoff so
  the slave restarts too) and returns `true` from there, while `settings_more_hidden()`
  sat **~200 lines further down**. So the advanced row rendered blank and the blank
  Restart keycap still rebooted the board — which reached the field as *"two crashes in
  a row with the multisplash RGB matrix"* (2026-09-09). The log held no crash: it held
  two presses of a key nobody could see.
  - **The comment beside the gate asserted the premise that made it wrong** — that all
    three of `QK_BOOTLOADER` / `QK_REBOOT` / `QK_DEBUG_TOGGLE` are left to
    `process_action()`. True of `QK_DEBUG_TOGGLE` alone, which is precisely why it was
    the one of the three that really was gated. A comment naming a set is worth
    checking against the set.
  - **The fix is ONE check ABOVE the switch, not a test inside each case** — otherwise
    a third irreversible keycode added later inherits the same hole, which is the
    enumerating-guard shape this file keeps recording. When you gate a keycode for
    *display*, grep `process_record_user()` for it in the same pass.
- **Where the base glyph, the Shift preview and the AltGr hint GO — and how they are
  kept off each other and on the panel — is
  [`keyboards/polykybd/LEGEND_LAYOUT.md`](LEGEND_LAYOUT.md).**
  Read it before touching `lang_lut.xlsx`, `render_key()` or `base/legend_plan.c`;
  the `tune-lang-lut-cells` and `keycap-layout-preview` skills drive that work. Four
  things live there, each with the measurement that produced it: the Shift/AltGr pull
  (24 keys across 19 layouts drew the two hints through each other, and it reads as a
  MISSING GLYPH rather than a layout bug), the build-time Shift-preview suppression
  bitmap (⚠️ **emptying a Shift CELL to hide the preview destroys the uppercase** — the
  cell is both), the four-edge clamp through `legend_plan_clamp()`, and the
  per-category half-size AltGr opt-in.
- **The per-keycap DISPLAY grid is NOT a rectangle** (split72). Only the **bottom
  row (display row 4) is a full 8-wide row**; the upper rows (0–3) have panels at
  **cols 0–6 only** — display **col 7 is a routing phantom** (a `BITMASK` entry
  exists in `split72.c` `key_display[]` but there is no OLED behind it, so writing
  it shows nothing). The two inner **thumb keys** per half live *only* on the bottom
  matrix row (left disp cols 6/7, right 0/1), stacked vertically (same x, different
  y) yet on the same matrix row — so they can't be part of a rectangular block on
  the rows above. Also: `LAYOUT_TO_INDEX(row,col)=row*8+col` **wraps** — `col ==
  MATRIX_COLS` folds into the next row's col 0 (bound `disp_col` to
  `[0, MATRIX_COLS-1]`); and the right half applies a `c--` display-index shift on
  its upper rows (5–8) but not its bottom row (9). ⚠️ **Model placement from the
  OLED chip-select, NOT the RGB `g_led_config` x-order** — they do **not** match:
  because of the `c--` fold, **disp_col 0 is the OUTER edge on the LEFT half but the
  INNER edge on the RIGHT**, so a sweep that looks left→right in RGB space runs
  backwards on the right half's OLEDs. Reasoning from RGB position produced several
  wrong IDDQD-screensaver revisions before this was caught. The composed model +
  verifier is committed as `doom/tools/keycap_dispmap.py` (run it after any
  placement change); full write-up in `doom/README.md` § anti-burn-in placement.
  - ⚠️ **TWO PHYSICAL KEYS have no OLED at all — 74 keys, 72 OLEDs — and the
    in-code comment about them is misleading.** The inner key at matrix **(3,7)**
    on the left half and **(8,0)** on the right have neither an OLED nor an RGB
    LED (both read `NO_LED` in `g_led_config`; both sit at y=2 on the inner edge).
    They are the only two `NO_LED` slots that exist as keys — the other six are
    matrix positions with no key. `invert_display()`'s comment says "on the right
    side of the split layout the first 4 rows have no key", which is true of
    (5,0)/(6,0)/(7,0) — they are absent from `keyboard.json` — but **(8,0) is a
    real key**, so reading the comment as "col 0 never happens" is wrong.
  - **Anything that maps a key to a display must gate on `key_has_display(r,c)`
    FIRST** (declared in each variant header; split42 returns unconditional
    `true`, since all 42 of its keys have OLEDs). `invert_display()` deliberately
    does *not* carry this knowledge — it stays a general "invert the display at
    matrix (r,c)" primitive, and the three callers (the split72 scan loop,
    `hid_com.c`, `split_sync.c`) screen the keys out. ⚠️ **A bounds check is not
    a substitute**: the right key underflows the `c--` fold to 255 and
    `LAYOUT_TO_INDEX` truncates it to **23**, the left indexes **31** directly —
    both in range, both the phantom inner column, so each press *and* release
    latched a chip-select for a slot the key does not own. The old
    `if (disp_idx != 255)` guard was written for exactly this and could never
    fire: it sat *after* the indexed read, and 255 needs `r%5==0`, which no col-0
    key satisfies. Found by cppcheck (2026-08-19), invisible on hardware in both
    directions because the target slots are phantoms.

- **The status OLED — both variants, the composers, the flicker fix, the telemetry
  screen and split42's portrait layout — is
  [`keyboards/polykybd/STATUS_OLED.md`](STATUS_OLED.md).**
  `oled_helper.c` dispatches, each variant's `status_oled.c` composes into the
  shared kdisp scratch buffer, and `oled_write_raw` blits. Four rules worth carrying:
  - ⚠️ **It is a DIFFERENT BUS from the per-keycap displays** — I2C on `I2CD0`
    (GP0/GP1) at 400 kHz, against the SPI panels `disp_array.c` drives. Each half
    drives its own status OLED locally.
  - ⚠️ **Never reintroduce a per-frame `oled_clear()`.** `oled_write_raw` already
    diffs byte-for-byte and dirties only changed blocks; an `oled_clear()` marks all
    16 dirty every 66 ms tick and is what made a full repaint dribble out band by
    band. A composer that needs a full swap ends with `oled_render_dirty(true)`
    instead, which is a no-op when nothing changed.
  - ⚠️ **split42's panel is mounted rotated 90 degrees and the poly pipeline BYPASSES
    QMK's `OLED_ROTATION`** — it blits a raw page-format buffer, so setting
    `OLED_ROTATION_90` does nothing. That variant composes in a logical 32x128
    portrait space and software-rotates each lit pixel, with its own portrait
    primitives.
  - **Layout work MEASURES the pixel bands** rather than eyeballing the render — the
    `status-oled-layout` skill wraps it. `--diag` only catches pixels off the panel;
    it cannot see two rows colliding, which is what it found (a layout name's
    descenders overlapping the row below by 2 px).

