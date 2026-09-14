---
paths:
  - "keyboards/polykybd/poly_keymap.c"
  - "keyboards/polykybd/keycode_helper.c"
---
# Editing the shared keymap / keycode legends

Notes: `KEYCODE_HANDLING.md` · `DISPLAY_PIPELINE.md` · `KEYMAP_STORAGE.md`.

- ⚠️ **Handle and SWALLOW a custom keycode in `process_record_user()`.** On an `OSL()`
  layer a release-edge action fires up to THREE times (one tap stepped the legend size
  three tiers; a toggle reads as doing nothing at all). A REAL keycode cannot use this —
  swallowing stops it reaching the host.
- ⚠️ **`render_key()` and `to_static_text()` are a PAIR** — `render_key()` is consulted
  only when `to_static_text()` returns NULL, so a mismatch draws chrome and NO legend.
  A third seam, `update_displays()`, can carry a per-keycode branch that makes the
  legend DEAD: **grep for your keycode there before believing a legend edit does
  anything.**
- ⚠️ **"Hidden" is TWO invariants — blank AND inert.** Gate ONE check above the
  `process_record_user()` switch, not per-case; a blank Restart keycap still rebooted
  the board in the field.
- ⚠️ **A new custom keycode with NO legend renders a BLANK KEYCAP**, indistinguishable
  from "the feature did not ship". Grep both legend switches before calling it done.
- ⚠️ **`get_local_layer()` is the SYNCED snapshot — never gate RENDERING on it.** It
  lags a layer change by up to one housekeeping pass.
- ⚠️ **A change to a layer below the write cap is INVISIBLE on any board that has ever
  stored a keymap** — the EEPROM copy wins. Bumping the format stamp wipes the user's
  macros; `polyctl keymap set` does not.
