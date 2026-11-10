# PolyKybd v1.11.5 Matching bundles on both halves
The protocol stays at 29, so you can update this firmware and PolyKybdHost independently.

## 1.11.5: Both halves get the same bundles
If the two halves hold different versions of a font or icon bundle, the keyboard now reports that bundle as out of date. The app then sends it to both halves again. Before, a half with a newer bundle than the other was never updated, and the halves kept showing different glyphs.

## 1.11.4: Icon library check
The keyboard now draws icons from its library only when both halves hold the same library. Otherwise the app sends those icons as images, so a half with another library can no longer show a wrong icon.
