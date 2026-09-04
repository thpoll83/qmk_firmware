# PolyKybd v0.18.4 Crash reporting & readable AltGr legends

## 0.18.2 – 0.18.3 — The keyboard tells you when it crashed 🩺
A HardFault, an unhandled exception or a main-loop hang used to leave no trace —
the board just sat dead until you replugged it.

- Every crash is now recorded to a RAM block, rebooted through with an 8 s
  watchdog, archived to flash at boot, and announced: on the console, and over
  **HID cmd 39** (this half, the slave, or clear). A hang has no fault frame, so
  a phase breadcrumb — HID command, split transaction, core1 wait, flash,
  suspend, apply, main loop — says where it stopped.
- **Protocol v16.** Pair it with host 0.14.17+ to get the alert; an older host
  still connects, just without it.
- Five crashes in a row halt instead of looping, so a board that faults at boot
  stops rather than fighting you.
- New enough to say plainly: the capture path is exercised in CI and on the rig,
  but has not yet been driven by a genuine fault.

## 0.18.0 — AltGr legends land where you expect
- The AltGr-held glyph is drawn where its hint already sits, with a per-layout
  offset you can tune, and clamped onto the panel like everything else.
- `§ £ ± µ` sat as low as a descender on every layout that draws them (their font
  is 7 px taller than the base face) — lifted 6 px across all 117 cells.

## 0.17.3 – 0.17.4 — The Shift and AltGr hints stop colliding
- On 19 layouts the two hints drew straight through each other — every Arabic
  layout on `F`, and up to 57 px on `bn-BD`. It reads as a missing glyph, not a
  layout bug. They are now laid out as a pair, and the AltGr hint can be drawn at
  half size on the 27 layouts whose script needs the room.
- **Every legend element is clamped onto all four panel edges.** Measured across
  160 layouts: 420 keys were losing ink off the edge; now 1, and that one is a
  43 px Hebrew glyph in a 40 px panel.
- A Shift preview that only repeated the base letter is suppressed (236 of them,
  75 layouts) — without losing the uppercase when you actually hold Shift.
- **Eden idle:** the dimmed legend rolled its scanline phase, so it no longer
  lights the same panel rows for the life of the board.

Plus maintenance releases 0.17.2, 0.18.1 and 0.18.4 🧹
