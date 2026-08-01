# PolyKybd v0.9.86 — All nine modifier overlays

## 0.9.85 — Every modifier combination gets its own keycap art 🎛️
Overlay images are no longer rationed per modifier: **Ctrl+Alt+Shift** and the
**GUI/Win** key finally show their own artwork instead of falling back to the
unmodified image.
- The overlay pool is now a flat bank of 600 images the host assigns freely, rather
  than a fixed 90 keys × 7 variants grid — which also hands **10.8 KB of RAM** back
  to the firmware (the DOOM build was down to *20 bytes* free).
- Artwork repeated across variants costs one slot, not one per variant.
- ⚠️ **Update the host too (PolyKybdHost 0.10.1+).** The protocol is unchanged at
  v11, so the app won't warn you — but the overlay storage changed, and an
  out-of-date host puts the higher modifier combinations on the wrong keycap image.
- 🎮 If you've installed the *other* thing: re-install its engine pack after
  flashing (`polyctl doom install-pack`). The old one is refused on purpose.

### Also in 0.9.85 — three overlay fixes 🩹
- An overlay could wipe a wide rectangle through the legend underneath it instead of
  tracing the icon.
- An overlay set that didn't reach the second half left one keycap on its plain
  legend until you switched apps and back — the halves now detect and repair that.
- Firmware release builds no longer fail silently: **v0.9.82 published with no
  downloadable files at all** because its build error was being discarded, which is
  why keyboards were told they were already up to date.

*Plus rig performance tooling and design docs in 0.9.83, 0.9.84 and 0.9.86. 🛠️*
