# Upstream Patches

Files outside `keyboards/polykybd/` that the PolyKybd build depends
on. Re-apply these whenever upstream QMK master is merged into PolyKybd
and the patched file shows up as a merge conflict (or silently overwritten).

To check the current state at any time:

```sh
git diff master..HEAD -- tmk_core quantum platforms builddefs lib
```

⚠️ That command is only exact while `master` **is** the upstream point PolyKybd
was merged from. Merge a *stable tag* while `master` tracks the upstream tip (as
in the 0.33.13 merge, where master sat 7 commits ahead) and the diff also carries
those extra commits, reversed. Check the gap is empty first —
`git diff --stat <merged-tag>..master -- tmk_core quantum platforms builddefs lib`
— or just diff against the tag you actually merged.

**To PROVE the patches survived a merge, diff the diffs — don't read them.**
Capture the patch set before merging and again after; identical output means every
patch survived *and* upstream didn't restructure the code around it, which reading
the diff by eye cannot tell you:

```sh
git diff <old-merge-base>..HEAD -- tmk_core quantum platforms builddefs drivers > /tmp/before.diff
git merge <tag>
git diff <tag>..HEAD             -- tmk_core quantum platforms builddefs drivers > /tmp/after.diff
diff /tmp/before.diff /tmp/after.diff && echo "all patches intact, context unchanged"
```

A clean merge is **not** evidence on its own: git resolves these files without a
conflict whenever upstream didn't touch the same hunks, so "no conflicts" and
"patch silently dropped" look identical from the merge output. Confirm separately
that upstream left the files alone —
`git diff --stat <old-base>..<tag> -- <the five files>` (empty in the 0.33.13
merge, 2026-08-11) — and finish with a `grep` for one marker per patch
(`raw_hid_pre_receive_kb`, `ifndef RAW_EPSIZE`, `POLY_SPLIT_SHMEM_RPC_GUARD`,
`POLYKYBD_VREG_VSEL`, `oled_render_dirty(true)`).

## tmk_core/protocol/usb_descriptor.h

Wrap `RAW_EPSIZE` default in an `#ifndef` so per-keyboard `config.h` can
override it. Required for the 64-byte Raw HID endpoint used by both PolyKybd
variants.

```diff
-#define RAW_EPSIZE 32
+#ifndef RAW_EPSIZE
+#    define RAW_EPSIZE 32
+#endif
```

Originally introduced before this tracker existed. Single hunk around the
endpoint-size block (`#define KEYBOARD_EPSIZE 8` … `#define DIGITIZER_EPSIZE 8`).

⚠️ **The nested `#define` must be indented `#    define` (space after `#`), not
`    #define` (space before `#`)** — that's `qmk format-c`'s (clang-format)
required style for a preprocessor directive nested inside `#ifndef`/`#endif`.
The file carried the wrong indent for a long time undetected, because the
`lint` CI job only reformats and diffs files that are *in the current PR's
diff* — this file wasn't touched again until the 2026-09-25 upstream merge
touched the neighbouring line, which is when `lint` finally caught it. Run
`qmk format-c --core-only tmk_core/protocol/usb_descriptor.h` after any
hand-edit to this hunk.

## tmk_core/protocol/chibios/usb_main.c

Two changes to `raw_hid_task` for backpressure during overlay transfers:

1. Add a weak `bool raw_hid_pre_receive_kb(void)` hook so PolyKybd can refuse
   to pull a packet off the OUT queue while core1 is still chewing on the
   previous one. The packet stays queued (RAW_OUT_CAPACITY=4) and
   `matrix_task` gets to run instead of starving in the busy-wait inside
   `core1_decompress_fragment` / `core1_update_roi`.
2. Replace `while (receive_report(...))` with `if (receive_report(...))` —
   handle at most one packet per main-loop pass so matrix scan always gets a
   turn between two raw HID receives, even when the OUT queue is full.

The strong override of `raw_hid_pre_receive_kb` lives in
`keyboards/polykybd/multicore_exec.c` and calls `core1_is_busy()`.

If a future QMK upstream changes the signature or semantics of `raw_hid_task`,
re-apply by hand: keep the weak hook, keep the `if`-not-`while`.

## drivers/oled/oled_driver.c

Defer the status-OLED `DISPLAY_ON` until the framebuffer has been pushed, so
the boot no longer flashes the panel's random power-on GDDRAM before the splash
appears (the SSD1306 status OLED is on I2C @ 400 kHz).

Two hunks in `oled_init()`:

1. Drop `DISPLAY_ON` from the `display_setup2[]` init command list so the panel
   stays physically OFF after the init commands:

   ```diff
   -    static const uint8_t PROGMEM display_setup2[] = {…, DEACTIVATE_SCROLL, DISPLAY_ON};
   +    static const uint8_t PROGMEM display_setup2[] = {…, DEACTIVATE_SCROLL};
   ```

2. At the tail of `oled_init()`, after `oled_clear()`, flush the cleared
   (all-black) buffer to GDDRAM with the panel still off, then enable it:

   ```diff
        oled_clear();
        oled_initialized = true;
   -    oled_active      = true;
        oled_scrolling   = false;
   +    oled_active = true;          // suppress oled_render_dirty()'s internal oled_on()
   +    oled_render_dirty(true);     // flush all-black GDDRAM while the panel is off
   +    oled_active = false;         // make the next oled_on() actually emit DISPLAY_ON
   +    oled_on();                   // light the panel — GDDRAM already black
        return true;
   ```

`oled_render_dirty()` calls `oled_on()` at its top when there is dirty data;
holding `oled_active = true` across the flush makes that a no-op, so exactly one
`DISPLAY_ON` is emitted (by the final `oled_on()`) after GDDRAM is clean.

If upstream restructures `oled_init()` / the init command list, re-apply by
hand: no `DISPLAY_ON` in the command list; flush-then-enable at the tail. End
state is unchanged (`oled_active == true`, panel on).

## quantum/split_common/transport.h

`POLY_SPLIT_SHMEM_RPC_GUARD`: when defined (split42 `rules.mk`) and
`SPLIT_POINTING_ENABLE` is not, insert `split_slave_pointing_pad_t
poly_pointing_pad` — a byte-identical stand-in for the pointing sync member —
at the pointing member's position in `split_shared_memory_t`, immediately
before `rpc_info`/`rpc_m2s_buffer`/`rpc_s2m_buffer`. Empirically required for
split42's split link to establish (see
`keyboards/polykybd/split42/SPLIT42_LINK_STATUS.md`, rows 20–24: the pad alone
revives the link that the whole pointing subsystem was previously carried to
fix). Guards against a suspected latent out-of-bounds write into the RPC
region; remove once that writer is found and fixed. split72 (real
`SPLIT_POINTING_ENABLE`) is unaffected.

```diff
 #if defined(POINTING_DEVICE_ENABLE) && defined(SPLIT_POINTING_ENABLE)
     split_slave_pointing_sync_t pointing;
+#elif defined(POLY_SPLIT_SHMEM_RPC_GUARD)
+    split_slave_pointing_pad_t poly_pointing_pad;
 #endif
```

## platforms/chibios/bootloaders/rp2040.c

`POLYKYBD_VREG_VSEL`: when defined (by `POLYKYBD_SYS_CLK=200`, the default since
0.10.x — see
`keyboards/polykybd/rules.mk`), raise the core-voltage select at the top of
`__late_init()` — **before** the `clocks_init()` call already there.

That `clocks_init()` is the first point in the boot where the compile-time
`SYS_CLK_KHZ` is applied, and the double-tap window immediately after it
busy-waits for a full second, so this is the only place the voltage can be
raised without running the longest stretch of the boot out of spec. There is
no QMK hook early enough: `__late_init` runs before `main()`, hence before
`halInit()` and every `early_hardware_init_*` hook. The one earlier hook,
`__early_init()`, lives in ChibiOS's `board.c` (a pinned submodule, worse to
patch), and overriding the board dir wholesale would also detach the
`configs/` include path QMK adds for the stock board.

The raise is guarded, so a build with `-e POLYKYBD_SYS_CLK=125` compiles to
byte-identical code (verified by disassembly: stock `__late_init` still
branches straight to `clocks_init`). The vendored pico-sdk predates the SDK's
own automatic VREG raise for `SYS_CLK_MHZ=200` and does not compile
`hardware_vreg` at all, which is why this is a register write rather than a
`vreg_set_voltage()` call.

```diff
 void __late_init(void) {
+#if defined(POLYKYBD_VREG_VSEL)
+    hw_write_masked(&vreg_and_chip_reset_hw->vreg, ...VSEL...);
+    /* spin — the timer is not ticking yet */
+#endif
     clocks_init();
```

## ChibiOS-Contrib RP2040 USB driver (vendored, `chibios_overrides/`)

**Not a patch to a tracked file — a COPY.** `lib/chibios-contrib` is a pinned
submodule of `qmk/ChibiOS-Contrib`, so it cannot carry a local change. Instead
`keyboards/polykybd/chibios_overrides/` holds:

- `platform.mk` — Contrib's `os/hal/ports/RP/RP2040/platform.mk` with ONE line
  changed: the USBDv1 `driver.mk` include points at the copy below.
  `keyboards/polykybd/rules.mk` selects it with `PLATFORM_MK`, and **fails the
  build if the file is missing**, because `platforms/chibios/platform.mk` would
  otherwise fall back to Contrib's own file with no message.
- `USBDv1/` — Contrib's `os/hal/ports/RP/LLD/USBDv1` at `5a9ad82b` (the submodule
  pin when copied), plus the one fix and the opt-in diagnostics below.

**The fix:** the USB ISR handles `USB_INTS_BUS_RESET` **before**
`USB_INTS_SETUP_REQ` — ChibiOS trunk r17878 (`29391bf57`, "Fix RP USB reset
sequencing"), which Contrib's copy never received. With both flags pending in one
pass, the old order armed the SETUP reply and then `_usb_reset()` rewound the EP0
state machine to `USB_EP0_STP_WAITING` under it; the IN stage then completed in
the wrong state and `_usb_ep0in()` STALLed EP0, so the host saw a failed
enumeration step. Both flags are pending together only when the interrupt is
blocked across a reset AND the SETUP after it (≥ ~10–20 ms): a flash erase does
exactly that (BY25Q64ES tSE 35 ms typ / 300 ms max per 4 KB sector; the 8 KB
wear-levelling erase is one masked call). The realistic trigger is the boot after
a `KEYMAP_LAYERS_FL_MERGED` bump — QMK connects the pull-up in
`protocol_pre_init()`, before `keyboard_init()`, so the host enumerates while
post_init rewrites the keymap.

**Diagnostics (`-DPOLYKYBD_USB_RACE_DIAG`, set by `-e POLYKYBD_USB_STRESS=yes`):**
`poly_usb_diag.h` counters in the ISR, read by `base/usb_stress.c`, which also
masks interrupts in erase-sized windows during enumeration. The rig drives it
with `tools/hil_probes/usb_reset_race.py`. A normal build contains none of it.

**Verification:** with the copy in place and no edits, `split72:default` linked
to a byte-identical image. The rig A/B results are in the PR that introduced the
copy (RIG_RESULTS_PLACEHOLDER).

**When Contrib or QMK picks the fix up** (check
`grep -n "USB_INTS_BUS_RESET" -A3 lib/chibios-contrib/os/hal/ports/RP/LLD/USBDv1/hal_usb_lld.c`
— the reset branch must come first): delete `chibios_overrides/` and the
`PLATFORM_MK` block in `rules.mk`. Until then, **after every submodule bump, diff
the copy against the new Contrib file** and carry any upstream change across —
the copy does not follow the submodule on its own:

```sh
diff -u lib/chibios-contrib/os/hal/ports/RP/LLD/USBDv1/hal_usb_lld.c \
        keyboards/polykybd/chibios_overrides/USBDv1/hal_usb_lld.c
diff lib/chibios-contrib/os/hal/ports/RP/RP2040/platform.mk \
     keyboards/polykybd/chibios_overrides/platform.mk
```

The expected `hal_usb_lld.c` diff is the reordered reset/SETUP block plus the
`POLYKYBD_USB_RACE_DIAG` hunks; `hal_usb_lld.h` and `rp2040_usb.h` should be
identical.
