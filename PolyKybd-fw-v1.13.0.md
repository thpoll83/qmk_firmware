# PolyKybd v1.13.0 AI agent keys and USB MIDI

The protocol moves to 32. Update PolyKybdHost to v1.22.4 first. With an older app, the agent keys are not available, and the layout editor does not know the MIDI layer.

⚠️ This update resets the stored keymap and macros once, because the MIDI layer is a new stored layer. Note your remapped keys and macros before you update.

## 1.13.0: USB MIDI 🎹
- **The keyboard is also a USB MIDI device** (send-only). The new MIDI layer turns it into a small piano, and each keycap shows the note it plays on a staff. The treble-clef key on the utility layer switches to it.
- Octave, velocity, sustain and pitch bend have their own keys, and the encoder sets the velocity.

## 1.12.0: AI agent keys 🤖
- **The number row shows your AI coding agents.** Each key's light shows the agent's state, and a waiting agent blinks red with a ripple across the keycaps.
- **The AI key**: tap it for Copilot (it sends Hyper). Hold it to see one card per agent, then press a number key to bring that agent forward. Put it on a key with the keymap editor or `polyctl keymap set … 0x7E2A`.
- The keys clear themselves when the computer sleeps or the app stops, and come back when the app runs again.
