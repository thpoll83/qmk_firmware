# PolyKybd v1.13.11 Keyboard IDs, steady keys around the ring
The protocol moves to 37. Update PolyKybdHost to 1.28.12 first. With an older host, this firmware works, but the usage report sends no keyboard ID.

## 1.13.11: Keyboard IDs 🔢
- **The keyboard reports the unique ID of the flash chip in each half** (cmd 50). PolyKybdHost 1.28.12 sends a hash of each ID in the usage report, so a keyboard counts once on every computer. `polyctl hwid` shows them.

## 1.13.10: No flicker around the attention ring ✨
- **Keys around the attention ring keep their app icons.** While the ring ran (key attention, the host tutorial, the agent ripple), the keys it passed over flickered between the icon and the plain legend.
