# PolyKybd v1.13.34 MacCompose for macOS
The protocol moves from 37 to 38 for MacCompose. Update PolyKybdHost to v1.28.30 first, then this firmware. With an older host, the keyboard works as before and macOS keeps the old Unicode mode.

## 1.13.14: MacCompose for macOS 🍎
- **Emoji and other Unicode characters type correctly on macOS** with PolyKybdHost v1.28.30 or newer. The keyboard holds F20 instead of Option while it types the code, and the host posts the character, so the Unicode Hex Input source is no longer needed.
- ⚠️ **It needs PolyKybdHost with the Accessibility permission.** Without it, the keyboard uses the old Mac mode.

## 1.13.22: Safer reboot after a firmware update
At the end of a firmware update, the keyboard now holds the second core stopped while it saves the settings, and releases it again before the reset. A core left stopped across the reset would hang the next boot.

## 1.13.16: Two build options removed
For people who build their own firmware: `POLYKYBD_SYS_CLK=125` and `POLYKYBD_CIRQUE_RELATIVE=yes` are gone. Every build runs at 200 MHz and uses the PolyKybd trackpad gestures.

Plus maintenance releases 1.13.15, 1.13.17–1.13.21, 1.13.23–1.13.31 and 1.13.34 🧹
