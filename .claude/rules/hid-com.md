---
paths:
  - "keyboards/polykybd/hid_com.c"
  - "keyboards/polykybd/fill_overlay.c"
---
# Editing the HID dispatcher

Notes: `PROTOCOL_HISTORY.md`. The `add-gated-hid-command` skill drives a new command
end to end (firmware + host + tests + docs + bump label).

- **Bump `FW_VERSION` + `PROTOCOL_VERSION` and the host's `__protocol__` in lockstep.**
  The connect gate is not exact-match, so forgetting it silently leaves the feature
  disabled rather than rejecting the keyboard.
- ⚠️ **A `*_set_user` hook is a NOTIFICATION, never a setter.** Calling one to change
  state moves the UI and not the behaviour. Grep for any being CALLED, not implemented.
- ⚠️ **Anything added to the GET_ID reply goes AFTER the `V` font-pack block** — the
  host finds it positionally, and prepending makes every deployed host re-flash all
  eight bundles on every connect. The budget is a `_Static_assert`, not a comment.
- ⚠️ **The overlay identity mapping is LOAD-BEARING FOR WRITES**, not a display
  convenience — the flat index is the only address an upload has. Zeroing it piled
  every image onto slot 0.
- **Do not add unsolicited raw HID replies.** `send_and_read_validate`'s drain rests on
  "since v3 the firmware sends no unsolicited replies"; the console may announce, never
  define.
- ⚠️ **Never do heavy work in a split-transaction handler** (~20 ms budget) — re-CRCing
  the font pack there made a perfect flash report as a CRC failure.
