# PolyKybd v0.9.68 — Idle reliability & diagnostics

> Protocol unchanged at **v11** — pairs with PolyKybdHost 0.9.42.

### The keyboard idles again while you're using the computer 😴
Every time the focused window changed, the host pushed a fresh set of keycap overlays — and each of those pushes counted as "user activity", restarting the idle countdown. A keyboard attached to a machine you were actively working on could therefore *never* reach its fade, screensaver or turn-off. Overlay uploads now refresh the keycaps without touching the activity timer; only real input (typing, a wake, the proximity sensor) resets it.

- Also stops an overlay push from re-arming the displays after a deliberate turn-off or a host display-off command.

### The console now says which idle style is running 🔎
Idle problems were hard to report because the log couldn't identify the style: pulse, jitter, and a DOOM screensaver that failed to start all printed the same line.

- A boot line reports the configured style and the fade/turn-off timings.
- Every idle transition names its style, and says so explicitly when **IDDQD falls back to the pulse** because the firmware wasn't built with DOOM — previously that downgrade was completely silent while the host still reported "iddqd".
- Wakes now log their source, so a proximity wake no longer restarts the idle cycle invisibly.

### Light-sensor brightness is logged 💡
For keyboards with the optional LTR-559 fitted, the sensor reports each brightness it drives along with the measured lux — throttled to actual changes, and marked when a manual override means the value isn't being applied.
