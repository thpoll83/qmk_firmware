# PolyKybd v0.25.0 Trackpad rebuilt, home row mods on Workman

**No protocol change — still protocol 17**, so this firmware works with the host you
already have. Pairs with **PolyKybdHost 0.22.0**.

## 0.25.0 — The trackpad, rebuilt 🖱️
split72's Cirque pad now runs its own gestures instead of the ASIC's: **tap** to click,
**tap-and-drag** to drag, and a **rotary dial on the outer ring** to scroll — start the
dial in the wedge at top-left, right-click in the top-right corner.

- The right half's **status OLED draws the pad while your finger is on it** — a circle
  with the finger position, the dashed dial ring and the corner markers.
- Cursor motion is filtered in firmware (median-of-3, low pass, glitch cap), so slow
  tracking is steadier while fast flicks still cross the screen.
- ⚠️ **The stock gestures are gone by default.** They could never be tuned: the 90°
  rotation is applied *after* the ASIC picks its corner, so the corner tap landed at
  11 o'clock and neither stock scroll ever fired.
- Build with `-e POLYKYBD_CIRQUE_RELATIVE=yes` to hand the pad back to the ASIC.

## 0.24.0 — Home row mods, on one layout ⌨️
Pick the **Workman** base layout and its home row doubles as your modifiers — tap for the
letter, hold for the modifier.

```
          ❖ GUI    ⎇ Alt    ⎈ Ctrl    ⇧ Shift
left        a         s         h          t
right       i         o         e          n
```

- **Only Workman.** The other four base layouts are untouched, so choosing your layout is
  the on/off switch — nothing to configure, and no timing change anywhere else.
- The keycap draws the held modifier as a **small corner badge** in those same symbols
  (the GUI mark follows your OS), so the hold is visible rather than something to remember.
- ⚠️ **Right ring is ⎇ Alt, not AltGr.** AltGr is a separate function on this board with
  its own legends, so a home row mod producing accented characters would be a surprise.
- Workman's bottom row gets its missing **`<` / `\`** key — it had carried `B` twice.

Plus maintenance releases 0.23.1–0.23.5 and 0.24.1–0.24.4 🧹
