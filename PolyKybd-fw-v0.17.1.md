# PolyKybd v0.17.1 The update brick fixed & Eden by default

⚠️ **Install this release over BOOTSEL + UF2, not over HID.** The bug fixed below
lives in the *applier of the firmware you already have*, so it runs before this fix
does — a board on **v0.16.9 or v0.16.10 cannot take this update over HID**. Hold
BOOTSEL, drop the `.uf2` on the drive, once. From this release onward HID updates
work normally again.

No protocol change in this range — both ends still speak **protocol 15**, so no
matching host update is required.

## 0.17.0 — A crafted font pack is stopped before it reads past the pack 🔐
The guard that bounds a font's codepoint range could be walked straight past.
- It computed `last - first + 1` in a 32-bit count, which **wraps to zero** for a
  range spanning the whole codepoint space — so a crafted range passed the capacity
  check and then declared itself to cover every codepoint. A single lookup then read
  about 1 MB beyond a 70 KB pack, over the same unsigned transport the guard exists
  to defend. It compares spans now, which cannot overflow.
- All 8 shipped bundles and 158 fonts are still accepted — the correction costs no
  compatibility, and no re-flash is needed.
- The DOOM engine's core1 stack could sit 4-byte aligned where it needs 8, the same
  class of fault as the update brick below.
- The build now **fails** on that class in PolyKybd's own sources rather than shipping
  it — which is how the two above were found.

## 0.16.23 — Clipped previews on Arabic and Pashto layouts 🔤
The legend measurement resolved glyphs by a different route than the drawing did, so
it could place a preview using a glyph the keycap would not actually draw.
- On every **Arabic** layout the `T` key's AltGr preview was pushed off the right edge
  and **clipped by 24px**. Same class on **ps-AF** (9 keys) and **ku-IQ** (3).
- Both now resolve through one lookup, so the measurement describes the glyphs that
  get drawn.

## 0.16.21 — Eden runs by default, and the status OLED can tell you what is wrong 🌌
- **The Eden screensaver is now the idle default on Split72.** ⚠️ A keyboard that
  never explicitly chose an idle style comes up in it **once** after flashing; pick
  another from the settings layer or the host if you preferred the pulse. Split42
  keeps the pulse — the animation does not exist there, and defaulting it would have
  left that board with no anti-burn-in at all.
- **Settings → More now shows telemetry** instead of the ordinary status screen:
  firmware and protocol version, hardware revision, uptime, and the split-link error
  rate — so the health of the cable between the halves is finally somewhere you can
  read it.
- **Idle art moves with the legend again.** The anti-burn-in drift shifted the text
  but left composited artwork pinned in place, so the context-menu key's cursor, the
  Scroll Lock badge and the Media Stop mark never moved — burning in exactly the
  pixels the feature exists to protect.
- Eden's dimmed resting legend now dims that artwork too, instead of drawing the text
  at half density beside fully-lit marks.

## 0.16.19 — Firmware updates come back ⚠️
A HID update reported complete success — staging written, CRC matched, both halves
acknowledged — and then the keyboard never returned. Only BOOTSEL + UF2 recovered it.
- **Root cause:** the applier copies each flash page through a static buffer using
  word-sized loads and stores, but the buffer was declared as bytes, so the linker was
  free to place it at any address. Landing off a 4-byte boundary makes the first copy
  a HardFault on the M0+ — inside a function that never returns, with the running
  image already part-erased.
- Where it lands is decided per build and re-rolled by anything that shifts memory
  layout, so **earlier releases that worked were lucky, not correct**. Measured across
  the release tags: v0.15.2 and v0.15.14 land safely, **v0.16.9 and v0.16.10 do not**.
- The fix is in the declaration rather than an alignment annotation, so no later
  change can quietly undo it.
- The apply path gained the hardening the investigation exposed: the staged image is
  verified against flash before the running firmware is overwritten, a bad staging
  header no longer retries forever, and core1 is confirmed stopped before flash is
  touched.
- Release downloads now carry the version in the filename, so two `.bin`s in a
  downloads folder can be told apart.

## 0.16.15 — The DOOM screensaver lets go when the host asks 👾
- Asking the keyboard to leave idle now tears down the attract demo instead of leaving
  it owning the keycaps.
- The core1 restart during a flash is bounded, so the other half can no longer hang
  part-way through a font-pack or engine-pack update.

Plus maintenance releases 0.16.11–0.16.14, 0.16.16–0.16.18, 0.16.20, 0.16.22, 0.16.24
and 0.17.1 🧹 — the CI that makes the update path above verifiable: every merge now drives a real HID update through APPLY on the test
rig, and a release is refused outright if no such run covers it.
