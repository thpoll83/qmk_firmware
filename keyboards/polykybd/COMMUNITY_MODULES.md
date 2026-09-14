# Community modules and the LTR-559 sensor

Extracted from `CLAUDE.md` 2026-09-14. The prose is unchanged; only heading levels
and relative links were adjusted to suit a standalone file.

## LTR-559 light+proximity sensor (`modules/polykybd/polymod_ltr559/`) — ENTIRELY OPTIONAL

An **entirely optional** ambient-light + proximity sensor (Pimoroni LTR-559, I2C
addr `0x23`) on the expansion port. It **shares the Cirque I2C0 bus** (GP0/GP1) — no
new pins. It is a **clean no-op when no sensor is fitted**: the probe fails and the
driver disables itself after a few bounded retries (`LTR559_MAX_RETRIES`). So anyone
who solders the part gets it and nobody else pays more than ~30 s of cheap probes.

- ⚠️ **The DRIVER is a community module (`polykybd/polymod_ltr559`), not a
  `keyboards/` source file** — it moved out of `base/ltr559.c/.h` (2026-08-12).
  Consequences, all easy to trip over:
  - **Listing it in `keyboard.json` `modules` is the entire enable.** There is no
    `SRC +=` line and no `-DPOLYKYBD_LTR559`; the build defines
    **`COMMUNITY_MODULE_POLYMOD_LTR559_ENABLE`** for you, and that is what
    the consumer code gates on. `POLYKYBD_LTR559_DRIVE` survives
    unchanged as the separate gate for the PolyKybd **policy** (auto-brightness +
    idle-inhibit + the `USER_SYNC_SLAVE_DATA` slot), so a board can carry the driver
    without the policy. Since #237 that policy lives in **`ltr559_policy.c`**
    (the lux→contrast curve, the drive tick, the proximity wake, the slave-pull
    handler); `poly_keymap.c` only registers the split handler and calls
    `poly_ltr559_drive()` from housekeeping.
  - **The module probes and polls itself** from `keyboard_post_init_polymod_ltr559` /
    `housekeeping_task_polymod_ltr559`. `poly_keymap.c` no longer calls
    `ltr559_init()`/`ltr559_task()` — **don't re-add them**, that would double-probe.
    The ordering is safe because `quantum/keyboard.c` runs `*_modules()` **before**
    `_kb`/`_user`, so post_init's `ltr559_available()` and housekeeping's reading are
    both current.
  - **It has 19 unit tests** (`make test:polymod_ltr559`) driving the real driver
    against a mock LTR-559 + mock I2C bus — the bounded retry, the config-write
    refusal, the ALS byte order, the invalid-sample rule, the growing-then-rolling
    average. Wired into the harness via `builddefs/testlist.mk` +
    `builddefs/build_test.mk`. Run them after touching the driver; they are ~1 s.
  - Both variants list it. Precedent: `polymod_crc32` / `polymod_rle`.
- **Side-agnostic** — auto-detected on **whichever half it's soldered to**. The
  module's hooks run on **both** halves; the one that answers uses it, the other
  gives up after the bounded retries. ⚠️ Do **not** re-gate on
  `is_right_side()` — it was, and a left/master-soldered sensor was never read (field).
- **Slave→master backchannel** — brightness/idle decisions are master-only, but the
  sensor may be on the slave, so the master **pulls** its values over a **generic
  op-dispatched RPC** `USER_SYNC_SLAVE_DATA` (a `kind` byte selects the payload;
  `SLAVE_DATA_SENSOR` → `{avg lux, prox}`). Works in either USB orientation and is
  reusable for other slave-side data; consumes one split-transaction slot, guarded by
  `POLYKYBD_LTR559_DRIVE`. If the master holds the sensor it reads locally instead.
- **Auto-brightness** (`poly_ltr559_drive()`, master-only, every `LTR559_DRIVE_MS`)
  feeds the 5 s average lux through the **same volatile/host-auto path the host
  daylight feature uses** (`set_brightness_auto_mode`/`set_auto_brightness_value`). So
  the sensor drives **only while auto mode is on**; it engages auto **once** (first
  real reading); a **manual** change (preset keys / host explicit set) turns auto OFF
  and the sensor **backs off** (its per-tick push no-ops while auto is off and the
  `engaged` static never re-engages) — **manual always wins** until auto is re-enabled
  or reboot. Refreshing ~0.5 s vs the host's ~10-min daylight, it overrides host
  daylight values while auto is on.
- **Boot dark-screen guards** (learned the hard way) — `ltr559_avg_lux()` is a
  **growing-window** average (0 only until the first VALID sample). Two guards keep it
  off the near-off floor: (1) `poly_ltr559_drive()` doesn't engage while `avg == 0`
  (first ~1 s), so boot holds the manual/restored brightness instead of dipping; (2)
  `lux_to_contrast()` floors at `LTR559_MIN_CONTRAST` so it never drives below a
  visible dim level (never `B=1`/`DISP_OFF`).
- **Proximity → idle-inhibit** — 11-bit **relative reflectance** (not calibrated
  distance; ~5–6 cm max). `prox > LTR559_NEAR_THRESHOLD` wakes the displays +
  `update_performed()`. Uses the PS channel (works in the dark), NOT the ambient-shadow
  drop on the ALS channels. ⚠️ The resting baseline is **housing-dependent** — ~129 on
  the open bench but ~325 once mounted (enclosure walls reflect IR back); re-check
  `PRX` and the threshold after any housing/hole change.
- **Measured tuning** (hardware): proximity resting ~129 bench / ~325 housed, ~5 cm
  400, ~1 cm 1000, hole covered ~2000 (saturated) → `NEAR_THRESHOLD` 350. Lux (sqrt
  curve) `LUX_FULL_REF` 100 → B≈4 dark room, 26 @ 28 lux, 35 @ 50 lux, full @ 100+ lux;
  night floor `MIN_CONTRAST` 4; `LTR559_ALS_GAIN` 4×.
- **Telemetry** — a 10-min `uprintf` heartbeat in housekeeping, gated on
  `ltr559_available()` so only the sensor half logs (`LTR-559: lux=.. avg=.. prox=..
  ch0=.. ch1=.. B=..`). The status-OLED test readout + I2C bring-up diagnostics were
  removed once it worked; the bus scan is kept as a disabled `#if 0` reference block in
  `polymod_ltr559.c`. No shared timed-log framework yet — see `readme.md` "Diagnostics" →
  "Timed console logs".

## Community modules (`modules/polykybd/`)

Self-contained, keyboard-independent code lives in **QMK community modules** rather
than `keyboards/polykybd/`: currently `polymod_crc32` and `polymod_rle` (both ~55 LOC
pure-algorithm libraries), with `polymod_ltr559` (the LTR-559 driver) extracted the
same way. The mechanics are not obvious from the QMK docs alone:

- **Declared in `keyboard.json`, not `keymap.json`.** Both variants carry a
  `"modules": ["polykybd/polymod_crc32", …]` array. The docs describe the
  `keymap.json` route (and External Userspace); the keyboard-level array is what this
  fork uses, so a module lands on every keymap of that board.
- **Listing a module IS the enable — the build defines
  `COMMUNITY_MODULE_<NAME>_ENABLE`** (upper-cased directory name) for you, plus
  `COMMUNITY_MODULES_ENABLE`. So a module needs **no `SRC +=` line and no bespoke
  `-D<FEATURE>` in `rules.mk`**; gate consumer code on the generated define instead of
  inventing a parallel one. `modules/<ns>/<name>/<name>.c` is compiled automatically
  (matching the directory name); any *other* source file needs `SRC +=` in the module's
  own `rules.mk`.
- ⚠️ **Module hooks run BEFORE `_kb`/`_user`.** `quantum/keyboard.c` calls
  `keyboard_post_init_modules()` then `keyboard_post_init_kb()`, and
  `housekeeping_task_modules()` then `_kb` then `_user`. **This is what makes a
  self-driving module safe**: a module that probes hardware in its post_init hook is
  already done by the time `keyboard_post_init_user()` runs, and one that polls in its
  housekeeping hook has produced *this* pass's sample before `housekeeping_task_user()`
  reads it. Verify this before deleting explicit init/task calls in favour of hooks —
  it is the whole argument.
- ⚠️ **Overriding the non-suffixed hook means you must call the `_kb` link yourself.**
  The build generates a weak `<api>_<module>()` → `<api>_<module>_kb()` →
  `<api>_<module>_user()` chain. Defining `housekeeping_task_<module>()` replaces the
  top of that chain, so it must call `housekeeping_task_<module>_kb()` or the keyboard/
  keymap specialisations are silently dropped. `modules/qmk/hello_world` is the pattern.
- **This fork is on module API 1.1.2.** The available hooks are the union of
  `data/constants/module_hooks/*.hjson` (0.1.0 → 1.1.2); read those files rather than
  the docs table, which stops at 1.1.0. 1.1.1 added LED/RGB matrix effects, **1.1.2
  added custom split data sync** (`SPLIT_TRANSACTION_IDS_MODULE_<MODULE>`) — relevant
  here, where several subsystems carry their own split transactions. Assert the floor
  you rely on with `ASSERT_COMMUNITY_MODULES_MIN_API_VERSION(1, 0, 0);` (commas, not
  periods) after `#include "community_modules.h"`.
- **What is worth extracting**: code with no PolyKybd types and no display/protocol
  coupling. Surveyed 2026-08: the remaining strong candidates are `base/crypto/`
  (vendored Monocypher Ed25519), `base/multicore/` (RP2040 core1 launch + FIFO),
  `os_actions.c` (per-OS chord table — the best *community* candidate, since
  `qmk_module.json` `keycodes` is built for exactly that), and with a decoupling pass
  the idle-timestamp half of `base/update.c` and `base/fw_staging.c`. **Not**
  `poly_keymap.c` / `hid_com.c` / the overlay + display stack — that is the product.
  The `extract-qmk-module` skill drives the whole conversion.

- **Wiring a unit-test suite — the two registrations, the standalone-test gotchas
  and the mutation-testing traps — is
  [`keyboards/polykybd/UNIT_TESTS.md`](UNIT_TESTS.md).**
  `make test:polymod_ltr559` is the pattern for a driver against a mock bus,
  `make test:fw_up_verdict` for DECISION logic. Four rules kept here:
  - ⚠️ **`make test:<name>` with a name not in `TEST_LIST` exits 0 and prints
    NOTHING** — no "unknown target", no output, just a clean prompt. **Judge a run by
    the `[  PASSED  ] N tests.` line, never by the exit code.**
    `grep -rn "TEST_LIST +=" --include=testlist.mk .` is the authoritative list.
  - ✅ **These run in CI via `polykybd-unit-test.yml`, NOT upstream's
    `unit_test.yml`** — upstream's filters on paths a PolyKybd change never touches,
    so for as long as these suites existed CI ran none of them. Do not "fix" that by
    adding our paths to the stock workflow; it would conflict at the next catch-up
    merge. Suite names are DERIVED by grepping the testlist files, so a new suite
    needs no workflow edit.
  - **Extracting the decision from the I/O is worth doing on its own terms.**
    `base/fw_up_verdict.c`, `base/macro_decode.c` and `base/macro_record.c` are pure
    (no quantum.h, no EEPROM, no timer) because in each case the arithmetic was the
    part with a bug history and the only part unreachable — it shared a function with
    the transport.
  - ⚠️ **Mutation-test the suite before trusting it**, and know the three fail-open
    traps that all report "your tests caught nothing": ANSI escapes in gtest output
    defeating a `^\[` grep, a mutation that never APPLIED, and a
    `git diff --quiet` guard that compares against HEAD on a tree that is already
    dirty. The `mutation-test-suite` skill carries the recipe.

