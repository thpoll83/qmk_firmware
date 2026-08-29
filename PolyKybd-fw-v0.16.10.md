# PolyKybd v0.16.10 Macros & a signed engine pack

## 0.16.8 — The DOOM engine pack is signed 🔐
The `.plyx` engine pack is executable code flashed over HID and jumped into, and it
was authenticated by a CRC32 — an integrity check anyone crafting a pack satisfies.
It now carries an Ed25519 signature over the header *and* the image, checked at load.
- Closes the last unsigned code path into the firmware: anything that could talk raw
  HID could previously flash a crafted pack, select the DOOM screensaver (cmd 28) and
  run its own code with full firmware privilege on the next idle — around the firmware
  image signing entirely.
- Checked at **load**, not at flash time, because flash can be rewritten after a
  successful commit. The header is signed too — `entry_off`/`ram_base` are what an
  attacker would edit to re-point an otherwise valid image.
- ⚠️ **An unsigned pack is refused and the fire demo runs instead.** There is no keycap
  prompt for this one: the load happens while the board is idle, with nobody there to
  answer it. Flash the signed `.plyx` from this release (`PACK_VERSION 4`) to get DOOM
  back. No host update is needed for this, and a signed pack still loads on older
  firmware.

## 0.16.0 — Macros ⌨️
Store a phrase or a key sequence on the keyboard and type it back with one key — and
the keycap tells you *which* macro it is.
- **16 macros** sharing ~2.1 KB of body space, each with a caption of up to 12
  characters. A row of macro keys reads `push`, `work mail`, `sign off` rather than
  M0, M1, M2.
- **Four keycap layouts** per macro: the index above the caption (default), a chosen
  icon above the caption, the caption alone at the largest size that fits, or the
  icon alone.
- Playback runs **one step per pass**, so a macro with a long pause never freezes the
  board or drops the split link. Any keypress aborts it.
- Set them up in PolyKybdHost's layout editor — the new **Macros** tab. **Needs host
  v0.14.0** (protocol v15); with an older host the keyboard works as before and
  macros simply don't appear.
- ⚠️ **Your stored keymap is reset once** on the first boot after flashing. Making
  room for macros moved the EEPROM layout, so remaps made in the layout editor need
  re-doing. The firmware's own layouts are unaffected.

## 0.15.17 — Layer names come from the keyboard 🏷️
The keyboard now reports what each remappable layer is actually called (cmd 35,
protocol v14).
- The layout editor labels its tabs `Qwerty`, `ColemkDH`, `Fn`, `Numpad`… instead of
  internal tags read from a shipped file that had quietly gone stale two renames ago.

## 0.15.15 — The DoomPack slot no longer overlaps the EEPROM 🛡️
The declared engine-pack slot ran to the end of flash, and its last 8 KB *was* the
emulated EEPROM.
- It never bit — the shipped pack is 211 KB and only the sectors an image needs get
  erased — but a pack grown past ~240 KB would have wiped the stored keymap,
  brightness, language and Intl settings with no diagnostic. The slot now ends exactly
  at the EEPROM base, keeping ~41 KB of headroom.

Plus maintenance releases 0.15.16, 0.15.18 and 0.16.1–0.16.7, 0.16.9–0.16.10 🧹 — an
architecture pass that extracted testable seams out of the display and keymap code
(+62 unit tests across four new suites), and the CI to rig-test the signed pack.
