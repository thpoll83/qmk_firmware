# How a keycap legend is DRAWN

The composition half of the keycap legend: how a glyph is resolved and placed, the
plotter modes, the courtyard, the display-list op vocabulary (`HINT_*`), the three
size tiers, and the two walkers that must agree about all of it. Moved out of
`CLAUDE.md` on 2026-09-10 — ~58 KB read while you are in `base/disp_array.c`,
`base/font_lookup.c`, `base/legend_plan.c` or `keycode_helper.c`.

**Its sibling is [`LEGEND_LAYOUT.md`](LEGEND_LAYOUT.md)**, which covers WHERE the
base glyph, the Shift preview and the AltGr hint go. This file is HOW any of them
reaches the panel. Read that one for placement across languages, this one for the
drawing primitives and the legend's own mini display list.

Two rules govern nearly everything here, and both are the reason a note is kept
whole with its measurement:

⚠️ **Adding an op is TWO walkers, not one — three counting the host.** The draw
dispatch in `base/disp_array.c` and the measurement in `base/font_lookup.c` must
agree, or the bbox describes a legend the draw does not produce and every consumer
of that box is working from fiction. `PolyKybdHost`'s `oled_preview.py` (and its
`SUPPORTED_OPS`) is the third edit, and skipping it is silent — a refused op makes
the layout editor fall back to the keycode *text*, which looks exactly like the op
not working.

⚠️ **Nudge-run arithmetic is unverifiable by any test in this repo.** `-Werror`, 52
bbox tests, cppcheck and `qmk lint --strict` were all green while a legend's
descender sat two rows off the panel. The only check is rendering every legend you
touched through `PolyKybdHost/tools/oled_preview.py` — the firmware's own
interpreter — and counting pixels outside the 72x40 window. Require **0**.

---

### Per-keycap rendering gotchas (`base/disp_array.c`)

- **`kdisp_write_gfx_char` baseline-aligns every glyph to `fonts[0]`**:
  `y += currentFont->yAdvance - fonts[0]->yAdvance`. So drawing a *single* icon
  whose font differs in height from `g_all_fonts[0]` (IconsFont, yAdvance 40)
  shifts it vertically by the difference. This was the **language-flag gap-at-top
  regression** when flags moved into the pack (flag yAdvance 54 − 40 = +14 px down,
  filling 0..39 → 14..53). **Fix pattern: draw such a glyph through a *single-font
  array* `{ that_font }`** so `fonts[0]` is the glyph's own font (adjustment 0), as
  the old compiled-in `{ &flag_font }` path did. `kdisp_gfx_glyph_font(fonts, n, cp,
  &out_font)` returns the glyph **and** its owning font in one scan for exactly this
  (`kdisp_gfx_glyph` is the `out_font = NULL` wrapper).
  - ⚠️ **The same shift makes a LONE Latin-1 symbol sit as low as a descender, and
    that is what "this glyph renders under the baseline" means in practice.** The
    two notes above frame the yAdvance gap as a *multi-glyph* hazard (`à»ñ`); the
    single-glyph corollary is separate and is a per-cell tuning matter. `_Base_` is
    yAdvance 37 and `_SupAndExtA_` is 44, so **every** Latin-1 supplement glyph is
    drawn 7 px lower than a base-face letter: measured against the base face's own
    ink bottom, `§ £ ± ¢ ¥ ½ ¼ ¾ © ®` all land where a `y` descender does, and
    `µ ¦ ¸` deeper still. It is not a bug in any one glyph — it is the whole font.
    - **The fix is the cursor nudge in the LUT cell**, `\f` = 2 px up (`\b` = 2 px
      left), so a −6 px lift is `U"\f\f\f" <TOKEN>` prepended to the cell. That is
      what the hand-tuned cells already used; the 2026-09-03 pass normalised
      `§ £ ± µ` to exactly three across all 117 cells that draw them, having found
      them spread over **five** different values (0, −2, −4, −6, −8).
    - ⚠️ **The panel clamp can EAT the nudge, so measure the realised move rather
      than the requested one.** A glyph already jammed against the south edge spends
      part of the lift merely releasing that clamp: of 92 element positions that
      moved, 13 moved 1–4 px instead of 6 (`af-ZA`/`se-NO` `KC_3` by 1 px). Nothing
      reports this — the cell says −6 and the keycap moved 1.
    - **Survey before tuning, per distinct codepoint, not per cell**: resolve every
      single-glyph cell, take `Renderer.bbox([cp])`, and rank by `ymax`. Symbols the
      font pushes down separate cleanly from real descenders (`g j p q y`, Arabic,
      Hebrew nikud, brackets) that way, and it is the only way to see that a glyph
      is inconsistently nudged across layouts.
- **GFXfont bitmaps are COLUMN-NATIVE (OLED page format) since the PolyColGfx
  rollout (font-pack ABI 2).** 1 byte = 8 *vertical* pixels, so the firmware blits a
  whole column-byte into the SSD1306 page memory at once. `cb = (h + 7) >> 3`
  page-bytes per column; index `bitmap[bitmapOffset + xx*cb + (yy>>3)]`, bit
  `1 << (yy & 7)` (**LSB = top of the page**); a glyph is `width * cb` whole bytes.
  Canonical types are `PolyColGfx`/`PolyColGlyph` (`base/fonts/gfxfont.h`) with
  `GFXfont`/`GFXglyph` kept as compat aliases. ⚠️ **NOT** the classic Adafruit
  row-major layout (`bit = yy*w + xx`, MSB-first) — a row-major reader (or an
  un-transposed header, see below) produces garbage that *looks* like dithering
  noise. `fontconvert` emits column-native (`emit_buf_col`); the ABI-2 `.plyf` packs
  and every reader (host + firmware) match, so it's a coordinated host↔firmware↔rig
  ABI 1→2 change (a keyboard rejects a mismatched-ABI pack by design). Column
  padding grows the *resident* fonts ~10 KB in the image (only glyphs whose height
  isn't a multiple of 8 grow — IconsFont at h=40 grew 0 B) — the inherent, benign
  cost of one uniform format with no runtime transpose cache.
  - ⚠️ **TWO layouts coexist and a 72×40 image is EXACTLY 360 bytes in BOTH, so
    crossing them fails silently — no size mismatch, no crash, just a scrambled
    read.** Font glyphs are column-native (above, read by `kdisp_write_gfx_char`);
    **overlay images are ROW-MAJOR MSB-first** — 9 bytes/row × 40 (host:
    `np.packbits` over the 40×72 mask; firmware: `kdisp_draw_bitmap`, index
    `pgm_bmp[y*byte_width + (x>>3)] & 0x80`) — versus 5 page-bytes/col × 72
    column-native. **Any helper that reads a bitmap must be paired with the DRAW
    function that owns its layout.** `b69eddcf` moved `kdisp_clear_bitmap_courtyard`
    to column-native for its glyph caller and left the row-major overlay call site
    behind: the courtyard then dilated a garbage mask and wiped **82 %** of the
    keycap (measured on a shipped template cell) instead of the intended 39 %,
    erasing the legend underneath. Hence the split into
    `kdisp_clear_bitmap_courtyard` (column-native) / `kdisp_clear_rowmajor_courtyard`
    (row-major) — named by layout, deliberately not a `bool` parameter, so the
    pairing is visible at the call site.
  - ⚠️ **`base/fonts/gfx_icons.h` (IconsFont + a NULL-bitmap HelperFont) is
    hand-maintained and lives OUTSIDE `fonts.yaml`/`generated/`, so bulk font-header
    tooling silently skips it.** Any glyph-format change (the column transpose, a
    future re-pack) MUST include it explicitly — missing it leaves every resident
    icon (layer / arrows / caps+num lock / OS logos / mouse buttons) rendering
    garbage while pack glyphs look fine (the exact 2026-07 symptom that cost a debug
    round on hardware).
  - ⚠️ **When transposing/parsing a committed header, STRIP the `/* 0x80 ICON_LAYER
    14x16 */` comment tags before pulling `0x..` bytes** — the tag hex pollutes a
    naive byte regex and shifts the whole array. And **verify against the rendered
    glyph shape, not a transform∘inverse round-trip**: a self-check that reads back
    through the same (polluted) data falsely reports "0 mismatches" — that shipped a
    broken IconsFont fix once; ASCII-rendering LAYER/LEFT/RIGHT against the row-major
    source is what actually caught it.

- **Composable plotter modes** (`disp_array.c/.h`, static flags toggled around a
  draw): `kdisp_set_gfx_erase(bool)` makes the glyph plotter **clear** pixels
  instead of setting them, and `kdisp_set_gfx_scanline(bool, phase)` /
  `kdisp_set_gfx_scanline2(bool, phase)` light only every other row (1-on/1-off) or
  2-on/2-off bands. The gate is on the **ABSOLUTE buffer y**, not a glyph-local
  row, so two glyphs at different y still interleave into one consistent pattern.
  Used to render the Eden idle legend as a dim half-density overlay. Always pair the
  set with a reset (`(false, 0)`) after the draw.
  - ⚠️ **`phase` exists because that absolute gate is a BURN-IN trap, and the drift
    offset does not spring it for you.** Left at a fixed phase the fine mode lights
    the even panel rows and only ever the even panel rows — every idle session, for
    the life of the board — so half the panel takes the legend's entire share of the
    wear and the other half takes none. `kdisp_set_draw_offset()` does not spread
    it: it moves the **cursor** (`gfx_text_run`), i.e. it slides the glyph past a
    *stationary* stripe pattern, changing which rows OF THE GLYPH are dropped while
    the lit PANEL rows never move. Only rolling the phase moves the stripes. It is a
    parameter of the enable rather than a setter of its own so that a caller cannot
    turn the mode on without answering that question, and so a phase cannot leak
    into the next keycap of the same pass.
  - **The Eden legend rolls it off the SAME `epoch` as its position drift**
    (`poly_keymap.c`, a third salt `0x2000` on `jitter_axis`, so it is uncorrelated
    with dx/dy): the stripe flip then lands on the very frame the letter jumps
    anyway, which is what keeps it invisible instead of a shimmer. The boot splash
    passes phase 0 deliberately — it runs for a few seconds, so a fixed alignment
    costs nothing and a rolling one would read as flicker across the reveal.
  - ⚠️ **`disp_array.c` has no unit suite** (it owns the scratch buffer), so the
    phase table was checked by compiling `scanline_skip_row()` standalone: phase 0
    reproduces the old pattern **exactly** (so nothing that passes 0 changed), fine
    phase 1 is its exact complement (every panel row reachable across the two),
    coarse phase 0..3 gives four distinct band alignments, an out-of-range phase is
    masked, and `(false, …)` ignores it. Do the same rather than eyeballing it — the
    modes are invisible from every preview tool (`oled_preview.py` models no plotter
    mode) and on hardware only over months.

- **To draw an INVERTED keycap, render it inverted — do NOT reach for
  `kdisp_invert()`.** That is a panel-level SSD1306 command, and `split72.c`'s
  `matrix_scan_kb` already toggles it on every press and un-toggles on release,
  independently of `process_record`. So any *state* driven through it is undone by
  the next keypress on that key — which is exactly what a latched indicator must
  survive. Render it instead: `kdisp_set_buffer(0xFF)` for the ground +
  `kdisp_set_gfx_erase(true)` around the legend draw (paired reset after — the flags
  are static, so leaving erase on blanks every following keycap in that pass). Gate
  it on **synced** state (`poly_layer_t`), not a master-only static, or the slave
  half won't follow. The Intl picker's armed-Ctrl indicator is the worked example.

- ⚠️ **The courtyard clear is WRONG on a deliberately-filled ground — pass
  `cy_radius` 0 there.** `kdisp_write_gfx_text_cy(..., KDISP_CY_DEFAULT)` clears a
  3px margin around each glyph so a legend punches cleanly through whatever is drawn
  *underneath* (a tab frame, a row bar, an overlay image). On an inverted keycap
  there is nothing underneath — the fill **is** the thing you want to keep — so the
  clear eats a dark halo out of it and the key reads as *outlined* rather than
  inverted (field, 2026-08-11: "the inversion looks like it has also a courtyard").
  `kdisp_write_gfx_text` is itself just `_cy(..., 0)`, so radius 0 is the documented
  no-courtyard path, not a new mode. ⚠️ Check **both** draw paths: a bottom/thumb-row
  legend goes through `draw_legend_cx()` (now `draw_legend_cx_cy()`), everything else
  through `kdisp_write_gfx_text_cy()` directly — on split72 `MATRIX_ROWS_PER_SIDE` is
  5, so the Ctrl at `[4,0]` takes the *bottom-row* one and fixing only the obvious
  call site changes nothing.
- ⚠️ **`kdisp_write_gfx_text_cy()` walks the list TWICE when a courtyard is asked
  for, and the second pass is what makes the first correct — do not "optimise" it
  back to one.** The courtyard is cleared per GLYPH, immediately before that glyph
  is plotted, so glyph N+1's 3px margin removed glyph N's ink wherever the two sat
  closer than the radius: every letter cut a slice out of the one before it (field,
  2026-08-26). Pass 1 clears and draws as before; pass 2 redraws with NO clearing
  and restores what was removed. Underlying art stays cleared because nothing
  redraws it, so the courtyard keeps punching the legend through a tab frame / row
  bar / overlay image and stops eating the legend. Safe because every drawing op is
  idempotent (glyphs OR in, badge/frame fills are stable, an erase-mode glyph clears
  the same pixels twice), so pass 2 can only put ink back.
  - **It was never mid-face-specific, which is why it survived so long.** Measured
    ink loss: `SCRIPT:/Rune` **-6.2%**, `Qwerty` -4.1%, `IDLE:/Pulse` -3.0% — but the
    27px `Qwty` lost 10px too. The tighter 19px spacing only made a long-standing
    defect impossible to miss. **The check is a pixel count against the same legend
    drawn with `cy_radius` 0**, not a visual read; all 15 legends now match it exactly.

- ⚠️ **The anti-burn-in jitter offset is applied ONCE at the display-list cursor
  (`gfx_text_run`), NOT inside the drawing primitives — it used to be the other way
  round and that moved only the text.** `s_draw_ox/oy` were added in
  `kdisp_write_gfx_char` and `kdisp_write_gfx_char_half`, which between them cover
  ordinary glyphs and `HINT_SMALL` and nothing else: every **composite op** plots
  through a primitive of its own (`\x0F` HALF, `\x11` THIN, `\x15` ROT →
  `kdisp_draw_glyph_*_at`; `\x13` BADGE, `\x12` FRAME → the rect drawers), none of
  which ever saw the offset. So an idle relocation slid the letters and left the
  composited art pinned to the buffer — reported from hardware as *"the cursor on the
  context menu icon is not moving in the idle modes, but the hamburger menu icon
  does"* (2026-08-31). It is a CLASS bug, not one icon: `ICON_CONTEXT_MENU`'s pointer,
  and the `ICON_SCRLOCK_ON/OFF` + `ICON_MEDIA_STOP` badges, which are composite art
  **only** and so never moved at all.
  - **The fix is the choke point, not five more `+= s_draw_ox` lines.** Offsetting the
    cursor once means a sixth composite op inherits it by construction; adding it per
    primitive is the enumerating-guard shape this repo keeps getting caught by
    (`sync_is_link_fault()`, the log-source registry, the `find_matching_entry` gate).
    ⚠️ A MOVE must **re-apply** it (`x_cursor = sat8(text[1] + s_draw_ox)`) — an
    absolute position is exactly what the cursor's own offset does not reach, and
    assigning the raw coordinate is what pinned the art in the first place.
  - ⚠️ **Fixing the DRAW alone would have made it worse, and that is the half worth
    remembering.** `roll_idle_offset()` measured the slack with the RELATIVE bbox,
    which for `ICON_CONTEXT_MENU` is the hamburger alone — so once the whole cell
    moved as a unit, that slack would have carried the pointer off the panel; and for
    `ICON_MEDIA_STOP`, whose legend is a MOVE plus a BADGE and nothing else, the
    relative box is **empty** (`0,0,0,0`), i.e. "you may move it anywhere". Hence
    `kdisp_gfx_text_bbox_abs()` above, and hence the pair had to land together.
  - **The ROT geometry moved to `font_lookup.c` (`kdisp_gfx_rot_half_extent`) so the
    drawer and the measurement cannot disagree** about which pixels a rotated glyph
    touches — a box that disagrees with the pixels is a legend that clips. ⚠️ Its
    tests derive the expectation from **arithmetic** (a 0° turn is exactly the halved
    size; a 90° turn swaps the axes), because the first version asked the helper what
    to expect and therefore agreed with it by construction — a "don't halve the width"
    mutation sailed straight through. Same trap as the macro-icon preview note below.
  - ⚠️ **The offset was not the only per-primitive property the composite ops
    bypassed — `s_gfx_erase` and `s_gfx_scanline` were the SAME shape, and the
    scanline one was live.** Both are static plotter modes that only the two char
    writers honoured, so under `IDLE_STYLE_EDEN` — which draws the resting legend
    `kdisp_set_gfx_scanline(true, phase)` as a dim half-density ghost — the text came out
    half-density while the composited art stayed fully lit. Measured over the shipped
    legends, three carry composite art AND are reachable as a resting legend (i.e. at
    idle), all three on the split72 default keymap: `ICON_SCRLOCK_ON/OFF` (`KC_SCRL`,
    a 19×19 `HINT_BADGE`), `ICON_MEDIA_STOP` (`KC_MSTP`, **badge only** — so the whole
    keycap ignored the dimming) and `ICON_CONTEXT_MENU` (`KC_APP`, the ROT'd pointer).
    The other three composite legends (`ICON_GFX_RESTART`/`_RELOAD`, and the
    `HINT_FRAME` hints) are held-modifier hints, never drawn at idle.
    - **Same remedy, one level down: `kdisp_plot_ink()`.** Every ink primitive —
      HALF / THIN / ROT / BADGE / FRAME / `_double_at`, and both char writers — plots
      through it, so it is now the single definition of "ink" and a sixth op inherits
      both modes by construction.
    - ⚠️ **Deliberately NOT pushed down into `SET_PIXEL_CLIPPED`.** Ground fills and
      bitmap blits (`kdisp_fill_rect`, `kdisp_draw_bitmap`, the tab/MRU chrome,
      `clear_line`) must stay unconditional, or an overlay image drawn while the flag
      is up would silently scanline-dim. The split is what the primitive **is**, not a
      list of call sites to keep in sync.
    - **It made the image SMALLER**: `.text` 285320 → 284320 (**−1000 B**), `.data`
      and `.bss` byte-identical (monolith `.heap` free 2772 either way) — fourteen
      duplicated per-pixel plot sequences collapsed into one out-of-line call. Verified
      in the compiled image rather than the source: `kdisp_plot_ink` is emitted
      out-of-line and `objdump` shows every composite primitive `bl`-ing it.
    - ⚠️ **`disp_array.c` has NO unit suite** (it owns the scratch buffer), so this
      class is only ever caught by reading the code or by looking at hardware —
      `oled_preview.py` refuses these ops, so the usual "render it" rule does not
      reach them either. That is why the same mistake was made twice in one file.

- **`kdisp_send_window()` vs `kdisp_send_buffer()`**: `kdisp_send_buffer()` pushes
  the full 1024-byte scratch; `kdisp_send_window()` pushes only the **visible 360
  bytes** (pages 0–4 at column `BUFFER_X`) — the same region the keycap actually
  shows — so it is ~2.9× less SPI. Prefer `kdisp_send_window()` for any per-key
  redraw that only touches the visible window (the standard case).

- **To preview a keycap faithfully, use `PolyKybdHost/tools/oled_preview.py`** (its
  `gfx_font` loader + `oled_to_rgb`) — it parses the generated headers correctly and
  renders the real 72×40 OLED look. A hand-rolled renderer cost two wrong "flag
  offset" guesses this session before the real cause (the baseline-align above) was
  found. `gfx_font.load_all_fonts(base/fonts)` includes `flag_fonts.h`, so it can
  render pack/flag glyphs too. **Caveat:** the preview models glyph `xOffset/yOffset`
  but NOT the `kdisp` baseline-align shift, so it won't reproduce that bug — reason
  about `fonts[0]` separately.

- ⚠️ **A hint/overlay string is drawn OVER the legend at the SAME origin, so
  full-size extra art ERASES it — a secondary mark belongs MOVE'd into a corner, and
  that corner is the BOTTOM-right.** `update_displays()` draws the legend at
  `(BUFFER_X, 23)`, sets `text = NULL`, then draws `keycode_to_disp_overlay()`'s
  string at the *same* origin with `KDISP_CY_DEFAULT` — whose 3px courtyard clears the
  legend underneath before the glyph even lands. That is correct for a held-modifier
  shortcut hint (the whole keycap *means* Ctrl+C while Ctrl is down) and wrong for a
  mark that must coexist with the legend. The bottom anchor is not taste:
  `render_key()` draws the **shift preview in the UPPER right** (baseline 23, x from
  `*_HOFFSET VAR_SHIFT`), so a top-anchored corner mark lands on it. Measured badge-
  ink-on-legend-ink over all 15 modifier combinations: a letter is 0 either way
  (en-US sets `LETTER_*_OFFSET VAR_SHIFT` to `HIDE_KEY`), but a **digit/symbol key
  always has a preview** and went from 21 px of overlap at 2 marks (top-anchored) to
  21 px at 4 marks only (bottom-anchored). ⚠️ **Measure that as the INTERSECTION of
  the two ink sets** — a "how many legend pixels survived" count reads **0 damage**
  for a real collision, because overlapping lit-on-lit loses no pixels and still reads
  as merged (that metric hid the digit collision for a round). The
  `keycap-layout-preview` skill wraps the whole measure-don't-eyeball loop.

---

### The utility layer's remaining text keys (`keycode_helper.c`, `poly_keymap.c`)

Three `_UL` keys still spelled themselves out in four letters while every neighbour
drew an icon, and one pair of keys was replaced by a single state-reflecting key.

- **Mute is the speaker we already had, with a cancellation X beside it** —
  `PRIVATE_MUTE` (U+1F568) + U+1F5D9, placed by the ordinary cursor advance.
  U+1F507 (the emoji cancelled speaker) was shipped first and reverted: at 40×39 the
  slashed circle fills the whole cell and reads as busy rather than as "muted".
  U+1F5D9 is the crispest of the three X glyphs already in the pack (U+2717 is a
  script ballot X, U+2718 a heavy one) and comes from the same `Window` font the
  legend-size icons use.
- **Why the old glyph failed, which is the part worth keeping:** The old `PRIVATE_MUTE` (U+1F568) is
  a speaker with **no wave arcs**, i.e. it differs from `PRIVATE_VOL_DOWN` (U+1F569,
  one arc) and `PRIVATE_VOL_UP` (U+1F56A) only by an *absence* — nothing on it says
  "muted", which is exactly how it was reported. ⚠️ **Do not "finish the family" by
  moving the volume keys to U+1F508/U+1F50A**: those NotoEmoji glyphs are filled and
  render visibly heavier beside the NotoSansSymbols2 line art (rendered and compared).
  - A slash **composited over** the speaker cannot work, which is why the X sits
    BESIDE it: a legend display list has no erase op, so a lit slash over a solid
    glyph merges into it, and a dark-gap version would need a baked glyph — which the
    **full** C1 band has no room for (see the icon-slot note above). The speaker is
    only 19 px wide, so there is room for a separate mark at no cost.
- **Scroll Lock keeps the word and gains a STATE badge**: `U"Scr"` + `ARROWS_DOWNSTOP`
  (U+2B73, the glyph the **status OLED** already lights for this state) at half size
  inside a rounded box that goes **solid when the lock is engaged** — the same shape
  Caps Lock and Num Lock use, which is why it reads at a glance. `led_t.scroll_lock`
  rides `poly_layer_t.led_state`, which is **synced**, so the slave half shows it too.
  - **The status OLED no longer shows scroll lock** (`split72/status_oled.c`). It used
    to draw the same glyph while engaged, but with no *off* state and by **replacing
    the L/R side marker** — so the half lost its side marker exactly while the lock was
    on. The keycap badge supersedes it, which is where Caps and Num are read from
    anyway. split42's panel never had one.
  - ⚠️ **The badge is DRAWN, not baked**, and that is forced: the resident C1 band is
    full (32/32), so there is nowhere to put the OFF/ON glyph pair Caps and Num each
    get. **`HINT_BADGE` (`\x13`, args `w, h, style`)** draws either state — style 1 a
    2px outline, style 2 the solid — and **`HINT_ERASE` (`\x14`)** punches the arrow
    back out of the solid one; that knock-out is what makes the engaged state read as
    *inverted* rather than as a blob.
  - ⚠️ **The corner radius is MEASURED off the baked glyphs, and `HINT_FRAME` is the
    wrong shape for this.** `ICON_CAPSLOCK_*` insets its corners **2, 1, 0 px** — a
    radius-**2** arc — while `HINT_FRAME` draws at radius 4 (4, 2, 1, 1, 0), which
    reads visibly rounder beside it. That is why `HINT_BADGE` fixes the radius at
    `KDISP_BADGE_RADIUS` instead of taking it as an argument, and why `\x12` keeps its
    own radius for the run-dialog hint: **do not merge the two ops.**
  - ⚠️ **A style argument can never be 0** — these are `U"…"` strings, so a 0
    codepoint terminates them. Hence outline = 1, solid = 2.
  - ⚠️ **`kdisp_draw_round_rect()` CANNOT draw the released state, and two attempts to
    make it shipped wrong.** Its Bresenham arc renders a radius-2 corner as insets
    **1,0** where the scanline formula gives **2,1,0** — so the two disagree about what
    "r = 2" *looks like*, and the outlined badge came out squarer than the solid one
    even though both asked for the same radius. Stroking the 2px border as two nested
    Bresenham rects is worse: the outer arc's pixel and the inner rect's first pixel
    sit two apart, leaving a **1px hole in every corner**.
  - `kdisp_draw_badge_rect(x, y, w, h, r, border)` draws both states from ONE scanline
    fill (`border` 0 = solid, else a ring that thick), so the engaged badge is exactly
    the released one with its middle removed — they cannot drift apart. Per-row inset
    is `r - floor(sqrt(r² - d²))`; `r ≤ 4` in every caller, so the integer-sqrt loop is
    a few iterations, no float and no table.
  - ⚠️ **The HOLE keeps a 1px corner nick, which a true concentric offset does not
    give.** Offsetting inward by `border` implies an inner radius of `r - border`, and
    at `r == border` that is a perfectly square inner corner — one pixel short of the
    baked `ICON_CAPSLOCK_OFF`, whose hole still insets 1 on its first row. Reported
    from hardware as "it misses a single pixel on the inside corner", so the radius is
    floored at 1 whenever the outer corner is rounded at all. Only the released state
    has a hole, which is why the engaged one was right throughout.
  - **Verify a drawn badge against the baked glyph as ASCII, not as a render.** The
    radius error was invisible at 1× and obvious the moment both were dumped as
    character grids and the corner insets compared row by row.
  - ⚠️ **`HINT_ERASE` restores the PREVIOUS `s_gfx_erase`, not `false`.** It is a
    static plotter mode, so leaving it on blanks every keycap drawn after this one in
    the same pass — and a caller may already be mid-erase (the inverted-keycap
    pattern), which a hardcoded `false` would clobber. ⚠️ It used to cover **only the
    text paths** — `\x0F`/`\x11`/`\x15`/`\x13`/`\x12` composite through their own
    primitives, which plotted unconditionally — so `HINT_ERASE` before a HALF or a
    BADGE silently drew it lit. Fixed by the same choke-point move as the jitter
    offset: every ink primitive now plots through **`kdisp_plot_ink()`**. See the
    plotter-mode note below.
  - The arrow **alone** was rendered first and is too sparse to identify, and the
    Caps/Num badge glyphs could not be borrowed because they carry a literal `A` / `1`.
- **Pause spells the word out**, at half the **L legend tier** (`0xF3000`) — 14 px
  caps, 56 px wide, the largest that still fits the 72 px panel. `HINT_SMALL` halves
  whatever face the glyph comes from, so the size is chosen by picking WHICH face: the
  resident 27 px base halves to 10 px caps (41 px), M to 12 px (49 px), L to 14 px
  (56 px); the full 27 px face would need 106 px. Two solid U+275A bars were tried
  first and read as ambiguous.
  - ⚠️ **This is the one legend on the layer that needs the `latinbig` bundle** rather
    than `symbol`/`emoji`. A missing glyph makes `kdisp_write_gfx_char_half` draw
    **nothing** — unlike the full-size writer, which substitutes `'!'` — so with no
    font pack the keycap is blank, as its whole row already is (every neighbour is a
    pack glyph too). Drop back to the base face if that stops being acceptable.

**`HINT_SMALL` (`\x10`) is what makes small TEXT possible on a keycap, and it is not
`HINT_HALF`.** The three standalone UI faces (`_Small_` 15 px, `_Mid_` 19 px, `_Nano_`
10 px) are **not in `g_all_fonts`**, so no codepoint can reach them — the resident
latin face has exactly one size, and `latinbig` only goes *bigger*. So a smaller face
has to be synthesised at draw time.

- `HINT_HALF` (`\x0F`) could not do it: it takes the **literal top-left of the ink**
  and **does not advance the cursor**, because it exists to composite one icon into a
  hint. Spelling a word with it needs a `HINT_MOVE` per letter, with the top-left
  computed per glyph — 20 codepoints for "Pause", and each MOVE is an absolute buffer
  position that `kdisp_gfx_text_bbox()` cannot measure.
- `HINT_SMALL` instead latches a mode for the **rest of the string**, and
  `kdisp_write_gfx_char_half()` keeps `kdisp_write_gfx_char`'s baseline and advance
  semantics — only the glyph's own offsets and extents are halved, never the baseline.
  So `U"  \x05\x05" HINT_SMALL U"Pause"` centres itself with ordinary full-size
  spaces, exactly like the `U"  " ICON_*` legends beside it.
- ⚠️ **Halve offsets with FLOOR, not C truncation.** `xOffset`/`yOffset` are negative
  (above the baseline) and `/2` rounds toward zero, which puts lowercase 1 px off the
  run's baseline. `half_floor()` is written out rather than `>> 1` because a right
  shift of a negative value is only arithmetic by implementation guarantee.
- ⚠️ **There IS a "back to full size" op now — `HINT_BASE` (`\x17`), added 2026-09-09
  — and this line used to say there deliberately was not.** The old reasoning ("the one
  use is a legend that is entirely small text, and a toggle is a second thing to get
  wrong") held only while nothing needed two sizes in one legend. `\x18` (reset) still
  does **not** clear it; it resets the cursor only. See the `HINT_BASE` note below.

**`HINT_MID` (`\x16`) is the other direction, and the only size BETWEEN the two.**
`HINT_SMALL` synthesises a smaller face by halving; `HINT_MID` reaches the real
standalone **`_Mid_` 19px** one (~14px caps against the keycap face's ~20px), for
the rest of the string. It exists because the settings labels at half the 27px face
were reported as too small to read at a glance (2026-08-26).

- **It is a SINGLE-font array on purpose** — `kdisp_write_gfx_char` baseline-aligns
  by `font->yAdvance - fonts[0]->yAdvance`, so making the face its own `fonts[0]`
  makes that adjustment 0, the same reason the language flags draw through
  `{ &flag_font }`.
- **It falls back to the caller's pool PER GLYPH.** The mid face is ASCII-only
  (0x20..0x7E), so anything outside it — an icon — renders at its normal size
  instead of `'!'`. That is what makes a **word-over-icon** legend possible at all
  (the layout picks: a name over its on/off switch). The bbox mirrors this, and the
  baseline reference must follow the **per-glyph** choice, not the latch: `fonts[0]`
  is the mid face only for the glyphs the mid face supplied.
- It is what let a bespoke `else if (keycode == KC_EDEN)` branch be **deleted** from
  `update_displays()` rather than replaced — the size that branch existed to reach
  is now expressible in an ordinary legend.

⚠️ **TWO full-size TEXT lines CANNOT fit a 40px keycap, and the descender budget is
exactly ONE — which is what decides between the two MID stacks.** `\v` advances a
fixed 15px while the keycap face inks ~20px above the baseline, so two plain
`\r\v` text lines overlap outright; at the mid face they fit, but only just.
Nineteen legends shipped overlapped this way for a long time (Store/EE, Word/sel,
Line/join — which also lost **55px** off the panel — App/sw and the twelve OS
auto/pin cells), all now half-scale.

- **`MID_TWO_LINE(top, bottom)`** — lift 10px / push 6px — spends the descender at
  the **BOTTOM**. Values like `Teng`, `Amiga` and `Jittr` fit; the top may have
  **neither a descender nor an ascender**, which is why the labels are `IDLE:` /
  `SCRIPT:` / `RESET`. All caps is how that is guaranteed, not the rule itself.
- **`MID_TWO_WORD(top, bottom)`** / **`MID_WORD_OVER_ICON(word, icon)`** — lift 8px /
  push 8px — spend it at the **TOP**. An ascender fits (`Mods`, `Cmds`, `Colemk`);
  the bottom must not descend.
- ⚠️ **They cannot be merged.** Swapping the two spacings breaks four legends in
  each direction — measured by sweeping every (lift, push) pair, not reasoned.
  Re-measure rather than eyeball whenever a word changes.

**`HINT_BASE` (`\x17`) is the way OUT of the other two, and until 2026-09-09 there
was none — which made a small LABEL over a bigger VALUE inexpressible.** Both
`HINT_SMALL` and `HINT_MID` latch for the rest of the run, and the intuitive escape
does not work: `\x10` **after** `\x16` halves the *mid* face rather than returning to
the base one, so the second line always came out the smaller of the two. `\x17`
returns to the caller's pool at full size. The RGB preset keycaps are what needed it —
a half-scale `Preset:` over a mid-face `Solid` / `Breath` / `Cycle` / `Rainbw`.

- ⚠️ **Adding an op is TWO walkers, not one.** The draw dispatch in
  `base/disp_array.c` and the measurement in `base/font_lookup.c` must clear the same
  flags, or the bbox describes a legend the draw does not produce — and every consumer
  of that box (`plan_main_legend()`'s shift-preview layout, `roll_idle_offset()`'s
  idle travel) is then working from fiction. `make test:polykybd_font_bbox` pins all
  four cases, including the one that says why the op exists: that `\x10` after `\x16`
  is *not* the base face.
- ⚠️ **The host mirror is a THIRD edit** (`oled_preview.py`, and `SUPPORTED_OPS`
  beside it), and skipping it is silent — a refused op makes the layout editor fall
  back to the keycode *text*, which looks exactly like the op not working rather than
  like a missing renderer. See `PolyKybdHost/CLAUDE.md`'s ops ledger.

⚠️ **Two constraints decide where a legend element can go, and neither is visible from
the macro:**

- **A `HINT_MOVE` argument of 0 TERMINATES the string.** The walker's guard is
  `if (text[1] && text[2])`, so a 0 in either coordinate ends the legend there — i.e.
  **nothing is placeable on row 0 or column 0**, and the failure is a truncated legend
  rather than a misplaced glyph. The halved droplet on the saturation keys sits at
  row **1** for exactly this reason.
- **Every cursor nudge is 2px, so `\v` is the ONLY op that changes the cursor's
  PARITY.** `\f`/`\x05`/`\x06`/`\x08` move in twos, so an odd baseline can never reach
  an even one by nudging. `\v` jumps to the next 15px multiple, which is what lets
  line 1's baseline of 19 (as high as a full-size `+` reaches) get to line 2's 34.
  A layout that will not close by 2px steps needs a `\r\v` in it, not more nudges.
- ⚠️ **Nudge-run arithmetic is unverifiable by any test in this repo — the off-panel
  pixel count is the only check.** A 5-nudge lift transcribed as `UP_8PX` (four)
  pushed the Speed+ keycap's `p` descender two rows off the panel while `-Werror`, 52
  bbox tests, cppcheck and `qmk lint --strict` were all green. The count that caught
  it renders every legend through `PolyKybdHost/tools/oled_preview.py` — the
  firmware's own interpreter — and counts pixels outside the 72x40 window. Run it on
  every legend you touch, and require **0**.

⚠️ **The WORD is the size ceiling, not the face — measure before promising a bigger
legend.** The obvious request on a cramped legend is "use the next size up", and for a
long word there is no next size: at the mid face "Saturation" measures **97px against
a 72px panel**, and even at half the keycap face it was already **71 of 72**, i.e. as
large as it can ever be drawn. So growing the legend and keeping the word were
mutually exclusive, and the answer was to shorten the word (`Sat`; `Rainbow` 78px →
`Rainbw`). Measure the candidate string at each face first — the trade is the user's
to make, and it cannot be made without the numbers.

⚠️ **`kdisp_gfx_text_bbox()` did not know the display-list ops at all, and that was a
real bug the moment a MAIN legend started using them.** Every op byte *and each of its
argument codepoints* fell into `default:`, matched no font, and was substituted with
`'!'` — so a legend carrying one `HINT_MOVE` measured **three** bogus glyphs. That box
feeds `plan_main_legend()`'s shift-preview layout and `roll_idle_offset()`'s jitter
travel, so it was luck rather than design that nothing visibly broke (none of the
affected keys has a shift preview, and none is drawn at idle). The ops are mirrored
now: `\x10` switches the measurement to half-scale, `\x16` to the mid face,
`\x0F`/`\x11` consume one argument, `\x0E`/`\x12` consume two, `\x13` three.
Since #238 the interpreter (and the glyph resolver) live in pure
**`base/font_lookup.c`** — `kdisp_gfx_text_bbox_in()` takes the HINT_MID pool as a
parameter, `disp_array.c` keeps `kdisp_gfx_text_bbox()` as the wrapper binding the
resident mid face, and `make test:polykybd_font_bbox` (34 tests) pins the whole
op-argument table, the SMALL/MID semantics and the baseline-shift rule.
- ⚠️ **`\x0E` (MOVE) skipped NO arguments until 2026-08-26, and that sentence above
  was FALSE for it** — it fell through to `\x14`'s bare `break`, so a MOVE's two
  coordinate bytes were dispatched through the same switch on the next iterations.
  A coordinate is an arbitrary byte: **13 of the 31 `HINT_POS_*` / `HINT_SZ_*` /
  `MTB_*` macros carry one that is also an op** — `HINT_SZ_STOPSQ` is (15,15), i.e.
  `\x0F \x0F`, two HALFs; `HINT_SZ_SCRBOX` is (19,19), two BADGEs; `HINT_POS_SCRBOX`'s
  y is `\x06`. Ten of those predate the ops added in 2026-08 and cost only a
  mis-measured glyph, but the newer `\x16` latches a different FONT for the rest of
  the run and `\x15` eats the next two codepoints — the Ctrl mod-badge hint measured
  x1..30 where it is really x0..47. Skipping the arguments (what `\x12` two lines
  below always did) closes the class for the existing ops **and any op added later**.
  ⚠️ **So check a new op byte against those macros' argument bytes** — or rather,
  don't have to, now that MOVE consumes its own.
- ⚠️ **`kdisp_gfx_text_bbox()` still ignores the MOVE itself** — it works relative to
  the draw origin and MOVE names an ABSOLUTE buffer position, which is not knowable
  without one, so a MOVE'd legend's box only covers the part laid out relatively (and
  the composite ops contribute no extent at all — measuring them at an unresolvable
  cursor would be worse than skipping them). **Prefer the ordinary cursor advance over
  a MOVE in a main legend** — that is why `ICON_MUTE` places its X with `\f\f` and the
  glyph's own `xAdvance` rather than a MOVE.
  - ✅ **There IS now a form that resolves it: `kdisp_gfx_text_bbox_abs()`**, which
    takes the draw origin and returns the ABSOLUTE buffer box — MOVE resolved, and
    `\x0F`/`\x11`/`\x15`/`\x13`/`\x12` measured at their real extents. The relative
    form is unchanged to the byte (both are one walk in `bbox_walk()`, parameterised
    by origin), so its 34 existing tests still pin it. **Use the absolute one whenever
    you need to know where ALL of a legend lands** — which is exactly what the idle
    jitter needs, and what it did not have (below).
- ✅ **A THIRD asymmetry, FIXED 2026-09-02, and it was the biggest of the three by
  pixels: the RELATIVE walk saturated the cursor nudges at 0.** `\f` is `y = y > 1 ?
  y - 2 : 0` and `\b` the same in x — correct in the DRAW, where the cursor is a real
  buffer coordinate and 0 is the panel edge, and wrong in a relative walk that *starts*
  at 0, where the clamp swallows the nudge and the op measures as a no-op.
  `bbox_walk()` now takes a `saturate` flag: **false for the relative form, true for
  the absolute one**, which is the draw's own rule at a real origin.
  - **It mattered because 73 of the 160 layouts' legends open with one to six of these
    nudges** — `é è ç à` on AZERTY are `\f\f <letter>`, cs-CZ uses four, he-IL's
    `KC_BACKSLASH` AltGr six — so the measured box sat up to **12 px** below its own
    ink and `render_key()`'s panel clamp could not see the overrun. With the four-edge
    clamp in place but the saturation still there, 11 keys / 92 px stayed clipped; with
    both, **1 key / 9 px**, and that one is a 43 px glyph in a 40 px panel.
  - **The test that pins it is the RELATIONSHIP, not either number**:
    `RelativeAndAbsoluteAgreeAcrossTheCursorNudges` requires that shifting the origin
    shifts the box by exactly that much, for each nudge. Two older tests asserted the
    saturation as the contract (`measure({'\f','a'}) == measure({'a'})`) and were
    inverted — a pinned behaviour is only as good as the reason it was pinned.
  - ⚠️ **The host mirror moved with it** (`oled_preview.Renderer.bbox`, and the JS in
    `keycap_tuner_template.html`), and `tests/tools/oled_preview_bbox_test.py` carries
    the same two inverted fixtures. This is the cross-repo parity pin the note below
    warns about, working as intended: name the change that invalidates it, then make it
    on both sides in one go.
- ✅ **TWO bbox-vs-draw asymmetries were FIXED 2026-08-29 — both made the measured
  box describe glyphs the draw would not produce.** Worth knowing they existed,
  because the shape recurs: this function and the draw resolved glyphs by two
  different routes, so they could disagree without either looking wrong.
  1. **The `'!'` substitution had no `small` guard.** The measure path did
     `if (!f) { f = pool[0]; …; ch = U'!'; }` unconditionally, while the SMALL draw
     (`kdisp_write_gfx_char_half`) does `if (glyph == NULL …) return 0;` — no ink and
     **no advance**. So a `HINT_SMALL` run containing an uncovered codepoint measured a
     half-`'!'` *and* spent an advance the draw never spends, putting every following
     glyph at the wrong x.
  2. **The scan did not skip 0x0 GAP records.** It was a bare
     `if (ch >= first && ch <= last) { f = pool[i]; break; }`, where every draw path
     goes through `kdisp_gfx_glyph_font`, which skips a `{0,0,0,0,0,0}` padding record
     so a later font wins. A codepoint inside a padded span — Pashto letters under
     `_PerArab_`'s wider range — measured the empty gap while the draw resolved a real
     glyph from the next font.
  **The fix is one line of intent: resolve through `kdisp_gfx_glyph_font`, the same
  lookup the draw uses**, and skip the codepoint instead of substituting when a SMALL
  run finds nothing. Don't reintroduce a private range scan here — that is what made
  (2) possible, and a second resolver can always drift from the first.
  - **It mattered because `plan_main_legend()` positions the main legend from this box
    and clamps it to the panel**, and `roll_idle_offset()` derives a glyph's idle travel
    from it — so a wrong box is a mis-placed or clipped legend, not just a wrong number.
    Reachability was narrow (a legend needs a missing glyph *and* a size op, or a gapped
    codepoint), which is why nothing had reported it.
  - ⚠️ **The old C suite passed over BOTH.** `GapRecordFallsThroughToTheNextFont` covers
    the *resolver*, not the bbox, and there was no missing-glyph-in-a-SMALL-run case at
    all — 34 tests, neither asymmetry visible. The two added with the fix
    (`SmallSkipsAMissingGlyphInsteadOfSubstitutingBang`, `FontBboxGapTest`) were
    confirmed to FAIL against the pre-fix file and pass after, with the other 34
    unmoved. **A suite that measures a resolver is not measuring its callers.**
  - ⚠️ **The host's Python mirror moves with this.** `PolyKybdHost`'s
    `oled_preview.Renderer` reproduces this function; it already skipped gaps (so (2)
    was never wrong there) but deliberately pinned the `'!'` substitution as C parity.
    ✅ **That pin has been INVERTED to match (PolyKybdHost#209, 2026-09-01)** —
    `Renderer.bbox()` now skips an unresolvable glyph in a `HINT_SMALL` run too, and
    its test is ported from `SmallSkipsAMissingGlyphInsteadOfSubstitutingBang`.
    ⚠️ **The general point outlives this instance: a cross-repo parity pin is a
    LIABILITY the moment one side moves, because nothing fails when it goes stale.**
    Neither suite would have gone red — the host would simply have been wrong in the
    opposite direction, silently. Measured after the fix: of the host's 197 static
    legends, 12 use `HINT_SMALL` and **none** carries a glyph the pool cannot resolve,
    so nothing rendered differently either way, which is exactly why only a written
    note could have caught it. **When you pin parity, name the change on the other
    side that would invalidate the pin.**
**The legend-size key is now ONE key that states its own tier.** `KC_GLYPH_SIZE_UP` on
`_UL` draws `ICON_FONT_BIGGER` plus the current tier as a digit in the top-right;
holding **Shift** swaps the icon to `ICON_FONT_SMALLER` and reverses the step, so the
`KC_GLYPH_SIZE_DOWN` keycode survives but is bound nowhere.

- ⚠️ **The legend lives in `to_static_text()` (`poly_keymap.c`), NOT in
  `keycode_to_static_text()`.** Both halves of it are **synced** state — the tier from
  `poly_sync_t.glyph_size` and the modifier from `poly_layer_t.mods` — and
  `keycode_to_static_text()` only receives `led_t`, so on the **slave** it would draw
  the master's tier with its own (always-clear) mods. Any legend that depends on a
  synced field belongs on this side of that seam.
- **The action reads the LIVE `get_mods()`, the legend reads the synced copy** — and
  that asymmetry is deliberate. The action runs on the master at the instant of the
  release and must follow the finger; the legend must render identically on a half that
  only ever sees the housekeeping snapshot.
- **Placement is measured** (`HINT_POS_SIZENUM` = buffer (85,25)): the digit inks rows
  3–23 and columns 86–98 of the 72×40 window, clearing both the 43 px icon (which ends
  at column 70) and the panel edge at 99. ⚠️ What makes room for it is that this legend
  carries **no leading pad space**, unlike the `U"  " ICON_*` legends beside it — with
  the usual two spaces the icon ends at column 61 and the digit will not fit.
- The legend contains a `HINT_MOVE`, so `glyph_size_remap()` bails and the key itself
  always draws at the small face. That is correct — it is a mixed icon cell, not a
  latin legend — but it means the size key does not demonstrate the setting it changes.

---

### Keycap legend size (`base/legend_plan.c` + `poly_keymap.c`, HID cmd 34, protocol v13+)

*(The planner — `glyph_size_remap()` / `plan_main_legend()` — is pure in
`base/legend_plan.c` since #237, behind has-glyph/bbox callbacks with the firmware
binding kept as wrappers in `poly_keymap.c`; `make test:polykybd_legend_plan` pins
the tier bases, the all-or-nothing fallback and the origin clamps.)

Three sizes for a key's **MAIN** legend — `GLYPH_SIZE_S` (0, the 27 px face the board
has always drawn, and the default), `M` (1, 33 px em) and `L` (2, 39 px em). State and
plumbing mirror the glyph-script override exactly (`poly_eeconf_t.glyph_size` tail byte,
`poly_sync_t.glyph_size` master-authoritative, housekeeping diff + `request_disp_refresh`,
`KC_GLYPH_SIZE` on the settings layer). What is worth knowing is the parts that are NOT
like the glyph script:

- ⚠️ **The range is CLOSED — see the cmd-34 note in the protocol list above.** Do not
  "make it open like the script".
- **No migration sentinel, and for once that is sound rather than lucky.** The default
  is 0 and QMK's wear levelling normalises an unwritten byte to **ZERO** — the exact
  fact that made `latin_assign` read as "every key hosts 'a'" (see the Intl-remap traps)
  works in our favour here. `load_user_eeconf()` still bounds-guards it.
- **The bigger faces are RELOCATED, not a second lookup path.** `g_all_fonts` is scanned
  front-to-back and the resident `latin` font is always in front, so a second face at
  native codepoints could never be reached. `fonts.yaml`'s `latinbig` category emits each
  tier at a fixed offset into supplementary PUA plane 15 (**fontconvert `-o`**, the
  range-mode sibling of the `-F` the glyph scripts use): M at `0xF0000 + cp`, L at
  `0xF3000 + cp`. `glyph_size_base[]` in `base/legend_plan.c` must stay identical to the
  `offset:` values in the yaml (a `_Static_assert` pins the size indices; the unit
  suite pins the base values). ⚠️ `-o` was documented as "add" but implemented as
  "subtract" (an exact alias of `-n`) until 2026-08-20; nothing used it, so the fix was
  inert — but an older fontconvert will silently emit the WRONG range here.
- **`glyph_size_remap()` is ALL-OR-NOTHING.** If the `latinbig` bundle is absent, or any
  glyph of the legend is missing at that size (a CJK/Arabic/Indic keycap — this is a
  latin-only feature), the WHOLE legend falls back to the small face. A partial hit would
  mix two fonts in one legend, which by the documented baseline-align rule also means two
  baselines.
  - ⚠️ **A legend can carry LEADING CURSOR OPS, and refusing them silently halved the
    French number row.** A base legend is a mini display list like a hint string, and 73
    of them across the 160 layouts open with a zero-argument cursor nudge — `é è ç à` on
    AZERTY are spelled `\f\f <letter>` (a 4 px lift hand-tuned for the small face), cs-CZ
    uses four. The first version bailed on any codepoint `< 0x20`, so **`& " ' ( - _` grew
    with the setting while `é è ç à` stayed small** — a real gap, invisible from the code
    and obvious the moment the row was rendered (2026-08-21). `glyph_size_remap()` now
    **drops** the five ops that occur (`0x05 0x06 0x08 0x0B 0x0C`) and still bails on
    every other one: `HINT_MOVE`/`HINT_FRAME` consume the two codepoints after them, which
    would then be relocated as if they were glyphs, and `HINT_HALF`/`HINT_THIN` rescale the
    next glyph. Measured, so it can be re-checked: every op present is one of those five
    and every one **leads** the legend (`0x0C` ×150, `0x0B` ×8, `0x06` ×9, `0x08` ×1,
    `0x05` ×1; **not one after a glyph**).
  - ⚠️ **DROPPED rather than carried — and the REASON CHANGED on 2026-09-02, so do not
    restore the op on the old rationale.** It used to be that `kdisp_gfx_text_bbox()`
    and the draw disagreed about `\f`: the draw clamps its cursor at buffer 0, while the
    relative walk starts at 0 where that clamp swallowed the lift entirely and the op
    measured as a no-op. **That is fixed** (see the `saturate` note under the bbox
    section below), so the measured box now matches the draw for these ops too. What
    remains is that the nudge was hand-tuned for the SMALL face's fixed baseline — 2 px
    lifts chosen against a 27 px glyph at baseline 23 — and the planner replaces exactly
    that baseline with a measured, clamped one. Carrying it was tried and clipped 6–8 px
    off the accents of `é è à` at M/L.
  - **Measured after the fix**: 1467 of 1500 latin number-row keys reach the bigger face
    (was 1338); the 33 that don't are genuinely non-latin (Thai, Bopomofo, Armenian,
    Cherokee, Vietnamese PUA composites). Clipped pixels **drop** at M/L rather than
    rising — cs-CZ's nine number keys clip at small and are clean at M/L, because the
    clamp fixes what the hand nudge could not.
- ⚠️ **THE SIZES ARE MEASURED, AND THE PANEL IS THE BINDING CONSTRAINT.** The keycap is
  40 px tall and the tallest latin glyph (Ḉ, `_LatinExtAdd_`) already inks **33** of them
  at the base size, so a uniform scale factor clips the accent stacks long before the
  plain letters run out of room — there is no single factor that gives two clean tiers.
  Each `latinbig` entry instead takes the largest pixel size whose TALLEST glyph still
  inks ≤ 40 px, capped at the tier target, so four entries deliberately grow less than
  the rest (`_SupAndExtA_` L 35, `_LatinExtB_` L 37, `_LatinExtAdd_` M 31 / L 33,
  `_Cyrillic_` M 35). `fonts/measure_glyph_sizes.py` is how those were chosen; re-run it
  after ANY change there, and `PolyKybdHost/tools/glyph_size_preview.py --check` to
  confirm zero clipped pixels.
  - ⚠️ **Before adding a SCRIPT to the bigger tiers, MEASURE it — the obvious proxy is
    not predictive, and it was wrong about every script it was applied to.** The
    tempting estimate is `40 px panel ÷ what the script inks today`; it assumes the
    glyph would fill the panel at the bigger tier, which depends on the face's own
    ink-to-em ratio and is only knowable by rendering. `measure_glyph_sizes.py
    --category <names>` does that — it reads each entry's real source, ranges and
    options out of `fonts.yaml` through `generate_fonts.py`'s own `resolve()` /
    `build_argv()`, so it cannot drift from what would actually be emitted. Measured
    per entry (2026-08-21), largest fitting ppem's ink ÷ as-shipped ink: **latin
    ×1.18–1.50** (the shipped feature), Cherokee ×1.52, **Japanese ×1.29–1.38**,
    Telugu ×1.26, Bengali/Ethiopic ×1.23, Armenian/Georgian/Bopomofo/Tamil/Thai/
    Canadian ×1.21–1.22, **Hebrew ×1.20**, Devanagari ×1.06, and **Hangul ×0.92 — it
    would get SMALLER.** The estimate had called Hebrew and Hangul ×1.60 apiece and
    written Japanese off as hopeless; all three were wrong, and Korean — the layout
    that prompted the question — is the one script that provably cannot benefit.
  - ⚠️ **`render_height` (fontconvert `-r`) is NOT an ink ceiling.** `latin` carries
    `render_height: 44` and grows fine, because a tier overrides it with a pixel size.
    An earlier cut of the tool verdicted off the presence of that flag and declared
    latin unable to grow. Read the measured ink, never the flag. It does mean a script
    can already be drawn LARGER than any tier ppem would give it, which is exactly why
    Hangul (`render_height: 51`) shrinks.
  - ⚠️ **Measure per ENTRY, not per category, and a range's tallest glyph may not be a
    legend.** Latin needed four of its twelve entries capped below the tier target; a
    per-category maximum hides that and condemns the whole category on one glyph.
    Hebrew's range maximum is a standalone nikud mark inking 43 px that never appears
    on a keycap, so the category number reads far worse than the letters do.
  - **Coverage as shipped is Latin, Cyrillic and Greek.** Those scale completely
    (`ru-RU` and `el-GR` measure 49/49 keys). The other **41 layouts come out MIXED** —
    their digits and punctuation are latin and grow while the letters do not (`ko-KR`
    is 23 grown / 26 unchanged). That is stated on the public `using/legend-size` page
    rather than hidden. Don't "fix" it by gating the setting off per layout: that only
    takes the feature away from the keys it does reach.
- **`yadvance: 40` on every `latinbig` entry** makes `kdisp_write_gfx_char`'s baseline
  align a no-op, so the y `plan_main_legend()` computes IS the baseline. Side effect
  worth having: at M/L every latin sub-font shares one baseline, where at S `a`
  (`_Base_`, yAdv 37) and `ä` (`_SupAndExtA_`, 44) sit 7 px apart.
- **Placement = nominal baseline THEN clamp against the legend's own bbox.** The nominal
  keeps ordinary letters on a shared baseline; the clamp is what stops a tall accent or a
  deep descender clipping. Two knock-ons that only bite at the bigger sizes, both in
  `render_key()`: the overlap **stagger must not lift a big base** (already clamped to the
  panel, so a 6 px lift pushes it off the top), and the **AltGr preview** — kept off the
  legend by its VERTICAL offset at S — is pushed clear HORIZONTALLY instead, because a big
  legend fills that height.
- The shift/AltGr previews stay small **by design**: a keycap has room for one big thing.

---

### Brightness keys — one icon family (`keycode_helper.c`, `base/fonts/gfx_icons.h`)

The eight brightness keycodes now draw **one resident IconsFont glyph each**, all
built on the sun the status OLED already uses for brightness: `KC_DMIN` / `KC_D1Q` /
`KC_DHLF` / `KC_D3Q` / `KC_DMAX` are a sun whose **rays grow with the level** beside a
staircase that states the level outright; `KC_DDIM` / `KC_DBRI` are a small/large sun
with `−`/`+` and no staircase (they name no level); `KC_DAUTO` spells **AUTO** or
**MANUAL** under the sun.

- ⚠️ **What it replaced was actively misleading, not merely inconsistent.** The five
  presets were **moon phases**, and the mapping ran BACKWARDS from the obvious
  reading: `PRIVATE_DISP_BRIGHT` was **U+1F311 🌑 NEW MOON**, the all-black disc,
  because it depicted the unlit *screen* rather than the brightness. Nothing else on
  the board used that convention. `KC_DDIM`/`KC_DBRI` meanwhile borrowed the plain
  page arrows `ICON_LEFT`/`ICON_RIGHT`, which say nothing about light at all.
- **The staircase has FOUR steps because the presets ARE quarters** (`FULL_BRIGHT`
  × 1/4, 1/2, 3/4, 1/1), so each lights exactly its own number of them and `KC_DMIN`
  — brightness **2 of 50**, below the first quarter — lights none. Five steps put 50%
  and 75% on 2 and 3 of 5, i.e. a meter misreporting the value it exists to state.
  An unlit step keeps a **1px foot**, the status OLED's own rule
  (`split72/status_oled.c` `draw_brightness_bars`), so the full scale stays visible.
- ⚠️ **`KC_DMIN` keeps a FILLED sun with zero rays — do not "improve" it to a hollow
  one.** It sets brightness 2, the dimmest **lit** level (`DISP_OFF` is 0,
  `MIN_BRIGHT` is 1), so a hollow sun would claim an off state the key cannot reach.
- ⚠️ **`KC_DAUTO` spells the mode out instead of wearing `ICON_SWITCH_ON/OFF`.** A
  toggle beside a sun reads as *"the light is on/off"*, which is the one thing this
  key does not control — reported in the field as exactly that confusion.
- **One glyph per legend is the point, not an accident.** `kdisp_write_gfx_char`
  baseline-aligns by `font->yAdvance - fonts[0]->yAdvance`, so a legend composed from
  an icon plus base-font text sits on two baselines (the `à»ñ` note in the Intl-remap
  traps). Baking each cell as a single IconsFont glyph — `IconsFont` **is** `fonts[0]`,
  so its adjustment is 0 — makes the whole cell one unit at one baseline.
  - ⚠️ **Therefore NO leading pad space**, unlike the `U"  " ICON_LEFT` legends beside
    them. Each glyph carries its own `xOffset` measured from `BUFFER_X`; a space would
    advance the cursor and shift the whole cell right.
- **Cost: +1747 B of flash, 0 B of RAM** (`.data` 318292 → 320052, `.bss` unchanged).
  Nine glyphs at 50–58 px wide; the monolithic `POLYKYBD_DOOM=yes` flavour still links.
- ⚠️ **The now-unused `PRIVATE_DISP_*` moon macros stay, and so does the resident
  `_Brightness_` font (1208 B) — it is NOT dead.** It covers `0x1F311..0x1F318`, which
  the **emoji layer** also lists (`emoji/emoji_data.h`), and resident wins the
  front-to-back lookup — so dropping it would silently re-render those emoji from the
  pack at a different size. Removing a resident font also shifts every pack font's
  gidx and forces a full-pack reship, for 1.2 KB against a 2 MB partition at ~38%.
- **Verify by rendering, never by reading the header.** `PolyKybdHost/tools/gfx_font.py`
  parses the committed headers and walks the same front-to-back `ALL_FONTS` lookup the
  firmware does, so a sheet drawn through it checks the shipped bytes *and* the
  codepoint routing. Count the pixels it drops outside the 72×40 window — that is the
  clipping check, and it must be 0.
