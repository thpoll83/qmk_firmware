# First-run tutorial (split72) — DESIGN

> **Continuing this work?** The worklist — what is verified, what is postponed and
> the order the remaining steps have to happen in — is [`TUTORIAL_NEXT.md`](TUTORIAL_NEXT.md).

**Status: IN THE MAKING.** Chapters 1 (three letters) and 2 (Shift, once per hand) are
implemented and confirmed on hardware; chapter 3 (layers) is postponed behind one
transition; chapters 4–5 are unwritten. The entry is still the prototype `KC_EDEN` key,
so **a cold boot has never been exercised**. This document is the design contract plus,
from §"Hardware rounds" down, the post-mortem of each round against it.

A calm, guided introduction to the keyboard's most important functions, played once on a new
board, immediately after the Eden intro. The mood target is *Monument Valley*: slow, quiet,
inspiring, nothing blinking for attention. Silence is the correction — a wrong key does
nothing at all.

Scope: **split72 only**, for the same reason Eden is — the geometry table
(`startup_anim_geom.h`) is split72-specific. split42 gets no-op stubs.

---

## 1. Lifecycle

```
power on
  → boot splash
  → Eden intro          (one-shot, existing renderer, unchanged)
  → TUTORIAL            (new mode: blank keycaps + blank status OLEDs, then steps)
  → done or skipped     → write the marker → normal legends
```

⚠️ **The Eden intro is not currently wired to boot at all.** `boot_intro_pending()`
(`state.c:463`) has **zero callers**: the marker, the pending check and the finish edge all
exist, but nothing starts the animation at power-on. Eden only runs today from `KC_EDEN` and
HID cmd 31. Wiring that trigger is part of this work (the deferred TODO in
`startup_anim.h`).

## 2. The marker

One byte: `poly_eeconf_t.boot_flags`, sentinel `BOOT_INTRO_DONE` (`0x5A`). Non-zero is
load-bearing — an erased or wear-levelled-clear EEPROM reads 0, which must mean *pending*.

- Written **only** at the tutorial's done/skip edge. Never after Eden alone.
- ⚠️ `mark_boot_intro_done()` currently runs on **Eden's finishing edge**
  (`poly_keymap.c:955`). It must move. Left there, pressing RESET Eden would clear the
  marker, play the animation, and immediately re-stamp it — no tutorial, ever, and nothing
  in any log to say why.
- Written **straight through**, not via the `g_boot_dirty` → `save_all_dirty()` path. The
  deferred path only flushes at suspend/shutdown/store, so a user who finishes the tutorial
  and then unplugs would see the whole thing again on every boot, forever. Precedent: the
  `keymap_layers_fmt` stamp is written through for exactly this reason.
- An interrupted run replays **Eden + tutorial** next boot. That is the intent: nobody
  should miss it because they unplugged halfway.

### Persistence across flashing — verified

EEPROM lives at `0x7FE000..0x800000`, the last 8 KB of the 8 MB chip
(`WEAR_LEVELING_BACKING_SIZE 8192`, placed at `PICO_FLASH_SIZE_BYTES - size`). The flash map
puts firmware at 0–2 MB, staging at 2–4 MB, and the resource region stops short at
`0x7FE000`. **No flash path touches it** — not the HID updater, not a UF2 drag-and-drop. So
"flash a new build, don't see the tutorial again" holds by construction.

Three things still clear it, all acceptable: `flash_nuke.uf2`; an upstream merge that changes
QMK's EEPROM magic (`eeconfig_init_user()` zeroes the struct); and inserting a field *before*
`boot_flags` — it is an appended tail byte, so keep appending after it.

## 3. RESET Eden

`KC_EDEN` already renders `MID_TWO_LINE("RESET", "Eden")` — the keycap literally reads
**RESET / Eden**. Its job becomes what the label always claimed: **clear the marker**, so
Eden + tutorial return at the next startup. Nothing else re-arms them.

It keeps its current immediate Eden replay as the acknowledgement that the reset landed. It
does **not** launch the tutorial on the spot — pressing it mid-work should not blank the
board and lock the user into a lesson.

HID cmd 31 (REPLAY_ANIM) is **unchanged**: replay the animation, arm nothing. A host "replay"
must not surprise anyone with a tutorial at their next boot.

## 4. Rendering — the tutorial cannot reuse the one-shot renderer

⚠️ `startup_anim_tick()` time-slices **only** when `s_loop` is true; the one-shot path renders
a whole frame per call, and `process_record_user` swallows every key for its duration. Both
are correct for an intro nobody interacts with, and both are fatal for a tutorial.

Measured: a full Eden frame is **~150 ms of CPU** for ~36 keycaps (~4.3 ms each, of which only
~0.3 ms is the SPI push — ~93% is compute). The documented field bug is *"Eden doesn't wake on
the first keypress"*: a short tap starts and ends inside one frame and is never seen.

So the tutorial gets its own renderer, built on the **sliced** pattern the idle loop already
proved:

- render keycaps until a slice budget (~`EDEN_IDLE_SLICE_MS`, 3 ms) is spent, then return;
- resume at the same keycap next pass;
- latch the frame's time and its animation state **once** per frame so the slices compose
  into one coherent instant with no shear across the board.

`update_displays()` early-returns while the tutorial owns the keycaps (as it does for
`startup_anim_active()`). `update_performed()` is called each pass so the idle fade cannot
dim the tutorial out from under the user.

## 4a. Handing the panels back — the DOOM contract

⚠️ **Found on hardware (round 1): after the tutorial, keycaps came back part black and
part garbage.** The cause is documented in `doom_slave_stop()` for its own reason, and
the tutorial hits it by a different route:

`update_displays()` is reached **only** through the refresh drain, and housekeeping does
not call `sync_and_refresh_displays()` while the tutorial owns the panels. So
`update_displays()` never runs during the tutorial, and its
`if (tutorial_active()) { s_disp_render_active = false; return; }` early-return is **dead
code**. `s_disp_render_active` stays true, so the generic
`kdisp_invalidate_all_windows()` at the top of the next render is skipped — and that
render streams a stale sub-rectangle per panel instead of erasing the whole window.
DOOM calls the same failure *"DOOM-exit leftovers"*.

So a mode that owns the panels must hand them back explicitly. `tutorial_stop()` does
what `doom_exit()` / `doom_slave_stop()` do:

- `clear_keyboard()` — every key event was swallowed, so anything held would stay
  registered on the host and auto-repeat;
- blank every panel on this half, and restore contrast;
- **force `kdisp_invalidate_all_windows()`** (`doom_blit_invalidate_windows()` is
  literally this call) so the next render redraws each window in full.

The overlay-pool half of DOOM's handback (`reset_overlay_pool`, `clear_display_has_overlay`,
`reset_display_to_pool`, `reset_fragment_context`) does **not** apply — the tutorial never
touches the overlay pool.

The teardown order in housekeeping matters too: blank → restore brightness → repaint
(two-pass, as `display_wakeup()` does) → **then** the EEPROM write, whose wear-levelling
consolidation can hold interrupts off for ~50-100 ms and must not land between the SPI
writes handing the displays back.

## 5. Brightness — both display kinds, plus the sensor

⚠️ Three things will otherwise dim the tutorial mid-run: the restored user brightness, the
LTR-559 auto-brightness drive (pushes roughly every 500 ms while auto mode is on), and the
status OLED's own default.

- **Keycap OLEDs**: pin contrast for the duration; restore on exit, the way Eden already
  restores `set_displays(get_local_state()->contrast, false)` on its finishing edge.
- **Status OLED**: normally `oled_set_brightness(OLED_BRIGHTNESS)` with **`OLED_BRIGHTNESS`
  = 60** of 255 (`config.h:373`) — about 24%. The tutorial raises it and restores 60 after.
  ⚠️ It is set from `sync_and_refresh_displays()`, which is **skipped entirely** while Eden
  owns the displays — so the tutorial must set it itself rather than expecting the normal
  path to.
- **Sensor**: gate `poly_ltr559_drive()` off for the whole Eden + tutorial window, the same
  shape as the existing `if (!fw_staging_fw_up_active())` guard.

## 6. Fades on 1-bit panels

Two mechanisms, not interchangeable:

- **Per-key SSD1306 contrast register** (what `kdisp_idle()` already modulates) — genuine
  smooth brightness, one command, no re-render. This is the **key fade-in**.
- **Dither / scanline density** (`kdisp_set_gfx_scanline()`, already used for the Eden idle
  legend) — needed wherever a dim thing must coexist *inside one keycap* with a bright thing,
  since contrast is whole-panel. This is the **ripple's** fade.

## 7. The ripple

A solid 5 px disc at the pressed key's centre, expanding across the whole board and thinning
as it grows, gone in ~400 ms.

`startup_anim_geom.h` gives every key a board-space centre and rotation (`cx, cy, ang`; board
1673×563, and those units are ≈ display pixels — ~87 between key centres). Each keycap draws
whatever arc crosses its own 72×40 window, including the rotated thumb keys. Eden's `sa_bg`
already does exactly this per-pixel transform, so the machinery exists.

**Crossing the halves.** Only the master runs `process_record`, and the two MCUs have
independent clocks — there is no shared time base. The master sends *"ripple from (cx,cy),
start now"*; each half runs its own ripple clock from receipt. Skew is one bridge round-trip
plus a housekeeping pass, invisible against a ~400 ms expansion. Eden's `anim_nonce` is the
pattern; this fires per keypress rather than once.

## 8. Text

⚠️ **Resident fonts only.** At true first boot the font pack may not be flashed — the host
flashes bundles on connect, and a new board may never have been connected. `latin` is
resident so ASCII is safe; the 19 px `_Mid_` face is reachable via `HINT_MID`. No emoji, no
pack glyphs, or the very first thing a new user sees is blank keycaps.

Sentences live on the **status OLED**. Keycaps carry the interaction.

## 9. Input

- Swallowed in **`process_record_user()`**, never left to the release edge — an `OSL()` layer
  re-dispatches a release-edge action up to three times.
- `clear_keyboard()` on entry and on exit, so a key held across the boundary cannot stay
  registered and auto-repeat on the host.
- **Skip: hold Esc ~1 s**, on either half's outer-edge key. Documented only — no hint on
  screen, keycaps stay blank.
  ⚠️ The mirror is **not** the same local matrix column. `keycap_dispmap.py` gives right
  `disp (0,6)` at x=223 (outer edge) and right `disp (0,0)` at x=143 (**inner** edge), versus
  left `disp (0,0)` at x=0. This is the documented `c--` fold. Resolve the matrix position
  through `key_display[]` and verify with `keycap_dispmap.py` — the LAYOUT macro maps array
  order to matrix order, so neither the array nor the block-comment art is the physical truth.

## 10. HID: enable / disable

One get/set command in the shape cmds 28 / 30 / 34 already use:

| payload | meaning |
|---|---|
| `0xFF` | query — reply is the current state |
| `0` | disabled (marker set). **While the tutorial is running this is the remote skip** |
| `1` | armed (marker cleared) — same effect as pressing RESET Eden |

Reads and writes the same `boot_flags` byte, so it is persisted and survives flashing like
everything else. One command covers arm, skip and query — no second command needed.

Safe to expose in **both** directions, unlike the FW-2 signing prompt where only *cancel* is
remote: neither arming nor skipping a tutorial can authorize anything.

Version-gated: a `FEATURE_MIN_PROTOCOL` entry plus `PROTOCOL_VERSION` / host `__protocol__`
bumped in lockstep. This is an ordinary device-facing feature, not one of the
independently-dispatched oddballs (cmd 31, the profiler, the fontpack transport).

Host: `polyctl tutorial on|off|status`.

## 11. The rig

The tutorial only owns the displays and swallows keys — **HID keeps answering throughout**,
so an armed board does not wedge the rig. What it would break is the narrower set of tests
that need idle to engage, since the tutorial holds `update_performed()`.

So the fix is ordering, not a build flag: the rig **disables it during setup**, before the
suite runs.

⚠️ Deliberately **no `POLYKYBD_HIL` compile-time skip** — it would leave the tutorial with no
hardware coverage at all, and it would widen the divergence between the HIL image and the
shipping image.

One extended-tier HIL test: arm → power-cycle (the `reboot_persistence` machinery already
does this) → assert the board comes up in the tutorial → skip remotely → assert normal.

## 12. Step 1 — three letters

| t | Keycaps | Status OLED |
|---|---|---|
| Eden ends | all blank, panels on, contrast pinned | blank |
| +0.6 s | — | first line fades in |
| +1.5 s | letter #1 fades in (contrast 0→full, ~500 ms) | line stays |
| press | ripple from that key across both halves; the letter settles out with it | — |
| +0.4 s | letter #2 fades in | second line |
| … | letter #3 | … |
| done | → step 2 | — |

**One letter lit at a time**, so there is no wrong-key state: a press on anything else does
nothing, silently.

**At least one of the three is forced onto each half**, so the ripple visibly crosses the
split — that is the moment that sells the effect. Letters are drawn from keys that pass
`key_has_display()` (matrix (3,7) and (8,0) are real keys with no OLED behind them).

Steps 2+ are designed below. The word "step" is retiring in favour of **chapter**:
`tut_state_t.step` counts letters *inside* chapter 1, so it cannot also count chapters.

## 12b. The arc — what the tutorial is actually teaching

*"We have to guide the user through the PolyKybd interface — there are menus etc and we
should teach them in a meaningful way."*

PolyKybd has exactly **one** idea, and everything else is a variation of it: **hold a key
and the whole board becomes something else.** Shift, the Fn/Numpad layers, the Intl
layer and the settings menu are all that same gesture at increasing depth. So the
chapters are not a feature list — they are that one idea, four times, each time deeper:

| # | Chapter | Gesture | What it teaches |
|---|---|---|---|
| 1 | Three letters | tap | the keys are screens, and they answer you *(done)* |
| 2 | **Shift** | **hold** | **holding a key transforms every other key** |
| 3 | **Layers: Fn** | **hold, release** | **the same gesture with a new payload — and the NOTATION: the layer symbol, `!`, `*`** |
| 4 | Settings | tap `*`, tap | a *menu*, and the way back out |
| 5 | Intl | hold | the transformation no other keyboard can do |

**Shift comes before layers on purpose.** Shift is a modifier every keyboard has, so
chapter 2 costs no new concept — it only shows that on *this* board a modifier is
visible. Chapter 3 then reuses the identical gesture and the user discovers that a layer
is "shift, but further", rather than learning a keyboard-specific idea cold.

**Intl is LAST** — it is the most PolyKybd-specific chapter and also the trickiest
(`MO(_ADDLANG1)` carries a latching picker and a remap mode underneath it, and the
CLAUDE.md notes record that layer breaking twice on release-swallow ownership alone). Put
at the end it can be trimmed or deferred without unpicking anything before it.

Budget: 2 ≈ 8 s, 3 ≈ 12 s, 4 ≈ 15 s, 5 ≈ 8 s — around 50 s, which is about the limit for
something that cannot be paused.

⚠️ **Chapter 4 is the one that can STRAND someone.** Chapters 2, 3 and 5 end when a
finger lifts. A menu does not: `OSL(_UL)` is one-shot and `TO(_SL)` is a toggle, and
nothing times out. If chapter 4 does not teach the exit as hard as the entry, it teaches
a trap.

## 12d. Chapter 3 — a layer, and what the status OLED is telling you

*"Let the user press the function layer and then return to the base layer, then point out
the differences with big icons on the status screen — the layer symbol switches, the
exclamation mark only while pressing, the asterisk for a single press."*

The gesture is chapter 2's, so the chapter is cheap: reveal, prompt **"Hold Fn"**, the
board becomes the Fn layer under the same outward sweep, release returns it. What is
*new* is the second screen.

### The notation already exists on the keycaps, and nothing explains it

This is the real find. `keycode_helper.c` already spells the three cases, and they are
the whole grammar of the board's layer keys:

| keycode | legend | meaning |
|---|---|---|
| `MO(_NL)` | `Nm!` + `ICON_LAYER` | **`!` — only while you hold it** |
| `OSL(_UL)` | `Util*` + `ICON_LAYER` | **`*` — for a single press, then it is gone** |
| `TO(_NL)` / `TO(_UL)` / `KC_BASE` | `Nm` / `Util` / `Base` + `ICON_LAYER` | no suffix — it stays until you leave |

So chapter 3 is not inventing a notation to teach; it is explaining one that already
ships and that a new owner has no way to decode. The status OLED carries the icons at a
size the keycap cannot — `ICON_LAYER` beside the layer that is live, then `!` and `*`
large, each paired with the phrase that defines it.

⚠️ **`MO(_FL)` renders `"Fn"` with NO `!`, while `MO(_NL)` renders `"Nm!"`** — the same
kind of key, one carrying the mark and one not. A chapter that teaches "`!` means only
while pressing" and then asks the user to hold a key with no `!` on it contradicts the
board in its own lesson. Two ways to close it, and they are not equivalent:
- add the `!` to `MO(_FL)` (the notation becomes consistent, the legend grows a char), or
- run the chapter on `MO(_NL)` instead (Numpad, which already carries the mark).

The first is better and is a one-line legend change, but it touches a shipped legend, so
it wants saying out loud rather than folding silently into a tutorial PR.

### The `*` is explained here and EXERCISED in chapter 4

Chapter 3 shows both marks; only `!` is felt, because Fn is momentary. `OSL(_UL)` is the
one-shot the user then presses to open chapter 4, so the `*` is demonstrated exactly
where it becomes load-bearing — which is also the entry to the only chapter that can
strand them.

### Sequencing

`TUT_REVEAL` → `TUT_LAYER_WAIT` ("Hold Fn") → `TUT_LAYER_SWEEP` (the layer's legends
arrive under the chapter-2 wave; status swaps to `ICON_LAYER` + the live layer) →
`TUT_LAYER_HELD` (minimum dwell, so a tap still shows it) → release →
`TUT_LAYER_NOTATION` (status only: the big `!` and `*` with their phrases; keycaps stay
on the base layer so the contrast with the held state is fresh) → `TUT_GAP`.

⚠️ The layer key must fall through the tutorial's key swallow for the same reason Shift
must (§12c) — `get_highest_layer(local_layer->layer)` only moves if QMK actually applies
the layer. That is one exception covering modifiers AND layer keys, written once.

## 12c. Chapter 2 — the reveal, and Shift

*"After pressing these 3 letters we should fade in all latin letters and the shift keys
and then ask the user to press shift and see how the keys react."*

| t | Keycaps | Status OLED |
|---|---|---|
| chapter 1 ends | dark | — |
| +0.6 s | the **lit set** paints at contrast 0, then ramps up (~1.2 s) | "Every key" / "is a screen" |
| +2 s | lit set steady; only the shifts do anything | "Hold" / "Shift" |
| Shift down | the transformation sweeps out from the pressed shift | "Shift" / (the board is the message) |
| Shift up | snaps back, instantly | — |
| +0.7 s | → chapter 3 | — |

**The lit set is the letters plus both shifts** — both, so the split is represented on
each half again, exactly as chapter 1 forces a letter onto each. Everything else stays
dark, which is the same "there is only one thing to look at" grammar chapter 1 uses, and
the same grammar `render_key()` already implements for the letter-remap prompt.

### The legends are the BOARD's, not the tutorial's

The payoff is *"see how the keys react"*, so the reaction has to be the keyboard's own.
The tutorial therefore does **not** draw its own A–Z: it calls the real legend renderer
per key, through a new `tutorial_draw_key_legend(slot)` beside `tutorial_slot_letter()`
in `poly_keymap.c`.

⚠️ **That callback must reproduce the `to_static_text()` / `render_key()` PAIRING**, not
just call `render_key()` — they are consulted in that order and a key that has a static
legend draws nothing from `render_key()` alone. This is the seam CLAUDE.md already warns
about ("both must normalise the keycode the same way, or a key draws its chrome and NO
legend"), and the shift keys are exactly the case that would break: their legend is
static text, not a letter.

The win is that the transformation is then **real** — accented variations, the selected
language, caps-lock interaction, the glyph-size setting — instead of a simulation that
drifts from the board it is teaching.

### Shift must actually register

`process_record_user()` currently swallows **every** key while the tutorial runs
(`return false`, unconditional). `render_key()` reads `local_layer->mods`, which only
moves if QMK registers the mod — so chapter 2 needs exactly one exception: **modifiers
fall through.** A bare Shift hold types nothing, so "nothing reaches the host mid-lesson"
survives intact.

This is also the standing repo rule rather than a new idea — *"gate a release swallow on
OWNERSHIP, not on the keycode; modifiers and layer keys must fall through"* — which has
already bitten twice in the Intl layer, one function apart. Chapters 3 and 4 need the
same exception for their layer keys, so it is written once, now.

⚠️ It also means the SLAVE needs nothing new: `poly_layer_t.mods` is already synced, and
the slave's legends already follow it. No new tutorial sync bytes for this chapter.

### The transformation sweeps; it does not flip

The shifted legends arrive as a **wavefront out from the pressed shift key**, reusing
`tut_ripple_radius()` — inside the front the key shows its shifted legend, outside it
still shows the plain one. Release is **instant**, not a reverse wave: letting go should
feel like the board snapping back to the truth, and a second wave doubles the cost to
teach nothing new.

Three reasons, and the third is the one that decides it:

- It reuses the motion language four hardware rounds were spent getting right, so the
  ripple reads as *the* PolyKybd gesture rather than a chapter-1 flourish.
- It makes the causation visible: *this* key did that to *those* keys.
- ⚠️ **It is what makes the repaint affordable.** A full-board legend repaint is the
  Eden-intro cost (~150 ms of pushes); the renderer slices at `TUT_SLICE_MS` 3 within a
  `TUT_FRAME_MS` 16 budget, and chapter 1 only ever repaints one or two keys because
  `s_lit` skips a key that was blank and stays blank. Flipping 30-odd keys in one frame
  blows that budget outright. The wave spreads the same work over ~0.8 s, repainting
  each key once as the front crosses it.

The cost is that for ~0.8 s the board deliberately shows a state that is not quite true.
That is acceptable **once, while teaching**, and nowhere else.

### The fade is contrast, and the paint happens once

Same split as chapter 1, and for the same reason: a 1-bit panel cannot dim ink, so the
fade-in lives in the panel's contrast register. The reveal therefore **paints the lit set
once** (one sliced full-board pass at contrast 0) and then ramps contrast — a cheap
per-panel register write with no buffer push. A per-frame repaint of 30 keys would be the
same budget failure as the flip above.

### Phases

`TUT_REVEAL` (paint + contrast ramp) → `TUT_SHIFT_WAIT` (no timeout — only a shift leaves
it) → `TUT_SHIFT_SWEEP` (the wave, `TUT_RIPPLE_MS`-ish) → `TUT_SHIFT_HELD` (steady, with a
minimum dwell so a *tap* still shows the transformation) → `TUT_GAP` → chapter 3.

A wrong key still does nothing, silently — the chapter-1 grammar is unchanged.

## 13. Open

- Exact wording of every chapter's two-panel prose.
- Whether the ripple also crosses onto the status OLEDs, or stays on the keycaps.
- **`MO(_FL)` carries no `!` while `MO(_NL)` does** — fix the legend, or run chapter 3
  on Numpad? See §12d.
- Whether chapter 5 (Intl) ships at all, or is deferred.
- **Chapter 4's exit.** Two keys deep with no timeout — see the warning in §12b.

## 14. Suggested implementation order

1. Wire the Eden boot trigger (`boot_intro_pending()`), marker still set on Eden's edge.
   Verifiable on its own: a new board plays Eden once.
2. Move the marker to a tutorial-owned edge; add the sliced tutorial mode with a blank
   screen and hold-Esc skip. Board now boots → Eden → blank → skip → normal.
3. Brightness pinning (both kinds) + sensor gating.
4. Ripple, single half.
5. Ripple across the split.
6. Step 1 letters + status-OLED text.
7. Chapter 2: the modifier/layer-key fall-through, `tutorial_draw_key_legend()`, the
   reveal, the shift sweep.
8. Chapter 3 (Fn + the status-OLED notation), then 4 (settings), then 5 (Intl).
9. HID enable/disable + `polyctl` + the HIL test.
10. RESET Eden re-arm semantics — and the first COLD-BOOT round, since every hardware
    round so far has entered through the temporary RESET Eden path.
11. Docs page.

## Why the slave joins: copy Eden, do not invent a start message

The Eden idle screensaver runs correctly on both halves and the tutorial did not, and
the difference is not in the renderer — it is in **how each half learns it should be
running**.

**Eden never receives a start message.**

- *Boot intro*: `keyboard_post_init_user()` runs on BOTH halves and each reads its OWN
  EEPROM marker (`boot_intro_pending()`), so a cold boot needs no cross-half message at
  all. The halves animate independently off their own timers; a few ms of skew is
  invisible.
- *Idle loop*: `eden_idle_tick()` runs every housekeeping pass on both halves and
  re-derives `want` from state each half already holds — `idle_style == IDLE_STYLE_EDEN`
  and the `DISP_IDLE` flag, both ordinary `poly_sync_t` fields carried by the normal
  `sync_and_refresh_displays()` diff, where the diff IS the retry queue. It is
  **level-triggered, every pass**: nothing to lose, no ordering requirement, and the
  stop is derived the same way (`else if (startup_anim_is_loop())`).
- The only thing Eden ever pushes is the **replay nonce**, for the one case with no
  local trigger (HID cmd 31 / `KC_EDEN`) — and even that is retried every pass until
  `sync_succeeded()`, then latched.

**The tutorial was edge-triggered on a single message.** The slave entered on a `0->1`
transition of `tut[0]`, from the master, over a bespoke **one-retry** push, on a path
where the normal state sync is skipped. Worse on the `KC_EDEN` path:
`s_tutorial_armed` was set inside `process_record_user()`, which only ever runs on the
master, so the slave had **no local trigger at all** and that one push was the entire
mechanism. Lose it and the slave sits dark for the whole session while the master
happily runs the lesson. The 400 ms re-arm added later is a hand-rolled retry queue for
an event — the guard shape this project keeps getting caught by.

**The fix is to give the slave a local trigger, not a better retry.** `tut[0]` is now a
bitfield: `TUT_SYNC_ACTIVE` (the tutorial is running) and `TUT_SYNC_ARMED` (the
first-run experience is armed — start the tutorial when the intro ends). ARMED is a
**level held for the whole of Eden**, so it rides every sync for seconds rather than
once; the split handler calls `poly_arm_tutorial_after_intro()` on the slave, and the
slave then enters the tutorial from its **own** Eden finish edge, exactly as a cold boot
does. The per-step `tut[]` sync is thereby demoted from "the mechanism" to "a
correction".

- ⚠️ `tutorial_sync_apply()` must test `in[0] & TUT_SYNC_ACTIVE`, **never `in[0] != 0`**
  — ARMED is set throughout the intro, so a bare non-zero test starts the tutorial on
  top of the animation.
- ⚠️ The slave's local start picks its own slots (different seed), so it can light a
  different key until the master's `tut[2]` correction arrives. That converges within a
  push and is strictly better than dark; if it ever reads as a flicker, carry the seed.
- **Generalise**: before adding a start/stop message between the halves, check whether
  each half can *derive* the state instead. Eden does, twice, and that is why it works.

## Third hardware round (2026-09-22)

Three findings, and two of them are about the two panels being one board rather than
two independent ones.

**The ripple was still too fast.** 2200 ms was already the second try (400 ms read as
a flicker). `TUT_RIPPLE_MS` is **3400**. The dissolve was retuned at the same time and
then found to be broken outright — see the fourth round below.

**Both status panels said the same words.** `tutorial_line()` chose by line number
alone, so "Good" appeared twice — reported as *"I saw 'Good' 'Good'"*. The keyboard is
30 cm of screen read left to right, so the prose now reads **across** the two halves:
`Welcome` / `to PolyKybd`, `Press the` / `lit key`, and on a correct press the LEFT
panel confirms **which** key landed while the right says `Good`.
- The confirmation letter is drawn at **2×** (`kdisp_draw_glyph_double_at`, the FW-2
  prompt's primitive) rather than set as a line: the keycap faces stop at 19 px and one
  character in that face is lost on a 128×64 panel. Measured 28–34 px tall.
- It comes from **`ripple_slot`**, not from the step's slot — that is the key actually
  pressed, it survives into `TUT_GAP`, and it is the one field the slave is *told*
  rather than deriving, so the halves cannot disagree about what was confirmed.

⚠️ **The slave's ripple ran BEHIND the master's, and the cause was the clock, not the
link.** The slave stamped `phase_start` at RECEIPT, so its wave trailed by the whole
press-to-sync latency and the offset showed as a step where the two halves meet. The
fix is not a faster push: the master now sends **how far its own ripple has already
run** (`tut[5]`, `tut_elapsed_encode`, 16 ms a unit), and the slave back-dates
`phase_start` by that much. It needs no shared time base — an ELAPSED is the one
quantity both MCUs can agree on — and because every push carries it, a lost frame costs
the slave a later *start* rather than a permanently offset wave.
- ⚠️ The codec **saturates**; it must never wrap. A wrapped value past the end of the
  ripple reads as "just started" and would restart the slave's wave at the origin —
  the exact artefact the field is there to remove. Mutation-tested.
- ⚠️ This is separate from the tutorial's **start**, which is local on both halves (see
  the section above). On a cold boot both halves read their own EEPROM marker, so the
  start is already aligned; only the per-press ripple ever depended on the wire.

## Fourth hardware round (2026-09-23)

⚠️ **THE DISSOLVE WAS KEYED TO ELAPSED TIME WHILE THE RADIUS EASES OUT, so it spent
itself after the wavefront had left the board.** Reported as *"the ring does not
dissolve on expansion — it should fade out, pixel out"*, and the arithmetic says
exactly that. Measured at `TUT_RIPPLE_MS` 3400 with the old time-keyed curve:

| elapsed | radius | density |
|--------:|-------:|--------:|
|   533 ms |   523 | 255 (solid) |
|   986 ms |   895 | 215 (84 %) |
|  1706 ms |  1354 | 151 (59 %) |
|  3400 ms |  1800 |   0 |

A strike near the middle of a half has passed every key by radius ~900 — where the
ring was still 84 % solid. Everything after that happened off the board. The ring was
not failing to dissolve; it was dissolving where nobody could see it.

**Both curves now read one shared easing, `tut_ripple_travel()`**, so the ring cannot
expand on one schedule and fade on another. Solid while travel ≤ `TUT_RIPPLE_SOLID_T`
(60 → ~430 ms, radius ~420), dithered away by `TUT_RIPPLE_GONE_T` (235 → ~2.45 s,
radius ~1660): 159/255 at radius 891, 65/255 at radius 1349, gone before it leaves.

- The test that matters states the **defect**, not the implementation
  (`TheRingHasVisiblyThinnedBeforeItLeavesTheKeys`): density must be ≤ 160 by radius
  900 and ≤ 60 by radius 1400. Any re-tune may move any constant as long as that holds.
- ⚠️ `DensityFallsAsTheRadiusGrows` is **weaker than it looks** — any two monotonic
  curves satisfy it, so it does not pin the coupling. Mutation-testing found that:
  making the radius linear escaped it. `RadiusIsDerivedFromTheSharedTravelCurve` is
  deliberately white-box for that reason.

⚠️ **The confirmation letter was drawn by DOUBLING a 19 px glyph, which doubles the
stair-steps with it** — *"looks too much pixelated"*. `kdisp_draw_glyph_double_at()`
repeats every pixel, so a 2× draw is never the way to get a big letter when a real
larger cut exists. `tutorial_draw_big_letter()` walks the latin tiers instead and takes
the largest that is actually flashed:

| tier | face | `Q` ink |
|---|---|---|
| `0xF3000` (L) | `BaseL_39px` | 26×33 |
| `0xF0000` (M) | `BaseM_33px` | 22×30 |
| bare codepoint | resident `Base_14pt` | 18×25 |

The L cut is **the same size on the glass as the old doubled draw** (26×34) and smooth,
because the rasteriser drew it at that size. The chain ends at the resident face, so a
keyboard that has never met the host app still shows a letter rather than nothing.
⚠️ The **keycap** keeps M first: it is only 40 px tall and the L cut's ink reaches that,
so the tier that suits the roomier 64 px status panel would clip there. One helper,
two tier lists — the fallback logic is not written twice.

## Fifth hardware round (2026-09-23) — the ripple, finally in the right units

*"The ring still does not dissolve — it should start with a thicker line and dissolve
in a distance of ~3 keys, so maybe 200px radius, and in a de-pixel manner lose more and
more pixels on the way to the max expansion."*

⚠️ **Four rounds were spent tuning this by TIME, and the reason none of them worked is
that the effect was being described in the wrong units.** What the eye judges is how
much ink is left **at a given radius**. Every earlier curve was a function of elapsed
progress, so each fix moved the dissolve around in time while the wavefront kept
outrunning it:

| round | change | why it still failed |
|---|---|---|
| 1 | 400 ms | ring crossed the board in a couple of frames — a flicker |
| 2 | 2200 ms | still read as fast |
| 3 | 3400 ms, dissolve keyed to elapsed time | the radius eases OUT, so the ring was past every key while ~84 % solid and faded off the board |
| 4 | keyed to travel, spread over the full 1795-unit reach | only a third gone by the outer keys |

**Now the density is a pure function of the RADIUS**, and the constants say what the
feedback said — in board units, where one key width is 72:

| constant | value | meaning |
|---|---|---|
| `TUT_RING_W` | 11 | ring thickness |
| `TUT_RIPPLE_SOLID_R` | 90 | solid out to ~1.25 keys |
| `TUT_RIPPLE_MAX_R` | 305 | gone by here — a 215-unit (~3 key) dissolve |
| `TUT_RIPPLE_MS` | 1600 | dot to nothing, ~384 ms per key width |

| elapsed | radius | keys out | density |
|--:|--:|--:|--:|
| 250 ms | 90 | 1.2 | 255 (solid) |
| 376 ms | 128 | 1.8 | 172 |
| 501 ms | 162 | 2.2 | 112 |
| 690 ms | 207 | 2.9 | 52 |
| 1129 ms | 277 | 3.8 | 4 |
| 1380 ms | 299 | 4.2 | 0 |

Three things generalise beyond this feature:

- ⚠️ **A 5-unit ring CANNOT show a dither.** A 4×4 ordered dither needs a band several
  pixels across before losing half its pixels reads as *thinning*; on the old ring it
  read as a dotted line. Part of why four rounds of curve tuning produced no visible
  dissolve was that the thing being tuned could not express the result. Hence the
  thicker line, which is what the hardware asked for first.
- **The falloff is QUADRATIC in the remaining distance**, not linear: a linear ramp is
  exactly 128/255 at the halfway radius and still reads as a solid ring there. Shedding
  fastest right after the solid stretch is what makes it legible.
- **The ripple no longer runs off the board.** It used to expand past the far corner
  (~1800 units) so it "left cleanly"; it now dies of its own dissolve at 305, which is
  what *fades out* actually means. The clears-the-far-corner test is gone with it.

The tests state the **defect**, not the implementation:
`TheRingHasVisiblyThinnedWithinACoupleOfKeyWidths` (≤ 160/255 one key past the solid
stretch, ≤ 80 two keys past) and `DensityIsAFunctionOfTheRadiusAlone`. Mutation-checked
against six breaks including both previously-shipped bugs — a time-keyed density and a
linear ramp over the whole board — each caught by the intended test.

## Sixth hardware round (2026-09-23)

*"Can we use some error diffusion — it looks too uniform when fading out + reduce the
radius by 50px and a bit slower. The Welcome to PolyKybd should stay a bit longer, and
when the PolyKybd letters transition to scanline it happens too sudden — can we do that
randomly letter by letter?"*

**The dither was uniform twice over, and the second one was the bigger half.** It was a
4×4 ordered Bayer indexed by the **local keycap pixel**: sixteen thresholds repeating
every 4 px, *and* the identical stencil on all 36 panels. Fading it out looked like a
screen door closing rather than ink eroding. `tut_dither()` is now a hash of the **board**
position, so the field is one continuous aperiodic grain the wavefront travels through.

- ⚠️ **It is not Floyd–Steinberg, and that is a constraint rather than a shortcut.**
  Error diffusion is serial along a scanline; this renderer draws one *rotated* keycap
  at a time, time-sliced, so the error has nowhere to flow — it could not cross a keycap
  edge and the seams would be the new artefact. A static aperiodic field buys the
  irregular look without the ordering error diffusion needs.
- **Static in board space on purpose.** A field that varied per frame sparkles instead
  of eroding, which is what made the original reach for an ordered matrix.
- Only pixels that survive the two ring tests pay for the hash, so the cost lands on the
  band and not on the 2 880 pixels of each keycap.

⚠️ **A byte-level randomness test is NOT enough, and only mutation-testing showed it.**
Dropping the hash's avalanche step leaves `((x*93) ^ (y*159)) & 0xFF` — bytes with no
short period and a perfectly flat distribution, which **passed every test written for
it** — while its bit 0 is `(x&1)^(y&1)`, a literal checkerboard. Structure in a bit plane
is structure on the panel. `NoBitPlaneIsPeriodic` checks each plane on its own and
catches it.

**The scanline glitch hit all 36 panels in the same frame.** That was deliberate ("a
one-frame glitch") and on a board of separate displays it reads as a fault rather than
an effect. Each key now trips at its own moment, scattered over `SA_LINE_CLEAR_SPREAD_MS`
(1200 ms) by a hash of its index, so the letters go over one by one in no discernible
order. ⚠️ The spread must close before the final fade starts, or the last keys would
still be flipping while the rest dissolve — a `_Static_assert` enforces it rather than a
comment asking the next editor to remember. `idx + 1` feeds the hash because `sa_hash8(0)`
would pin key 0 to the same point in the order on every boot.

**Timings:** `TUT_RIPPLE_MAX_R` 305 → **255** (−50, a 165-unit ≈ 2.3-key dissolve),
`TUT_RIPPLE_MS` 1600 → **2000** (~576 ms per key width), and `TUT_TEXT_MS` 900 →
**2000** — the welcome is the first thing a new keyboard says and it spans both panels,
so it is read rather than glanced at.


## Seventh hardware round (2026-09-23) — scale only

*"Great improvement! Still a tiny bit slower and still cut the radius by 30px."*

Two constants, no mechanism change: `TUT_RIPPLE_MAX_R` 255 → **225** and `TUT_RIPPLE_MS`
2000 → **2400** (~785 ms per key width). `TUT_RIPPLE_SOLID_R` came down 90 → **75** with
them, so the dissolve still occupies 150 units (~2 key widths) rather than being squeezed
to 135 — shrinking the reach alone would have shortened the *dissolve*, which is the one
part of the ripple three rounds were spent making visible.

**The shape has been right since round 5; every round since has moved its scale**
(305 → 255 → 225 units, 1600 → 2000 → 2400 ms). That is now said in `tutorial_plan.h`
above the constants, so the next tuning request is read as a dial rather than a redesign.
`TheDissolveSpansAboutThreeKeyWidths` was renamed `…ACoupleOfKeyWidths`: its bounds
(2–4 key widths) never changed and were always a range, but the name had quietly become a
claim about the shipped value.

## Seventh hardware round, part 2 — the ring was an octagon

*"I noticed the circle is not round — more like 8 or 10 corners."*

`tut_dist()` was an **octagonal minimax** distance, `(123*max + 51*min) >> 7`, chosen to
keep a per-pixel loop free of sqrt. The comment above it asserted that "a few px of
irregularity is invisible on a dithered ripple". It is not — the approximation is off by
up to 3.95 %, which at this reach is ~9 units, and the faceting is exactly the 8 corners
reported.

**The fix costs nothing, because nothing ever needed the distance.** The only questions
asked are "is this point inside the wavefront" and "is it inside the hole", and both are
answered EXACTLY by comparing **squared** distances: no sqrt, no approximation, two
multiplies per pixel where the octagon already spent two. `tut_ring_bounds()` squares the
two radii once per frame; `tut_ring_hit()` is a pair of compares. One definition, shared
by the pixel loop and the per-key cull so the coarse and fine tests cannot disagree about
what a circle is.

⚠️ **Every constant in `tutorial_plan.h` was ~4 % short of what shipped.** The octagon
under-reported, so a ring whose constant said 255 drew at ~265, and four rounds of
tuning-by-eye were calibrated against that inflated ring. The set was therefore rescaled
by 0.886 as part of the fix (`MAX_R` 255 → 235, `SOLID_R` 90 → 85, `MS` 2000 → 2200), so
that "cut the radius by 30" is 30 off **what was seen** rather than 40. From here the
drawn radius is the constant, and tuning is honest.

### ⚠️ The obvious roundness test cannot see the bug

`TheDiagonalReachesAsFarAsTheAxis` was the first test written, and it **survived** a
mutation that put the octagon back. A *minimax* approximation is fitted precisely so that
0° and 45° agree — it hides its whole error between them, at the octagon's vertex. The
two angles anyone would reach for are the two that cannot fail.

The full 360° sweep (`IsRoundAtEveryAngle`) catches it regardless; the targeted test now
samples **22.5°** and is named for it. Generalises past this bug: when testing an
approximation, sample where its error is designed to be worst, not where the shape is
most obviously symmetric — and mutation-test it, because a roundness test that passes on
an octagon reads exactly like coverage.

## Eighth hardware round (2026-09-23) — the taper, and chapter 2 lands

*"Make it 2px thicker at the beginning and 30px radius-reduced. Also add the shift step."*

**The ring TAPERS now.** `tut_ring_width()` runs `TUT_RING_W0` (13) at the strike down to
`TUT_RING_W` (11) at full reach, on the same shared travel curve the radius and density
read. A spreading wave getting thinner is the physical reading, and the fat end is where
the ring is most closely looked at, so it is also where the dither needs the most rows to
erode. Reach `MAX_R` 235 → **205**, with `SOLID_R` 85 → **74** so the proportions hold.

⚠️ **The dissolve-span test was re-derived, for the second time.** It asserted "2 to 4 key
widths", which fought every request to shrink the ripple — an absolute floor in key
widths cannot survive a wave that keeps getting smaller. What actually has to hold is
scale-free: **most of the travel is spent dissolving** (50–95 %), plus a loose absolute
floor of one key width so it cannot collapse to nothing. `MostOfTheTravelIsSpentDissolving`.

### Chapter 2 is implemented

Phases `TUT_REVEAL` → `TUT_SHIFT_WAIT` → `TUT_SHIFT_SWEEP` → `TUT_SHIFT_HELD`, with the
prose *"Press and hold"* / *"SHIFT"*, then *"All keys"* / *"react..."*. Four things that
turned out to matter:

- **The sweep IS a ripple.** It sets `ripple_slot` to the pressed Shift and bumps
  `ripple_seq`, so the existing sync carries slot + sequence + the master's elapsed time,
  and the slave's wave lands in step. **The whole chapter cost zero new sync bytes.**
  ⚠️ Two existing sites had to stop assuming a wave meant chapter 1: `sync_fill` sends the
  elapsed for both wave phases (otherwise the chapter-2 seam-step comes straight back),
  and `sync_apply`'s sequence branch takes the master's phase byte instead of forcing
  `TUT_RIPPLE`, which would have dropped the slave back onto a letter it is not showing.
- **The lit set is derived per half** (`tutorial_collect_lit_set`) from that half's own
  keymap, so it crosses no link either.
- **A release raises its own wave**, because the wave is a repaint FRONT, not a
  decoration — every key it crosses is redrawn from the board's live modifiers. Snapping
  the set back in one frame would cost the same ~30-key repaint the wave exists to spread.
- **The dwell is on the clock.** Someone who taps Shift and lets go still sees the board
  change and change back, rather than being told they did it wrong. An edge inside the
  chapter re-runs the front but does NOT restart the phase clock, so drumming on Shift
  cannot make the chapter run forever.

### ⚠️ A mutation that escaped, and what it shows

`ShiftDoesNothingDuringTheLetters` checked `TUT_LETTER_IN` and survived a mutation adding
`TUT_LETTER_WAIT` to `tut_shift()`'s accepting cases — which would let a fumbled Shift
jump straight to the sweep and silently eat the remaining letters. **`LETTER_WAIT` is the
phase that waits indefinitely, so it is exactly where a stray Shift lands**, and it was
the one the test skipped. It now walks every chapter-1 phase and asserts both edges do
nothing in each. Same shape as the octagon's diagonal test one round earlier: the case
that is easiest to write is not the case the bug lives in.

Cost: 6 mutations, all caught. 56 tests. Monolith `.heap` free 1904 → 1864 B.

## Ninth round (2026-09-23) — the layer notation, and chapter 3

### `!` changed MEANING: it marks the SWITCH now, not the momentary

The board already carried a notation on its layer keys and nothing explained it — and it
was inconsistent: `MO(_NL)` drew `Nm!` while `MO(_FL)` drew a bare `Fn`, the same kind of
key with and without the mark. The fix inverts which case is marked:

| kind | mark | why |
|---|---|---|
| momentary `MO` | **none** | the common case, and the least surprising: let go and you are back |
| one-shot `OSL` | `*` | outlives the finger by exactly one keypress |
| switch `TO` / `KC_BASE` | `!` | outlives the finger until something takes it back |

Marking the two behaviours that **outlive the finger** is what a new owner actually needs
warning about, and it makes `MO(_FL)`'s bare `Fn` correct rather than an oversight — the
inconsistency closes for free. ⚠️ A board running older firmware shows `!` meaning the
opposite, so the two cannot be mixed in one explanation.

Two keys had no legend at all and now do: **`MO(_UL)`** and **`TO(_BL)`**. They are used
in the shipped keymaps, so they were rendering nothing.

### ⚠️ The mark goes after the LAYER ICON, never after the layer's name

`Base` is 58 px of the 72 px window on its own. With the mark after the word, `Base!`
measured 64 px — it fits, but with 5 px to spare. After the icon it costs **nothing**:
the second line's icon ends at x=50, so there are 21 px sitting unused on every one of
these keys regardless of how long the name is. Measured across the set, the tightest
result is `Util*` at 6 px of margin and everything else has 11–21 px.

It also reads better: the mark modifies the **layer**, not the word. `LAYER_ONESHOT` and
`LAYER_SWITCH` in `keycode_helper.h` are the two marks, so a future change is one edit.

⚠️ **Measure this, do not eyeball it** — and drive the measurement through
`oled_preview.Renderer.bbox()`, whose return is **`(xmn, xmx, ymn, ymx)`**, not
`(x0, y0, x1, y1)`. Unpacking it in the obvious order produces boxes with a negative
width that still look like plausible numbers. A hand-rolled render also has to draw at
origin **`BUFFER_X`**, the way `lang_demo.render_static()` does; at origin 0 every legend
comes back 28 px to the left and reports the *shipping* ones as running off the panel,
which reads as a real finding and is not one.

### Chapter 3

`TUT_LAYER_WAIT` → `TUT_LAYER_SWEEP` → `TUT_LAYER_HELD` → `TUT_NOTATION`. Hold the
momentary layer key, the board becomes that layer under the same wave chapter 2 uses,
release and it comes back; then the status panels explain the three marks.

- **`tut_shift()` became `tut_hold(kind, …)`.** The behaviour is identical in both
  chapters — wait, sweep, dwell, ignore a repeated edge, never restart the clock — and
  two copies would be the keep-these-in-sync shape this repo keeps getting caught by.
  Only the phases and the prose differ. The `kind` is what stops a stray Shift driving
  chapter 3 forward, and vice versa.
- ⚠️ **The chapter boundary CLEARS `hold_on`.** Shift may still be physically down when
  chapter 2's dwell expires; without the clear, chapter 3 reads its first Fn press as a
  repeated edge and swallows it, stranding the tutorial on a "Now hold Fn" screen that
  no press can leave. `AStillHeldShiftDoesNotSwallowTheFirstLayerPress` pins it.
- **ONE lit set for both chapters** — letters, both shifts, and the layer keys. Swapping
  sets at the boundary would mean repainting the whole board in one frame, the exact
  slice-budget failure the repaint wave exists to avoid. It also means chapter 3's key
  has been on screen since the reveal rather than appearing when it is asked for.
- **`tutorial_is_layer_key()` is derived from QMK's keycode RANGES**, not from a list of
  the layer keycodes this keymap happens to use — a hand-kept list goes stale the moment
  a keymap gains a layer key, and stale here means the tutorial leaves that key dark.
- ⚠️ **A layer key the chapter is NOT asking for still must not be swallowed.**
  Swallowing a layer key's release leaves the board stuck on that layer — the exact bug
  `MO(_ADDLANG1)` shipped. It falls through and simply drives nothing.
- **`tutorial_line()` gained its second line**, gated to `TUT_NOTATION` alone: three
  marks do not fit in one phrase, and shortening them to fit loses the words that make
  each mark mean anything.
- **`tut_phase_is_board()` / `tut_phase_is_wave()`** live in the plan header, so the
  renderer and the sync read one definition. A unit test pins both as exact SETS, so a
  new phase that nobody classified fails there rather than silently rendering nothing or
  dropping the slave's wave timing.

5 mutations run on the chapter, all caught. 62 tests; three flavours link; monolith
`.heap` free unchanged at 1864 B; lint clean.

## Tenth round — "pressing shift, nothing happens"

Three bugs, and the first two are the same shape: **the tutorial's housekeeping branch
skips the function that feeds the renderer its state.**

### 1. `local_layer->mods` was never refreshed while the tutorial ran

Every legend is drawn from `local_layer->mods`, and the ONLY place that field is
assigned is `sync_and_refresh_displays()` — which the tutorial branch deliberately
skips, because its `set_displays()` fights the pinned contrast and its repaint erases
the sliced frame. So `get_mods()` moved and `local_layer->mods` did not: chapter 2 asked
the user to hold Shift and nothing on the board changed.

⚠️ **The layer half needs no equivalent** and that asymmetry is worth knowing: QMK's
`layer_state_set_user()` hook assigns `->layer` and runs regardless of us. Only the mods
snapshot rides on the skipped function.

### 2. …and the SLAVE's copy was never pushed

`poly_layer_t` reaches the other half through `USER_SYNC_LAYER_DATA`, sent exclusively
from that same skipped function. Fixing only the master would have transformed one half
of the board and left the other untouched — the worse half of the bug on a split
keyboard, since "all keys react" has to mean all of them. The tutorial branch now pushes
the layer diff itself, advancing `global` only on a successful sync (the diff IS the
retry queue).

### 3. The layer chapter drew the BASE layer's keycodes

`tutorial_draw_key_legend()` resolved `poly_keycode_at(_BL, r, c)` — hardcoded. Holding
the layer key therefore redrew *the same legends*. It uses `display_keycode_at()` now,
which resolves the live layer stack **and** does the `KC_TRNS` fall-through, so a key the
layer leaves transparent keeps showing what is underneath instead of going blank.

### 4. The shift step lit more than the letters

One lit set shared by both chapters meant chapter 2 lit the layer keys too. Each chapter
has its own set now (letters + the key it asks you to hold). ⚠️ The reason one set was
chosen originally — that swapping would repaint the whole board in a single frame — was
**wrong**: the renderer acts on a key only when its OWN membership changes, so the
boundary costs about seven keys. A key that is in the set but dark now paints at once
whatever the wave is doing; only an already-lit key waits for the front, because only it
has something to lose by repainting early.

**Generalises past this feature:** when a mode takes over the displays by skipping the
normal refresh path, list what that path *also* did. Here it did three things — snapshot
the mods, push the layer to the slave, and repaint — and only the third was unwanted.

## Eleventh round — the marks are ICON glyphs now

*"The exclamation mark should have the same height as the layer icon … instead of the
asterisk let's use ⚡ — also same height."*

Measured first: `ICON_LAYER` is **h=16**, the text face's `!` is **h=20** and its `*` is
**h=12**. So the mark beside the icon was a quarter taller than the icon, which is what
reads as a mistake rather than as a style.

Both marks are now **resident icon glyphs** at `0xA0` / `0xA1`, authored at exactly
**h=16 with yOffset −15** — `ICON_LAYER`'s own metrics, so the match is by construction
rather than by tuning.

⚠️ **⚡ U+26A1 does exist, and using it would have been a bug.** It is in the *symbol
pack* (`SymBmp6`, h=29 — half-size lands near 15, so the "half size gets us close
enough" instinct was right on the arithmetic). But a **core layer key must not go blank
on a keyboard whose font pack was never flashed**, and the font pack is exactly what a
first-run keyboard does not have. A resident bolt costs 20 bytes and cannot disappear.

⚠️ **Extend `IconsFont`, never add a resident FONT** — a new font shifts every pack
font's gidx and forces a full-pack reship (CLAUDE.md). The range went 0x80–0x9F →
0x80–0xA1.

❌ **And that was WRONG — the range now straddles as 0x7F–0xA0 (fixed in round 20).**
This paragraph used to end *"Nothing else claimed 0xA0/0xA1: checked by walking every
font's first/last, not assumed"*, and **that claim was false**:
`NotoSans_Regular_SupAndExtA_14pt16b` begins at **exactly 0xA1**, which is `¡` —
`INVERTED_EMARK`, rendered by ~20 `es-*` layouts. `IconsFont` is `g_all_fonts[0]` and
the scan is first-match-wins, so the mark simply won. The walk it names would have found
this in one pass; whatever was actually run, it was not that.

⚠️ **Keep the failure mode, not just the fix: a stated measurement is only worth what
the command that produced it is worth, and prose cannot carry that.** The repo already
had the rule (`FONT_PACK.md`: *"never `0xA0+`"*) **and** a script to check it
(`tools/check_icon_slots.py`) — but the script printed a caution and exited 0, so running
it felt like verification and proved nothing. It exits 1 now. The two marks live on the
**shoulders** 0x7F (DEL) and 0xA0 (NBSP), the last two codepoints adjacent to a full C1
block that no other resident or pack font covers and that appear in no legend.

⚠️ **APPEND the bitmaps.** `bitmapOffset` is an absolute index into one shared array, so
inserting anywhere but the end silently re-points every later glyph. The new offsets
continue at 3198, which is `0x9F`'s 2908 + 58×5 — and matches the byte count the file's
own trailing comment already carried.

⚠️ **The first bolt was hand-drawn as ASCII art and read as "two slashes and a bar"** —
uniform 4 px strokes with blunt 2 px ends. A lightning bolt is recognisable because it
TAPERS to points, and a hand-drawn grid gives every stroke the same width by default.
The shipped one is a **7-point polygon, scanline-filled**, sampled at the pixel centre:
the taper and the 1 px bottom tip fall out of the geometry instead of being drawn in.
Rule: for a glyph whose identity is its silhouette, define the silhouette and rasterise
it — do not draw pixels and hope they add up to the shape.

### Authoring a 1-bit glyph without fontconvert

ASCII art → the **column-native** encoder (`byte = off + x*cb + (y>>3)`, bit `1<<(y&7)`,
`cb = (h+7)/8`, **LSB = top of page**), then decoded straight back and diffed against the
art. ⚠️ This is *not* the classic Adafruit row-major layout, and a row-major encoder
produces bytes that are valid, plausible and wrong. Verified a second time by reading the
patched header through the host's own `gfx_font` loader and printing the glyph — i.e. by
the consumer, not by the thing that wrote it.

⚠️ **The keycap preview harness used here had an origin bug that survived two attempts**
— `Renderer.draw()`'s x origin and what its `setpix` receives did not agree, so whole
legends rendered ~28 px left and every *shipping* legend reported pixels off-panel. That
looks exactly like a real finding. `bbox()` (whose numbers did match the shipping
legends) is the measurement to trust here; `lang_demo.render_static()` is the reference
implementation of the draw call, and a preview that disagrees with a legend known to ship
is wrong about the preview, not about the legend.

## Twelfth round — the tutorial must start on the BASE layer

*"I was on the settings layer and could not see any letter or shift or anything else."*

Every legend the tutorial draws resolves through `display_keycode_at()`, which follows the
**live** layer stack. So a tutorial started from another layer teaches *that* layer's
keycaps — no letters, no shift, nothing the chapters ask for.

⚠️ **This is not a corner case: the prototype RESET Eden key lives ON the settings
layer.** So the launch path that every hardware round has used always started on `_SL`,
and the only reason chapter 1 ever worked is that `tutorial_slot_letter()` reads
`keymaps[_BL]` directly while the *render* followed `_SL`. The two disagreed from the
first build; chapters 2 and 3, which draw the real legends, are where it became visible.

`layer_clear()` before `tutorial_start()` drops every momentary/toggled layer and leaves
the default one — "chapter 1 starts on the board the user actually types on". The local
layer snapshot is updated in the same breath so the slave follows through the ordinary
layer sync rather than a pass later.

**Generalises:** anything that reads `keymaps[_BL]` directly and anything that resolves
through the live stack will disagree the moment the board is not on the base layer, and
the disagreement is invisible while it is.

## Thirteenth round — the repaint front never crossed the board

*"The shift is really laggy and the master did not update until the next step, so the
left had upper case and the right lower case."*

**The chapter-2 repaint front was driven from the LETTER RIPPLE's radius.** Those are two
different jobs and conflating them was the bug:

| | reach | job |
|---|---|---|
| `TUT_RIPPLE_MAX_R` | **205** units | a decorative splash, ~2.8 key widths, tuned over five rounds to look small and local |
| the board | **1673 × 563** units | — |

So a Shift press repainted only the keycaps within ~205 units of the Shift key — about
12 % of the board's width — and **every other key kept its stale legend indefinitely**.
That is the whole report: "laggy" (only the neighbourhood updated), "did not update until
the next step" (the rest waited for another wave), and the half-vs-half split (the Shift
key sits on one half, and its 205-unit disc barely leaves it).

`tut_sweep_radius()` is now its own curve: **1792** units, which clears the corner-to-
corner diagonal (`hypot(1673, 563) ≈ 1765`) from *any* key, over **700 ms** rather than
the splash's 2200. A modifier has to feel immediate; 2.2 s is a fine life for a splash
and far too slow for "hold Shift and watch".

⚠️ **The guard that was missing is the obvious one nobody wrote**: nothing asserted that
the front reaches anything. `ReachesEveryKeyFromEveryKey` compares the reach against the
board diagonal, and `IsMuchBiggerThanTheDecorativeRipple` keeps the two radii from being
quietly re-merged by someone tidying up.

⚠️ **A mutation making the front LINEAR escaped the first version of the ease test.**
`tut_sweep_radius(128) * 2 >= MAX` passes for a linear ramp by exactly one unit. Ease-out
is not decoration here — it is what gets the far side of the board caught up in the first
third, i.e. what makes the difference between "immediate" and the slow wipe this round is
fixing. The bound is 65 % at halfway now (eased measures ~75 %, linear 50 %).

**Chapter 3 is POSTPONED, not deleted.** One transition is redirected to `TUT_DONE`; the
layer chapter's phases, lit set, prose and tests all remain, with its tests entering the
chapter directly so restoring the single line does not also mean rewriting them.

**Generalises:** when one curve starts serving two purposes — one cosmetic, one
functional — the cosmetic one gets tuned and silently breaks the functional one. Give the
functional use its own constant the moment it appears, and assert the property that makes
it functional.

## Fourteenth round — stop re-implementing the board

*"When pressing shift only the master reacts — I think we should not re-invent anything
there. After we reach the press-the-lit-key phase we should transition to normal
operation where everything works, plus some global intro mode so we can display status
messages and hide some keys. Apart from that it should function normal, and when the
tutorial is done we should return to the persisted default layer."*

**This is the root cause behind the last four rounds, and it was architectural.** The
tutorial owned the render pipeline for its whole life: `update_displays()` early-returned
on `tutorial_active()`, the housekeeping branch skipped `sync_and_refresh_displays()`, and
the tutorial's own sliced renderer drew every keycap. Chapter 1 genuinely needs that — a
blank board, one lit letter and a ripple are things the normal renderer has no concept of.
**Chapter 2 does not.** "Hold Shift and watch the board" IS the normal board.

So owning the pipeline meant re-implementing the board inside the tutorial, and **every
one of those re-implementations was a bug**:

| re-implemented | how it failed |
|---|---|
| the mods snapshot | never refreshed → Shift changed nothing |
| the layer push to the slave | never sent → one half reacted, one did not |
| the legend resolver | hardcoded `_BL` → the layer chapter redrew the same legends |
| the repaint front | 205 units of a 1673-unit board → 12 % of the keycaps updated |

None of that code needs to exist. It is all deleted.

### The split

- **`tut_phase_is_exclusive()`** — chapter 1. The tutorial's sliced renderer owns the
  panels; `update_displays()` stands down; the housekeeping branch is its own.
- **`tut_phase_is_intro()`** — chapter 2 on. The board renders **normally**: stock
  `update_displays()`, stock `sync_and_refresh_displays()`, stock mods, stock layer sync,
  both halves. The tutorial only *annotates* — it hides the keys outside its lit set and
  owns the status panels. `tutorial_tick()` refuses to touch a panel here.

The hide is one branch in `update_displays()`' per-key loop, right beside
`local_state->fw_confirm` — the same "the board becomes a dialog" shape that was already
there and already works on both halves. A visible key falls straight through to the
ordinary legend path, so it gets real mods, real layer, real language, for free.

⚠️ **The slave needs nothing new.** `update_displays()` runs on both halves, both already
have the phase from the ordinary tutorial sync, and each answers `tutorial_key_visible()`
from its own keymap. The whole cross-half problem disappears rather than being solved.

### Returning the board

`layer_clear()` on the finish edge, before the repaint. The chapters ask the user to HOLD
layer keys and the skip gesture can land mid-hold, so without it the tutorial can end on
`_FL` or `_NL` — and the first thing a new owner meets is a keyboard typing the wrong
things.

**The rule, which is worth more than the feature:** *a mode that wants the board to
behave normally must let the board behave normally.* It annotates; it does not redraw.
Reach for exclusive ownership only for what the normal renderer genuinely cannot express,
and hand it back the moment that stops being true.

### …and chapter 1 borrows the same renderer

*"We could maybe already do that as well in step one and let the keyboard render the keys
with the built-in font."*

The lit letter is drawn by `tutorial_draw_board_legend()` now — `to_static_text()` /
`render_key()`, the board's own pair — instead of a tutorial-specific tier glyph. The key
you are asked to press looks exactly like the key you will press for the rest of the
keyboard's life, in the built-in face at the built-in size. Teaching a letter in a face
the board never uses again was always slightly wrong.

⚠️ **Chapter 1 stays EXCLUSIVE, and the reason is the RIPPLE.** It draws across every
keycap at once, which the per-key renderer has no way to express. Running LETTER_IN /
LETTER_WAIT in intro mode while RIPPLE stayed exclusive would hand the panels back and
forth **twice per letter** — six ~107 ms full repaints inside the one chapter whose whole
point is calm. So the mode stays and only the DRAW is borrowed; the fade still lives in
the panel's contrast register, and the ripple is untouched.

If intro mode should eventually cover chapter 1 too, the thing that has to be solved
first is giving intro mode an *overpaint* hook — a pass that runs after
`update_displays()` has drawn a key — which is the same hook a "circle something on the
keycap" annotation would need. That is worth building once, for both.

## Fifteenth round — the layout, caps, and overlays

### ⚠️ `layer_clear()` does NOT touch the default LAYOUT, and that is the whole bug

Two different things are called "layer" here, and the previous round conflated them:

- **the layer STACK** (`layer_state`) — momentary/toggled layers. `layer_clear()` drops these.
- **poly's `def_layer`** — a layer INDEX (`_L0.._L4`), the user's Qwerty / Stag / Colemak /
  Neo / Workman choice, persisted in EEPROM and folded in by `display_keycode_at()` as
  `1 << def_layer`. **`layer_clear()` leaves it completely alone.**

So the tutorial kept teaching on whatever layout the user runs, and `tutorial_slot_letter()`
reads `keymaps[_BL]` while the keycap resolves through `def_layer` — hence *"the letter I
had to press differed from what the status display said"*. It also explains why "restore
the default layer" appeared not to happen: the stack was cleared, the layout never moved
because it never had.

`tutorial_enter_base_layout()` parks `def_layer` and forces `_L0`;
`tutorial_restore_layout()` puts it back. ⚠️ **No `defer_default_layer_save()`** — a
tutorial must not persist a change to what the user types afterwards.

⚠️ **Both are called from `tutorial_start()` / `tutorial_stop()`, so BOTH HALVES run
them.** The slave resolves its own keycaps through its own `def_layer`; forcing it only on
the master letters one half from Qwerty and the other from the user's layout. Doing this
at the master's arm site was the previous attempt, and that is why it was only half a fix.

### Caps lock, internally, for chapter 1 only

The lit key is drawn by the board's own renderer now, which letters it in **lower** case —
while the status panel confirms it as a **capital**. `led_state.caps_lock` is forced true
in the LOCAL snapshot while chapter 1 runs, so the two agree without touching the host's
real lock state.

It is set every pass and **deliberately never cleared**: the moment chapter 1 ends that
housekeeping branch stops running, the normal path re-reads `led_state` from the host, and
caps drops by itself. One less piece of state with a lifetime to get wrong.

### Overlays are blocked for the whole tutorial

⚠️ **The gate goes in the OUTER condition, beside `!add_lang`** — never folded into the
first arm. `if(display_overlays){…} else {…hardcoded Ctrl hints…}`: gating only the first
arm lets the **else** fire and paint the built-in hints over the lesson instead, which is
the identical trap the Intl layer already documents one line above. A lesson's keycaps are
the payload; an app overlay or a shortcut hint over them teaches the wrong thing.

## 15. The FOCUS RIPPLE (built — `anim/focus_ring.h`)

*"It would be great to have the circular focus also in normal operation to point out keys
— if we support that we can maybe also do step 1 without the special mode?"*

**Yes to both, and the second is the real prize.** A ripple that works over live legends
makes chapter 1 an ordinary intro-mode chapter (lit set = the one key being asked for),
which deletes exclusive mode, the sliced renderer, `tut_draw_letter_tiered`, the frame
latch and the ring/dither drawing — the last things the tutorial still owns.

It is also reusable well beyond the tutorial: "point at this key" is what the host's
layout editor, a shortcut-discovery mode and any future "did you know" prompt all want.

### The cost, measured against the real geometry

Not estimated — walked over `SA_GEOM_LEFT/RIGHT` for every possible ripple origin:

| | keys the ring band touches at once |
|---|---|
| average | **6** |
| worst case | **15** |

At the measured ~3 ms per key render (a 36-key pass is ~107 ms), the worst frame is
**~45 ms of redraw, and ~90 ms** counting the second render each key needs when the band
leaves it and its plain legend has to come back. A 16 ms frame cannot hold that.

⚠️ **But the AVERAGE is fine and that is what decides the design.** Over a 2.2 s wave the
whole board costs 72 keys × 2 renders × 3 ms ≈ **430 ms of work spread over 2200 ms — a
20 % duty cycle.** The problem is purely the peak, so the answer is the one the tutorial's
renderer already uses: **a dirty queue drained on a ~3 ms slice per housekeeping pass**,
not a render-everything-this-frame loop. Keys enter the queue as the front reaches them
and again as it leaves; on a dense frame the arc lags a pass or two, which is invisible at
this speed.

### What it needs

- **Composite, don't own.** Per queued key: clear buffer → draw the real legend (the
  `to_static_text()` / `render_key()` pair) → draw the arc → push. The panel keeps its own
  GDDRAM, so there is no read-modify-write available; redrawing the legend under the arc
  is the cost of entry and is already counted above.
- **A per-key contrast override**, so the focused key can still fade in. That is the same
  hook, and it is what chapter 1's fade needs to survive the move.
- **Board-space geometry** (`startup_anim_key_geom`) is already the right coordinate
  system and is already shared with Eden.

### ⚠️ "It runs on the render path" was the WRONG worry

The first version of this note argued the service was risky because it sits on the path
that draws every keycap. **That is not the risk** — both hooks start with
`poly_focus_active()`, so a keyboard with no ripple running pays one boolean test per
keycap and nothing else. The cost and the behaviour are confined to the moments something
is actually pointing at a key.

The real hazards are two SPECIFIC ones, both already in this repo's history:

- ⚠️ **The gfx plotter flags are STATIC.** A key whose legend set erase (an inverted
  keycap) leaves it set, and every keycap drawn afterwards — here *and in the next
  `update_displays()` pass* — comes out blank. Every draw site owns clearing it;
  `focus_repaint()` is one, and does.
- ⚠️ **The dirty-window bookkeeping.** These writes happen outside `update_displays()`'
  own pass, so an untracked one leaves that panel's box describing whatever was there
  before, and the next repaint streams a stale sub-rectangle: black and half-erased
  keycaps. That is the exact bug the tutorial's own renderer hit in round 1 and why it
  has to `kdisp_invalidate_all_windows()` on teardown. `focus_repaint()` calls
  `kdisp_track_panel()` instead, which avoids needing the big hammer.

### ⚠️ The first cut had no ring at all — "a scatter light-up of surrounding keys"

Two mistakes, and the second is the one worth remembering.

1. **The driver repainted a key only when its band membership CHANGED.** The arc does not
   sit on a key, it *moves through* it — so painting once on entry freezes it at the
   radius it had then, and the key lights up and stays that way until the band leaves.
   Every key blinks on in turn: a scatter, not a ring. A key in the band must be
   repainted **every frame**.

   ⚠️ **The design note's own measurement said so and I implemented something else.**
   "6 keys in the band on average, 15 at worst" is *per frame* — that is the whole point
   of the number. The membership diff looked like a free optimisation of it and is
   actually a different, much smaller quantity: keys per *ripple*. When an optimisation
   makes a measured cost disappear, check whether it also made the behaviour disappear.

2. **I deleted the FRAME LATCH as incidental.** It is not. Every slice of one frame has
   to draw the same instant, or the keys drawn early in the walk show a smaller radius
   than those drawn late and the ring shears apart. The old renderer latched once per
   frame and sliced the key walk inside it; the service does that again now
   (`s_frame_busy` / `s_last_frame` / `s_live`).

**Generalises:** when replacing a renderer, the loop structure is part of the contract,
not scaffolding. A latch that exists so that N slices agree about "now" has no local
justification at any one slice — which is exactly what makes it look removable.

### What it deleted

Chapter 1 is an ordinary intro chapter now — lit set of exactly one key, the board draws
that key itself, the ripple points at it. Gone with it: the sliced renderer, the frame
latch, `tut_draw_ring`, `tut_render_key`, the per-slot lit bitmap and the panel takeover
in `tutorial_start()`. **Net −120 lines**, and the monolith heap is unchanged (the
service's 48 bytes of state are paid for by the frame latch it replaced).

`tut_phase_is_exclusive()` is KEPT and now always false. It is the seam for a future
chapter that genuinely needs the panels to itself, and a unit test asserts nothing claims
them — so reintroducing one is a deliberate, visible act rather than a side effect.

## Sixteenth round — idle, hidden keys, and the arcs nobody owned

### 1. Idle was suppressed in a branch that no longer runs

`update_performed()` sat in the tutorial's own housekeeping branch. Once the tutorial
moved onto the normal render path that branch stopped running, and the lesson could fade
out mid-step. ⚠️ **A regression created by the refactor, not found by it** — nothing
asserts "the board stays awake while something is on screen".

The same call was gated on `fw_staging_fw_up_active()`, which covers the **chunk transfer
and nothing else** — so staging, the apply and the signing prompt were all free to idle
out from under their own screen (seen on hardware). `poly_fw_screen()` is the one selector
that knows about all four phases, and it is what the gate reads now.

The rule the two share: **suppress idle whenever something is on screen that the user did
not put there.** That is a property of the situation, not of a feature, which is why it
belongs in one place rather than in each feature's own branch.

### 2. The ring un-hid the keys the lesson had darkened

`focus_repaint()` drew the key's real legend before the arc — unconditionally. So the
wavefront crossing a hidden keycap drew **both** the legend and the arc, un-hiding exactly
the keys chapter 1 had darkened.

`poly_focus_draw_legend()` returns false for a key nobody is rendering, and the ripple
then leaves it blank and draws no arc on it. ⚠️ **The visibility question now has ONE
answer** (`tutorial_key_visible()`), asked by both the normal render path and the ripple —
rather than the ripple assuming every key is drawable.

### 3. ⚠️ The arcs the refresh hook painted were owned by nobody

The driver restores a key when its band membership drops — but it only restores keys **it
has marked**. The `poly_focus_overlay()` hook is *also* called from `update_displays()`'
own pass, so a full refresh mid-ripple painted arcs on keys the driver was not tracking,
and nothing ever cleaned them up.

That is the "parts of the ring not being removed on the slave" report, and it is worse on
the slave for a reason: the slave takes more full refreshes (every layer and state sync
lands there), so it collects more untracked arcs.

**Whoever draws the ink owns saying so.** The hook marks the key it paints, so the driver
restores it whichever path drew it. Generalises to any overlay with two entry points: a
cleanup pass keyed on a set only *one* of the writers updates will always leak the other's
work, and it leaks more wherever that other writer runs more often.

## Seventeenth round — two regressions of my own making

### ⚠️ I gated the ARC on visibility, and the ring disappeared

The report was *"for the keys where we do not render a key it should then not draw the key
either"* — **the KEY, meaning its legend.** I gated the arc on the same test. Chapter 1's
lit set is exactly one key, so every other key is hidden and the arc was suppressed
everywhere: *"only a few ring artifacts on the actual key"*, no expanding ring at all.

The legend and the arc answer to different questions and must be gated separately:

| | hidden key |
|---|---|
| the legend | **not drawn** — a lesson darkened that key on purpose |
| the arc | **drawn** — a ring that stops at the lit set is not a ring |

`poly_focus_draw_legend()` still reports whether it drew, but that value is now
informational; the arc does not consult it.

### ⚠️ The caps-lock hold was written where the next line erases it

`sync_and_refresh_displays()` re-reads `led_state` from the host **every pass**, and calls
`update_displays()` from further down the same function. So an override set anywhere
outside that window is gone before anything renders — which is why holding caps from the
tutorial's own housekeeping branch never showed, even in the build where that branch still
ran. (I had assumed the dead branch was the whole story; it was not, and the earlier build
disproves it.)

`tutorial_caps_hold()` is applied on the line immediately after the host read, which is
the only place it survives. **Generalises: a snapshot that is re-derived every pass can
only be overridden between the derive and its use** — an override "somewhere before" is
not early, it is erased.

## Round 18 — the leftover arcs were never the ripple's fault

**Report:** *"We still have some artifacts — only on the slave side parts of the ring are
not cleared any more and stay."*

Three rounds had aimed at the focus ring's own bookkeeping — who marks a key, who owns
restoring it, when `s_active` may drop. All of that was correct. The bug was one layer
down and **older than this branch**:

`update_displays()` computed the panel it was addressing as `LAYOUT_TO_INDEX(r, c)` — the
**matrix** index. Every panel-addressed thing is in **display** space, and on split72's
right half the two differ by one column on the upper four rows (no col-0 key there). So
`kdisp_track_panel()` remembered each right-half panel's dirty-window bbox under its
**neighbour's** index: 28 of 74 keys, right half only.

That had been true for as long as the dirty-window optimisation existed and had never
shown, because a legend is a similar centred box on either key — `union(neighbour's
previous, new)` covered the old ink by luck. An arc is a thin band anywhere on the panel.
The luck ran out, and only on the half that folds, which is the half that happens to be
the slave.

Three things worth keeping:

- ⚠️ **A wrong panel index does not look like a wrong panel index.** It looks like a
  rendering bug in whatever drew the unusual shape — which is exactly where the previous
  rounds went. **The tell is "one half only."** Nothing about a ripple is side-specific;
  the moment a symptom is, look for something that folds.
- **The fix was to delete the second copy, not to add a third.** The fold already existed
  in `invert_display()` and again in `tutorial_slot_of()`. It is now
  `key_display_index()`, and both callers go through it — the same shape as every other
  "guard that enumerates its siblings" this repo has been caught by.
- **It was settled by replaying the walk, not by reading it.** `tools/check_disp_index.py`
  simulates the walking-zero panel walk against the real keymap and `keyboard.json` and
  compares every key: **0 mismatches** with the helper, **28** with the old expression,
  all on the right half. Reading the two functions and agreeing that they agreed is what
  let the mismatch sit there in the first place; the script is mutation-checked so it
  cannot become that.

## Round 19 — the sentence whose halves disagreed, and Shift once per hand

Three things, one of them a real bug.

### The right panel said "lit key" for all three letters

The status panels are **two halves of one sentence**, and which words each half shows is
chosen by `s_st.step`. The step was never synced: `tut[]` carried the phase, the slot,
the ripple sequence, the ripple origin and the master's elapsed — six bytes, all of them
spoken for — so the slave's step stayed **0** for the whole of chapter 1. Letter 1 read
correctly ("Press the / lit key") and letters 2 and 3 read "And now / lit key" and "One
more / lit key".

- ⚠️ **The phase cannot carry it**: all three letters use the same two phases. That is
  exactly why it was missed — every *visible* thing about the three letters is synced,
  and the one field that differs between them is the one that is not.
- It fits in `tut[0]`'s spare bits (two flags, two bits, `TUT_LETTERS` is 3) rather than
  costing `poly_sync_t` a seventh byte, which the RPC cap would have made a real cost.
- **Generalises:** when a per-half render reads a field, that field is part of the sync
  contract whether or not it draws anything itself.

### Chapter 2 now runs ONCE PER HAND

Shift is the one modifier that exists twice, and a lesson that only ever shows the left
one has taught that **the left key** does this rather than that **the key** does.
`TUT_SHIFT_AGAIN` is simply a second wait: hold the left Shift, watch the board, then
"Try again" with the ring on the right Shift. Every other phase is reused.

- **The other Shift does not satisfy a stage.** A ring pointing at one key while its twin
  also works makes the pointer a decoration, and both stages could be cleared with the
  same hand — the one thing the second stage exists to prevent. A wrong Shift does
  nothing at all, the same silence a wrong letter gets in chapter 1.
- **The slots are scanned from the keymap** (`tutorial_shift_slots()`), by HAND from the
  slot's own side bit rather than from the keycode: `KC_RSFT` on the left half is an
  ordinary keymap, and the chapter is teaching "either hand".
- A board with only one Shift finishes after one stage — a stage nothing can clear is a
  tutorial nobody can leave.

### The ring POINTS here, and that is the opposite of chapter 1

⚠️ **The same service, two opposite jobs, and only one of them can be fired once.** In
chapter 1 the ring is **struck by the press** — it confirms, so one shot is the whole
point. In chapter 2 it **points**: it has to say "this key, here" to someone who has not
found it yet, and a single 2.2 s splash fired on entering the phase is gone before a
first-time user has looked up from the status line. So a pointing wait re-fires it every
`TUT_POINT_PERIOD_MS`, and `tut_enter()` back-dates `point_at` so the first one lands
**at once** rather than three silent seconds later.

- ⚠️ `tut_point_tick()` runs **before** `tut_tick()`'s duration gate. A pointing wait has
  no duration, which is precisely why anything past that early return could never reach
  the phases that need a pointer.
- ⚠️ **One place arms the ring on this half**, keyed off `ripple_seq` — the same field the
  slave watches. Three things now raise a ripple (a letter press, a Shift edge, the
  pointing re-fire), and giving each its own `poly_focus_start()` is how the third would
  have shipped working on the slave, which reads it off the wire, and doing nothing on
  the master.

### The ⇧ beside the word

The status face (`NotoSans_Regular_Small_15px7b`) covers **0x20..0x7E and nothing else**,
so the glyph is not in it, and `kdisp_write_gfx_char` baseline-aligns every glyph to
`fonts[0]` — a two-font array would drop `IconsFont` (yAdvance 40) by 20 px against the
small face, straight out of its band. So `tutorial_line_icon()` returns the codepoint
**separately** and the caller draws it with its own single-font array: two calls, two
correct baselines, and the text+icon measured and centred as one unit so "SHIFT" does not
stay centred with the glyph hanging off the edge.

⚠️ `#include "base/fonts/gfx_icons.h"` is **not** how to reach `IconsFont` from a second
translation unit — the font headers *define* their tables with no extern and no guard, so
a second include is a link error on `IconsBitmaps`/`IconsGlyphs`/`HelperGlyphs`. An
`extern const GFXfont IconsFont;` is how every other font in `oled_helper.c` is reached.

Verified by rendering rather than by reading: a model of `oled_tutorial_screen()` driven
by `tools/status_oled_preview.py`'s own font parser, over all five screens, **0 pixels
outside the 128×64 panel**.

## Round 20 — the review round, and two rules this repo had already written down

CodeRabbit's first real pass on PR #306 raised ten findings. Eight were real; the two
worth generalising had both been **documented in this repo before the PR was written**,
which is the part that matters.

- **The layer-key marks shadowed `¡` on every Spanish layout.** They were appended to
  `IconsFont` at `0xA0`/`0xA1`, and `IconsFont` is `g_all_fonts[0]` — first match wins,
  so `INVERTED_EMARK` (U+00A1, used by ~20 `es-*` layouts) resolved to a lightning bolt.
  `FONT_PACK.md` already carried the trap *and* the reason the C1 band stops at `0x9F`,
  and `tools/check_icon_slots.py` already existed to answer "is this slot free?".
  **The gate printed the caution and exited 0**, so running it proved nothing. It
  enforces now, and the two marks live on the shoulders `0x7F`/`0xA0` — measured to be
  covered by no other font and used in no legend.
  ⚠️ **Generalise: a caution a tool prints and then exits 0 on is documentation, not a
  gate.** It is weaker than a comment, because running it feels like having checked.
- **Two hand-written `#define`s were added INSIDE the cog-generated block** of
  `named_glyphs.h`, where the next `cog -r` deletes them and every consumer of
  `keycode_helper.h` stops compiling. `FONT_PACK.md` describes this exact failure for
  `ICON_BACKSPACE`, and the file has a hand-written tail for precisely this. Moved there.

The tutorial's own bug was quieter: **`ARMED` without `ACTIVE` was read as "stop".**
The master holds `TUT_SYNC_ARMED` for the whole of its Eden and pushes it every pass, so
a half whose Eden finished first receives those packets *while running the tutorial* —
and tore itself down, stamping the boot-intro marker, losing its first-run tutorial for
good. The two animations run on independent clocks, so either order happens.
`tut_sync_word_stops()` (`base/tutorial_plan.h`) is the one place that decides now, and
it is unit-tested — the three `TutorialSyncWord` cases fail against the old semantics.
The word is also **retired** (zeroed) at teardown, so a later Eden replay cannot find a
stale `ACTIVE`/`ARMED` level and re-trigger a tutorial on the slave alone.

Three smaller ones, each a one-liner with a real consequence: the confirm prompt now sits
**above** the tutorial swallow (both are "the board IS the dialog" modes, and with the
tutorial first the FW-2 / DOOM `ACCEPT`/`REJECT` keys were eaten while the prompt was on
screen — a dialog nobody could answer); the focus ring marks a key on the **ink**, not on
the call, so a full refresh mid-ripple no longer owes a restore repaint on every key it
touched; and the status OLED is handed back at the **live** contrast rather than the
compile-time `OLED_BRIGHTNESS`.

## Round 21 — the boot trigger is opt-in, and that is not caution

The whole feature hangs off four lines near the end of `keyboard_post_init_user()`:

```c
if (boot_intro_pending()) { arm_tutorial_after_intro(); startup_anim_start(); }
```

They are now behind **`#ifdef POLYKYBD_BOOT_INTRO`**, off unless a build asks for it
(`-e POLYKYBD_BOOT_INTRO=yes`, wired in `keyboards/polykybd/rules.mk` like every other
PolyKybd opt-in). Three reasons, and the first is the one that decides it:

- ⚠️ **The comment six lines above this code already said not to do it.** It records that
  boot auto-play was disabled after a startup hang, and asks for it back *"once the
  startup hang is understood"*. It is not understood. Re-enabling it by default while
  that sentence still stands is reversing a decision without the evidence that decision
  was waiting for.
- **This is the pre-watchdog window.** `crash_watchdog_start()` is called *below* this
  point, and per `CRASH_DIAGNOSTICS.md` boot is the one span with no reset, no crash
  record and no console — `console_task()` is a main-loop call that has not been reached.
  A hang here is permanent and mute, on a path that had never run a cold boot.
- **Default-on would ship it to every first boot of every board**, which is the widest
  possible blast radius for the least-tested path in the feature.

⚠️ **Nothing is stubbed out.** `KC_EDEN`, HID cmd 28 and `poly_arm_tutorial_after_intro()`
all still compile and still work, so the tutorial is fully drivable by hand for testing —
the define gates *who starts it*, not whether it exists. Measured, not assumed: the
opt-in build's `.text` is 177716 against the default's 177676, a real 40-byte delta, so
the define reaches the compiler. That check is not ceremony here — `PERMISSIVE_HOLD` and
`HOLD_ON_OTHER_KEY_PRESS` sat in both `rules.mk` files for years doing nothing, because
no `.mk` file translated them, and the build stayed green while the source read as
configured.

**What would flip the default:** a cold boot exercised on hardware (step 1 of
`TUTORIAL_NEXT.md`), which only the user can run. Until then the gate is the honest
statement of what has and has not been tested.

## Round 22 — chapter 3: the board reveal, languages and scripts (2026-09-24)

*"It's a bit too fast — the tutorial needs more steps first. We have to teach all the
features and make a wow effect."* Scope agreed for this round: the languages and glyph
scripts as a **board-only preview**, a **chapter count on the Esc keycap** that is also
the hold-to-exit key, and a **board reveal after Shift**, where the lesson stops hiding
keys.

### The chain

`TUT_SHIFT_HELD` (second hand) → `TUT_BOARD_REVEAL` → `TUT_BOARD_SHOW` →
`TUT_LANG_INTRO` → `TUT_LANG_SHOW` × N → `TUT_LANG_POINT` → `TUT_FINALE` → `TUT_DONE`.
(Round 29 replaced `TUT_LANG_POINT` with the key tour and added the dark cut; see there.)
About 45 s on top of chapters 1–2 with the full nine-item list. `TUT_LANG_SHOW` is ONE
phase re-entered per item, so the phase clock is the item clock and nothing new crosses
the link. The layer chapter stays postponed; it now sits between Shift and the reveal
if it is ever restored.

### The reveal is the focus ring at board scale

`poly_focus_start_sweep()` is a second PROFILE of the same ring, not a second renderer:
radius on `tut_sweep_radius()` out to `TUT_SWEEP_MAX_R` over `TUT_BOARD_REVEAL_MS`, a
20-unit band, solid until the last quarter. A key becomes visible when the front passes
its centre (`tut_reveal_reached()`), and the ring's own repaint is what draws its legend
there, so nothing else has to know a reveal is happening.
- ⚠️ **Keys chapter 2 already showed stay lit.** The front only ADDS keys; a letter going
  dark and relighting as the wave reached it would read as a fault.
- ⚠️ **The ring and the visibility test must run on one clock.** The slave's ring used to
  start "from now" on receipt; for the reveal it is back-dated by the master's elapsed
  (`poly_focus_start_sweep(slot, already_ms)`), the same number that back-dates
  `phase_start`. Otherwise the slave lights keys ahead of its own wavefront.
- `TUT_BOARD_REVEAL` is a wave phase, so its elapsed rides `tut[5]` like the letter
  ripple's.

### ⚠️ A language preview must not reach the host

The host polls `GET_LANG` every second and **switches the OS layout to match**
(PolyHost's language-changed flow). Writing a preview into `local_state->lang` is the
only way to get it onto both halves' keycaps, so `GET_LANG` (`hid_com.c` case 7) and
`save_user_settings()` now read `poly_reported_lang()`, which answers with the user's
real language while a preview is on screen. A host `SET_LANG` that lands mid-preview is
recognised (the value is no longer the one the preview wrote) and becomes the new real
language instead of being overwritten and later "restored" to the old one.
- The glyph script needs no guard: cmd 30 and the EEPROM save read `get_glyph_script()`,
  and the preview only changes what the master writes into `local_state->glyph_script`.
- ⚠️ The SLAVE still marks its settings dirty when the synced language changes, so a
  flush that lands mid-preview can store the preview on the slave. The restore marks it
  dirty again, so it converges at the next flush. Not fixed, because the slave never
  answers `GET_LANG` and its stored language is overwritten by the master's sync at boot.

### Only what the board can draw

`tutorial_preview_prepare()` keeps an entry only when the glyph for `KC_A` in that
language or script resolves in the flashed fonts, through the same lookup the renderer
uses. A first-boot board with no font pack gets `n_preview == 0`, and the chapter goes
straight from the reveal to the finale rather than showing a board of blank keycaps.
The user's own language is skipped; showing it would change nothing.

### Pacing

`TUT_TEXT_MS` 2000 → 2600, `TUT_GAP_MS` 700 → 1000, `TUT_SHIFT_HELD_MS` 2000 → 2600.

### Not verified on hardware

All of it. In particular: whether the reveal front keeps up (a 20-unit band crosses up
to ~20 keys per frame per half, against a 3 ms slice), whether `MID_TWO_LINE("2/3",
"Hold=exit")` fits the Esc keycap without clipping, and how long each script needs on
screen to be read.

### Round 22, part 2 — the chrome keys and a test build

- **Esc reads "Hold to / skip..."; the top-right outer key shows the chapter.** They are
  the two halves of the existing skip gesture, so either still ends the lesson when held.
- ⚠️ **Neither stock two-line stack fits "Hold to / skip...".** The top line has ascenders
  and the bottom a descender, the combination `MID_TWO_LINE`'s note says a 40 px panel
  cannot hold. Measured with `tools/oled_preview.py`'s renderer, which reproduces the
  known-good "RESET / Eden" with zero clipped pixels: `MID_TWO_LINE` spacing clips 4 px off
  the top, `MID_TWO_WORD` 8 px off the bottom, and lift 4 × 2 px with push 2 × 2 px clips
  none and leaves a 4 px gap. The progress run ("1/3") sits at rows 1..20 on baseline 23,
  so it is drawn on baseline 32 to centre it.
- **`POLYKYBD_TUTORIAL_TEST=yes`** starts the tutorial from housekeeping ~1.5 s after every
  reset, with no Eden and no marker check. It starts outside the pre-watchdog window on
  purpose, so a test build does not exercise the boot path `POLYKYBD_BOOT_INTRO` is gated
  for.

## Round 23 — the slave that sat out, and the reveal's sparks (2026-09-24)

- ⚠️ **A zero sync word STOPPED a slave that started first.** The test build started
  the tutorial on each half's own timer. The master's `tut[0]` stayed zero until its
  first tutorial push, and `tut[]` rides every `poly_sync_t` send, so an ordinary state
  sync in that window (a host language or brightness change at connect) was read by the
  already-running slave as "stop": it tore its lesson down and showed normal legends
  ("the slave stayed at the default layer", hardware, intermittent).
  Two fixes. The master now writes `TUT_SYNC_ACTIVE` into `tut[0]` on every pass while a
  lesson runs (`poly_tutorial_publish_active()`), and right at the Eden hand-off, so no
  send can carry a zero word mid-lesson. Only the flag byte: the rest of the word changes
  per pass during a wave, and writing it every pass would make every pass a state diff and
  a full repaint. And the test build no longer starts the tutorial on a timer at all.
- **`POLYKYBD_TUTORIAL_TEST` now plays Eden, then the tutorial, on every reset** — the
  real first-run path with the marker ignored. That path never had the race, because
  `TUT_SYNC_ARMED` is set in post_init before the first sync. It also means every test
  boot exercises the pre-watchdog start `POLYKYBD_BOOT_INTRO` is gated for.
- **The reveal front is wider (20 → 44 units) and leaves a spark trail.** Behind the
  front, 2x2-px sparks light at up to ~5 % of cells, thinning to none ~220 units back,
  and re-roll every 90 ms so they twinkle and die out. The cull band covers the trail,
  since a trail key the ring does not repaint would neither show nor clear its sparks.
  ⚠️ The trail roughly triples the keys repainted per frame; if the front stutters on
  hardware, shorten `POLY_FOCUS_TRAIL` first.

## Round 24 — names before glyphs, Hindi for Russian, stars in Eden, suspend (2026-09-25)

- **The reveal stuttered with the 220-unit spark trail**, as predicted; it is 80 now
  (about one key width).
- **Russian is out of the preview tour** — politically too delicate at the moment.
  Hindi (Devanagari) took its place: Greek, Arabic, Hindi, Japanese, Korean, then the
  scripts.
- **Each item is NAMED first.** `TUT_LANG_NAME` (1 s) darkens the board and spells the
  name across the middle display row, one capital per keycap, in the larger latin tier
  when it is flashed. The row is 14 keys, 7 per half (display row 2; index 23 has no
  panel on either half), ordered by board x, and each half builds the same ordering
  from the shared geometry table, so it knows its own letters without asking. A name
  therefore spans the split (GRE | EK).
  ⚠️ The slave needs to know WHICH item: `tut[2]` carries the item's table ROW during
  the name and show phases (the slave never builds the renderable subset, so a position
  in it would mean nothing there), and the chapter-1 slot write on the slave is now
  gated to chapter-1 phases so the row cannot land in a letter slot.
  The status panel's title now comes from that row too, not from reading the synced
  lang/script back, which could not name an item that is not applied yet.
- **Eden's letter fade has stars.** Each keycap gets two chances; a hash picks ~43 % of
  them, a time within the 3.2 s fade and a spot. A star is 1 px, then a 5-px plus, then
  1 px, over 650 ms. Drawn after the dither dissolve, so the dither never eats one.
- ⚠️ **The tutorial's status screen ignored `STATUS_DISP_ON`.** Suspend clears the flag
  and the sync calls `oled_off()`, but the tutorial branch of `oled_task_user()` redraws
  every tick, and a redraw switches the SSD1306 back on. The slave's loop keeps running
  while the host sleeps, so its panel stayed lit on "Braille" at full brightness after
  the computer shut down (hardware). The branch now turns the panel off while the flag
  is clear; the lesson resumes on wake.

### Round 24, part 2 — the name in two scripts

The name is now spelled TWICE: the Latin name on the LEFT half's middle row, and the name
in the language's own script on the RIGHT half's (a glyph script spells the Latin name
through its own glyphs). Every letter row has 7 panels per half, so an 8-unit name puts
its last two units on the last key (JAPANE|SE, ΕΛΛΗΝΙ|ΚΑ).
- The native spellings are KEYBOARD units, since the keycap renderer has no shaper:
  Devanagari with its vowel signs and virama as separate characters, Arabic laid out
  from the right, Japanese in hiragana.
- ⚠️ **Korean is CONJOINING jamo (U+1100 initials, U+1161 vowels), not the compatibility
  jamo at U+3131** — no font here carries U+3131.., nor precomposed syllables, nor the
  final-consonant forms (U+11A8..), so finals are written in their initial form, as on
  the Korean keycaps. Every native codepoint was checked with `tools/oled_preview.py`'s
  font loader, and every name row rendered with 0 clipped pixels.

## Round 25 — language names, not script names; pre-rendered where the keys cannot spell

- **Every native name is the LANGUAGE's name for itself** (한국어, not 한글; 日本語;
  ΕΛΛΗΝΙΚΑ). List: Greek, Arabic, Hebrew, Hindi, Thai, Japanese, Korean, then the
  glyph scripts Elvish, Runes, Aurebesh, Braille.
- ⚠️ **Joined and attached scripts cannot be spelled one character per key.** The keycap
  renderer draws one glyph at a time with no shaper, so Arabic's joins and Devanagari's
  vowel signs and conjuncts fall apart, and 日本語 / 한국어 have no glyphs in the keycap
  fonts at all (only kana and conjoining jamo). Those four are PRE-RENDERED:
  `tools/gen_tutorial_names.py` shapes each with HarfBuzz from the Noto fonts listed in
  `fonts/noto-fonts.yaml` (weight 500, 1 bit), cuts it into 72x40 tiles on one common
  baseline — one grapheme cluster per key for Hindi/Japanese/Korean, key-width pieces cut
  at glyph boundaries for Arabic — and writes `anim/tutorial_names_gen.h` (3240 bytes).
  The tiles are in VISUAL order, so Arabic reads right to left across its keys with no
  special case. Tiles need no font pack, so these names show on a board that has none.
- **Long names use two rows.** Up to 7 units sit on display row 2; more are split over
  rows 1 and 2, the larger half on top, each row centred (GREEK over ΕΛΛΗ/ΝΙΚΑ,
  JAPA/NESE, AURE/BESH).
- ⚠️ `tutorial_draw_key_letter()` looked every single character up at 0xF0000 + cp (the
  latinbig relocation); for a non-Latin codepoint that is an unrelated glyph — にほ drew
  as "k{". Only A–Z take the larger tier now.
- `tools/tutorial_name_sheet.py` renders every item's name screen as the keys draw it,
  reading the pre-rendered tiles out of the generated header. Its item list is a replica
  of `s_tut_preview_all[]` — change both.

### Round 25, part 2 — "and many more", and Thai

- **`TUT_LANG_MORE` (3 s) closes the tour**: the board goes dark and the keys read
  `160 / LAYOUTS` on the left and `10 / SCRIPTS` on the right (number on row 1, word on
  row 2), with "...and many / more to pick" on the status panels. Both numbers are read
  from `NUM_LANG` and `GLYPH_SCRIPT_COUNT - 1`, so the screen cannot go stale. A board
  that previewed nothing (no font pack) skips it — "more" after an empty tour is wrong.
  Digits use the larger latin tier too; the latinbig bundle carries 0-9.
- **Thai compared three ways** (keycap font per character, Noto Sans Thai per character,
  Noto word strip): the keycap font's Thai IS essentially Noto Sans Thai, a size smaller,
  so per-character pre-rendering buys nothing. The word strip reads as a word, and
  `gen_tutorial_names.py` now takes a `|` in the text as a forced cut (`ภาษา|ไทย`,
  "language" | "Thai") so a strip never breaks mid-word. Cluster offsets from uharfbuzz's
  `add_str` are CHARACTER indices, not UTF-8 bytes.

## Round 26 — the emoji layer took over the lesson

⚠️ **Layer keys passed straight through the tutorial's key swallow**, a rule written so
that a layer key's RELEASE can never be eaten (the MO(_ADDLANG1) bug). It let the PRESS
through too, and `TO(_EMJ)` sits beside B on the base layer — dark in chapter 1, so
blank but not inert. One press moved the board to the emoji layer, where `TO()` latches,
and every letter key showed an emoji for the rest of the lesson (hardware).
- A layer key's press is now swallowed during the tutorial, except the layer chapter's
  own momentary key IN that chapter (`tutorial_in_layer_chapter()`); its release still
  passes, which is a no-op for a press that never happened.
- `poly_tutorial_hold_lesson_layer()` re-parks the master on `_L0` (layer stack AND
  `def_layer`) if either is found anywhere else outside the layer chapter, and logs
  `Tutorial: layer drifted (...)` so a future cause names itself on the console.

## Round 27 — pacing, a pulse, ten steps, and one brightness

Hardware feedback, all in one round:
- **Eden's stars start with the scanline wipe** (not the final fade) and live 1.6 s
  instead of 0.65 s; 3 slots per keycap at ~39 %.
- **The key to press PULSES.** `tut_pulse_slot()` names it — the lit letter while it
  waits, and whatever the pointing ring circles (both Shifts, the Lang key) — and
  `tut_pulse_level()` eases its panel's contrast between 24/255 of full and full over
  1.4 s. Both are pure and unit-tested; `tutorial_pulse_tick()` re-writes that ONE
  panel's contrast every 30 ms, because `update_displays()` writes every key's contrast
  back on each repaint. ⚠️ The Lang key's slot is now resolved on BOTH halves: the pulse
  runs on the half that owns the key, which need not be the master.
- **Progress counts in ten steps**, not three chapters (`tut_progress()`): opening, each
  letter, each Shift, the reveal, the first and second half of the tour, and the close.
  A walk through a whole run pins that it never goes backwards and uses all ten.
- **The tour is twice as slow**: name 2 s, glyphs 4.4 s.
- **"Now in" is gone**: the left status panel rotates "How about" / "You may speak" /
  "Or perhaps" / "Maybe you read" / "Do you speak" by the item's table row (scripts get
  "Or write in"), and the right panel finishes with the name.
- **Latin and native swap halves from item to item** (`tut_native_on()`, by table-row
  parity — the one thing both halves know about the item).
- **One brightness for the whole first run**: `POLY_INTRO_CONTRAST` (128 of 255) on the
  keycaps AND the status panels, through Eden and the tutorial. `set_displays()` forces it
  while the tutorial is active (OFF still turns panels off), `update_displays()`'s per-key
  write uses it, and the status panel's tutorial edge sets it. The finish edge's
  `set_displays()` runs after the tutorial is inactive, which hands the user's persisted
  level back.

## Round 28 — stars from the solid logo, lean inward, two heavy closing screens

- **Eden's stars open when POLYKYBD is solid**: `SA_STAR_START_MS` is the end of the
  letters' dither-in (tt 165 of 256 of the intro, ~3.2 s), not the scanline wipe. The
  window is ~10 s now, so five slots per keycap.
- **Names get 3 s** to be read (was 2).
- **A name leans toward the split when it cannot centre exactly**: with an odd number of
  spare keys the spare goes to the OUTER edge (`tut_name_slot_unit()`); keys are ordered by
  board x, so that is the left end of the left half and the right end of the right half.
- **The closing screen is TWO screens**, `TUT_LANG_MORE` then `TUT_LANG_MORE2`, 2.6 s
  each: `160 | LAYOUTS`, then `10 | SCRIPTS` — number on the left half, word on the right,
  both on the middle row, in `FreeSansBold24pt7b`, the heavy face of the boot splash and
  the BOOT-/LOADER! message (`tut_draw_heavy()`).

## Round 29 — the marker, the dark cut, one phrase per item, and the key tour

Hardware feedback:
- **"I completed the tutorial, but after a restart it showed up again."** That was the
  test build doing what it was written to do: `POLYKYBD_TUTORIAL_TEST` forced `first_run`
  on every reset. It now reads the marker like a normal build, but the value it stores
  as "played" is a hash of `QMK_GIT_HASH QMK_BUILDDATE` (`boot_done_value()` in
  `state.c`). A newly flashed test image therefore plays once, a finished lesson stays
  finished, and RESET Eden still replays it. A normal build stores the fixed
  `BOOT_INTRO_DONE`, unchanged.
- **The status panel was too dim.** Two causes. The keycaps' register value (128) reads
  dimmer on the 128x64 panel's thin prose, so the status panel now runs at
  `POLY_INTRO_STATUS_BRIGHT` (255) during the lesson. ⚠️ And the tutorial's own edge
  write did not hold: `status_oled_level()`, which the housekeeping contrast branch calls
  on any contrast change, dropped the panel back to the user's mapped level (at most
  `OLED_BRIGHTNESS`, 60). It now returns the tutorial level while the tutorial runs.
- **A 200 ms dark cut** (`TUT_LANG_DARK`, `TUT_DARK_MS`) before every name, every board of
  glyphs, and both "more" screens. One phase with a `dark_next` field rather than four
  phases; `tut[5]` carries `dark_next` to the slave (the phase has no wave to time). The
  status panels do NOT go dark: `tutorial_line()` reads the cut as the screen it leads to.
- **No repeated lead-in.** Each preview row carries its own `lead` ("How about", "You may
  speak", …, "Read by touch:"). The old 5-phrase rotation repeated over 11 items and every
  script said "Or write in".
- **The key tour** replaces the timed `TUT_LANG_POINT`. `TUT_TOUR_WAIT` points the ring
  and the pulse at `tour[tour_i]` with no timeout; the press moves to `TUT_TOUR_SEEN`
  (1.6 s on the result), then the next key. The keys are resolved from the keymap by
  `tutorial_tour_build()` on BOTH halves: the Lang key, `LCAT(0..5)` and `KC_BASE` on
  `_LL`, then `TO(_EMJ)`, emoji tabs 0/4/7/8 (two per half) and `KC_BASE` on `_EMJ`.
  Only the step index crosses the link (`tut[2]`).
  - ⚠️ **The asked-for key ACTS; every other key stays swallowed.** `poly_tutorial_tour_passes()`
    lets that one press through, and later that same key's RELEASE: `LCAT` and `KC_BASE`
    act on the release, and a swallowed release would make them do nothing. It FALLS
    THROUGH the rest of `process_record_user()` instead of returning true, because the
    custom keycodes are handled at its tail (`poly_custom_key_action`), which a
    `return true` would skip.
  - ⚠️ **The layer guard ENFORCES the tour's layer**, not just allows it
    (`tut_tour_layer()`): `_L0` plus what the last pressed step opened. So the menu the
    prose describes is on screen even if the key did not open it, and `KC_BASE` cannot
    leave a menu behind. `TO(_EMJ)` turns `_L0` off, which is not counted as drift.
  - Progress: 1 opening, 2-3 letters, 4-5 one Shift each, 6 the reveal, 7 the
    languages, 8 the language menu, 9 the emoji menu, 10 the close.

## Round 30 — slower stars, the stale script, the ring on the menus, the active tab

Hardware feedback:
- **Eden's stars**: 2.8 s each (was 1.6), ~25 % of four slots per keycap (was ~39 % of
  five), and five shapes (`sa_star_shape()`): plus, eight-point cross, diamond, a small
  form that turns from + to x, and a thin spike. Each passes through five equal stages:
  pixel, small, full, small, pixel.
- **"When the screens come back I can still see the previous script, then it
  changes."** An ordering bug, not a render one. `poly_tutorial_apply_preview()` ran in
  housekeeping's master block, AFTER the tutorial branch had ticked into `TUT_LANG_SHOW`,
  pushed that phase to the slave and rendered. So every board of glyphs was drawn once
  in the previous language, then again a pass later. `poly_apply_draw_script()` now runs
  right after `tutorial_tick()`, before the push and the render, so both halves get the
  phase and the language in one packet and draw once.
- **The ring blanked language keys.** The ring redraws the legend under itself through
  `tutorial_draw_board_legend()`, which knew only the static-text / `render_key()` pair.
  Flags, region tabs and MRU controls are drawn by bespoke branches of
  `update_displays()`, so every such key the ring crossed went dark. Those branches are
  one function now, `render_menu_key()`, used by both; the ring also draws the emoji tab
  frames and the MRU bar.
- **The active tab is not asked for**: the tour skips the region already open and the
  empty regions, and the emoji tabs are two per half from a preference list that skips
  the open category. Region and category are synced, so both halves build the same tour.
- **The emoji page key** (`TUT_TOUR_EPAGE`, `KC_EMJ_PAGE_NEXT`) follows the last chosen
  tab whose category has a second page. ⚠️ That key sits where the progress chrome is
  (right half, top-right outer key), so `tutorial_chrome_label()` gives up the chrome for
  whatever key the tour is asking for: a key reading "9/10" cannot be asked for as "the
  next page".
