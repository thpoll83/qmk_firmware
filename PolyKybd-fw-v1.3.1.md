# PolyKybd v1.3.1 Faster switches between recent apps

**Protocol 20 → 21.** Update the host to **1.7.0** first. With an older host the keyboard works as before; app switches just don't get faster.

## 1.3.1: Split-link repair
After a transfer to the other half fails, the keyboard repairs that half's icon mapping. The repair now clears the half's old mapping first, so keycaps no longer keep the previous app's icons.

## 1.3.0: Faster switches between recent apps ⚡
- **The steps that clear the old icons and show the new ones now travel inside the mapping reports** (cmd 33, protocol 21). With host 1.7.0, switching back to a recent app takes about half the reports: 249 → 129 over all 60 templates.
- **A malformed mapping report is refused before it changes anything.**
