# Custom keycodes, tap-hold and the release edge

Moved out of `CLAUDE.md` 2026-09-14. Verbatim.

### ⚠️ Tap-hold settings are `config.h` DEFINES — the `rules.mk` lines were inert for years

`PERMISSIVE_HOLD = yes` and `HOLD_ON_OTHER_KEY_PRESS = yes` sat in **both** variants'
`rules.mk` and did **nothing**. `quantum/action_tapping.c` tests them with `#ifdef`, and
nothing in `builddefs/` turns a make variable of that name into a `-D`, so the board ran
QMK's defaults throughout while the build stayed green and the source read as configured.
The general shape: a make variable is only a feature switch when some `.mk` file
translates it: `grep -rn "<NAME>" builddefs/` before believing a line in a `rules.mk`.
Both were removed and the real defines now live in `split72/config.h` with their
rationale (2026-09-10, qmk#288).

⚠️ **`CHORDAL_HOLD`'s weak hook is what a SPLIT board wants — do not hand-maintain the
table.** The default `chordal_hold_handedness()` reads
`chordal_hold_layout[MATRIX_ROWS][MATRIX_COLS]`, i.e. an 80-entry `PROGMEM` table here
that must be kept in step with the matrix by hand (and is an undefined symbol at link
until you write it). The hook is `__attribute__((weak))`, so overriding it answers the
same question as arithmetic:

```c
char chordal_hold_handedness(keypos_t key) {
    return (key.row < MATRIX_ROWS_PER_SIDE) ? 'L' : 'R';
}
```

That is the split `LAYOUT_TO_INDEX()` and `is_left_side()` already assume, so it cannot
drift from the matrix the way a table can. It lives in `poly_keymap.c` under an
`#ifdef CHORDAL_HOLD`, and the override alone links — the table is never referenced.

⚠️ **`HOLD_ON_OTHER_KEY_PRESS` is deliberately NOT defined**, and the reason survives any
retuning: it settles a tap-hold as *held* on any other key press, so it fires a mod on
ordinary fast rolls — precisely what `CHORDAL_HOLD` (opposite-hands only) and
`FLOW_TAP_TERM` (forces a tap soon after the preceding key) exist to prevent. A home row
mod that fires while typing is worse than one that is occasionally slow.

### ⚠️ A release-edge action fires up to THREE times on a ONE-SHOT layer

`post_process_record_user()`'s big `switch` lives inside `if (!record->event.pressed)`
— every settings/utility keycode acts on the **release** edge. That is free on a
`TO()` layer and **not** free on an `OSL()` one:

- `process_action()` (`quantum/action.c`, the `do_release_oneshot` block at the very
  end) re-dispatches a key pressed while a one-shot layer is active as a synthetic
  release: `record->event.pressed = false; layer_on(oneshot); process_record(record);`.
  That inner `process_record` runs the whole chain, `post_process_record_user`
  included — **dispatch 1**.
- It mutated the **same record**, so when it returns, the outer `process_record`'s own
  `post_process_record_quantum(record)` also sees `pressed == false` — **dispatch 2**.
- The finger then lifts and the real release arrives — **dispatch 3**.

So one tap of `KC_GLYPH_SIZE_UP` stepped the legend size **three** tiers. It was
reported as "triggered twice" (field, 2026-08-25) because the third step clamps at the
end tier — a step key reads as ×2, an inc/dec (`KC_DDIM`/`KC_DBRI`) as ×3, and a
**toggle** (`KC_DAUTO`) reads as *doing nothing at all*, which is the shape that would
have been hardest to diagnose. These keys had lived on `_SL`, entered with `TO()`, for
years; moving them to the `OSL()`-entered `_UL` is what exposed it.

**Rule: a custom PolyKybd keycode is handled and SWALLOWED in
`process_record_user()`, never left to `post_process_record_user()`.** QMK has no
per-press dedupe for `post_process_record_*` — the docs describe it only as "runs
after each key press" — but it does not need one, because `process_record()` returns
**before** `process_action()` when `process_record_user()` returns false, so the
synthetic release is never generated at all. QMK compensates for the swallow in the
same early-return path (`clear_oneshot_layer_state(ONESHOT_OTHER_KEY_PRESSED)`), so
`OSL()` still resolves after one key. `poly_custom_key_action()` in `poly_keymap.c`
holds the whole settings switch (both edges) and returns whether it owned the
keycode; `process_record_user()` calls it last, before `display_wakeup()`.
- ⚠️ **A REAL keycode cannot use this** — swallowing it would stop it reaching the
  host. The shifts, the `_LL` F-keys and `RM_NEXT`/`RM_PREV` therefore stay in
  `post_process_record_user()`; all three are idempotent repaints, so the extra
  dispatches are harmless, and a modifier never gets them (`process_action()`
  excludes `IS_MODIFIER_KEYCODE` from the re-dispatch).
- ⚠️ A per-key "armed on press, consumed on release" bitmap was written first and
  **replaced** — it worked, but it is a bespoke guard for something the framework
  already answers.

