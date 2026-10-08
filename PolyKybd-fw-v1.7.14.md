# PolyKybd v1.7.14 C64 keycap script
**Protocol stays at 22.** PolyKybdHost 1.15.0 still works with this firmware. The new C64 keycap script needs PolyKybdHost 1.15.6 or later, which ships the font it draws with and lists it in the menu.

## 1.7.12 – 1.7.13: Intl variations reach the second half 🔗
A variation pick or letter remap on the Intl layer is re-sent until the second half confirms it. Before, one lost message left that half showing the old variations until the next pick.

## 1.7.11: Commodore 64 keycap script 🕹️
- **New glyph script 11, "Commodore 64 (keycap)", draws the letters the way the real C64 keycaps printed them.** Each letter key also shows its two PETSCII graphics below, Commodore+key on the left and Shift+key on the right.
- **The existing C64 script is now named "Commodore 64 (screen)"**, after the screen font it uses.

## 1.7.10: Switching idle style while idle
A new idle style set from the app now starts on a board that is already idle. Before, the board kept the old style until it woke and went idle again.

## 1.7.8: Tutorial letters match the keycaps
The first-run tutorial now runs in en-US, so the letter it asks for is the letter on the lit keycap. On a Korean board it asked for "A" over a key showing a Hangul letter. Your own language comes back when the lesson ends.

## 1.7.5: USB enumeration after a bus reset 🔌
The keyboard now answers a USB setup request that arrives right after a bus reset during a flash erase. Before, that request was refused and the computer had to retry it.

Plus maintenance releases 1.7.4, 1.7.6, 1.7.7, 1.7.9 and 1.7.14 🧹
