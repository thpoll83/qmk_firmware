# PolyKybd v0.15.2 Dual-function keys & the Intl remap

## 0.15.0 — Dual-function keys say what they do ⌨️
A key that types a letter on a tap and holds a modifier now shows both.
- The tap legend stays where it always was; the held modifier appears as a small
  mark in the **bottom-right** corner, drawn with the modifier keycaps' own icons.
- Up to four marks stack (Ctrl · Shift · Alt · GUI), so `LCTL_T(KC_A)` reads as
  "a" with a Ctrl mark rather than as a mystery key.
- Configure them in PolyKybdHost's keymap editor — see
  https://www.polykybd.org/using/keymap-editor/

## 0.14.1 — Release builds are signed, or they fail ✍️
A release build that could not sign the image used to ship it unsigned.
- The build now fails instead. Nothing changes for locally-built firmware, which
  still asks for the A/ACCEPT keypress on the keyboard.

## 0.14.0 — Put another letter on a key 🔤
French needs `è é ê` at once — more forms than one key's picker can offer.
- Hold **Intl**, tap the remap key, tap the key to change, then tap the letter it
  should host. `e`, `q` and `j` can each carry a different accented `e`.
- Extended to the twelve punctuation keys, so `-` `=` `[` `;` `'` `,` `.` `/` and
  friends can host a letter too.
- Re-assign a key to its own letter to reset it; **Shift + remap** clears the lot.

## 0.11.4 – 0.13.0 — The Intl variation picker, finished 🔤
The accented-letter picker grew from a capped, fiddly thing into the layer it was
meant to be: twelve picker keys with paging and 14 slots per letter, covering 488
of 489 latin variations — the African hook letters, Irish séimhiú (Ḃ Ċ Ḋ Ḟ Ġ Ṁ Ṗ
Ṡ Ṫ), the Welsh and Guaraní letters, Romanian Ș ș Ț ț, and `Q`, all in the
resident font so they render with no font pack flashed. The layer now draws its
own legends instead of letting app overlays paint over them, the picker latches
(tap Ctrl to arm it, and that keycap inverts while armed), and the Intl key is a
hand-composed "Intl" tile in accented latin. Along the way: `Intl`+`i` produced
nothing, `Intl`+`Shift`+`I` typed lowercase, an empty slot emitted garbage, a
repeated letter dropped a variation, and Ctrl was masked so the picker could not
be opened at all — all fixed.

## 0.11.1 — Upstream QMK 0.33.13 ⬆️
Caught up with upstream QMK; measured performance-neutral on the test rig.

Plus maintenance releases 0.11.2, 0.11.3, 0.12.1–0.12.3, 0.13.1, 0.14.2–0.14.8,
0.15.1 and 0.15.2 🧹 — clearer font-pack and split-link diagnostics, tests,
tooling, CI, docs, and an internal reorganisation of the keyboard sources with an
equivalence check behind it.

Firmware and host both speak **protocol 12**; no protocol change in this range.
