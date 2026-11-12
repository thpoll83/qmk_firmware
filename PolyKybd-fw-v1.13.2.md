# PolyKybd v1.13.2 AI key on the default layouts
The protocol stays at 32, so any PolyKybdHost from 1.22.4 works with this firmware. PolyKybdHost 1.22.8 names the keymap change below in its update dialog before you flash.

## 1.13.2: MIDI key legend
The MIDI layer key on the utility layer now reads MIDI instead of a clef that looked cut off. On a new or reset keymap it sits directly under the settings-layer key; a stored keymap keeps the key where it is.

## 1.13.1: AI key instead of Hyper 🤖
- **The AI key replaces Hyper** on the QWERTY, Stag, Colemak DH and Workman layouts (split72). A tap still sends Hyper, so Copilot on Windows still opens.
- ⚠️ **Your stored keymap changes once, at the first start after the update:** only keys that still hold Hyper at those positions become the AI key. Keys you remapped and your macros stay.
- On the idle screen, holding the AI key for 1 s starts the mine sweeper, as Hyper does.
