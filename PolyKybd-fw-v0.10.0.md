# PolyKybd v0.10.0 — 200 MHz option & unsigned-firmware confirmation 🔐

## 0.10.0 — An optional 200 MHz clock ⚡
Raspberry Pi certified the RP2040 for 200 MHz in 2025, and the firmware can now be built
for it with a single flag — `-e POLYKYBD_SYS_CLK=200` — which also raises the core voltage
to the 1.15 V that rating requires. Measured on the test rig against a 125 MHz build of
the same firmware:

| | change at 200 MHz |
|---|---|
| Switching applications (overlay upload) | **~28% faster** — about 74 ms off each switch |
| Keycap rendering | **~30% faster** |
| Idle main-loop rate | **~16–24% higher** |
| Typing latency (HID round-trip) | unchanged |
| Split-link timing | unchanged |

- **The released `.bin` here is the stock 125 MHz build** — the flag is opt-in, for people
  who compile their own. A build without it is byte-identical to before.
- The gains land on *compute* — drawing keycaps, decompressing overlays. Anything bound by
  a bus or a wire is unaffected: HID latency is set by the USB frame rate, and traffic to
  the other half by the fixed UART speed.
- Along the way: the keyboard has always run at **125 MHz**, not the 133 MHz the docs said.
  Nothing ever set the clock, so it took the SDK default; 133 MHz was only the chip's old
  rated maximum. The boot banner now prints the clock and regulator setting read back from
  the hardware, so it is checkable rather than assumed.
- Full details: [System clock](https://www.polykybd.org/development/firmware/#system-clock)

## 0.9.98 — Confirm unsigned firmware on the keys
Needs **PolyKybdHost 0.10.13 or newer** — older hosts don't understand the keyboard's new
"waiting for you" answer and will stall part-way through a flash.

- An image without a valid signature isn't refused outright any more: the keyboard turns
  its keycaps into a dialog — a big **A / ACCEPT** on the left home-row index key, **R /
  REJECT** on the right — states the question on the status OLED, breathes orange, and
  waits 60 seconds. That's the way through for firmware you built yourself.
- The answer is a keypress, never a click in the host app. Signing protects the USB
  channel, so a confirmation sent over that same channel could be sent by an attacker just
  as easily. A signature that is *present and fails to verify* gets no prompt at all —
  that is a damaged or tampered file, not your own build.
- Fixes a real bug found on the way: a key held down while a firmware image was applied
  kept repeating on the computer until the keyboard rebooted. Both halves now let go of
  everything before any apply.
- The orange "you can't type" backlight during an apply now actually appears — it was
  being painted into a frame that never got drawn.

## 0.9.97 — Honest refusal reasons
A refused flash now says *why*, so the host can tell an unsigned image from a corrupted
transfer instead of blaming the CRC for both.

## 0.9.96 — Signature enforcement is on ✅
The keyboard acts on its Ed25519 signature check instead of only logging it. Releases ship
a matching `.bin.sig` — **download it into the same folder as the `.bin`**, or the image
counts as unsigned. Flashing over BOOTSEL/UF2 is never affected, so this cannot lock you
out of your keyboard.

Plus maintenance release 0.9.95 🧹
