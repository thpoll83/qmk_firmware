# PolyKybd v0.9.74 — Snappier displays & sharper legends

> Protocol unchanged at **v11** — pairs with PolyKybdHost 0.9.45.

## 0.9.74 — Overlays redraw only when they change ⚡
A keycap overlay update now repaints only on an actual on/off transition (cmd 12)
rather than on every identical push — cutting redundant full-keycap redraws during
heavy app switching.

## 0.9.73 — Much snappier displays 🚀
A batch of per-keycap render optimizations makes overlay swaps and app switches
noticeably faster.
- Each keycap redraw pushes only the **visible 72×40 window** over SPI (~2.9× less
  bus traffic per key), the idle-jitter redraw included.
- Renders are **skipped for keycaps that can't be seen** — off-screen modifier
  variants, off-layer overlays (F-keys, etc.), and mapping updates that don't touch a
  visible key.
- Overlay-burst renders are **coalesced** (with a count-based flush so the displays
  stay reactive), plus a two-pass wake-from-idle.

## 0.9.72 — Faster split link 🔗
The UART link between the two halves now runs at **460800 baud** (doubled from
230400), so overlays and state reach the slave half quicker.

## 0.9.70 — Sharper keycap legends 🔤
Keycap fonts are now grid-fitted (hinting): the compiled-in Latin legends plus every
text script and the symbol set render crisper at keycap sizes; pictographic emoji are
unchanged (a measured no-op). Pair with PolyKybdHost 0.9.45 for the matching
reshipped script/symbol font-pack bundles.

*Plus tooling & maintenance in 0.9.71. 🛠️*
