# PolyKybd v1.13.4 Combos, tap dance and the MIDI key guide

The protocol moves from 32 to 34. Update PolyKybdHost to 1.22.16 first, then this firmware. An older PolyKybdHost still connects, but combos, tap dance and the MIDI guide stay off.

## 1.13.4: Fn + MIDI key 🎵
- **Fn plus the key above the right Shift** (`\` on QWERTY) switches to the MIDI layer on split72. On split42 the MIDI layer stays on the utility layer.
- ⚠️ **Your stored keymap changes once, at the first start after the update:** on split72 that Fn-layer key gets the MIDI layer only where it is still transparent. Keys you remapped and your macros stay.
- **Key guide** (cmd 47): the host can ring a path of keys to a layer. PolyKybdHost 1.22.16 uses it to show the way to the MIDI layer.

## 1.13.3: Combos and tap dance ⌨️
- **16 combos and 16 tap dances**, set in PolyKybdHost's layout editor or with `polyctl` (cmd 48, protocol 33).
- **Storage:** they are stored in two flash sectors of their own, apart from the keymap. A save is checked by reading it back, and the previous set stays until the new one is written.
- **Both halves:** both keep the same set. The keyboard checks the other half at start and every 60 s, and sends the set again if it differs.
