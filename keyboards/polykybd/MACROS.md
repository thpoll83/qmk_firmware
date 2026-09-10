# Macros: storage, playback, capture and the keycap

A macro is text (or a short key sequence) stored on the keyboard, typed back on one
keypress, with a label the keycap spells out along its bottom edge — plus the
gesture that records one with no host app. Moved out of `CLAUDE.md` on 2026-09-10:
~26 KB read while you are in `poly_macro.c`, `poly_macro_record.c` or
`base/macro_decode.c` / `base/macro_record.c`.

The design alternatives weighed for the recording gesture are in
`MACRO_RECORD_DESIGN.md`; this file is what the shipped code does and why.

⚠️ **Two things here are ours precisely because QMK's versions would freeze the
board.** `dynamic_keymap_macro_send()` runs a whole macro inline and spells its
delay `while (ms--) wait_ms(1)`; on this board the same loop scans the matrix,
drives the split UART, services USB HID and pushes 72 SPI displays. And capture is
a `host_driver_t` shim that must be installed from HOUSEKEEPING — installed from
`keyboard_post_init_user()` it is overwritten by `protocol_post_init()` a moment
later and the recording silently captures **nothing**.

---

### Dynamic macros (`poly_macro.c`, HID cmds 36/37/38, protocol v15+)

A macro is text (or a short key sequence) stored on the keyboard, typed back on one
keypress, with a **label the keycap spells out** along its bottom edge. What is worth
knowing is the parts that are NOT what you would write from scratch:

- **Storage is QMK's own dynamic-macro buffer** — a run of NUL-terminated bodies at
  `DYNAMIC_KEYMAP_MACRO_EEPROM_ADDR`, macro N found by counting N terminators. We did
  not invent a format; `dynamic_keymap_macro_get/set_buffer` already manage it.
- **The LABELS are a separate fixed-stride array**, carved off the TOP of the same
  region by shrinking `DYNAMIC_KEYMAP_MACRO_EEPROM_SIZE` (config.h). Deliberately not
  inside the NUL-delimited buffer: a body is addressed by counting separators — fine
  once per keypress, wrong for something `render_key()` reads for every macro keycap
  on every refresh. Shrinking the QMK constant is what keeps them apart, since every
  upstream path bounds itself on it and so cannot reach the labels.
- ⚠️ **Playback is OURS and must stay a state machine.**
  `dynamic_keymap_macro_send()` runs the whole macro inline and spells its delay
  `while (ms--) wait_ms(1)`. On a single-controller board that is merely rude; here
  the same loop scans the matrix, drives the split UART, services USB HID and pushes
  72 SPI displays, so a macro with a half-second delay would freeze the board and drop
  the link. `poly_macro_tick()` runs at most ONE step per housekeeping pass and treats
  a delay as a deadline. **No time-slicing is needed** (unlike Eden): steps have to be
  SPACED anyway for the host to see distinct events, so the pacing IS the yield.
- **The wire format is QMK's send-string encoding, NOT Vial's extension of it** —
  `0x01 0x01/02/03 <kc>` tap/down/up, `0x01 0x04 <ascii digits>` delay. Staying on the
  base encoding means the buffer is still playable by `dynamic_keymap_macro_send()`, a
  real cross-check rather than a theoretical one. Cost: 8-bit keycodes, so no mod-taps
  or layer keys; every modifier is 0xE0..0xE7, so chords are fine.
  - ⚠️ **The byte ENDING a delay is NOT consumed** — send_string re-reads it as the
    next step. Consuming it silently swallows the character after every delay, which
    presents as "the macro drops a letter sometimes".
- **The decoding is pure in `base/macro_decode.c`** (a byte-reader callback; no
  quantum.h, no EEPROM, no timer), the same seam as `base/fw_up_verdict.c`: the
  arithmetic is the part with a bug history and it was only unreachable because it
  shared a function with the I/O. `make test:polykybd_macro_decode` — 23 tests,
  mutation-tested against 7 deliberate breaks, each caught by the intended test.
- ⚠️ **`clear_keyboard()` on abort.** A DOWN step leaves a modifier registered, and
  any key press aborts playback — without the clear the host auto-repeats a key
  nothing will ever release. Same rule as the FW-2 prompt and `doom_begin()`.
- **Swallowed in `process_record_user()`**, not left to the release edge — an `OSL()`
  layer re-dispatches a release-edge action up to three times, which for a macro means
  playing it two or three times over (§ "A release-edge action fires up to THREE
  times").
- **Labels live in a RAM cache on BOTH halves** (192 B). Partly speed, mostly
  necessity: the host writes macros to the master, so the slave's own EEPROM never
  sees one. The master pushes each label over the split link, ONE per housekeeping
  pass, clearing its dirty bit only on a real ACK — so the mask is its own retry queue
  and nothing has to detect "the link is up". ⚠️ Never inline in the HID handler:
  sixteen bridges of up to ten retries each is seconds of dead main loop on exactly
  the link that needed the retries.
  - It **multiplexes onto `USER_SYNC_DYNAMIC_KEYMAP_DATA`** with a private op byte
    (`POLY_KEYMAP_OP_MACRO_LABEL`) rather than spending one of the 32 transaction
    slots — that handler was already op-dispatched, the same trick the MRU snapshots
    and the doom mirror use on `USER_SYNC_OVERLAY_MAP_DATA`.
- **The keycap draws the index above and the label below**, mirroring
  `render_lang_flag_key`. The INDEX rather than a generic macro glyph: a generic glyph
  is identical on all sixteen keys, so it says "this is a macro" and nothing else,
  while the index says which one and needs no font pack.
  - **The caption band has TWO faces, largest first** (`_Small_` 15px, then `_Nano_`
    10px), and `render_macro_key()` picks the largest whose WHOLE label fits.
    `_Nano_` alone was far smaller than the band can carry — "Macro 0" measures 40 px
    in a 72 px panel, and the caption is the thing a reader is meant to read.
    `_Small_` draws it at 57 px and still leaves 30 rows for the mark, which every
    stock numeral (4–19 px ink) clears.
    - ⚠️ **A label too wide for `_Small_` drops to `_Nano_` with its TEXT INTACT**,
      rather than being truncated at the bigger face. Losing characters to gain size
      is the wrong trade for a label whose job is to say what the macro does; the
      truncation loop is the floor face's last resort, not the ladder's.
    - ⚠️ **`extern`, not `#include`** — `NotoSans_Medium_Base_8pt.h` DEFINES the font
      (non-static) and each variant's `status_oled.c` already includes it, so a second
      include is a multiple-definition LINK error that compiles cleanly. Same pattern
      `oled_helper.c` uses for the same face.
    - **The host mirrors it in ONE place**: `macro_label.pick_face()`, called by the
      editor's pixel meter, the shared `MacroKeycapRenderer` and
      `tools/macro_label_preview.py`. The meter measuring at the floor face regardless
      would report "work mail" as 48 px of 72 — a third of the panel free where there
      is really 1 px.
  - ⚠️ **Truncate by MEASURED WIDTH, never by character count.** Measured against the
    shipped `_Nano_` face: `WWWWWWWW` is exactly 72 px (8 chars) and `iiiiiiiiiiii`
    is 34 px (12 chars) — an estimate is wrong in both directions.
    `PolyKybdHost/tools/macro_label_preview.py --check` renders the keycap the way
    `render_macro_key()` composes it and counts pixels outside the 72×40 window (320
    cells, 0 clipped). Its measurement lives in the Qt-free
    `polyhost/services/macro_label.py` because the host editor shows the same
    truncation while the user types, and an approximation would disagree with the key.
- **The default keymap binds `QK_MACRO_0..11` on `_UL`, where `F13..F24` used to
  live, and SHIFT reaches `M12..M15`.** `via.c` is the only core dispatcher for that
  range and we do not compile it, so the keycodes are ours outright. Two things about
  the banking:
  - **`poly_macro_banked_id(slot, shift)` is ONE implementation, called by the action
    path and the render path.** They are the pair that must never disagree — a keycap
    showing M13 while the key plays M1 is the same defect class as
    `render_key()`/`to_static_text()` unwrapping a mod-tap in only one of the two. The
    render path feeds it the **synced** modifier and the action path the live
    `get_mods()`, the same deliberate asymmetry the glyph-size key uses.
  - ⚠️ **`clear_keyboard()` before `poly_macro_start()`, or the bank modifier leaks
    into the macro's output.** Playback registers keycodes with a Shift the user is
    still holding, so M12..M15 would type in caps — and once capture exists, the same
    held Shift would be recorded as a spurious `DOWN Shift` step.
  - ⚠️ **`F13..F24` lose their default home**, so say so in the release notes; a user
    who wants them back assigns them from the layout editor.
- **An unclaimed slot ships a stock look: the MAYAN NUMERAL for its own index, over
  the caption "Macro"** (`poly_macro_seed_defaults()`). Without it a keyboard that
  has never met the host app shows sixteen keycaps distinguished only by "M0".."M15"
  in the index style, which is exactly the twelve-keys-that-look-alike problem the
  displays exist to solve. Five points, three of which are measurements:
  - ⚠️ **The caption does NOT repeat the index, and it used to** ("Macro 0" ..
    "Macro 15", changed 2026-09-08 on the report that it was redundant). The numeral
    above it already states the slot, so the index spent the widest thing on the
    keycap on the one fact the mark carries best; plain "Macro" also drops to 41 px in
    the `_Small_` face against 57 px, so every slot has room to spare rather than only
    the single digits.
    - ⚠️ **A change to the stock look is INVISIBLE on an already-flashed board**, which
      is what makes `slot_holds_legacy_seed()` necessary rather than tidy: the look is
      stamped into EEPROM on the first boot, so `slot_unclaimed()` is false from then
      on and the new caption would only ever reach a fresh keyboard. That helper
      re-seeds a slot whose body is EMPTY *and* whose record matches, byte for byte,
      what an older scheme would have written — so a caption someone typed themselves
      is never touched, and neither is a slot holding a real macro. Delete it once no
      field board predates the change.
  - **The condition is EMPTY, not "never seeded"** — no body and an all-zero look
    record — so there is no migration sentinel to keep and clearing a macro hands its
    keycap the stock look back. ⚠️ An unwritten record reads **all-zero, not 0xFF**
    (QMK's wear levelling normalises a cleared byte to zero — the fact that made
    `latin_assign` read as "every key hosts 'a'"), and zero *is* the default look, so
    the two are genuinely the same state.
  - **A COUNTING system, not a set of pictures.** The icon then states the same fact
    the caption does, and no purpose is read into a slot nobody has written yet — a
    gear or an envelope is a wrong label, not a neutral one. **Mayan is the only
    numeral system that fits**: base-20, so 0..15 are each a SINGLE glyph; it has a
    real glyph for **zero** (the shell, U+1D2E0) rather than an absence, which is what
    lets the set reach M0 at all; and bar-and-dot is what a 1-bit 72×40 panel draws
    well — three bars and four dots at worst.
  - ⚠️ **Nothing already in the pack covered them — measured, 0 of 20 codepoints
    resolved** — so this added a source font (`NotoSansMayanNumerals`, OFL, 50 KB) and
    a `_Mayan_` entry in the `symbols` category, and reshipped the `symbol` bundle
    (v8 → v9, 37,200 → 38,976 B in a 96 KB slot). The obvious alternative, geometric
    shapes, is *also* absent: **U+25A0/25CF/25B2/2B22 and friends are simply not in the
    shipped bundles**, which is worth knowing before proposing any icon by name.
  - ⚠️ **The entry sits at the very END of `fonts.yaml`'s `fonts` list, not at the end
    of the symbols block — the category picks the BUNDLE, the list position picks the
    global index, and the two are independent.** Appended after the other symbols
    entries it took index 147 and pushed the whole `fantasy` bundle up by one, which
    would have forced a second `.plyf` reship for a font nothing else touched. At the
    end of the list it takes index 181 and `--check` reports every other bundle
    identical.
  - ⚠️ **The sizes are measured, not chosen.** A captioned keycap leaves **32 rows**
    above the label and `draw_macro_mark()` draws at native size only while the glyph
    is *shorter* than that. As emitted these ink **4–19 px** (M15, three bars, is the
    tallest), so none is halved and 0 pixels clip. That check is
    `PolyKybdHost`'s `macro_look.find_glyph()` against the committed header, then
    rendering the keycap and looking at it. A `_Static_assert` pins
    `POLY_MACRO_COUNT <= 20`, past which a slot would seed a codepoint outside the
    emitted range and silently fall back to the index.
  - **They are PACK glyphs**, so a keyboard with no font pack draws the index instead —
    `render_macro_key()` already falls back that way for an icon it has no glyph for,
    and no keycap is ever left blank.
- ⚠️ **A PREVIEW THAT MIRRORS THE IMPLEMENTATION AGREES BY CONSTRUCTION — it cannot
  catch a placement bug, and this is the limit of the repo's "verify by rendering"
  rule.** `draw_macro_mark()` first drew a chosen icon at its native size or skipped
  it. A pack emoji inks **26–39 px** while a captioned keycap leaves about **29 rows**
  above the label, so measured over the icons the host picker offers, **four in five
  drew nothing at all**. `macro_label_preview.py` models the same placement, so it
  showed the same nothing — the field report was *"after selecting the icon I cannot
  see it in the preview and also not on the keyboard"* (2026-08-27), and neither half
  could contradict the other. Rendering only proves the C and the Python agree; the
  check that would have caught this is **measuring the glyph against the space it has
  to fit**, which is a different question and needs the real font metrics. The fix
  halves an overflowing icon through `kdisp_draw_glyph_half_at` (2×2-OR, which keeps
  the thin strokes plain decimation loses; half of even the tallest pack glyph is
  ~20 px) and falls back to the index when it fits at no size — the same fallback a
  missing glyph already took, so a keycap can never end up unmarked.
- ⚠️ **A guard whose precondition NOBODY ESTABLISHES is not a guard, and the comment
  claiming it holds is what hides that.** `poly_macro_start()` refuses to play a
  buffer whose last byte is not NUL (`poly_macro_buffer_intact`), and the case-37
  comment asserted this covered a half-streamed upload because "the host leaves the
  last byte clear until the final chunk". It does not: `join_buffer()` zero-fills to
  capacity, so the byte reads 0 **before** a write, **during** it and **after** it —
  the guard could never fire. An interrupted upload therefore left a *playable*
  splice, and the splice is made of the old macro:
  ```text
  before: "password123\0"   write: "hi\0" (interrupted)
  after:  "hi\0sword123\0"  -> macro 0 = "hi", macro 1 = "sword123"
  ```
  i.e. a fragment of a former macro becomes something a keypress types. Closed at
  **both** ends and the two are not redundant: the host raises a non-zero marker in
  the last byte *before* streaming and clears it with the final window
  (`write_macro_buffer`), and `poly_macro_write()` invalidates that byte on any
  window that does not carry it — the firmware must not depend on the host to arm its
  own integrity guard, and the host half is the one a mocked test can exercise.
  ⚠️ Consequence: a deliberate **prefix** write now leaves the buffer unplayable until
  something writes the tail. That is correct, and it is why the rig's prefix-write
  test restores the terminating NUL.
- **Cost: 208 B of RAM** (the 192 B label cache + the playback state), 0 B of EEPROM
  beyond the reclaim above. ⚠️ Verified against the **monolithic `POLYKYBD_DOOM=yes`**
  flavour, which PR CI does not build and which is the first thing to fail on any RAM
  growth: `.heap` 3828 → **3620 B** free. Re-measure there, not on the pack build,
  before adding another static.
  - **Reading that number** (this file quotes it repeatedly and never says how):
    ```bash
    qmk compile -kb polykybd/split72 -km default -e POLYKYBD_DOOM=yes
    printf '%d bytes\n' "$((16#$(arm-none-eabi-objdump -h .build/polykybd_split72_default.elf \
        | awk '$2==".heap"{print $3}')))"
    ```
    ⚠️ **Do NOT reach for `awk '{print strtonum("0x"$3)}'` — Debian's default awk is
    mawk (1.3.4 here), which has no `strtonum`.** It is not a silent failure — awk
    prints `awk: line N: function strtonum never defined` on **stderr** and exits
    **2** — but wrapped in the `printf "%d bytes" "$(…)"` form above the substitution
    swallows that status, so the **outer command exits 0 and prints a confident
    `0 bytes`**. That is the trap: a plausible number, not a missing one, with the
    real error a few lines up in stderr where a build log interleaves it out of
    sight. Hence the shell `$((16#…))`, which needs no awk function at all. Same
    family as the `objdump -s -j .data.<sym>` trap above — the tool answers a
    different question than the one you asked — except that here it does say so, and
    the wrapper is what hides it.
  - **Take the baseline from a build, not from this file.** Measured 2026-09-02 the
    monolith read 2772 B free at `44baf433`; that it matched the figure written here
    is what proved the baseline build was the right one. A quoted number can be
    several PRs stale — it is evidence only when you have just reproduced it.

---

### Recording a macro ON THE KEYBOARD (`poly_macro_record.*`, `base/macro_record.*`)

`KC_MACRO_REC` on `_UL` records a macro with no host app: tap REC (the board becomes a
slot picker), tap a macro key, type, tap REC again. The gesture and the alternatives
weighed against it are in `MACRO_RECORD_DESIGN.md`; what follows is the part a future
session gets wrong.

- **Capture is a `host_driver_t` SHIM, and it is installed from HOUSEKEEPING, not
  `keyboard_post_init_user()`.** `protocol_post_init()` runs *after* the post_init hooks
  (`quantum/main.c`) and installs the USB driver, so a shim set there is overwritten a
  moment later and the recording silently captures **nothing** — no error, no missing
  key, just an empty macro. `poly_macro_rec_tick()` installs it on its first call and
  leaves it in place for the life of the boot (it forwards to the previous driver
  unconditionally, so it costs one indirect call per report while idle).
- **What is recorded is the REPORT DIFF, not the keycode.** The shim sees
  `report_keyboard_t` / `report_nkro_t` after every layer, mod-tap and combo has already
  resolved, so a macro plays back what the keyboard actually SENT rather than what the
  matrix did. `poly_macro_rec_diff_6kro()` / `_diff_nkro()` in **`base/macro_record.c`**
  are pure (no quantum.h, no EEPROM, no timer) — the same seam as `base/fw_up_verdict.c`
  and `base/macro_decode.c`, and for the same reason: the arithmetic is the part with a
  bug future. `make test:polykybd_macro_record` — 47 tests, mutation-swept 7/7.
  - ⚠️ **`keys[6]` is an unordered SET, not a stack.** The host may compact it on any
    report, so "key at index 2 changed" means nothing; the diff has to ask whether each
    code is present in the other report. A positional comparison records a spurious
    release+press pair every time the host shuffles a held key down a slot.
  - ⚠️ **Order within one diff is load-bearing: releases before presses, and modifier
    releases before key presses.** Emitting a press first can leave the playback holding
    a modifier the user had already lifted, which types the *shifted* character — and
    the report the shim sees is the state AFTER the change, so the ordering is the only
    thing carrying the sequence.
- **Nothing reaches EEPROM until the recording stops.** Steps land in a 192 B RAM buffer
  (`POLY_MACRO_REC_BYTES`) and the splice is pumped a chunk per housekeeping pass
  (`POLY_REC_COMMIT_CHUNK`). A write per keystroke is a wear-levelling journal append,
  and the consolidation erase it eventually triggers is the documented mechanism behind
  the "slave becomes unresponsive" field bug — mid-recording is exactly when it would
  land. A recording that fills the buffer stops cleanly rather than truncating, because
  the encoder reserves room to close every held key.
- **`KC_MACRO_REC` is swallowed in `process_record_user()`**, like every other custom
  PolyKybd keycode — `_UL` is entered with `OSL()`, which re-dispatches a release-edge
  action up to three times (§ "A release-edge action fires up to THREE times"), and for
  a toggle that reads as *doing nothing at all*.
- ⚠️ **SWALLOWING THE REC PRESS DROPS THE VERY LAYER THE PICKER LIVES ON, and that is
  the cost of the swallow rule rather than a bug in it.** QMK's `process_record()` runs
  `clear_oneshot_layer_state(ONESHOT_OTHER_KEY_PRESSED)` whenever
  `process_record_user()` returns false **on a press** (`quantum/action.c`) — which is
  exactly what the swallow does — so a tap of `KC_MACRO_REC` reached through `OSL(_UL)`
  opens the picker and drops `_UL` on the same edge. Both halves of the picker then
  resolved the BASE layer, where there is no macro key and no REC key: **every keycap
  went dark, no slot could be picked, and pressing REC again did nothing** while the
  status OLED said `press M0-M15` (field, 2026-09-08).
  - **The fix is to resolve the picker against `_UL` explicitly**
    (`macro_picker_keycode_at()`), not to stop swallowing: the picker is a modal dialog
    over a known row, so which layer happens to be active is not information it wants.
    One resolver feeds the render AND `process_record_user`, so they cannot disagree —
    the same render/action pairing rule as `poly_keycode_at()`.
  - ⚠️ **The picker block therefore has to sit AHEAD of the `KC_MACRO_REC` block**, and
    own the cancel itself: by the time it runs, `keycode` is the base layer's, so
    neither the REC block nor the macro-playback block below can match. Entering `_UL`
    with `TO()` instead hides all of this — the layer is sticky, everything resolves,
    and the picker works — which is why it survived the desk test.
  - **Generalise: any "the board becomes a dialog" mode entered from a ONE-SHOT layer
    must resolve its own keys from that layer by number.** The FW-2 prompt is immune
    only because it addresses a fixed matrix POSITION (`FW_CONFIRM_ROW/COL`) rather
    than a keycode.
- **`poly_sync_t.rec_state` / `.rec_slot` are synced** for the same reason `fw_confirm`
  and `settings_more` are: the SLAVE draws its own half of the slot picker and only ever
  sees that struct, so without them the two halves disagree about which keys are the
  picker. Master-authoritative, never persisted — a recording does not survive a reboot.
- ⚠️ **The BYTE COUNT is deliberately NOT synced**, and that is why the two panels differ.
  It moves on every captured keystroke, so putting it in `poly_sync_t` buys a bridge
  frame per keypress on the one link this repo has been bitten by most. The master's
  panel shows `48/192 B` and the slave shows the stop hint in its place — true on both
  rather than a plausible zero on one, the same call `Lnk n/a` makes on the telemetry
  screen.
- ⚠️ **A new custom keycode with NO legend renders a BLANK KEYCAP, and that is
  indistinguishable from "the feature did not ship".** `KC_MACRO_REC` was added to the
  keymaps, the action path and the OLED, and every one of those was correct — but
  nothing gave it a case in `to_static_text()` / `keycode_to_static_text()`, so the key
  drew nothing and the first field report was *"I still do not see the REC key"*. The
  build is green either way: a missing legend is a missing `case`, not an error.
  **Grep the two legend switches for a new keycode before calling it done.**
  - **Its legend lives in `to_static_text()` (`poly_keymap.c`), NOT
    `keycode_to_static_text()`** — it names what the key will do NEXT (`REC/macro`
    vs `STOP/macro`), which comes from the synced `poly_sync_t.rec_state`, and that
    function only receives `led_t`. Same seam and same reason as `KC_GLYPH_SIZE_UP`.
  - **`MID_TWO_LINE` text, not an icon**: the resident C1 icon band is full (32/32),
    the pack has no record dot (U+23FA / U+25CF / U+2B24 all MISSING — only U+26AB at
    33x33), and the mid face is ASCII-only and RESIDENT, so the legend renders on a
    keyboard with no font pack. That matters more here than elsewhere: its neighbours
    on that row are the macro keys, and a REC key nobody can find is a gesture nobody
    can start.
  - **The picker draws `cancel` on it** (`render_macro_rec_cancel_key()`). Every other
    keycap goes dark while the picker is open, so without it the one key that backs out
    of the mode is invisible — the OLED says `REC = cancel`, but the board IS the
    dialog and the dialog should say it too.
  - ⚠️ **`Renderer.draw()` takes ABSOLUTE buffer coordinates and emits window-relative
    pixels** (`plot()` does `vx = bx - BUFFER_X` and drops anything outside the 72x40
    window). Verifying a legend at `x=0` therefore clips the first 28 columns and
    renders a plausible-looking fragment — two of five glyphs, no error. Draw at
    `oled_preview.BUFFER_X`, and sanity-check the harness against a SHIPPED legend
    (`MID_TWO_LINE("RESET","Eden")`) before believing anything it says about a new one.
    Measured that way: both states 0 off-panel pixels, ink x[1,57] y[0,34].
- **The status OLED is the ONLY indicator** (`oled_macro_rec_screen()`), because split42
  has no RGB matrix and the keycaps are busy showing what is being typed. Three things
  about its branch in the `oled_task_user()` ladder:
  - It sits **above `DISP_IDLE`**, or the idle timer swaps the panel to the logos
    mid-recording and takes the indicator with it. Belt and braces: housekeeping holds
    `update_performed()` while `rec_state != POLY_REC_IDLE`, exactly as the FW-2 prompt
    does — `update_displays()` early-returns once `DISP_IDLE` is set, so the keycaps
    would never be redrawn either.
  - **The whole line blinks, not a marker beside a fixed word.** Each line is centred
    from its own ink box (the `oled_telemetry_screen()` layout), so a marker that comes
    and goes would slide the text half a glyph twice a second.
  - ⚠️ **No `oled_clear()`, per frame or otherwise.** `oled_write_raw` diffs and dirties
    only the blocks that moved, so the 1 Hz blink costs one block per second; an
    `oled_clear()` defeats that and re-pushes the whole frame every tick (the "updates
    in multiple passes" flicker).
  - Measured with the committed `_Small_` face over every reachable line, both panel
    heights: widest ink **101 px** of 128 (`press M0-M15`), tallest **14 px** in a 16 px
    band, **0** clipped pixels. Re-measure rather than eyeball if a string changes.
- **Cost: 336 B of RAM** — the 192 B staging buffer, the previous-report snapshots and
  the state machine. Measured on the **monolithic `POLYKYBD_DOOM=yes`** flavour, which
  PR CI does not build and which is the first thing to fail on any RAM growth: `.heap`
  2592 → **2256 B** free. Re-measure there before adding another static.
