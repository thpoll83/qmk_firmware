# PolyKybd v1.9.2 Mines, Japanese romaji keycaps

**Protocol stays at 23.** PolyKybdHost 1.17.0 or later works with this firmware. To get the ja-US entry and the input-method key in the layout editor, update the app to 1.17.13 or later.

## 1.9.2: 7.5 KB of RAM back 🧠
v1.9.1 was published without firmware files, so no keyboard got it. This release ships the same changes.
- **The accent picker's table stays in flash instead of being copied to RAM at boot**, which frees 7.5 KB. That raises free RAM from 4.6 KB to 12 KB.
- **Every firmware change is now built the way the release builds it**, so a change that would not fit can no longer reach a release.

## 1.9.0: Mines on the keycaps 💣
- **On the idle screen, hold Hyper for 1 s to play Minesweeper**, one keycap per cell (14×4 on split72, 12×3 on split42).
- Tap opens a cell and a 300 ms hold flags it. Each win adds a mine. Two minutes without a key press returns to the idle animation.

## 1.8.10 – 1.9.1: Sleep and shutdown no longer stall the keyboard 🔧
- **A computer going to sleep or shutting down during the idle animation no longer resets the keyboard or stalls its overlays.** A settings write could stop the second core for good.
- **The fix covers every saved change**, such as keymap edits from the app while the screensaver runs.
- During a firmware update, the half without USB shows "Erasing..." instead of a stuck 0%.

## 1.8.5 – 1.8.9: Input-method key and Japanese keycaps 🗾
- **A new input-method key replaces Non-US Backslash.** It switches 한/영 on Korean and 英数/かな on Japanese, and acts as a second Right Alt on US-style English. Other languages keep Non-US Backslash.
- ⚠️ **A board with a saved keymap keeps its old key** until you remap it in the layout editor.
- **ja-JP keycaps show the romaji letter first, with the kana as a hint.** On Windows and Linux, Caps Lock is ja-JP's 英数 key.
- **New language ja-US:** Japanese on a US keyboard layout.
- **Flags use one glyph per country.** Keyboards switch to it once PolyKybdHost 1.17.6 or later re-flashes the flags bundle.

Plus maintenance releases 1.8.7 and 1.8.8 🧹
