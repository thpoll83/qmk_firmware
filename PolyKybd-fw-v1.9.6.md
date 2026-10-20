# PolyKybd v1.9.6 Fast link, typing during font-pack updates
**Protocol stays at 23.** PolyKybdHost 1.17.0 or later works with this firmware, and the two can be updated independently. The fast link is used by PolyKybdHost 1.17.28 or newer (1.17.30 for font packs and the Linux udev rule); an older app updates this firmware over HID as before.

## 1.9.6: ESC mark on the right half 🔧
- **With the master on the right, the ESC keycap now shows the new application's mark as soon as you switch apps.** Before, it changed only at the next keycap refresh, such as a modifier press.

## 1.9.5: Typing during a font-pack update ⌨️
- **Font packs, the icon library and game data are erased in 4 KB steps**, so the keyboard stays usable while the app writes them. Each 64 KB block held the keyboard for about a quarter of a second; a step now takes under 0.1 s.

## 1.9.4: Updates over a fast link ⚡
- **The keyboard has a USB serial port next to HID** (a COM port on Windows, `/dev/ttyACM*` on Linux, `/dev/cu.usbmodem*` on macOS). Firmware images and font packs stream over it at about 50 KB/s against 12 KB/s over HID. Commands and overlays stay on HID.
- **A firmware update takes about 23 s end to end** instead of about a minute, and the font-pack bundles about half their previous time.
- **The link between the halves runs at 1843200 baud** (460800 before). ⚠️ Both halves must run 1.9.4 or newer to talk to each other. A normal update flashes both; if one half was flashed by hand, flash the other too.
- **Erasing the update area is faster**: 64 KB blocks where aligned, and blank areas are skipped.
- ⚠️ **Linux:** the app's udev rule needs its serial-port lines; PolyKybdHost 1.17.30 offers the install under Updates. ⚠️ **Windows:** if no COM port appears on a computer that had the keyboard before, remove the device in Device Manager and plug it in again.
