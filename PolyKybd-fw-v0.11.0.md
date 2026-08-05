# PolyKybd v0.11.0 — 200 MHz & unsigned-firmware confirmation 🔐

## 200 MHz, by default ⚡
Raspberry Pi certified the RP2040 for 200 MHz in 2025, and this release runs there —
the firmware also raises the core voltage to the 1.15 V that rating requires, which is
what makes it a supported operating point rather than an overclock.

Measured on the test rig, 200 MHz against 125 MHz on the same hardware:

| | change |
|---|---|
| Switching applications (overlay upload) | **~28% faster** — about 72 ms off each switch |
| Keycap rendering | **~29% faster** |
| Idle main-loop rate | **~23% higher** |
| Typing latency (HID round-trip) | unchanged |
| Split-link timing | ~6% |

The gains land on **compute** — drawing keycaps, decompressing overlays. Anything bound
by a bus or a wire is unaffected: typing latency is set by the USB frame rate, and traffic
to the other half by the fixed link speed, so no amount of MHz shortens either.

Two smaller things worth knowing:

- The keyboard had always run at **125 MHz**, not the 133 MHz the documentation claimed —
  nothing ever set the clock, so it took the SDK default, and 133 was only the chip's old
  rated maximum. The boot banner now prints the clock and regulator setting read back from
  the hardware, so it can be checked rather than assumed.
- If a board ever misbehaves at the higher clock, `-e POLYKYBD_SYS_CLK=125` builds the old
  one — and that escape hatch is verified byte-for-byte identical to the previous firmware,
  not merely similar.

Full details: [System clock](https://www.polykybd.org/development/firmware/#system-clock)

## Confirm unsigned firmware on the keys 🔐
Needs **PolyKybdHost 0.10.13 or newer** — older hosts don't understand the keyboard's new
"waiting for you" answer and will stall part-way through a flash.

- An image without a valid signature is no longer refused outright: the keyboard turns its
  keycaps into a dialog — a big **A / ACCEPT** on the left home-row index key, **R /
  REJECT** on the right — states the question on the status OLED, breathes orange, and
  waits 60 seconds. That is the way through for firmware you built yourself.
- The answer is a keypress, never a click in the host app. Signing protects the USB
  channel, so a confirmation sent over that same channel could be sent by an attacker just
  as easily. A signature that is *present and fails to verify* gets no prompt at all — that
  is a damaged or tampered file, not your own build.
- Fixes a real bug found on the way: a key held down while a firmware image was applied
  kept repeating on the computer until the keyboard rebooted. Both halves now let go of
  everything before any apply.
- The orange "you can't type" backlight during an apply now actually appears — it was
  being painted into a frame that never got drawn.
- A refused flash says *why*, so the host can tell an unsigned image from a corrupted
  transfer instead of blaming the CRC for both.

## Signature enforcement is on ✅
The keyboard acts on its Ed25519 signature check instead of only logging it. Releases ship
a matching `.bin.sig` — **download it into the same folder as the `.bin`**, or the image
counts as unsigned. Flashing over BOOTSEL/UF2 is never affected, so this cannot lock you
out of your keyboard.
