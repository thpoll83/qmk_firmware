# PolyKybd v0.9.82 — Sharper, faster keycap legends

## 0.9.82 — Sharper, faster keycap legends 🔤
Every keycap glyph is now stored in the display's native column (OLED-page) order, so
the firmware paints whole columns at once — crisper legends and snappier redraws.
- New column-native glyph format (PolyColGfx) across the built-in fonts, the status
  OLED, and the flashable font packs (font-pack ABI 2).
- ⚠️ Update the host too — PolyKybdHost 0.9.48+ ships the matching ABI-2 font packs and
  re-flashes them on connect. Until both sides match, a keyboard ignores a mismatched
  pack and falls back to its built-in fonts (emoji / symbols / CJK / flags drop off the
  keycaps). This is a font-pack format change, **not** a protocol change (protocol stays 11).

## 0.9.81 — Eden screensaver wakes instantly 🌿
The Eden idle animation is now time-sliced, so the first keypress wakes it immediately
instead of being swallowed mid-frame.

## 0.9.80 — DOOM screensaver wakes on proximity 👋
The DOOM attract-mode idle now wakes from the proximity sensor, matching the other idle styles.

## 0.9.76–0.9.79 — Snappier displays 🏎️
- Keycap updates stream only the changed sub-rectangle instead of repainting the whole
  panel, and legends use a faster unclipped raster path.
- DOOM keycap tiles push through the smaller 360-byte window; slave halves now repaint
  correctly on DOOM exit and mid-game handback.

## 0.9.75 — Brightness fix 🌙
Unattended auto-brightness no longer re-lights displays that were meant to stay dark.
