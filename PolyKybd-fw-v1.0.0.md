# PolyKybd v1.0.0 First-run tutorial

**No protocol change.** This release still speaks protocol 18, the same as host **1.3.4**, so either can be updated first. Update the host too if you want Braille's new round dots, because the host delivers them.

## 1.0.0 — The keyboard shows you around 🎓

On a board's first start, the Eden intro plays, and then the keyboard teaches itself in ten steps. It lights and rings each key it wants you to press, and the top-right keycap counts 1/10 to 10/10.

- **Letters, Shift, the whole board, then eleven layouts:** Greek, Arabic, Hebrew, Hindi, Thai, Japanese and Korean, plus Elvish, runes, Aurebesh and Braille. Each is shown on the keys only; your computer's layout never changes.
- **The Lang and emoji menus, Fn, Num and the Intl accent picker, pressed for real.** Nothing is typed into your open app.
- **Hold Esc for a second to skip.** To see it again, press Eden on the settings layer (behind More…).
- ⚠️ **Boards updated from an earlier version show it once after the update**, because they have never recorded it as seen.
- **The Lang and emoji menus fill in key by key** whenever you open a layer, a tab or a page, not only during the tutorial.
- **The context-menu key has a new icon**, and the Lang layer's region tabs use a larger font.

## 1.0.0 — Boot fixes 🩺

- **The right half could freeze at "100%" at boot** when Eden was due to play. It now waits until its boot has finished.
- **A boot that stalls after 63% now resets once and leaves a crash record**, which `polyctl crash show` can read. Before this, a stall there was a permanent hang with no record.

## 0.29.2 — A calmer trackpad 🖱️

- **Acceleration now builds up more gently.** Top speed is unchanged.
- **Lifting your finger no longer makes the cursor jump**, and the scroll dial now starts from a thumb that lands in a far corner.

Plus maintenance releases 0.29.0, 0.29.1 and 0.29.3 🧹
