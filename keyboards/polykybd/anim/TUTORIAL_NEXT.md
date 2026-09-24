# First-run tutorial — what is done, and how to continue

Companion to [`TUTORIAL.md`](TUTORIAL.md), which holds the design and the per-round
post-mortems. **This file is the worklist**: what is finished, what is deliberately
postponed, and what a future session has to do to take it further. Read both before
touching `anim/tutorial.c`, `base/tutorial_plan.[ch]` or `anim/focus_ring.[ch]`.

Branch: `claude/eden-startup-tutorial-1exa5m`. Everything below is pushed there.

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

---

## The postponed chapter

**Chapter 3 (a layer) is POSTPONED, not deleted.** Its phases (`TUT_LAYER_WAIT` /
`_SWEEP` / `_HELD` / `TUT_NOTATION`), its lit set, its prose and its tests are all still
here and still green — entered directly by the test fixture. Exactly **one transition**
is redirected, in `tut_tick()`:

```c
case TUT_SHIFT_HELD:
    ...
    tut_enter(st, TUT_DONE, now);      // <- change to TUT_LAYER_WAIT to restore
```

`TutorialShift.ATapStillFinishesTheChapter` is the one assertion pinning the
postponement; restoring the transition means updating that test and nothing else.

Why it was held back: Shift had to feel right first, and the chapter-2 round found four
separate bugs in the machinery chapter 3 would ride on.

⚠️ **When it is restored, the notation screen needs a look on hardware.** It is the only
screen using both status lines, and the `!` / ⚡ marks were tuned in a preview, not on
the panel.

---

## Work still to do, in the order it should be done

### 1. A cold-boot round (blocking — nothing else is worth doing first)
Flash a build, wipe the boot marker (or use a board that has never run it), power
cycle, and watch the whole first-run sequence. Then power cycle again and confirm it
does **not** replay. See "NOT verified" above.

### 2. Restore the shipping `KC_EDEN` semantics
`poly_keymap.c`'s `case KC_EDEN` currently carries a comment marked **PROTOTYPE
BEHAVIOUR**: it arms the tutorial *and* replays Eden on the spot so the sequence can be
retried without rebooting. The shipping behaviour is to **re-arm the first-run
experience for the next startup** and only replay the animation now. Undo the prototype
path once step 1 no longer needs it — and not before, or there is no way to retry.

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
