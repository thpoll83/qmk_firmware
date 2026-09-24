# PolyKybd v0.28.0 Layer keys that say what they do

**No protocol change** — still protocol 18, the same as host **1.2.8**, so either side can be updated first.

## 0.28.0 — One notation for every layer key ⌨️

- **A layer keycap now says what pressing it does**: no mark = active while held, a bolt = one-shot for the next key, `!` = stays on until you switch back. "Fn", "Nm" and "Util" used to look alike whichever they were.
- **Both marks are built into the firmware**, so they draw on a board with no font pack flashed.
- **Stale ink on the right half is gone.** The dirty-window optimisation filed every right-half panel under its neighbour's index — 28 of 74 keys, for as long as it has existed. A centred legend covered the old ink anyway; a thin off-centre arc does not.

## 0.27.4 — The handedness UF2s are withdrawn ⚠️

- ⚠️ **`polykybd-handedness-left/right_*.uf2` have never worked** and are no longer release assets. They declared 12 bytes where the RP2040 bootrom accepts only 256, so nothing was written, the half stayed in BOOTSEL, and both halves came up as `right` with no split link. **Delete the copies from v0.23.0, v0.25.0 and v0.27.1.**
- **Set handedness from the app instead** — PolyKybdHost stamps both halves over HID and reboots them.
- ⚠️ **Fixing the size is not enough**, which is why the files are gone rather than corrected: the write then completes and the half will not boot until the firmware `.uf2` is re-flashed. Unexplained.
- **The boot banner now proves a write landed** — `hand: LEFT (flash stamp) slot=0/1 writer=0x55`, where the old line read the same either way.

## 0.27.2 — A boot that stops now says where 🩺

A MacBook cold boot can wedge the master in the final boot render, before the watchdog starts and before anything is logged — the only evidence was a photo of the panel reading "100%".

- **The panel names the row and the crash record names the key.**
- **The watchdog now covers that render**, so a stall becomes a reset with a record instead of a hang.
- ⚠️ **This locates the hang; it does not fix it.** BOOTSEL still recovers a board that stops here.

Plus maintenance release 0.27.3 🧹
