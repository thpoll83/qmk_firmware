# PolyKybd v0.23.0 On-keyboard macros & handedness in flash

⚠️ **This release is protocol 17 — update PolyKybdHost at the same time.** The last
published host (v0.14.18) speaks protocol 16, so it will connect but leave the new
features switched off, and it predates the macro editor entirely.

## 0.23.0 — Handedness that survives an EEPROM wipe 🧭
A half can no longer come up on the wrong side because the settings store was lost.

- Handedness moved out of the emulated EEPROM into **its own flash sector**, where QMK's wear levelling cannot reach it. The old `EE_HANDS` marker was a single byte with no "unknown" state — a cleared store reads zero, and zero is a valid `right` — so losing the store didn't present as an error, it presented as a half quietly swapping sides.
- **A hub being interrupted was one wipe with three symptoms**: wrong side, RGB back on, dynamic keymap reset. An EEPROM flush now refuses to start when USB power is already going away, rather than beginning a write that takes tens of milliseconds on a rail with a few left.
- Every release now ships **`polykybd-handedness-left/right_v<ver>.uf2`** — 512-byte UF2s that write only that one sector, so a fresh board (or a half that ended up on the wrong side) is provisioned over BOOTSEL with no host app. The firmware `.uf2` stays handedness-neutral, so re-flashing never changes a side.
- The boot banner prints a `hand:` line, and the old `make …:uf2-split-left/right` targets now refuse to build instead of silently producing an image that sets no handedness at all.

## 0.22.0 — A settings layer that is safe and does what it says 🎛️
- **Restart and Boot could fire from blank keycaps.** Both are meant to stay hidden and inert until `KC_SETTINGS_MORE` reveals them; the legend half worked, the action half did not, so pressing an unlabelled key rebooted the board or dropped it into the bootloader. Reported as two unexplained reboots with no crash record — because nothing had crashed.
- The four **RGB effect presets** (Plain / Breathe / Rainbow / Swirl) drew a legend and did nothing. They are mapped onto real RGB-matrix effects now.
- The **value row read 39% at full brightness** — it divided by 255 while the value is capped at 100. Full now reads 100%, and one step is one percent.
- RGB starts at 20% brightness and 10% speed instead of QMK's full-and-half, and the row's legends are bigger.

## 0.21.0 — Record a macro on the keyboard 🎬
No host app: tap **REC**, pick a slot, type, tap REC again.

- Capture is the **USB report diff**, so a macro replays what the keyboard actually sent — after layers, mod-taps and combos have resolved.
- **M0–M15 live on the Utility layer** (Shift reaches M12–M15), and an untouched slot is not blank: it shows the **Mayan numeral for its own number** over the caption `Macro`. Base-20, so 0–15 is one glyph each, and it has a real glyph for zero.
- ⚠️ **F13–F24 lose their default home** to make room; assign them from the layout editor if you use them.
- The `GET_ID` reply now carries a **state generation**, so the host notices a change you made on the board within about a second instead of showing stale data.

## 0.20.0 — Unicode input mode, actually applied 🔤
- The host's unicode-mode push changed **only the keycap legend, not the mode**.
- A new **volatile apply** lets the host set a mode without storing it, which is what stops the Windows logon sequence writing the EEPROM twice and flickering the legend to end where it started.
- ⚠️ **Protocol 17** — host and firmware want updating together.

## 0.19.0 — Crash reports that survive the trip 🩺
- The **slave half's crash record was printed once**, so a single dropped console read lost it for good; it repeats now, and a pull that lands before the slave has archived no longer counts as done.
- A TEST-ONLY build flag can fault the board on purpose, which is how the fault path itself was finally proven on hardware.

Plus maintenance releases 0.19.1, 0.19.2, 0.20.1 and 0.23.1 🧹
