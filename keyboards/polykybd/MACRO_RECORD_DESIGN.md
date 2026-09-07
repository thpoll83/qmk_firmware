# Design: recording a macro on the keyboard itself

**Status:** design agreed, implementation started 2026-09-07. The pure encoder +
splice module (`base/macro_record.c`) lands first with its own unit suite; the
capture shim, the gesture and the status-OLED screen follow.

> **One-line summary.** Add a REC gesture that captures live keystrokes into a RAM
> buffer and, on stop, splices them into the existing dynamic-macro body buffer as
> macro N — so a macro can be made without PolyKybdHost. Playback, storage, the
> caption/look record, the split sync and the keycap render already exist and are
> not touched.

---

## 1. What already exists, so the delta is small

Macros shipped in protocol v15 (qmk #234). The parts that carry over unchanged:

| Piece | Where |
|---|---|
| 16 slots, bodies in one NUL-delimited buffer (~2.1 KB on split72: a 2459 B region minus the 272 B label array) | `config.h:407-456` |
| Windowed read/write with the mid-write integrity marker | `poly_macro_write()` / `POLY_MACRO_INCOMPLETE` |
| A pure decoder, unit-tested (`make test:polykybd_macro_decode`) | `base/macro_decode.c` |
| Playback as a housekeeping state machine, `clear_keyboard()` on abort | `poly_macro_tick()` / `poly_macro_abort()` |
| The look record (style / icon / 12-char caption) with a dirty-bit queue that pushes to the slave | `poly_macro_look_set()` / `poly_macro_label_sync_tick()` |
| The keycap render, caption truncated by measured pixel width | `render_macro_key()` (`poly_keymap.c:2690`) |

What is missing is only: a way to capture keystrokes, an encoder, a way to splice a
body into the middle of the shared buffer, and a gesture to drive it.

---

## 2. Decisions, and why

### 2.1 Keys pass through to the host while recording

The alternative was to swallow them, so a recording cannot dump half a macro into
whatever window is focused. Pass-through wins because it **fails safe**.

Both designs need a "recording" flag, and any flag can get stuck — a missed stop
edge, a fault mid-recording, a suspend path that does not clear it. With
pass-through a stuck flag means the board is quietly filling a RAM buffer nobody
will use and typing works normally. With swallowing it means the keyboard types
nothing, and the only clue is an indicator the user has already failed to notice.
On a board that ships `crash_record` precisely because faults happen, a mode that
can silently stop the keyboard from typing is the wrong default.

It also matches QMK's own `DYNAMIC_MACRO`, so the behaviour is what anyone who has
used one expects, and you see the text land as you record it — a wrong macro is
obvious before it is saved.

### 2.2 Capture at the REPORT, not at the keycode

Two altitudes were considered:

- **Keycode capture** in `process_record_user()`. Cheap, but it only sees basic
  keycodes. Layer keys, mod-taps, the emoji and language layers, `KC_LAT*`, the Intl
  picker and every custom PolyKybd keycode are inexpressible in the 8-bit
  send-string encoding, so it needs a reject policy that has to be kept in step with
  the keymap forever.
- **Report capture** by wrapping the host driver (`host_set_driver()` /
  `host_get_driver()`, `tmk_core/protocol/host.c:74`) and diffing successive
  `report_keyboard_t`. This records what the host actually receives.

Take the report shim. It removes the whole "what cannot be recorded" category:
the macOS GUI/Alt swap (`poly_keymap.c`, `s_apple_swap_latch`), the Intl picker's
registered Ctrl, and unicode input — which bottoms out in real keycodes anyway — all
record correctly with no special cases. Mods arrive as plain `0xE0..0xE7`, which the
existing encoding already expresses as DOWN/UP.

The shim **records and forwards**, so it cannot lose a keystroke by construction.

### 2.3 RAM while recording, ONE commit at the end

No EEPROM write happens during recording. The rejected alternative was writing live
into the free tail of the body buffer (no RAM cost): that is one
`eeprom_update_byte()` per keystroke, each a journal append in QMK's wear levelling,
with a ~50 ms blocking consolidation landing at random in the middle of live typing.
That is the mechanism behind the documented "slave becomes unresponsive" field bug.

`POLY_MACRO_REC_BYTES` (192 B, ~50-90 keystrokes) of `.bss`, committed once.

### 2.4 The indicator is the status OLED, not RGB

RGB is not universal on this board: `RGB_MATRIX_ENABLE` is split72-only, and on
split72 the user can turn it off (`KC_RGB_TOG`, and the display-off path suppresses
it anyway). An RGB-only cue is invisible on one variant and optional on the other.
The status OLED exists on both and is lit whenever the board is awake.

---

## 3. The gesture

`KC_MACRO_REC` on `_UL`, in one of the many free `KC_NO` slots.

1. **Tap REC** → the board becomes a slot picker. Every keycap blanks except sixteen
   showing `M0..M15` with their captions and whether they are occupied. This is the
   established "the board is the dialog" shape — see `fw_confirm`
   (`poly_keymap.c:4074`) and the Intl remap's `LATIN_REMAP_PICKKEY`.
2. **Pick a slot** → recording starts. Keys pass through as normal.
3. **Tap REC again** to stop, or a cancel key to discard.
4. Optionally **name it**: keys type into the 12-char caption instead of the host,
   then `poly_macro_look_set()` stores it and the existing dirty-bit tick pushes the
   look to the slave. Skipping keeps the index style (`M3`), which needs no font pack.

State lives in `poly_sync_t` — `rec_state` (enum) and `rec_slot` — for the same
reason `picker_page`, `remap_mode` and `fw_confirm` do: the slave draws its own half
of the picker and only ever sees that struct. The struct is ~24 B against a 96 B
`RPC_M2S_BUFFER_SIZE` cap, so two more bytes are free.

⚠️ `KC_MACRO_REC` is handled and **swallowed** in `process_record_user()`, never left
to `post_process_record_user()`. `_UL` is entered with `OSL()`, and a release-edge
action there fires up to three times (`process_action`'s `do_release_oneshot`) — for
a REC toggle that is start/stop/start.

---

## 4. The status-OLED screen

A new branch in the `oled_task_user()` ladder (`oled_helper.c:416`), placed:

```
fw_confirm > fw_up_active > [doom] > recording > DISP_IDLE > settings_more > status
```

Above `DISP_IDLE` matters — otherwise the idle timer swaps the panel to the logos
mid-recording and the indicator disappears. Belt and braces: hold
`update_performed()` while recording, exactly as `fw_staging_confirm_tick()` does for
the A/ACCEPT prompt, so neither the panel nor the keycaps fade out from under it.

`oled_telemetry_screen()` (`oled_helper.c:321`) is the template, including its rule
for the small panel — split72's 64 px holds four lines, split42's 32 px holds two,
and the two it keeps are the ones nothing else on the board can tell you:

- `REC M3`, blinking at ~1 Hz so it reads as "recording now" rather than "a screen
  about macros".
- `48 / 192 B`, the budget. This is what warns you before the buffer caps out.

split72 adds elapsed time and a stop hint on lines 3-4.

Blinking is cheap: `oled_write_raw` diffs and dirties only the changed blocks, so a
toggling word costs one block per second. ⚠️ Do **not** add a per-frame
`oled_clear()` — that defeats the diffing and re-pushes the whole frame every tick
(the "updates in multiple passes" flicker).

⚠️ split42 draws the flash / confirm / apply screens in **landscape**, not through its
portrait primitives. The recording screen follows them.

Because the commit is chunked (§5) the panel also carries `Saving M3…` and then
`Saved M3 · 48 B` for a moment — that turns a pause which would read as a hang into
the last step of the gesture.

---

## 5. Storage: the splice

The bodies are one NUL-delimited run, so writing slot N means shifting every byte
after it. No scratch buffer is needed if the direction is right:

- **growing** → copy the tail backwards, from the highest offset down;
- **shrinking** → copy forwards.

Both are single-pass in-place moves. `poly_macro_splice(id, body, len)`:

1. Find `start` (`poly_macro_find`) and the end of slot N.
2. `delta = (len + 1) - (old_end - start)`. Refuse if `delta` exceeds
   `capacity() - bytes_used()`, before touching anything.
3. Raise `POLY_MACRO_INCOMPLETE` in the buffer's last byte.
4. Move the tail in the correct direction.
5. Write the new body plus its NUL.
6. Clear the marker — the buffer is whole again.

⚠️ Steps 3 and 6 are load-bearing, not hygiene. A power cut mid-shift would otherwise
leave a **playable** splice of two macros, which is exactly how the tail of a former
macro (a password) gets promoted into a macro of its own. `poly_macro_start()`
already refuses a non-intact buffer, so an interrupted commit is inert rather than
dangerous.

⚠️ The move is up to ~2 KB of `eeprom_update_byte()`, so it is **chunked from
housekeeping** (`POLY_MACRO_COMMIT_CHUNK`, 32 B a pass), never inline in the key
handler. Same reasoning as the overlay-map repair drain: a bulk EEPROM operation on
the main loop is what stalls the split UART. Playback is refused while a commit is
in flight.

### 5.1 Encoding

QMK's own send-string encoding, unchanged (`base/macro_decode.h`), so a recorded body
is still playable by `dynamic_keymap_macro_send()` — a real cross-check rather than a
theoretical one.

- A key that goes down and comes up with nothing in between → `TAP`.
- Otherwise `DOWN` … `UP`, which is what a chord or a held modifier produces.
- Printable characters are **not** emitted as CHAR steps: the report shim sees
  keycodes, and turning them back into characters would need the layout the host has,
  not the one the board has. TAP/DOWN/UP replays identically anyway.

**Timing** is quantised, not recorded raw. A `DELAY` step is emitted only when the gap
exceeds `POLY_MACRO_REC_GAP_MS` (150), rounded to 10 ms and capped at 5000. Everything
else replays at the existing `POLY_MACRO_STEP_MS` (8 ms). Recording exact gaps would
triple the size of a body for no benefit.

⚠️ On stop, emit an `UP` for every key still held, and do not record the stop key
itself. A body ending in a dangling `DOWN` leaves a modifier registered on the host
at the end of every replay.

⚠️ A `QK_MACRO_*` press during recording is ignored rather than recorded, or a macro
can invoke itself.

---

## 6. Getting the macro onto a key

A recorded macro does nothing unless something is bound to `QK_MACRO_n`, and the
default keymap bound none — the host editor was the only way to place one.

**Done: `QK_MACRO_0..11` replace `F13..F24` on `_UL`, and SHIFT reaches `M12..M15`.**
No layer-enum change, so no `KEYMAP_LAYERS_FL_MERGED` bump and no dynamic-keymap
reset. Twelve keys rather than sixteen because that is what the row holds; Shift
banking rather than a second layer because `_UL` already uses Shift to modify a key in
place (`KC_GLYPH_SIZE_UP` reverses direction with it), and because a keyboard whose
keycaps are displays can just SHOW the second bank — the four shifted slots draw
`M12..M15` and the other eight draw blank, which explains itself.

`poly_macro_banked_id(slot, shift)` is the single resolver both the action path and
the render path call, so the keycap and the key can never name different macros. The
action path passes the LIVE `get_mods()` and the render path the SYNCED one, the same
deliberate asymmetry the glyph-size key uses: the action must follow the finger, the
legend must render identically on a half that only sees the housekeeping snapshot.

⚠️ `clear_keyboard()` before `poly_macro_start()`, or the bank's Shift leaks into the
macro's output — M12..M15 would type in caps, and once capture exists the same held
Shift would be recorded as a spurious `DOWN Shift` step.

⚠️ `F13..F24` lose their default home; say so in the release notes.

**Still a follow-up:** extend the gesture so that after stopping you can pick any key
and write `QK_MACRO_n` into it with `dynamic_keymap_set_keycode_poly()`
(`split_sync.c:309`) — same shape as the Intl remap's pick-key-then-pick-letter, and
the same storage the host editor uses.

### 6.1 Sixteen keycaps that look alike is not a placement

Twelve macro keys drawing nothing but `M0`..`M11` is the problem the displays exist to
solve, so an unclaimed slot ships a stock look: a game-piece icon above the caption
`Macro N` (`poly_macro_seed_defaults()`, seeded at post_init and after a reset).

The condition is **empty** — no body and an all-zero look record — not "never seeded",
so there is no migration sentinel and clearing a macro hands its keycap the stock look
back. That works because an unwritten record reads all-zero (wear levelling normalises
a cleared byte to zero, the `latin_assign` fact) and zero *is* the default look.

Card suits, dice pips and chess pieces, because the icons have to be tellable apart
rather than suggest a purpose — a slot has none until someone fills it. Every one is
20–30 px tall, which is measured rather than chosen: a captioned keycap leaves 32 rows
and `draw_macro_mark()` draws at native size only below that. That rules out most
emoji (40 px) and, less obviously, the geometric shapes — `U+25A0`/`25CF`/`25B2` and
friends are absent from the shipped bundles.

---

## 7. Host

**Less than it looks, and the first two drafts of this section were both wrong.**

The host keeps no macro cache across calls: `PolyCore.macro_list()` reads live, and
`macro_set()` re-reads the whole buffer immediately before writing it back. So a macro
recorded on the board is NOT clobbered by a later host save of a different slot — the
read-modify-write picks it up. Nothing is needed for storage correctness.

What is left is only that a Macros tab already open shows the snapshot it loaded on
tab-show.

**The fix is the shared `['G'][u16 state_generation]` block in the GET_ID reply**, not
a macro-specific field — see `CLAUDE.md` § *Telling the host something changed ON THE
BOARD*. The reconnect probe fetches GET_ID every second anyway, so this costs zero
additional reports and covers every future board-side change with one counter. A
`macro_generation` on MACRO_INFO would have made the editor poll a second command for
a strictly smaller result.

- **No `PROTOCOL_VERSION` bump.** An older firmware simply has no `G` block, which the
  host reads as "no generation available" and falls back to reloading on view-open —
  today's behaviour. The precedent is the `styles` byte, added to the MACRO_INFO reply
  with no bump and no `FEATURE_MIN_PROTOCOL` entry because a zero from old firmware is
  a truthful answer rather than a missing one.
- Firmware: `poly_state_touch()` at the end of the commit, beside the other board-side
  mutations that call it.
- Host: parse the block in `query_id()`; the tab reloads when the value moves. `M_*`
  plumbing only if a view needs to ask outside the probe.

⚠️ If the gesture also gains "assign to a key" (§6), the LAYOUT editor is a second
stale surface and a worse one: `kb_layout_dialog.py` reads `keymap_buffer()` and
`_load_macros()` **once in `__init__`**. The same counter covers it; the reload does
not exist yet.

---

## 8. Traps specific to this codebase

- Only the master runs `process_record` (the slave's matrix is pulled over the split
  link), so both halves are captured with no extra work.
- `clear_keyboard()` on cancel, matching `poly_macro_abort()` / `doom_begin()` / the
  FW-2 prompt.
- The RAM buffer must stop **at a step boundary**, not mid-step: a truncated DOWN with
  no UP is the dangling-modifier case again.
- Recording live keystrokes into EEPROM that any local process can read over cmd 37 is
  the same exposure the host editor already has, but far easier to trigger by accident.
  The visible REC indicator is a security control, not decoration.
- The rig cannot press keys (matrix simulation is still a TODO in `polykybd-ctnd`), so
  end-to-end verification is the unit suite plus a hardware round. The splice itself is
  reachable over HID and can get a HIL test.

---

## 9. Testing

`base/macro_record.c` is pure — a byte reader/writer callback, no `quantum.h`, no
EEPROM, no timer — the same split as `base/fw_up_verdict.c` and `base/macro_decode.c`,
and for the same reason: the arithmetic is the part with a bug future, and it is only
unreachable while it lives inside a function that also does I/O.

`make test:polykybd_macro_record` covers:

- the encoder: TAP vs DOWN/UP, mods, the delay threshold and its rounding, the cap,
  the stop-time UP flush, stopping at a step boundary;
- the splice: grow, shrink, same-size, first slot, last slot, empty slot, a slot that
  does not exist yet, refusing an over-capacity body, and that a body round-trips
  through `poly_macro_decode()` unchanged;
- that a splice interrupted after step 3 leaves the buffer non-intact.

Then mutation-test it (the `mutation-test-suite` skill) — a suite that passes against
a deliberately broken splice is measuring nothing.

Register in `base/tests/testlist.mk` and `base/tests/test_rules.mk`. ⚠️ The file must
be `test_rules.mk`, not `rules.mk`: `qmk ci-validate-keyboard-targets` globs
`keyboards/**/rules.mk` and would read the test directory as a keyboard.

---

## 10. Rollout order

1. ~~`base/macro_record.c` + `.h` + the unit suite~~ — **done**, 35 tests,
   mutation-tested (nothing wired into the firmware; the module is built only by its
   test suite, so the image is unchanged).
2. The host-driver shim and the capture state machine, behind the gesture.
3. `poly_sync_t` fields, the slot picker, the keycap cues.
4. The status-OLED screen (measure with `tools/status_oled_preview.py` and the
   `status-oled-layout` skill — bands, not eyeballs).
5. `QK_MACRO_*` on `_UL`, so a recording is usable with no host.
6. `poly_state_touch()` + the `G` block in the GET_ID reply + the host read-back
   (no protocol bump — see §7).
7. Docs (`update-polykybd-docs`, the Macros page).

Sizes to watch: `POLY_MACRO_REC_BYTES` plus the shim is ~220 B of `.bss`. Measure on
the **monolithic** `POLYKYBD_DOOM=yes` flavour, which PR CI does not build and which
had 2772 B of `.heap` free at last measurement.
