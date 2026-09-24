# PolyKybd

Project progress is documented on https://ko-fi.com/polykb

Development version of PolyKybd, which uses OLED displays in its keycaps.
Hardware info at https://github.com/thpoll83/PolyKybd

Keyboard Maintainer: thpoll83
Hardware Supported: RP2040

# Build Notes

## Keyboard Variants

- `split72` is the current hardware (HW rev2 and up) iteration of `wave` with slight differences. Only `split72` is actively developed.
- `split42` is the 42-key (CRKBD-footprint) variant. **It was previously named `corne42` and was renamed to `split42` in 2026-06** — same hardware, same `LAYOUT_crkbd` footprint, same USB PID. If you have an older checkout or external reference to `polykybd/corne42`, use `polykybd/split42` instead.

> **Shared keymap logic** — both variants compile the same `poly_keymap.c` at the
> keyboard level (rendering, HID/overlay handling, language selection, idle/suspend,
> split sync, firmware-update state machine). Each variant's
> `keymaps/default/keymap.c` carries only its data (`keymaps[]`, `encoder_map[]`,
> and — RGB variants only — `g_led_config`). Variant differences resolve at compile
> time via `polykybd.h` (which pulls the active variant header), the per-variant
> `POLY_DISP_ROW_*` / `POLY_SPLASH_*` macros, and `RGB_MATRIX_ENABLE` /
> `POINTING_DEVICE_ENABLE` guards. Add a language once (in `poly_keymap.c` via cog)
> and both keyboards pick it up.

### Code generation

Please be aware that the code makes use of cog to generate some code parts. You can find cog here:

[https://github.com/nedbat/cog](https://github.com/nedbat/cog)

Please run `run_cog.sh` in the keymap folder of your choice.

## Clean

`make clean`
or
`rm -rf .build`

## Build

Replace `<variant>` with `split72` or `split42` (the 42-key board, formerly `corne42`):

`make polykybd/<variant>:default`
`qmk compile -kb polykybd/<variant> -km default`

For example:

`qmk compile -kb polykybd/split72 -km default`
`qmk compile -kb polykybd/split42 -km default`

## Flash

`make polykybd/<variant>:default:flash`

For example:

`make polykybd/split72:default:flash`

Put the half in BOOTSEL first so it mounts as a drive. **Both halves take the same
image** — which one is left is decided by the handedness stamp below, not by the
build, so no *release* carries a per-side firmware target. (A one-off per-side
provisioning build is one of the two ways to set the side; see below.)

To update a keyboard that is already running, PolyKybdHost flashes the `.bin` from a
release over HID instead, with no BOOTSEL and no cable swapping.

### Handedness — which half is left

(eg. when you flash for the first time)

Each half has to know its side, or both come up as `right` and the split link never
forms. PolyKybd keeps that in a flash sector of its own (`FW_HAND_STAMP_OFFSET`, see
[`base/hand_stamp.h`](base/hand_stamp.h)) rather than in the emulated EEPROM — so it
survives an EEPROM loss.

**Two ways to set it.** Both end in the same `stamp_write()`, so the result is
identical; they differ only in whether you build anything:

1. **Build a per-side image** and flash it over BOOTSEL. Needs a toolchain, needs no
   host app, and works on a half that cannot talk to anything.
2. **Flash the ordinary unified image, then set the side from the host app.** Needs no
   build and fixes both halves in one action — also the repair route when a half turns
   up on the wrong side.

⚠️ The 512-byte `polykybd-handedness-{left,right}_vX.Y.Z.uf2` files are a **third route
that does not work** — no release publishes them any more, and the copies that went out
with v0.23.0, v0.25.0 and v0.27.1 should be deleted. See *The stamp UF2, and why it is
not shipped* below.

#### 1. Build a per-side provisioning image

Build a one-off image that stamps the side itself, and flash it as an ordinary firmware
`.uf2` — a normal multi-block image, so it avoids fault (2) below entirely:

```bash
qmk compile -kb polykybd/split72 -km default -e POLYKYBD_FORCE_HAND=left     # or =right
```

It calls `poly_hand_force_stamp()` from `keyboard_pre_init_user()`, through the same
`stamp_write()` the host's HID cmd 25 uses, and only when the sector does not already
name that side — so no page is burned per boot. Confirm with the banner's `hand:` line,
then **flash the normal image back**: while a forcing image is on the half, every boot
that disagrees rewrites the side, which is precisely what the neutral release `.uf2`
avoids so a firmware update can never flip a half. The stamp is in its own sector and
survives the reflash.

#### 2. Flash the unified image, then set the side from the host

Both halves take the **same** release image — nothing in it names a side. Flash it to
both halves, then in PolyKybdHost's tray menu:

> **Maintenance → Fix Left/Right Side → "Connected half is LEFT (other is RIGHT)"**
> (or RIGHT)

or, from a terminal — the same thing, and the only route on a headless daemon where
there is no tray to click (**PolyKybdHost 1.1.0+**):

```bash
polyctl handedness left        # or right
```

Either way the side you name is the **connected** half, because a HID command only ever
reaches the one holding the USB cable. That sends HID cmd 25: the master stamps its own
side, pushes the complement to the slave over the split link, and both halves reboot
onto it — about 10 s, no replug, no BOOTSEL.

⚠️ The keyboard resets right after receiving cmd 25 and sends no reliable reply, so
neither route can tell you it *applied* — only that it was sent. Confirm from the boot
banner below.

This is the route to reach for when a half is simply on the wrong side. It needs the
split cable in place, since the slave is set over it.

#### Reading the side back

The boot banner names the source, so a half on the wrong side is one line to diagnose:

```
   hand: LEFT (flash stamp)
   hand: RIGHT (stamped from EEPROM)     <- first boot after the update; adopted the old value
   hand: RIGHT (EEPROM, UNSTAMPED)       <- never provisioned, set it
```

⚠️ A half whose EEPROM was wiped comes up with `reset=YES` in the `keymap:` banner line —
the stored keymap and the macros are back at defaults and want re-applying from the host.
The stamp itself is unaffected; that is the whole point of it living outside the
wear-levelling store.

⚠️ `make …:uf2-split-left` / `-right` are **refused** by `rules.mk`. They are QMK's
`EE_HANDS` targets and only add `-DINIT_EE_HANDS_LEFT`, which has an effect solely inside
the `EE_HANDS` branch of `is_keyboard_left_impl()` — and PolyKybd does not define
`EE_HANDS` ([`config.h`](config.h) says why). Unrefused they would build a perfectly good
image that sets no handedness at all.

#### The stamp UF2, and why it is not shipped

Because the stamp lives in an ordinary flash sector rather than the emulated EEPROM, a
512-byte UF2 can write it directly with no host app and no toolchain — one pair covering
both `split42` and `split72`, touching only that one 4 KB sector. That was the intended
first-flash route. It is **off** on two separate faults:

1. The v0.23.0, v0.25.0 and v0.27.1 files declared the record's own 12 bytes as the UF2
   `payloadSize`, and the RP2040 bootrom ignores any block that does not declare exactly
   256. Nothing was written, the download never completed, and the half sat in BOOTSEL
   with the drive still mounted instead of restarting. Fixed in the tool; `--verify` now
   rejects those files, so it tells you which kind you have.
2. With that fixed the write completes — the drive unmounts, which only happens once the
   bootrom's `safe_reboot()` fires — and the half then **does not boot**: no RGB, no
   displays, no console, and the bootrom's drive back on every power cycle, surviving
   30 s unpowered. Both halves, both sides. Re-flashing the firmware `.uf2` recovers it
   every time. **Narrowed twice by measurement, and NOT understood.** One
   already-provisioned half did not reproduce it — the fixed file applies and boots: the
   banner reports `slot=0/1 writer=0x55`, which is the bootrom's own signature
   (erase-then-program leaves exactly one record at page 0) carrying this tool's marker
   byte. A provisioning image writing the *same record* to the *same sector* through
   `stamp_write()` also boots. So neither the record's content, nor the side change, nor
   anything in handedness resolution is the cause. Ruled out the same way: a stale
   `.ram0.bootloader_magic` double-tap flag, an invalid boot2, and any software
   `reset_usb_boot()`, which has no boot-time caller. What remains is a single
   unreproduced event, from a session with two confounders found later: a second
   single-block UF2 in one BOOTSEL session is silently dropped (below), and the banner
   then could not tell an applied record from a pre-existing one. The original field
   report is not a second data point — it used a `payloadSize=12` release file, which
   writes nothing, so a half left in BOOTSEL is precisely the expected result.

⚠️ **Two single-block UF2s in ONE BOOTSEL session: the second is silently dropped.**
`vd_reset()` clears the bootrom's transfer state only on a USB reset, and its
written-blocks bitmap is re-cleared only when an arriving block's `num_blocks` differs
from the current transfer. Every stamp file has `num_blocks=1`, so a second one is
discarded as a duplicate, writes nothing, and does not even reboot. Power-cycle between
drags, or put a multi-block firmware `.uf2` in between.

Build them yourself — the release build just runs this, and still does, so the format
stays tested even while the assets are unpublished:

`python3 tools/make_hand_uf2.py --side left`
`python3 tools/make_hand_uf2.py --side right`
`python3 tools/make_hand_uf2.py --verify polykybd-hand-left.uf2`

`--append-to` is **refused**: the bootrom tracks which sectors it has already erased by
BLOCK NUMBER (`page_no = block_no * 256 / FLASH_SECTOR_ERASE_SIZE`), which is the sector
index only while blocks run contiguously from the image base. An appended stamp block
4 MB up collides with the bit for firmware blocks 2992–2999 and skips its own erase when
one of those was written first, so the record lands corrupt.

### Keymaps

There is a `default` keymap, also for the slightly older revision 2: `revision2`:

`make polykybd/split72:revision2:uf2`
`qmk compile -kb polykybd/split72 -km revision2`


## After merging master into branch update dependencies with

`make git-submodule`

### Init submodules

`git submodule update --init --recursive`

### Updates tags to display the right verion

`git fetch --tags upstream`

## Check image size

`size .build//polykybd_split72_rp2040pico_default.elf -B`

### Copy image to RPI

`cp .build/polykybd_split72_rp2040pico_default.uf2 /media/$USER/RPI-RP2/`

## Get console output

`qmk console`

# For developers

## Companion host software PolyKybdHost

The keyboard talks to a host-side application over a custom 64-byte raw-HID protocol — overlays, ROI updates, language switching, and brightness all flow that way. The host lives in the [`PolyKybdHost`](https://github.com/thpoll83/PolyKybdHost) repository. Without it running, the per-keycap displays only show the boot/default state.

## Font generation

The per-keycap OLED fonts are generated from Noto TTF files using the `fontconvert` tool from the [`AdafruitGFX`](https://github.com/thpoll83/AdafruitGFX) sibling repo. To regenerate:

```sh
keyboards/polykybd/fonts/dl-fonts.sh   # download Noto fonts (one-off)
keyboards/polykybd/create_fonts.sh     # invoke fontconvert per range; writes base/fonts/generated/*.h
```

`base/fonts/gfx_used_fonts.h` is auto-generated by `run_cog.sh` from the generated headers.

## Diagnostics

### Boot splash progress (always on)

The boot splash (`POLY KYBD` on the left half, `SPLIT 72` / `SPLIT 42` on the
right) shows the **whole word from the first frame**, but every letter starts
**dim** (a scanline / half-density render) and **solidifies one letter at a time**
as boot advances. This appears instantly — no "slow", letter-by-letter typing wait
— while still doubling as a progress indicator: because each keycap OLED holds the
last frame it was sent, a boot that **hangs** freezes the reveal at the exact
letter it reached, so you read how far boot got by **counting the SOLID (bright)
letters** (the rest are present but dim). No compile flag: this is always on
(`splash_progress()` in `boot_diag.c`, driven from `keyboard_pre_init_user()` +
several points in `keyboard_post_init_user()`).

Each solid letter maps to a boot milestone (read the **hung half** — in the
firmware-apply hang that's the master/USB half; letters shown below are the ones
rendered **solid**, the remainder are dim):

| Step | Milestone reached | Left (`POLY KYBD`) | Right (`SPLIT 72`) |
|---|---|---|---|
| 1 | `pre_init` (before split/USB init) | *(all dim)* | *(all dim)* |
| 2 | after `set_side()` (**split/USB init passed**) | `P` | `S` |
| 3 | language/emoji/MRU init done | `PO` | `SP` |
| 4 | before core 1 launch | `POL` | `SPL` |
| 5 | after core 1 launch | `POLY` | `SPLI` |
| 6 | split RPCs + fw-staging up | `POLY K` | `SPLIT` |
| 7 | EEPROM config loaded | `POLY KY` | `SPLIT 7` |
| all | boot complete → real key legends | `POLY KYBD` | `SPLIT 72` |

The whole word being present from step 1 frees a frame (the old scheme spent step
1 on a single letter), so the reveal now has 7 solidify frames. The right half has
exactly 7 visible glyphs → one solidifies per step, retiring the old `" 7 2"`
leading-space placeholder trick (there used to be a `SPLII → SPLIT` two-step
reveal). The left half has 8, one more than the frames, so the final step
solidifies its **last two letters** (`B D`) at once.

So a frozen frame with **no solid letter** (whole word dim) is the split/USB-init
hang — the
**"hangs on the boot splash after a firmware apply"** case (`hid_fw_up.c`
`CMD_FW_UP_APPLY`: the slave never rebooted, so the master waits for a split
handshake that never comes; recovery = replug/reset or re-run Apply). The more
letters are lit, the later boot stalled; a keyboard that reaches the real key
legends booted cleanly.

The reveal completes in well under a second on a healthy boot, so it reads as a
brief splash animation; only a genuine hang parks it on a partial frame. The
milestone placement is in `keyboard_post_init_user()`. (A keycap-**digit** boot
tracer also exists behind the `FW_UP_BOOT_TRACE` compile flag — see `boot_trace()`
— for numbered milestones if the letter resolution isn't enough.)

### Core 1 stack high-water mark (`CORE1_STACK_HWM`)

`modules/polykybd/polymod_core1/polymod_core1.c` (the polymod_core1 community module) ships a small stack-painting probe that measures how deep core 1 actually drives its stack. It is **off by default** (the painting loop and walk add cycles to `multicore_launch_core1` and to every overlay/ROI dispatch) and gated by `CORE1_STACK_HWM`.

Enable it for a build by adding the define to `rules.mk`:

```make
OPT_DEFS += -DCORE1_STACK_HWM
```

Or pass it on the command line for a one-off build:

```sh
qmk compile -kb polykybd/split72 -km default -e EXTRAFLAGS=-DCORE1_STACK_HWM
```

When enabled:

- `multicore_launch_core1_with_stack` paints the full `core1_stack[]` buffer with `0xDEADBEEF` before passing it to the hardware.
- `core1_stack_high_water_mark()` walks from the low address upward and returns how many bytes have been written at least once — i.e. the deepest the stack ever reached.
- Each `CORE1: …` log line emitted by `core1_decompress_fragment` and `core1_update_roi` is appended with `stack HWM: <bytes>`. Read these via `qmk console`.

Stack usage on core 1 is monotonic (once a low word is overwritten by a deep call frame, it stays overwritten even after return), so the HWM only ever climbs. Drive overlay uploads / ROI updates from `PolyKybdHost` until the reading stops growing — that plateau is the true peak for the build.

Current `CORE1_STACK_SIZE` is 384 bytes. Observed peak with normal overlay + ROI traffic is ~164 bytes, leaving ~220 bytes of headroom. If you change call chains on core 1 (e.g. add new FIFO commands), re-run the probe and adjust `CORE1_STACK_SIZE` if the peak climbs.

### Timed console logs — TODO: shared timer

There is currently **no generic timed-logging mechanism**. The existing periodic
logs each roll their own trigger:

- The **split-link health counter** (`bridge_helper.c`) logs every
  `LINK_STATS_LOG_EVERY` (200) frames — *count*-based, not time-based.
- The **LTR-559 sensor telemetry** (`poly_keymap.c` `housekeeping_task_user()`,
  gated on `ltr559_available()`) logs every `LTR559_LOG_MS` (10 min) via a
  self-contained `static uint32_t` + `timer_elapsed32()` guard.

⚠️ **When a third timed log appears, factor these into one shared timer** — e.g. a
small `{ interval_ms, last, callback }` table ticked once from
`housekeeping_task_user()`, so each consumer just registers `{interval, fn}`
instead of duplicating the `static last + timer_elapsed32()` boilerplate (and the
cadences don't drift apart). Two consumers didn't justify the abstraction; three
do. The LTR-559 log site carries a comment pointing here.

## Emoji Layer

The `_EMJ` layer organises emojis into 12 categories. The top row holds category tab keys; the active tab is highlighted with a ∩-shaped border. Use the leftmost and rightmost top-row keys (◀ / ▶) to page through categories that hold more than 49 emojis.

### Category tabs

| Tab | Category | Emoji count | Pages |
|-----|----------|-------------|-------|
| 😀 | Smileys & Faces | 139 | 3 |
| 👀 | Gestures & Body | 52 | 2 |
| 🏠 | People & Jobs | 80 | 2 |
| ❤ | Love & Celebrations | 46 | 1 |
| 🐀 | Animals | 113 | 3 |
| 🌰 | Nature & Plants | 32 | 1 |
| ☀ | Weather & Sky | 91 | 2 |
| 🍇 | Food & Drink | 114 | 3 |
| ✈ | Travel & Places | 75 | 2 |
| ⚽ | Sports & Entertainment | 103 | 3 |
| 💻 | Tools & Objects | 171 | 4 |

The tab key shows the first emoji of its category. Pressing a tab switches the grid immediately; pressing a slot key types that emoji via Unicode input. Categories with 2 pages use ◀ / ▶ to navigate.

Font data for the emoji categories is generated by `create_fonts.sh`. After any change to category ranges, re-run the script to rebuild `base/fonts/generated/` and the auto-generated `base/fonts/gfx_used_fonts.h`.



See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).
