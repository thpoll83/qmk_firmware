# The Intl layer: picker and letter remap

Moved out of `CLAUDE.md` 2026-09-14. Verbatim.

## Intl latin-variation picker (`poly_keymap.c`, `_ADDLANG1`)
Holding **Intl** shows each letter's selected accented variation; tapping **Ctrl**
(`LATIN_PICKER_MOD`) turns the number row into a picker of that letter's variations.
The mechanism is worth knowing because it is not the obvious implementation:

- ⚠️ **The picker modifier is `MOD_MASK_CTRL` and must not go back to Alt.** The
  picker swallows the keys it handles (`process_record_user` returns false), so the
  host only ever sees the modifier go down and back up — and a bare **Alt** tap is
  how Windows activates the menu bar, so picking a variation yanked focus out of the
  text field in a lot of programs. A bare Ctrl tap does nothing in the same apps.
- **The layer must PASS the modifier through.** `_ADDLANG1` masked `KC_LCTL` with
  `KC_NO`, so Ctrl never reached the base layer and the picker could not be opened at
  all — and that mask is *why* it was on Alt originally (Alt was the one modifier the
  layer let through). A masked modifier draws an **empty** keycap and is otherwise
  indistinguishable from a code bug, which is why `boot_diag.c` now reports
  `intl: … ctrl=pass shift=pass alt=masked -> picker OK`, read out of the compiled
  keymap. ⚠️ Verify such a fix against the **compiled `keymaps[]` in the ELF**, not by
  counting columns in the `LAYOUT` macro — column-counting produced a first attempt
  whose Ctrl was still masked, and the same banner scan (every position, **both**
  hands) later found two more masks nobody had spotted on split42.
- **The picker LATCHES by registering the REAL modifier** (`register_mods(MOD_BIT(
  KC_LEFT_CTRL))`), not by setting a private flag. Two reasons, both binding: the
  slave draws the picker digits on *its* own keys and only ever sees `poly_layer_t.mods`,
  which is already synced; and **both flag bytes in `base/com.h` are full** (all 8 bits
  used in each), so a private flag has nowhere to ride. `s_picker_latched` records only
  that *we* registered it.
- ⚠️ **Gate the release swallow on OWNERSHIP, not on the keycode.** The latch toggles
  on the press, so the release of the arming tap must be swallowed or QMK unregisters
  the modifier before the finger lifts. But a Ctrl **already held** when Intl was
  pressed is registered by QMK — swallowing *that* release leaves it registered
  forever and turns every later keystroke into Ctrl+key. `if(addlang && !pressed &&
  s_picker_latched && ...)` is the correct condition; the other two cases fall through
  harmlessly. `layer_state_set_user()` also unlatches on layer exit, for the same
  "never release a Ctrl the user is really holding" reason.
- **Nothing may overlay this layer.** Its letters *are* the payload (`render_key()`
  draws the variation) and the picker modifier is Ctrl, so both overlay sources would
  paint the *Ctrl view* over it — `copy_overlay_to_buffer()` the app's Ctrl-modifier
  image and `keycode_to_disp_overlay()` the built-in Ctrl-shortcut hints, on every key
  at once. ⚠️ The `!add_lang` guard must wrap **both** arms of the display_overlays
  if/else; folding it into the first condition (`!add_lang && display_overlays`) looks
  equivalent and is not — the `else` then fires on the Intl layer and paints the
  hardcoded hint straight back over the variation.
- The armed indicator is the inverted Ctrl keycap — see the two rendering bullets
  above (render it, don't `kdisp_invert()`; and pass `cy_radius` 0).

## Intl letter remap — a key can host ANOTHER letter's row (`KC_LAT_REMAP`)

French needs `è é ê` at once, which one letter's picker cannot give: the picker
chooses another *form* of the letter a key already hosts. So a key can now be
**reassigned to a different base letter**. Hold Intl, tap the remap key (split72
`[4,1]`, beside the Ctrl at `[4,0]`; split42 `[3,4]`), press the key to change (it
inverts), then the letter it should host. `e`, `q` and `j` can then carry `é`, `è`
and `ê` — and the sparse letters (`q` has one variation, `j` two) stop being dead
keys on this layer.

- **The storage splits two things the code had conflated.** The ROW comes from the
  letter a key HOSTS (`latin_ex_map`); the PICK comes from the KEY's own slot
  (`latin_sync_t.ex`). They are the same number only while nothing is remapped,
  which is why one index sufficed for years. Two keys on the same letter share a
  row but need independent picks — storing the pick at the row index has one key
  silently overwrite the other's choice. `latin_sync_t.assign[20]` holds one 6-bit
  base letter per target key, **shared across case** so Shift follows the remap
  (`r → e` gives E's upper-case form, not `<`); the pick stays per case because the
  two rows are not parallel (lower `n` has 12 variations, upper `N` 11).
- Pick fields are **case-INTERLEAVED** (`slot*2 + case`), not case-blocked, so
  growing `LATIN_TARGETS` appends fields instead of inserting a block mid-array.
  Extending the targets to the punctuation keys is the obvious next step —
  `KC_MINUS 0x2D … KC_SLASH 0x38` is a contiguous run of 12 printable punctuation
  keycodes, so `kc - KC_MINUS + 26` needs no table. It costs ~43 bytes (the pick
  array grows too), which is why `POLY_EECONFIG_USER_RESERVED` was taken 128 → 256
  in the letters-only change: that relocation resets the dynamic keymap once, and
  paying it early means punctuation later costs no second reset.
- **Re-assigning a key to its OWN letter is the per-key reset** (stored as
  `LATIN_ASSIGN_NONE`), so it needs no gesture of its own. **Shift+remap clears
  everything** — once the board is remapped the Intl legends no longer match the
  printed letters, so there has to be one way back that does not depend on
  remembering what was changed.

⚠️ **Four traps this feature hit, all of which generalise beyond it:**

- **An unwritten EEPROM byte reads `0x00` here, NOT `0xFF` — never infer "never
  written" from the bytes.** The assignment map was designed to need no migration
  sentinel because `LATIN_ASSIGN_NONE` is all-bits-set and "erased flash reads
  0xFF". QMK's **wear-levelling normalises its backing store so cleared bytes
  arrive as ZERO** (`quantum/wear_leveling/wear_leveling.c` clears its cache with
  `memset(...,0)` and its header requires a 0xFF-based store to return the
  *complement* "such that this wear-leveling algorithm receives zeros"). The map
  therefore read back all-zero = "every key hosts letter 0", and **every Intl
  keycap rendered a variation of `a`** (field, first flash). Gate such a field on
  the `latin_pick_migrated` **format version**, which is what that byte is for.
  - ⚠️ **A version gate alone does not HEAL an already-flashed board** — the broken
    build persisted the zeros at the next suspend *and stamped them valid*. The
    recovery is to **retire the version value**: `0xC3` now means "picks are fine,
    discard the map" and `0xD7` is current. Walk every version a field EEPROM can
    present before shipping such a fix.
  - ⚠️ The reservation bump relocates the **dynamic keymap** only. `poly_eeconf_t`
    sits *before* it at a fixed address and is **not** reset — which is exactly why
    the stale zeros survived the flash that was supposed to clear everything.
- **`render_key()` is only consulted when `to_static_text()` returns NULL.** A key
  that HAS a legend bypasses it completely. The remap prompt blanks the board from
  inside `render_key()`, so every non-letter *with a legend* — including the remap
  key itself — sailed past and kept drawing normally: never blanked, never
  inverted, so nothing on the board said the latched mode was open. Any future
  "the board becomes a dialog" mode must **also suppress `text`** in
  `update_displays()`, not just return false from `render_key()`.
- **Keep every glyph of a multi-glyph legend in ONE font.**
  `kdisp_write_gfx_char` baseline-aligns by `font->yAdvance - fonts[0]->yAdvance`,
  so glyphs from different fonts land on different baselines. `a` is in `_Base_`
  (yAdvance 37 → −3 px) while `»`/`ñ` are in `_SupAndExtA_` (44 → +4 px): the
  legend `a»ñ` sat its `a` **7 px** high. `Á»Æ` is even only because all three of
  its glyphs are Latin-1, i.e. one font — hence `INTL_REMAP_LEGEND` is **`à»ñ`**.
  ⚠️ `oled_preview.py` **cannot** show this (it models `xOffset`/`yOffset` but not
  the baseline-align shift), so both legends render identically there — the check
  is the font metrics, not the preview.
- **The "gate the release swallow on OWNERSHIP" rule applies to LAYER keys too.**
  The remap block returned `false` for every release, which swallowed the release
  of **`MO(_ADDLANG1)` itself** — QMK never unregistered the layer, so Intl went
  down and never came back up and the mode could not be escaped at all; a held
  Shift was stuck the same way. Modifiers and layer keys
  (`IS_MODIFIER_KEYCODE` / `IS_QK_MOMENTARY` / `IS_QK_TO`) must fall through. This
  is the same rule already written up for the picker's Ctrl latch, one function
  away, and it was still missed.

