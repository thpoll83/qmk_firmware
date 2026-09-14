---
paths:
  - "keyboards/polykybd/base/fw_staging.c"
  - "keyboards/polykybd/base/fw_staging.h"
  - "keyboards/polykybd/base/fw_up_verdict.c"
---
# The firmware applier

Notes: `FW_STAGING.md`.

- ⚠️ **The page buffer must be `uint32_t`.** A `uint8_t` one word-copied through a cast
  has alignment 1; an unaligned `STMIA` HardFaults the M0+ inside a function that never
  returns. It shipped, and it bricked boards. The TYPE is the fix, not `aligned(4)`.
- ⚠️ **COMMIT must NOT block waiting for the keypress** — it runs on the loop that scans
  the matrix. It is a state machine answering `?` until resolved, and it skips
  re-bridging to the slave while a prompt is up.
- ⚠️ **An UNSIGNED image gets the prompt; an INVALID one is refused outright.** Accept is
  physical; cancel may be remote.
- ⚠️ **`clear_keyboard()` before any path that swallows keys or does not return.**
- ⚠️ **A visual cue set on a path that never returns is never painted** — flush it
  synchronously, don't leave it to a periodic task.
- **Signing gates the FIRMWARE image only** — the resource region has no signature check
  at any target.
- ⚠️ Sectors carved off the TOP of staging need an **alignment** assert as well as an
  overlap one; each is derived by subtraction and can move without any overlap.
