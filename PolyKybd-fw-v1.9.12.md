# PolyKybd v1.9.12 Old Rev.2 boards connect again

**Protocol moves to 24.** Update PolyKybdHost to 1.17.49 first. Older apps still connect, without the Rev.2 setting.

## 1.9.12: Old Rev.2 boards connect again 🔧
- **The two halves of an old Rev.2 board (2023) connect again on current firmware.** Each half stores a Rev.2 mark, set once from PolyKybdHost 1.17.49, and the half connected to the computer then becomes the main half. Rev3 boards are unchanged.
- ⚠️ **On a Rev.2 board, flash each half over the UF2 drive.** The update from the app goes through the link between the halves, which works only once both have the mark. Steps: [Old Rev.2 Boards](https://www.polykybd.org/reference/old-rev2-boards/).
- The separate `revision2` build is gone. Every board uses the same firmware.

## 1.9.11: Restart cause in the startup log
The startup messages now say why the keyboard restarted (power loss, watchdog or a deliberate restart), also when no crash was recorded.

Plus maintenance release 1.9.10 🧹
