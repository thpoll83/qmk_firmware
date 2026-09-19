# PolyKybd v0.27.1 Idle timeout & a status panel that dims

**Protocol 17 → 18.** Update **PolyKybdHost to 0.23.2 first**, then flash this — the host is the side that drives the new command. An older host still connects; it just leaves the idle-timeout control switched off, with nothing on screen to say why.

## 0.27.0 — Set how long before the keycaps fade ⏱️

- **Six idle-timeout presets** — 15 sec, 30 sec, 45 sec, 1 min, 2 min, 5 min — replacing a value fixed at 2 minutes when the firmware was built. 2 min stays the default, so an untouched board behaves exactly as before.
- Set it on the **settings layer** (behind More…, a clock face over the current value) or from the host. ⚠️ The displays still switch **off** after 20 minutes whichever you pick — the shorter timeout only brings the fade forward.
- **An unsigned `.plyx` engine pack can now ask on the keycaps** — A = ACCEPT / R = REJECT — instead of being refused with no explanation, and only when you start it yourself. A pack whose signature is *wrong* is still refused outright.
- A boot that hangs now names a **sub-step** as well as the percent, so a wedged board says `63%.2` rather than just 63%.

## 0.26.0 — The status OLED joins the brightness scale 🔆

- **The 128×64 panel dims with the keycaps.** `KC_DMIN`, the host slider and the light sensor moved the keycaps while the status panel kept blaring at a fixed level; both halves now follow the same synced value, so the right one no longer stays bright on its own.
- ⚠️ **Fixes "dim for no reason, but only sometimes"** — going idle set the panel to contrast 0 and nothing restored it on wake, leaving it barely visible until you toggled the display or rebooted.
- **A boot hang says where it stopped** — "Booting…. 63%" on the panel, and the step stamped into the crash record. Boot was the one window the watchdog never covered.
- **Firmware updates name what they are doing** — Staging versus Applying, why an apply was refused, and a "Restart Now" you can actually reach.
- **The trackpad's dial no longer arms from a resting finger.** The top-left wedge moved out to the outer half of its diagonal, so a finger parked at the top left points instead of scrolling.

Plus maintenance releases 0.25.1–0.25.2 and 0.27.1 🧹
