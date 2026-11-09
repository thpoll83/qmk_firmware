# PolyKybd v1.11.3 Tap-hold timing

The protocol moves from 28 to 29. Update PolyKybdHost to 1.21.4 first, then this firmware. An older app still connects, but it cannot change the tap-hold timing.

## 1.11.2: Emoji search on the Shift keys 🔍
- **In search mode, left Shift types a space and right Shift deletes the last letter.** These positions are the same on every board.
- **The keyboard remembers search mode** after a restart or a power cycle.
- **Firmware updates now keep your settings, keymap and macros.** Before, a firmware that added a setting reset all three.
- ⚠️ Coming from 1.10.x or older, this update still resets settings, keymap and macros once, as 1.11.0 did. From 1.11.0, everything is kept.

## 1.11.1: Tap-hold timing ⏱️
- **The tapping term and the Flow Tap term are settings now** (cmd 44). The tapping term goes from 100 to 500 ms and Flow Tap from 0 to 300 ms (0 is off), in 10 ms steps. The defaults stay 200 and 150 ms.
- split42 has no Flow Tap, so only the tapping term applies there.

Plus maintenance release 1.11.3 🧹
