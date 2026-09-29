# PolyKybd v1.2.0 Icon library in flash

**Protocol 18 → 20.** Update the host to **1.6.0** first. With an older host the keyboard works as before, but every keycap image is uploaded in full.

## 1.2.0 — An icon library in flash ⚡
With host 1.6.0, a cold switch to an IDE with a large shortcut set went from 208 reports and 16.7 s to 32 reports in under a second.

- **599 shared shortcut icons live in flash on both halves.** The host fills a keycap with an icon number instead of uploading the image (cmd 42). Host 1.6.0 flashes the library on connect.
- **A half that missed a font-pack flash now gets it again.** Each half reports its own bundle versions, so the host sees the older one.
- **No font re-flash is needed.** The font bundles are unchanged. The largest font slot shrank to make room for the icon slot.

## 1.1.0 — Compressed keycap images 📦
- **Small keycap images arrive context-coded (PRC), several per report** (cmd 41). A cold switch sends 60% fewer image reports.
