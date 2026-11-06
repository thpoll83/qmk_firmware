# PolyKybd v1.10.2 Keycap script placement, larger icon library
The protocol moves from 25 to 27. Update PolyKybdHost to 1.19.58 first, then this firmware. An older app still connects, but the placement setting and the larger icon library are not available.

## 1.10.2: Larger icon library 🧩
- **The icon library slot grows from 256 KB to 512 KB**, and so does the emoji slot. The app's new library of 2,520 shared icons fits with room to spare.
- ⚠️ **After you flash this firmware, the app re-flashes the fantasy, Latin-big and icon font packs once.** Their slots moved, so these keycap faces stay blank until that finishes.

## 1.10.1: Keycap script placement 🔤
- **The keycap script letter can sit in the centre or in the top-left corner** (cmd 30, protocol 26). Top-left keeps it clear of an overlay icon on the right of the key. The C64 face uses top-left to fit its letter and two PETSCII cells.
- The choice is saved, and both halves follow it.
