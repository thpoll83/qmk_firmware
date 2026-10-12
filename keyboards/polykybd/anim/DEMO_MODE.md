# Demo mode (`anim/demo_mode.c`, `base/demo_plan.c`)

_Moved verbatim from `CLAUDE.md` on 2026-10-10. CLAUDE.md keeps a short pointer._


`KC_DEMO` on the settings layer (behind More) plays a 15-minute showroom loop until Esc
is held for 2 s. The playlist and its timing are pure (`make test:polykybd_demo_plan`).
`KC_DEMO_KEYS`, beside it, is the **key demo**: the same loop, and each TYPE segment
also reaches the host as plain keystrokes plus an Enter per line, as a notepad typing
test. ⚠️ **The demo's longer typing IS the idle screen's poems** (`idle_poem_0..4`,
pointed at, not copied — ~900 B of the demo's own prose was removed for it), so editing a
poem changes both the split72 idle screen and the demo, and `idle_poem_plan.c` is now in
the shared `POLY_SRC` rather than split72's `SRC`. Two decisions that will be questioned again:

- ⚠️ **The key demo sends NO modifier, ever** — a shifted character goes out as its
  unshifted key (`H` → h, `(` → 9), and Tab is refused (`demo_host_usage()`), so an
  hours-long run cannot switch windows or fire a shortcut. Strokes are **counted, not
  sampled** from the highlight (`host_keys_tick()`): a slow housekeeping pass can step
  over an 85 ms press, and a typing test must not lose that character.

- ⚠️ **Demo mode must not change any behaviour outside demo mode** (the maintainer's
  rule, 2026-10-01). A suspend veto added so the demo would run with no host was
  reverted for breaking it.
- **No change is needed for a power-only demo**: stock firmware already runs on a USB-C
  charger or power bank (confirmed on hardware, 2026-10-01).
