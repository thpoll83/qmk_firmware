# PolyKybd v0.24.4 Home row mods on Workman

Pairs with **PolyKybdHost 0.22.0** — no protocol change in this release (still protocol 17), so you can update the two independently.

## 0.24.0 — Home row mods, on one layout ⌨️

The **Workman** base layout now carries Miryoku-style home row mods: hold a home row key for a modifier, tap it for the letter.

```
left    a=GUI  s=Alt  h=Ctrl  t=Shift
right   i=GUI  o=Alt  e=Ctrl  n=Shift
```

- **Only Workman.** A mod-tap exists only on the layer that declares it, so the other four base layouts are untouched — picking your base layout *is* the on/off switch, and nothing about typing on Qwerty, Colemak, Neo or Stag changes.
- **The keycaps say so.** A mod-tap's held modifier is drawn as a small badge in the corner, so the hold is visible on the key rather than something you have to remember.
- Right ring is **Alt, not AltGr** — this board treats AltGr as a deliberate separate function with its own legends, and a home row mod that silently produced alternate characters would be a surprise.
- Tuned with `TAPPING_TERM` 200, `CHORDAL_HOLD` and Flow Tap (`FLOW_TAP_TERM` 150). `HOLD_ON_OTHER_KEY_PRESS` is deliberately **off** — it fires a modifier on ordinary fast rolls, which is exactly what the other two exist to prevent.
- Fixes a stray duplicate **B** on the Workman bottom row, where every other layout has a non-letter; it is `<` / `\` (`KC_NUBS`) now, as standard Workman expects.

Plus maintenance releases 0.23.1–0.23.5 and 0.24.1–0.24.4 🧹
