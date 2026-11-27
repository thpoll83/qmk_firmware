# PolyKybd v1.13.40 Key lighting control
The protocol moves to 39. Update PolyKybdHost to v1.28.49 first. An older host still connects but cannot use the new key lighting settings.

## 1.13.40: Key lighting control 💡
- **The host can switch the LEDs under the keys** on and off and set their brightness, effect, speed and colour (protocol 39). These changes last until the keyboard restarts.
- **Find this keyboard**: the host can blink the LEDs to show which keyboard it is talking to.
- ⚠️ **The pressed-key read (`polyctl matrix`) answers only while debug mode is on** (DB_TOGG), so no program can log your typing.

## 1.13.39: Firmware update light on both halves
During a firmware update, both halves now breathe the update light at the same time.

## 1.13.38: Light pulses in step
The firmware update light and the tutorial pulses now follow a clock that both halves share, so the halves no longer pulse out of step.

## 1.13.37: Recovery from a stuck USB suspend 🔌
When the PC is awake but the keyboard stays suspended, the keyboard now restarts after 2 s and reconnects by itself, and it records why. Before, it stayed dark until you replugged it (for example after the PC was shut down overnight behind a powered hub).
