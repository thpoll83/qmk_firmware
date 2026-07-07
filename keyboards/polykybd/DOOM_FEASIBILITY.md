# Can It Run Doom? — Feasibility Study

> **STATUS (2026-07-05): IMPLEMENTED — the game is playable on hardware** (41
> field-test rounds, v42). The study's verdict held on every axis: engine on
> the master's core1, twin-engine input-lockstep mirror on the slave (automap
> view), overlay pool borrowed at runtime, WHX in the resource flash, RGB
> matrix as the "speaker". The whole implementation, its hardware-test log,
> and the remaining milestones (DoomPack ship path, savegames) live in
> [`doom/README.md`](doom/README.md). The dev harness builds with
> `qmk compile -kb polykybd/split72 -km default -e POLYKYBD_DOOM=yes`; the
> engine snapshot (`f1f43171`) is vendored under `doom/engine/`. The
> "Option 2" executable-flash-pack ship path below is the active milestone —
> measured engine cost to remove from the firmware image: ~219 KB (603,884 B
> doom build vs 384,460 B normal build).


People keep asking. This page answers the question properly: **yes, the PolyKybd split72 can run Doom** — actually run it on the keyboard's own RP2040, rendered across the per-keycap OLEDs — and it can be done as a **hidden easter egg inside the normal firmware**, without shipping a separate "Doom edition" and without permanently sacrificing any feature. The trick is that the two scarce resources are already lying around: the **226 KB overlay RAM pool** can be borrowed while the game runs, and the flash map has **~2 MB of unused resource flash** for a compressed shareware WAD.

> **TL;DR verdict**
>
> **Feasible as an easter egg.** Game runs on the keyboard itself, viewport on the keycaps (5 keycap rows × 40 px = **exactly Doom's native 200-pixel vertical resolution**), input is, well, a keyboard. RAM comes from borrowing the overlay pool at runtime (no permanent cut), the WAD lives in the free upper 2 MB of resource flash, and the engine code fits the 2 MB firmware partition with room to spare. The split UART can never move pixels fast enough to animate the other half — so it doesn't: **both halves run their own copy of the engine in input-lockstep** (Doom's own multiplayer model), and the link carries only ~350 B/s of tic commands. Full 10-keycap-wide wall of Doom, each RP2040 rendering its own side.

## Prior art: Doom already runs on this exact chip

The heavy lifting has been done. [kilograham's rp2040-doom](https://github.com/kilograham/rp2040-doom) is a fully-featured Doom port for the RP2040 that fits the **entire shareware game — code and data — in 2 MB of flash and 264 KB of RAM**, rendering 320×200 at 60 fps to VGA with a 270 MHz overclock ([project write-up](https://kilograham.github.io/rp2040-doom/)). Two facts from its [RAM budget](https://kilograham.github.io/rp2040-doom/speed_and_ram.html) matter enormously for us:

- **~180 KB of its 264 KB RAM budget is display-related** (VGA scanout, beam-racing scanline machinery). The PolyKybd doesn't drive VGA — our "display" is a 1-bit tiled canvas — so that entire cost is replaced by a 64 KB 8-bit framebuffer plus a dither pass.
- The **game itself is small**: the Doom zone memory that started at ~700 KB in Chocolate Doom was squeezed to **~45 KB**, and the combined heap is ~58 KB. The engine, minus display, lives comfortably under 150 KB.

The 270 MHz overclock exists to hit 60 fps *while racing the VGA beam*. We need neither: a keycap-matrix Doom at 10–20 fps on the stock 133 MHz clock (with both cores — core1 is idle during a game) is a realistic target. rp2040-doom is GPL-2, same as QMK — licence-compatible.

## Hardware inventory (what we have to work with)

| Resource | Value | Source |
|---|---|---|
| CPU | RP2040, 2× Cortex-M0+ @ 133 MHz (`USE_CORE1` already enabled) | `config.h` |
| SRAM | 264 KB total | RP2040 |
| — of which overlay pool | **226,800 B** — `overlays[630][360]` (90 slots × 7 variants × 360 B) | `base/overlay.c`, `config.h` |
| — everything else (QMK + ChibiOS + USB + stacks) | ≤ ~43 KB (it all fits in 264 KB today) | arithmetic |
| Flash | 8 MB QSPI, partitioned | `base/fw_staging.h` |
| — firmware partition | 2 MB, ~0.76 MB used → **~1.2 MB free** | linker `flash1` |
| — staging partition | 2 MB (0x200000–0x400000) | `fw_staging.h` |
| — resource region | 4 MB (0x400000–0x800000): first 2 MB = font-pack slots, **upper 2 MB unused** (minus the 8 KB EEPROM journal at the very top) | `fontpack_layout.h`, wear-leveling config |
| Keycap displays | 72× SSD1306-class, 72×40 px visible (128×64 controller), 1-bit | `base/disp_array.c` |
| Display bus (per half) | Shared 10 MHz SPI + 5 shift registers for chip-select (36 displays per half) | `split72/config.h`, `base/shift_reg.c` |
| Split link | Full-duplex PIO UART, **230,400 baud** | `halconf.h`, `serial_usart.h` |
| Status OLED | 128×64 (master half) — free for a HUD | `split72/status_oled.c` |
| Input | 72 keys. It's a keyboard. | — |
| Sound | None. RGB matrix (72 LEDs) as damage-flash/muzzle-flash substitute | — |

## Challenge 1 — the display: 72 tiny windows, one wall

### Geometry: the keycaps are accidentally Doom-shaped

Doom renders 320×200. A keycap column is 72 px wide and a keycap row is 40 px tall, so **five keycap rows stack to exactly 200 px** — Doom's native vertical resolution, 1:1, no scaling. Each split72 half has 36 displays in 5 rows, so a 5-wide × 5-row block of one half forms a 360×200 canvas that a centred 320×200 frame drops into with 20 px side margins — and the full keyboard is a ~10-keycap-wide × 5-row wall once both halves render (see the lockstep section below). Each keycap displays its 72×40 crop of the frame.

The physical bezels between keycaps mean the image is seen "through a window blind" — segments of the scene with gaps. That's not a bug; that's the entire aesthetic appeal of Doom-on-a-keyboard. (For legibility experiments, Doom's built-in low-detail mode and a smaller `SCREENWIDTH` are tuning knobs; rp2040-doom keeps these paths.)

### Bandwidth: the master half is fast enough — comfortably

The current `kdisp_send_buffer()` transmits the controller's full 128×64 RAM (1,024 B + command bytes) per keycap: ~0.85 ms at 10 MHz SPI, ×36 keys ≈ 31 ms → a ~30 fps ceiling *with the existing, unoptimised path*. A game-mode blitter that sets the column window to the 72 visible columns and sends only the 5 used pages (360 B + commands ≈ 0.31 ms) updates the **25-key viewport in ~8 ms** — the display bus supports 60+ fps; the renderer, not the bus, sets the frame rate. Shift-register selects are tens of µs, noise.

Dither cost: 64,000 pixels through a 4×4 Bayer ordered-dither + bit-pack is ~5 ms at 133 MHz — pipelinable on the other core. (Floyd–Steinberg, as used offline by `fontconvert`, is too serial/expensive per frame; ordered dither also avoids frame-to-frame crawling.)

Realistic frame-rate estimate: **10–20 fps** on stock 133 MHz with the game on one core and dither+blit on the other. rp2040-doom does 60 fps at 270 MHz *including* VGA scanout, so this is conservative. A runtime overclock in game mode is a stretch goal, not a requirement (USB runs off the separate 48 MHz PLL, but SPI/UART dividers would need recalculating — not worth it for v1).

### The slave half: pixels can never cross the link…

Every keycap on the slave half is reachable only through the split UART. One half-frame is 36 × 360 B = 12,960 B; at 230,400 baud (≈23 KB/s line rate, less after transaction framing + CRC32 + ACKs) that is **~0.7–1 s per frame — about 1 fps**. Even at 460,800 (`SELECT_SOFT_SERIAL_SPEED 0`) it's ~2 fps. RLE helps little: dithered game frames are noise-like, the worst case for RLE.

So pixels don't cross the link. **Inputs do.**

### …so run Doom on both halves in lockstep, and transfer only the input

Each half of a split72 is the *same hardware*: its own RP2040 (two cores), its own 8 MB flash, its own 36 displays on its own 10 MHz SPI bus, its own status OLED. Instead of one half rendering and starving the other through a 23 KB/s straw, **both halves run an identical copy of the engine and simulate the same game in lockstep; the split link carries only tic commands.**

This is not a novel trick that needs inventing — **it is vanilla Doom's own multiplayer architecture.** Doom netgames never exchange game state: every peer runs the full deterministic simulation and peers exchange only each player's `ticcmd_t` (~10 bytes: forward/side move, turn angle, buttons) per 35 Hz tic. Demos work the same way — a recorded input stream replayed through the deterministic engine reproduces the identical game, byte for byte (the RNG is a fixed 256-entry table, no wall-clock anywhere in the simulation). rp2040-doom deliberately preserves demo compatibility, i.e. this determinism survives its optimisations. We're just running a 2-node "netgame" where both nodes happen to be controlled by the same player and bolted into one chassis.

**Protocol.** QMK's split transport already delivers the slave's key matrix to the master every scan, so the master sees all input regardless of which half a key sits on. Per tic, the master builds the consolidated `ticcmd`, then sends `(tic_number, ticcmd)` to the slave over a new split transaction (`USER_SYNC_DOOM_TIC`, alongside the existing 16 user transaction IDs) — CRC32-validated and retried like every other sync. Bandwidth: ~10 B × 35 Hz ≈ **350 B/s**, ~1.5 % of the link. Following Doom's own netcode, each message carries the last 3–4 ticcmds redundantly, so a dropped transaction costs nothing; the slave consumes tics strictly in sequence, and a genuine gap simply stalls its simulation one tic until the retry lands (a 28.6 ms hiccup on one half, self-healing, no desync possible). Game entry/exit and menu state need no special sync — menus are input-driven too, so lockstep covers them; only the "enter game mode" flag itself rides the existing state sync.

**Rendering split.** The combined canvas is now a 10-keycap-wide × 5-row wall. Each half renders **only its own columns** of the shared 320×200 view — and Doom's renderer is column-major (walls and sprites are drawn as vertical column strips), so restricting to a 160-column range is natural; splitting frame rendering by columns across processors is exactly what rp2040-doom already does between the RP2040's two cores. Each half therefore renders *half the pixels of the single-half plan with the same silicon* — the fps estimate improves rather than degrades, and each half still has its second core free for dither + blit. Simulation stays fixed at Doom's 35 Hz tics; each half presents its most recently completed frame, so worst-case the two halves briefly show tic *N* and *N+1* — a 28.6 ms skew across a physical bezel gap, imperceptible.

**The seam caveat.** Spanning both halves puts the physical gap between the two keyboard halves in the middle of the view — right where the weapon sprite and aim point live. Three options, all cheap to try once the blitter exists: accept it (the shotgun splitting across the chassis gap has a certain honesty), bias the 320-px window a couple of keycaps toward one half, or run the seam through a widened view (`SCREENWIDTH` > 320 costs render time linearly — the budget exists at 160 columns/half). Worth deciding on real hardware, not on paper.

**What lockstep requires of the platform** — all of it already true or already-solved:

| Requirement | Status |
|---|---|
| Identical binary on both halves | Already the shipping model (one image, role decided at runtime) |
| WAD present on **both** halves' flash | The `fw_staging` HID flow already bridges font-pack flashes to the slave — the `doomwad` slot reuses that path unchanged |
| Overlay-pool borrow on the slave too | Symmetric — same pool, same handoff, triggered by the synced game-mode flag |
| Deterministic simulation | Doom's defining property (demos, netplay); keep simulation strictly tic-driven, rendering local |
| Reliable in-order tic delivery | CRC32 + retries exist; add sequence numbers + 3-tic redundancy à la Doom netcode |

Each half's own 128×64 status OLED becomes a per-half HUD — health and face on the left, ammo and arms on the right, like an arcade cabinet.

## Challenge 2 — RAM: borrow the overlay pool, don't cut it

The user-visible suggestion behind this study was "reduce RAM usage by cutting the MRU overlay buffer." Good instinct, better news: **it doesn't need to be cut — it can be borrowed.**

The overlay pool `overlays[630][360]` (226,800 B, `base/overlay.c`) is the single dominant RAM consumer — ~84 % of the chip. It holds host-pushed keycap images, and it is *entirely reconstructible*: the host re-sends overlays on every app switch, and the firmware already has the machinery to blank and repopulate it (`reset_overlay_buffers()`, `reset_overlay_usage()`, and the host-side resend that follows any reconnect / font-pack wipe).

So the easter egg does a **runtime handoff**: entering game mode stops overlay processing, takes the pool (`get_overlays()` already exposes the raw array) and hands it to the game as its zone-memory arena; leaving game mode memsets it, resets usage/mapping, and nudges the host to resend — a code path that, from the host's perspective, looks like a reconnect. Zero permanent feature loss, zero RAM cost in normal operation.

### Game-mode RAM budget (inside the 226.8 KB pool)

| Consumer | Estimate | Notes |
|---|---|---|
| Framebuffer, 320×200 @ 8 bpp indexed | 64,000 B | palette→luma→dither at blit time; no 32-bit buffer ever exists |
| Doom zone memory + heap | ~60,000 B | rp2040-doom runs levels in ~45 KB zone + ~13 KB heap |
| Engine static/BSS beyond QMK's | ~40,000 B | level pointers, render state; most lookup tables stay in flash (XIP) |
| Dither scratch + per-key 1-bpp staging | ~4,000 B | one keycap tile (360 B) + Bayer LUT |
| **Headroom** | **~59,000 B** | ~26 % margin for the estimate being wrong |

The margin is real: kilograham's *entire* non-display RAM usage is ~85 KB, and we're granting the equivalent components ~100 KB before headroom. If it gets tight, game mode can additionally scavenge the RGB-effect buffers and the 1 KB display scratch buffer.

> **What must keep running**
>
> QMK's core loop cannot stop: USB keep-alive, matrix scan (that's the game controller!), and the split transport (slave keys) all stay live on core0. The clean split is **core0 = QMK + input + SPI blit, core1 = the game** (core1 currently only does RLE decompression, which is idle in game mode — but note the `cpsid i` workaround at `core1_entry` and the known SIO-FIFO IRQ quirks documented in the firmware's CLAUDE.md before redesigning core1's job dispatch). EEPROM writes must be suppressed during play (flash erase stalls both cores' XIP) — the `fw_staging_fw_up_active()` suppression pattern already exists for exactly this.

## Challenge 3 — flash: "store as much as possible in flash" — there's room

Goal stated up front: keep RAM tiny by keeping everything possible in flash. That is precisely rp2040-doom's architecture (compressed WAD read via XIP, tables const-in-flash), and the PolyKybd flash map has the space:

```
0x000000 ─ 0x200000  firmware (2 MB linker cap)     ~0.76 MB used
                     → dev build: engine ~0.3–0.5 MB fits (image ~1.1–1.3 MB) ✓
                     → ship build: only a ~2–4 KB pack loader (see next section) ✓
0x200000 ─ 0x400000  fw-update staging              untouched ✓
0x400000 ─ 0x600000  font-pack slots (2 MB window)  untouched ✓
0x600000 ─ 0x7FE000  2,088,960 B FREE               → DoomPack: engine code + doom1.whx (1,800,344 B) ✓
0x7FE000 ─ 0x800000  EEPROM wear-leveling journal   untouched ✓
```

- **Engine code** compiles into the normal firmware image. rp2040-doom's code is a few hundred KB; the partition has ~1.2 MB free, and the existing link-time guard (`FW_STAGING_OFFSET == flash1 length`) means an overweight build fails loudly at link, never silently.
- **The WAD** ships in rp2040-doom's compressed format, and the numbers are now exact: the repo bundles a pre-converted **`doom1.whx` of 1,800,344 bytes** (from the 4,196,020-byte shareware `DOOM1.WAD`), and the free window at `0x600000` is 2,088,960 bytes — the game data fits with ~288 KB to spare. Since the *whole* rp2040-doom Pico image (code + WHX) fits 2 MB, its code is ≤ ~296 KB; ours shrinks further by deleting what we don't have hardware for (VGA scanout, the OPL2 music emulator, sound effects, network) — so code + WHX in one pack squeezes into the window with a small margin, and if it ever doesn't, the engine falls back into the firmware partition's ~1.2 MB and only the WHX stays in the resource region.
- **Delivery over HID, no bootloader trip:** the font-pack machinery (`fw_staging` FONTPACK target, `BEGIN/CHUNK/COMMIT` cmds `0x50`–`0x52`) is a general "stream a blob into a resource-region slot" pipeline. A `doomwad` pseudo-slot at `0x200000` relative offset (i.e. flash `0x600000`) reuses it wholesale — `polyctl doom install DOOM1.WHD`. The WAD only needs to reach the **master** (the game runs there), so the slave-bridge half of the flow can be skipped. Like the font pack, the WAD survives firmware updates (different region) — install once.
- No WAD flashed → the trigger does nothing (or shows a one-keycap "NO WAD" gag). Same graceful-degradation pattern as the `fantasy` font bundle.

Savegames: skip in v1 (EEPROM journal is 8 KB and shared; a dedicated 4 KB flash sector later if anyone insists).

## Keeping the shipping image lean: `#ifdef` vs. an executable flash pack

Linking the engine into the main image costs ~0.3–0.5 MB of the firmware partition. It *fits* (0.76 → ~1.2 MB of 2 MB), but that headroom is the same headroom language/font growth lives on, and it's dead weight in every keyboard that never triggers the egg. Two ways to not pay it:

### Option 1 — `#ifdef POLYKYBD_DOOM`, separate sources

All game code in its own files, compiled only when the flag is set. Zero bytes, zero risk in the default build — but then the easter egg isn't *in* the shipping firmware anymore; it's a special build, which is really the "cartridge" option wearing a moustache. Right answer for **development** (full symbols, normal debugging, one link), wrong answer for shipping a discoverable egg.

### Option 2 — a flash pack that contains executable code (yes, this works)

The question was: *can the pack really contain code — jump to it and it runs?* On the RP2040, **yes, structurally**: the entire 8 MB QSPI flash is XIP-memory-mapped at `0x10000000`, and the Cortex-M0+ executes from any XIP address exactly as it executes the firmware itself (which also lives in that window). The resource region at flash offset `0x600000` **is** executable memory at `0x10600000` — `fontpack.c` already dereferences this region for font data; code is the same bus, plus the 16 KB XIP cache. It is *not* quite "point the PC at it and go" — three conditions make it work, all standard:

1. **Linked for its address.** No position-independent-code contortions (ROPI/RWPI on M0+ GCC is misery) — none are needed, because the slot address is *fixed by the flash map*. The engine links as its own tiny ELF at `0x10600000` with a dedicated linker script. The slot starts with a header: magic (`PlyX`), ABI version, entry-point offset, `.data`/`.bss` layout, expected RAM base, CRC32 — the same shape as a `PlyF` font bundle, one field richer.
2. **Its RAM has to exist at a known address.** Doom's globals (`.data`/`.bss`) need a home. The firmware pins the borrowed overlay pool at a **fixed linker address** (dedicated section), and the pack's linker script places its RAM there. On entry the loader (or the pack's own init) copies `.data` from flash and zeroes `.bss` — a five-line crt0. The header's *expected RAM base* is verified against the firmware's actual pool address before the jump, so an ABI mismatch refuses loudly instead of corrupting memory.
3. **Firmware services go through a call table, not symbols.** Firmware symbol addresses move every build, so the pack never links against them. The firmware passes one struct of function pointers — `blit_tile()`, `status_oled()`, `next_ticcmd()`, `millis()`, `log()` — and the pack exposes one entry point: `int doom_main(const doom_api_t *api)`. The jump is a C function-pointer call (entry address with the Thumb bit set). Version field in both directions gates drift.

What this buys: the shipping firmware carries only the **loader + API table + trigger, ~2–4 KB**. Code and WAD travel together as one pack, flashed to **both halves** through the same bridged `fw_staging` flow as a font bundle, CRC-validated at load, version-advertised in `GET_ID` like the v6 bundle block. No pack flashed → the trigger shrugs. A corrupt pack fails CRC/ABI check → the loader declines. And the 2 MB firmware-partition headroom stays untouched for its real job.

Hot-path caveat: XIP execution through the 16 KB cache is fine for the bulk of the engine, but the innermost render loops will want to live in RAM — the pack copies its own hot functions into its arena at init, the same trick as pico-sdk's `__not_in_flash_func`, no firmware involvement.

**Recommendation: both, in sequence.** Develop and validate under `#ifdef POLYKYBD_DOOM` (monolithic image on a dev keyboard — debuggable, no ABI layer in the way). Ship as the executable pack once the API surface between engine and firmware has stopped moving. The `#ifdef` build stays in-tree permanently as the debug harness; the pack is just the same objects re-linked at `0x10600000`.

## Easter egg vs. custom firmware

Three ways to package it:

| | **A. Easter egg in the shipping firmware** (recommended) | B. "Cartridge" firmware | C. Host-streamed demo |
|---|---|---|---|
| What it is | Engine linked into normal firmware; hidden trigger; borrows overlay RAM at runtime | Dedicated Doom-only image, temporarily flashed via the existing HID staging+apply, reflash QMK after | PolyKybdHost runs Doom on the PC and streams dithered frames as ROI overlay updates (cmds `0x12`/`0x13`) |
| Firmware change | Large (the port), but zero *architectural* change — same partitions, same protocols. Shipping image grows by only the ~2–4 KB pack loader (engine code ships in the DoomPack, see above) | The port, minus all coexistence constraints (all 264 KB RAM, no QMK) | **None at all** |
| The "wow" factor | Maximum — *the keyboard* runs Doom, discoverable in the wild | High, but it's visibly a stunt cartridge, and the keyboard stops being a keyboard | Low-medium — the keyboard *displays* Doom (~3–5 fps over the 64 KB/s HID channel, master half only) |
| Risk to daily use | A mode flag + a loader that declines bad packs; no image-size cost in the pack model | None to the shipped firmware | None |
| Effort | High | High (same port) + host flashing UX | **A weekend** |

**Recommendation: A**, with **C** first as a cheap teaser. C needs no firmware work at all and validates the whole visual concept (viewport mapping, dithering, the window-blind look) before anyone commits to the port. B is what A degrades to only if the engine genuinely cannot coexist with QMK's ~43 KB — the numbers above say it can.

The easter-egg trigger should be cheap and undiscoverable-by-accident: typing **`IDDQD`** with the language layer held, or holding both encoders through a boot splash. Entering game mode: swallow all key events in `process_record_*` (feed them to Doom's `D_PostEvent` instead of the OS — the host must see *no* keystrokes while fragging), freeze overlay/HID overlay commands (NACK or defer), park the RGB matrix (or repurpose it: red flash on damage, white on pickup — the only "sound" we have), status OLED becomes the HUD. Exit: any long-press of a dedicated key → restore pool, refresh displays, resume normal service.

## Implementation prerequisites: setting up the cloud container

Audited 2026-07-02 in a live Claude Code on the web session. The container's *tooling* needs nothing exotic — the blockers are all **network policy**. Three source inputs were unreachable under the restricted policy: the QMK submodules (via `codeload.github.com`), the `kilograham/rp2040-doom` source, and a shareware `DOOM1.WAD`.

### Step 1 — environment network access (the one setting that matters)

In [claude.ai/code](https://claude.ai/code): click the **cloud icon showing the environment name** → in the selector, hover the environment → click the **settings icon** on the right → set the **Network access** selector to **Trusted**. (There is no separate Environments page.)

**Trusted** is sufficient — no Custom domains needed: its default allowlist includes `github.com`, `api.github.com`, `codeload.github.com`, `raw.githubusercontent.com` and `objects.githubusercontent.com` ([documented list](https://code.claude.com/docs/en/claude-code-on-the-web#default-allowed-domains)), which covers the QMK submodule tarballs, the rp2040-doom source, and the WAD (fetched from a GitHub mirror via `raw.githubusercontent.com` — verified working even under the restricted policy).

Two behaviours to expect: the change applies to **new sessions only** (a running container keeps its policy), and editing allowed network hosts **invalidates the environment cache**, so the first new session re-runs the setup script.

### Step 2 — environment setup script (cached, runs once)

Paste this into the environment's **Setup script** field. Its output is snapshotted into the environment cache, so every later session starts with the toolchain and submodules already on disk:

```bash
# ARM toolchain for the firmware build
sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi

# QMK submodules at their pinned SHAs, via the codeload tarball endpoint
# (git clone of qmk/* is not served by the session git proxy — tarballs are the
# documented path; adjust the cd if qmk_firmware sits elsewhere in the layout)
cd qmk_firmware
git submodule status | while read -r line; do
  sha=$(echo "$line" | awk '{print $1}' | sed 's/^[-+]//'); path=$(echo "$line" | awk '{print $2}')
  case "$(basename "$path")" in
    chibios) repo=ChibiOS;; chibios-contrib) repo=ChibiOS-Contrib;;
    lufa) repo=lufa;; printf) repo=printf;; pico-sdk) repo=pico-sdk;; *) continue;;
  esac
  mkdir -p "$path"
  curl -sSL "https://codeload.github.com/qmk/$repo/tar.gz/$sha" | tar xz -C "$path" --strip-components=1
done

# rp2040-doom source (the engine to port) — rp2040 is its main branch
mkdir -p ../rp2040-doom
curl -sSL "https://codeload.github.com/kilograham/rp2040-doom/tar.gz/refs/heads/rp2040" \
  | tar xz -C ../rp2040-doom --strip-components=1

# Shareware DOOM1.WAD v1.9 from a GitHub mirror (works even under the
# restricted policy — raw.githubusercontent.com is allowed there too).
# Verified 2026-07-02: 4,196,020 bytes, the canonical 1.9 shareware IWAD.
# NOTE: the pre-converted doom1.whx (1,800,344 B) already ships at the
# rp2040-doom repo root — the raw WAD is only needed to re-run whd_gen.
curl -sSL -o ../rp2040-doom/doom1.wad \
  "https://raw.githubusercontent.com/Akbar30Bill/DOOM_wads/master/doom1.wad"
echo "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771  ../rp2040-doom/doom1.wad" \
  | sha256sum -c
```

### Step 3 — first-session verification checklist

Run these before starting any porting work; each should succeed in under a minute:

```bash
# 1. Toolchain present
arm-none-eabi-gcc --version                    # expect 13.x

# 2. Submodules populated (all five non-empty)
ls qmk_firmware/lib/chibios/os qmk_firmware/lib/pico-sdk/src

# 3. Firmware builds end-to-end
pip install qmk && export QMK_HOME=$PWD/qmk_firmware
qmk compile -kb polykybd/split72 -km default   # expect a .uf2, exit 0

# 4. Engine source + game data present and intact
ls rp2040-doom/src/doom rp2040-doom/src/whd_gen        # engine + converter tool
stat -c %s rp2040-doom/doom1.whx                        # expect 1800344 (bundled)
echo "1d7d43be501e67d927e415e0b8f3e29c3bf33075e859721816f652a526cac771  rp2040-doom/doom1.wad" | sha256sum -c
# (DOOM1.WAD v1.9 shareware: 4,196,020 bytes,
#  md5 f0cefca49926d00903cf57551d901abe — matches doomwiki.org/wiki/DOOM1.WAD)
```

The build in step 3 is the gate: once it passes, everything the porting work needs is in the container. What the container can *never* do: put pixels on real keycaps — first-light needs the physical keyboard (the HIL rig verifies protocol liveness, not the picture).

## What it costs (effort estimate)

| Work item | Size |
|---|---|
| **Tier 0 (teaser):** host-side doomgeneric → dither → ROI-stream to master half; no firmware change | ~1 weekend |
| Port rp2040-doom's core out of pico-sdk/CMake into the QMK/ChibiOS build (the real work — its runtime tricks, custom linker assumptions, and build system are the bulk of the risk) | weeks, the long pole |
| Replace its VGA/audio backends with the keycap blitter (window-addressed SSD1306 path + Bayer dither) + status-OLED HUD | days |
| Lockstep tic sync: `USER_SYNC_DOOM_TIC` transaction, sequence numbers + redundant-tic window, column-range render restriction per half | ~1 week (protocol is small; determinism validation is the real test — run both halves side by side and let them diverge or not) |
| Overlay-pool handoff + game-mode flag + input swallow + exit/restore path | days |
| WHD delivery: `doomwad` staging slot + `polyctl doom install` | 1–2 days (machinery exists) |
| Executable-pack ship path: pack linker script (`0x10600000`), `PlyX` header + CRC/ABI checks, loader + `doom_api_t` call table, pinned RAM section for the pool | ~1 week (after the `#ifdef` build is stable — same objects, re-linked) |
| HIL/rig: keep-out — assert game mode is unreachable without the trigger; GET_ID stays answerable in game mode | 1 day |

**Honest overall rating: ambitious but genuinely feasible.** Every hard resource constraint checks out with margin on paper; the schedule risk is concentrated in one place (extracting rp2040-doom from its native build), not spread across unknowns.

## Verdict

- **Can it run Doom?** Yes — across **both halves' keycaps** at native vertical resolution, each half simulating and rendering its own side in input-lockstep (Doom's own netgame model), 10–20 fps estimated with headroom to spare since each RP2040 only renders 160 columns. WASD included by definition.
- **Custom firmware needed?** No — an easter egg is the right call, and it costs the shipping image almost nothing: the engine ships as an **executable flash pack** (XIP-linked at `0x10600000`, entered through a versioned API table), leaving only a ~2–4 KB loader in the firmware. The `#ifdef POLYKYBD_DOOM` monolithic build exists alongside it as the development/debug harness. Game mode *borrows* the 226.8 KB overlay pool at runtime instead of permanently cutting the MRU/overlay feature — nothing is lost when you're not playing.
- **"Store as much as possible in flash"** — taken to its logical conclusion: not just the WHD WAD but the **engine code itself** lives in the unused upper 2 MB of the resource region (installed once over HID like a font-pack bundle, to both halves), tables const-in-XIP, RAM reserved for the zone arena and one 64 KB framebuffer.
- **The split link is not a limit — it's a design input.** Pixels can never cross it (~1–2 fps ceiling), so none do: both halves run identical engines and the link carries ~350 B/s of tic commands, exactly as Doom's multiplayer has done since 1993. The staged fallback if lockstep validation surprises us: master-half-only viewport with static art on the slave, which is also the natural first milestone before the tic sync lands.

Sources: [rp2040-doom (GitHub)](https://github.com/kilograham/rp2040-doom) · [RP2040 Doom write-up](https://kilograham.github.io/rp2040-doom/) · [Speed & RAM chapter](https://kilograham.github.io/rp2040-doom/speed_and_ram.html) · [Hackaday coverage](https://hackaday.com/2022/03/15/doom-comes-to-the-rp2040/)
