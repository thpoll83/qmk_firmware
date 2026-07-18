# PolyKybd v0.9.67 — Corne-size split42 + Eden

> Protocol unchanged at **v11** — pairs with PolyKybdHost 0.9.41.

### split42 — the Corne-size PolyKybd now works end-to-end ⌨️
The 42-key variant is fully alive: the split link comes up, keycap legends line up with the right keycodes, and the thumb keys are mapped correctly.
- A clean **Qwerty (Corne)** base layer plus a **Staggered** alternate, and a function layer with a full number row.
- **Portrait-native status OLED** (the 128×32 panel is mounted sideways) showing the layout name, USB/link role and side marker.
- A **left board can serve as the right half** via a compile-time mirror — no second board required.

### Eden — a procedural screensaver on the keycaps 🌌
A comet field that converges into "EDEN", selectable as an idle anti-burn-in style and replayable on demand (the Eden key). split72.

### Smoother boot & displays ✨
- **Progressive boot splash** revealing one glyph per startup milestone, plus a boot-ID banner and the variant name in the device ID.
- The status OLED no longer flashes RAM noise at power-on and no longer tears on the "Firmware Update" transition.

### Ambient light + proximity sensor (optional, split72) 💡
Support for a Pimoroni LTR-559 on the expansion port: auto-brightness from room light and wave-to-wake proximity. Entirely optional — nothing changes if it isn't fitted.

*Plus font-pack flash savings (deduped bundles), unified flash-staging, and maintenance across 0.9.45–0.9.67. 🛠️*
