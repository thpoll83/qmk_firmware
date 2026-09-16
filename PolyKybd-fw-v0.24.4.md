# PolyKybd v0.24.4 Home row mods on Workman

**No protocol change — still protocol 17**, the same as v0.23.0, so this firmware works
with the host you already have and neither side forces the other. Pairs with
**PolyKybdHost 0.22.0**.

A small release: one layout gains a feature, and one long-standing key mistake on the
same layout is fixed.

## 0.24.0 — Home row mods, on one layout ⌨️
Pick the **Workman** base layout and its home row doubles as your modifiers — tap for the
letter, hold for the modifier.

```
left    a = GUI    s = Alt    h = Ctrl    t = Shift
right   i = GUI    o = Alt    e = Ctrl    n = Shift
```

- **The arrangement is Miryoku's GACS**, outside in and mirrored across the two halves, so
  the strongest fingers carry Ctrl and Shift. `g` and `y` stay plain letters.
- ⚠️ **Only Workman.** A mod-tap exists only on the layer that declares it, so Qwerty,
  Colemak, Neo and Stag are untouched — and so is their typing feel, since nothing about
  the tap-hold timing applies anywhere else. **Choosing your base layout is the on/off
  switch**: there is no setting to find and nothing to turn off.
- **The keycaps say so.** A mod-tap's held modifier is already drawn as a small badge in
  the bottom-right of the key, so the hold is visible on the keyboard rather than folklore
  you have to remember.
- ⚠️ **Right ring is Alt, not AltGr.** The literal Miryoku arrangement puts AltGr there.
  This board treats AltGr as a deliberate separate function with its own keycap legends,
  and a home row mod that silently started producing accented characters would be a nasty
  surprise. One token to change if you want the literal behaviour.
- **Tuned to stay out of the way while you type**: `TAPPING_TERM` 200, `CHORDAL_HOLD` and
  Flow Tap (`FLOW_TAP_TERM` 150). `HOLD_ON_OTHER_KEY_PRESS` is deliberately **not** set —
  it settles as held on any other keypress, which fires a modifier on ordinary fast rolls,
  which is exactly what the other two exist to prevent.
- **Workman's bottom row gets its missing key.** It carried **`B` twice** — once correctly
  on the alpha row, and again in the spare right-hand column where every other layout puts
  a non-letter. Counting the special keys across all five base layouts, Workman was the
  only one without a `<` / `\` key; that is what sits there now, as standard Workman
  expects.

Plus maintenance releases 0.23.1–0.23.5 and 0.24.1–0.24.4 🧹
