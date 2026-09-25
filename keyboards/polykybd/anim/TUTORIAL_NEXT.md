# First-run tutorial — what is done, and how to continue

Companion to [`TUTORIAL.md`](TUTORIAL.md), which holds the design and the per-round
post-mortems. **This file is the worklist**: what is finished, what is deliberately
postponed, and what a future session has to do to take it further. Read both before
touching `anim/tutorial.c`, `base/tutorial_plan.[ch]` or `anim/focus_ring.[ch]`.

Chapters 1–2 merged to `PolyKybd` in #306 and #309. The boot trigger is **opt-in**
(`-e POLYKYBD_BOOT_INTRO=yes`, see `TUTORIAL.md` round 21) until step 1 below has run.

---

## What ships in this state

A new board plays the Eden intro and then teaches two chapters:

1. **Three letters.** The board goes dark, a status line welcomes you, then one keycap
   at a time lights with its own legend and the **focus ring** points at it. Pressing
   it is confirmed by a ripple struck from that key and by the letter drawn large on
   the status panel. Three keys, spread across both halves.
2. **Shift, once per hand.** The letters and both Shifts fade in; the ring points at
   the **left** Shift; holding it shows every legend change to its capital; then
   *"Try again"* with the ring on the **right** Shift.

Hold **Esc** (or the mirrored outer-edge key on the other half) for 1 s to skip. The
boot marker is stamped only when the tutorial finishes **or** is skipped, so an
interrupted first run replays.

### Verified on hardware
- Chapter 1 end to end, both halves, including the ripple crossing the seam.
- Chapter 2 both stages, real modifiers, real legends, both halves in step.
- Caps held internally for chapter 1 so the keycap matches the capital the status
  panel shows.
- The layout park/restore (`_L0` during the lesson, the user's default returned after).
- Overlays suppressed for the duration; idle suppressed for the duration.

### NOT verified on hardware
- ⚠️ **A COLD BOOT has never been done.** Every hardware round so far entered through
  the prototype `KC_EDEN` key, which arms and runs the sequence on the spot. The real
  entry — `boot_intro_pending()` at startup, Eden, hand-off, the EEPROM marker, and a
  second boot that does *not* replay — has never actually run. **Do this first**: it is
  the one path every user takes and the only one nothing has exercised.
- Whether the marker survives a **firmware flash** (it is EEPROM, so it should; the
  point of the marker living off Eden's finish edge was exactly this).

### Chapter 3 (2026-09-24, not yet on hardware)
3. **The board reveal, then languages and scripts.** After the second Shift a wave
   leaves that key and lights every legend it passes ("Every key / is a screen", then
   "72 screens, / one keyboard"). Then the board previews Greek, Russian, Arabic,
   Japanese, Korean, Elvish, Runes, Aurebesh and Braille, about 2 s each, on the
   keycaps only; the host keeps seeing the real language. The ring then circles the
   Lang key, and a finale screen ends it.

The lesson's chrome: **Esc reads "Hold to / skip..."** and its mirror, the **top-right
outer key, shows the chapter** (`1/3`…`3/3`). Holding either still skips. Design and
traps: `TUTORIAL.md` round 22.

4. **The key tour** (round 29): the Lang key, the six region tabs and Base, then the
   emoji key, four category tabs and Base. Each key is pointed at, pulses and must be
   pressed; the press acts for real.

**Test build:** `-e POLYKYBD_TUTORIAL_TEST=yes` plays Eden + the tutorial once per
flashed build: the boot marker is keyed to the build stamp, so a new image plays, a
finished lesson stays finished across restarts, and RESET Eden replays it. Never in a
release.

---

## The postponed chapter

**Chapter 3 (a layer) is POSTPONED, not deleted.** Its phases (`TUT_LAYER_WAIT` /
`_SWEEP` / `_HELD` / `TUT_NOTATION`), its lit set, its prose and its tests are all still
here and still green — entered directly by the test fixture. Exactly **one transition**
is redirected, in `tut_tick()`:

```c
case TUT_SHIFT_HELD:
    ...
    tut_enter_reveal(st, now);         // <- change to TUT_LAYER_WAIT to restore
```

To restore it, also send `TUT_NOTATION` on to `tut_enter_reveal()` instead of
`TUT_DONE`, so chapter 3 still follows. `TutorialShift.ATapStillFinishesTheChapter` is
the assertion pinning the postponement.

Why it was held back: Shift had to feel right first, and the chapter-2 round found four
separate bugs in the machinery chapter 3 would ride on.

⚠️ **When it is restored, the notation screen needs a look on hardware.** It is the only
screen using both status lines, and the `!` / ⚡ marks were tuned in a preview, not on
the panel.

---

## Work still to do, in the order it should be done

### 1. A cold-boot round (blocking — nothing else is worth doing first)

⚠️ **Until 2026-09-24 this round could not run: nothing in the firmware cleared the
marker.** Every earlier round went through `KC_EDEN`, whose tutorial done/skip edge
stamped `BOOT_INTRO_DONE` on both halves, so an opt-in build on any tested board booted
straight to the legends. "Wipe the marker" named a step with no mechanism. RESET Eden
now clears it (step 2), and at boot the master bumps `anim_nonce` so a slave whose own
marker still reads DONE plays Eden anyway, through the `anim_replay` path `KC_EDEN`
already proved on hardware.

The procedure, on a `-e POLYKYBD_DOOM_PACK=yes -e POLYKYBD_BOOT_INTRO=yes` build:

1. Flash the `.bin`. It reboots; nothing plays, since the marker still reads DONE.
2. Settings layer → tap **RESET Eden**. Eden and the tutorial run; **unplug during the
   tutorial**, before it finishes, so the cleared marker is not re-stamped.
3. Power up. Expect Eden on **both** halves, then chapter 1.
4. Finish or skip the tutorial, then power cycle again. Expect **no** replay.

Watch step 3 for a wedge in the boot window: no console reaches the host there, so a
half stuck on the splash is the only sign.

### 2. The `KC_EDEN` semantics — OPEN again
RESET Eden clears the marker AND plays Eden plus the tutorial on the spot. The planned
"clear the marker, replay only the animation" split was tried with Shift+RESET as the
run-now path, and ⚠️ **Shift cannot be held on `_SL`** — both Shift positions there are
other keys — so the tutorial became unreachable. Deciding the shipping behaviour needs a
different gesture (or none). Only the master's EEPROM is written; the boot-time nonce
covers the slave.

### 3. The HID enable/disable command (needed by the rig)
So a host — and the HIL rig — can turn the tutorial on and off and read its state. Use
the **`add-gated-hid-command`** skill, which drives this end to end. It costs a
`PROTOCOL_VERSION` bump, so:
- firmware `PROTOCOL_VERSION` + `FW_VERSION`, host `__protocol__`, and a
  `FEATURE_MIN_PROTOCOL["tutorial"]` entry, all in lockstep;
- `polyctl tutorial on|off|status`;
- the PR needs the **`bump:protocol`** label **applied at open** (`issue_write` right
  after `create_pull_request` — `create_pull_request` cannot set labels, and a label
  applied as the merge happens lands too late);
- ⚠️ **publish the HOST release before the firmware release** — the connect gate is not
  exact-match, so the reverse order silently leaves the feature off.

### 4. The HIL test (`polykybd-ctnd`)
An **extended-tier** test: the tutorial is slow and it takes over the board, which is
the definition of `TIER_EXTENDED` (cost, never confidence). Use the **`add-hil-test`**
skill.
- ⚠️ **The ctnd PR must merge BEFORE the firmware PR.** CI force-syncs the rig to ctnd
  `main`, so a test that exists only in an unmerged ctnd PR does not exist on the rig —
  the firmware board goes green having never run it. Verify by grepping the HIL job log
  for the test's own name, not by reading the conclusion.
- It depends on step 3: without the HID command the rig has no way to start or stop the
  tutorial, and a test that cannot end it would leave the board mid-lesson for whatever
  runs next.

### 5. A docs-site page
Use the **`update-polykybd-docs`** skill. ⚠️ **A docs PR ships the moment it merges**
(`deploy.yml` runs on push to `main`) while a firmware PR only bumps a version — so the
page waits for the *release* that carries the tutorial, not merely for the firmware PR
to merge. Say so in the docs PR body; nothing else will catch it.

### 5b. A hardware round for chapter 3
Check the reveal's speed and smoothness, the Esc label's fit, each preview's dwell,
and that the tray/OS language does NOT change during the preview (watch the host's
language indicator while Greek is on the keycaps).

### 6. Later chapters (optional, in this order)
- **Chapter 4 — the settings layer.** What the layer holds and how to reach it.
- **Chapter 5 — Intl.** Deliberately last: it is the most intricate mechanism on the
  board (hold Intl, tap Ctrl for the picker, `KC_LAT_REMAP`), and a first-run tutorial
  that opens with it teaches nothing. ⚠️ `INTL_LAYER.md`'s standing rule applies —
  **nothing may overlay that layer**, its letters are the payload, so a chapter there
  cannot use the hide-the-rest mechanism the other chapters use.

---

## Things that are easy to get wrong here

These are the ones that have already cost a hardware round each. The full narratives are
in `TUTORIAL.md`; this is the checklist.

- ⚠️ **A mode that wants the board to behave normally must let the board behave
  normally.** Chapter 1 was once "exclusive" — the tutorial owned the panels and
  re-implemented the renderer — and every re-implementation was a bug: the mods snapshot
  was never refreshed, the layer never reached the slave, the legend resolved the wrong
  layer, the repaint front covered 12 % of the board. `tut_phase_is_exclusive()` now
  returns false for everything. The predicate is kept as the seam, not as a hint.
- ⚠️ **The ring does two opposite jobs.** Chapter 1: struck by the press, it
  **confirms**, so one shot. Chapter 2: it **points** at a key you have not found, so it
  re-fires on a period while waiting and stops the moment you are holding it. A ripple
  bursting from under your own finger answers a question you have just answered.
- ⚠️ **One place arms the ring per half**, keyed off `ripple_seq` — the field the slave
  watches. Three things raise a ripple; giving each its own `poly_focus_start()` is how
  one of them ships working on the slave and dead on the master.
- ⚠️ **Anything the per-half render READS is part of the sync contract.** `s_st.step`
  draws nothing itself, picks the status words, and was not synced — so two of the three
  letters showed a sentence whose halves disagreed.
- ⚠️ **The status face covers 0x20..0x7E.** Anything else needs its own single-font
  array (`kdisp_write_gfx_char` baseline-aligns to `fonts[0]`), and `IconsFont` is
  reached by `extern`, never by including the font header a second time.
- ⚠️ **`tut_*` is pure logic with no keymap access.** Anything it must know about keys —
  the letter slots, the Shift slots — is resolved in `poly_keymap.c` and handed in. Both
  halves resolve them, because both answer `tut_hold()`'s "is this the key I am pointing
  at".
- ⚠️ **Test the PHASE, not `tut_tick()`'s return**, on a waiting phase: the pointing ring
  makes it return true without a phase change.

---

## Where everything lives

| File | What |
|---|---|
| `base/tutorial_plan.h` / `.c` | pure timeline, phases, curves, ring geometry — no quantum.h, unit-tested |
| `base/tests/tutorial_plan_tests.cpp` | 73 tests (`make test:polykybd_tutorial_plan`) |
| `anim/tutorial.h` / `.c` | the firmware binding: lifecycle, split sync, status prose |
| `anim/focus_ring.h` / `.c` | the reusable "point at this key" ripple — general, not tutorial-only |
| `anim/TUTORIAL.md` | design + per-round post-mortems |
| `poly_keymap.c` | the keymap-facing half: slot resolution, the lit-set gate, layout park/restore, the housekeeping hook |
| `oled_helper.c` | `oled_tutorial_screen()` — the status panels |
