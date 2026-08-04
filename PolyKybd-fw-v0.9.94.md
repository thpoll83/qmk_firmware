# PolyKybd v0.9.94 — Every modifier combo & signed firmware

## 0.9.94 — Firmware image signing 🔐
Ed25519 verification for firmware updates, landed across 0.9.87–0.9.94.
- The keyboard verifies a signature over the image before applying it, against the
  project's real public key which now ships in the firmware.
- Still **warn-only**: nothing is rejected, the outcome is just logged
  (`image signature OK` / `UNSIGNED` / `INVALID`).
- Release CI signs the `.bin` and ships a `.bin.sig` alongside it. Until a release is
  actually signed you'll see `UNSIGNED` on every flash — expected, not a fault.

## 0.9.91 — Leaner overlay updates ⚡
App switches send less data again.
- New `cmd 33` carries a width byte, so the host picks the narrowest encoding each batch
  of mapping pairs fits: 8/9/10/11 bits → 30/27/24/22 pairs per report.
- Cmd, Cmd+Ctrl and Cmd+Shift keep the dense 10-bit form — no regression from the
  16-variant change below.

## 0.9.90 — Every modifier combination 🎛️
GUI/Cmd can finally be combined with other modifiers for overlays.
- Previously every Cmd+x chord drew the plain Cmd image — Sublime Text's Cmd+Shift+P and
  Cmd+Shift+Alt+P looked identical. All 16 combinations are now addressable.
- The variant number is simply the QMK modifier bitmask (bit0 Ctrl, bit1 Shift, bit2 Alt,
  bit3 GUI).
- ⚠️ **Protocol 12 — update the host app and the firmware together.**

Plus maintenance releases 0.9.88–0.9.89 and 0.9.92–0.9.93 🧹
