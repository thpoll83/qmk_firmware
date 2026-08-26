# PolyKybd v0.15.14 Bigger legends & a rebuilt settings layer

⚠️ **Update PolyKybdHost to v0.13.16 at the same time.** This release moves the HID
protocol to **v13**, and the connect gate is an exact match — an older host will not
talk to this firmware.

## 0.15.14 — One function layer 🎹
Fn now lines its F-keys up with whichever layout you are actually using.
- The two near-identical function layers are merged into one, and the F-row is derived
  from the active layout's own number row: F5 sits on the key that types `5`.
- Fixes **Colemak and Workman**, where F6 and up sat one key to the left of their number.
- Edit any key on the Fn number row and the alignment steps aside for that whole row, so
  the layout editor stays authoritative.
- ⚠️ Clears a customised keymap **once** on the first boot — layer numbering shifted and
  remaps are stored by layer number. Re-apply it; later updates leave it alone.

## 0.15.13 — The settings and utility layers, rebuilt 🎛️
What you reach for while typing moved to the utility layer; what you set once stayed put.
- **Brightness is one icon family** — a sun whose rays grow with the level, beside a
  staircase that states it outright. The old moon phases ran backwards: a black disc
  meant *bright*.
- **Restart, Boot, debug logging and the display settings sit behind `More…`** — blank
  and inert until you tap it, so a reboot key isn't one press away on a layer you open
  to change the OS pin.
- **Keys name their own state**: `IDLE:`/`Eden`, `SCRIPT:`/`Rune`, the layout picks over
  a lit or unlit switch, `Mods`/`Icon`, `Cmds`/`Text`.
- Scroll Lock gains a Caps-Lock-style badge that goes solid when engaged; Mute, Pause,
  Stop and the context-menu key get real icons.
- Fixes a long-standing rendering bug where each letter of a legend clipped the one
  before it — worst on `SCRIPT:`/`Rune`, but every multi-letter legend was losing ink.

## 0.15.8 — Keycap legend size 🔠
Choose how large the main legend is drawn: small, medium or large.
- New **HID cmd 34** plus a settings-layer key; the bigger faces ship in a new
  `latinbig` font-pack bundle that the host flashes for you.
- Latin, Cyrillic and Greek scale fully. Other scripts keep their own size — their
  digits and punctuation still grow.
- **Protocol v13** — pair with PolyKybdHost v0.13.16.

Plus maintenance releases 0.15.3–0.15.12 🧹 (CI, docs and tooling).
