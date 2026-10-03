# PolyKybd v1.7.1 Boot hang fix
**Protocol moves to 22.** Update PolyKybdHost to 1.15.0 first. An older app still connects, but it can't run the boot-loop test.

## 1.7.1: Boot hang fixed 🛠️
- **The board no longer sometimes stops at "63%, 4 / 4" during boot.** An I2C interrupt could interrupt a running USB interrupt and send the processor astray. The board recovered after 8 s, but only through the watchdog. In a reboot loop this hit about one boot in three; after the fix, 845 boots in a row were clean.

## 1.7.0: Reboot from the app 🔁
- **New HID command 43 reboots both halves without saving anything**, so the app's boot-loop test can reboot the board hundreds of times without wearing the settings flash. Protocol 22.

## 1.6.3: One alert per slave crash
A crash on the second half was reported again every time the USB half rebooted. It is now reported once, as soon as the app has read it.

## 1.6.2: Finer boot breadcrumbs
A crash record from the boot screen now names which part of the status-panel paint stalled, not just the step.

## 1.6.0: Status-display diagnostics
A failed status-display write is retried once, and the console line now says why it failed. A panel that stops answering is reported once, not on every frame.

Plus maintenance release 1.6.1 🧹
