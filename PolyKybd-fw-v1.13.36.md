# PolyKybd v1.13.36 Split link and font pack checks
The protocol stays at 38. This firmware works with PolyKybdHost v1.28.30 and newer, and the two can be updated in either order.

## 1.13.36: Checked split-link updates
The keyboard half without USB now checks every keymap update it gets from the other half against the length of the received frame. A frame that names more bytes than it holds, or is too short to carry an offset and a size, is refused and writes nothing to the keymap.

## 1.13.35: Font packs with more than 129 fonts
A font pack with more than 129 fonts no longer corrupts memory at boot, and its fonts stay in order. Today's pack has about 119 fonts.
