---
name: extract-qmk-module
description: Extract a self-contained subsystem out of keyboards/polykybd/ into a reusable QMK community module under modules/polykybd/ — assess the coupling, scaffold the module, convert consumers to its auto-hooks, add a mocked unit-test suite, and verify with a both-variant build + lint. Use when asked to "make X a module", "extract the <driver/library> into a community module", "modularise <subsystem>", "can this be a qmk module", or when a piece of firmware code turns out to be generic enough to share. NOT for adding a font, language, glyph script or keycap hint (those have their own skills), and NOT for code that touches the overlay/display stack, the HID protocol or poly_keymap's layer logic — that is the product, not a library.
---

# Extract a subsystem into a QMK community module

Turning `keyboards/polykybd/<thing>.c` into `modules/polykybd/polymod_<thing>/`. The
existing modules are `polymod_crc32`, `polymod_rle` (pure-algorithm libraries) and
`polymod_ltr559` (a driver with hooks + tests) — the last is the fullest worked
example of this procedure.

Background facts this skill relies on are in `CLAUDE.md` § "Community modules" and
§ "Unit tests"; read them if anything below is surprising.

## 0. Decide whether it should be a module at all

Run the filter **before** touching anything. A candidate qualifies when:

- its `.c` includes **no PolyKybd headers** — check first, it's one command:
  ```bash
  grep '#include' keyboards/polykybd/<path>.c
  ```
  QMK core headers (`i2c_master.h`, `timer.h`, `quantum.h`) are fine; `polykybd.h`,
  `base/com.h`, `state.h`, `QMK_KEYBOARD_H` are disqualifying (or a decoupling job
  first — say so and stop).
- it uses **no PolyKybd types** in its public API (`poly_sync_t`, `poly_layer_t`,
  `enum poly_os`, display/overlay structs).
- **every consumer already lives outside it** — the seam exists:
  ```bash
  grep -rn "<public_symbol>" keyboards/polykybd --include=*.c --include=*.h | grep -v '^keyboards/polykybd/<path>'
  ```
  All hits in one or two files = clean. Hits scattered through the display/overlay
  stack = not a module.
- it would plausibly be **useful to another keyboard**. A module nobody else could
  use is just a moved file; say so and let the user decide if it's still worth it.

Report the verdict with the evidence before proceeding.

## 1. Scaffold

```bash
mkdir -p modules/polykybd/polymod_<name>/tests
git mv keyboards/polykybd/<path>.c modules/polykybd/polymod_<name>/polymod_<name>.c
git mv keyboards/polykybd/<path>.h modules/polykybd/polymod_<name>/polymod_<name>.h
```

Use `git mv` so the diff reads as a **rename** — reviewers can then see the real
changes instead of a delete+add of several hundred lines. Guard the rest of the
procedure against destroying that: avoid gratuitous reformatting of the moved file
(see §5).

`qmk_module.json` — minimum is `module_name` + `maintainer`; add `license` and `url`:

```json
{
    "module_name": "<Human readable name>",
    "maintainer": "PolyKybd",
    "license": "GPL-2.0-or-later",
    "url": "https://github.com/thpoll83/qmk_firmware"
}
```

Add `"keycodes": [{"key": "COMMUNITY_MODULE_<X>", "aliases": ["CM_<X>"]}]` only if the
module genuinely owns new keycodes, and `"features": {…}` only if it needs a QMK
feature switched on.

## 2. Drop the old enable define; the build provides one

In the module `.c`, **remove the `#ifdef POLYKYBD_<X>` wrapper** — a module's source
is compiled only when the module is listed, so the guard is now redundant.

Then in **`<variant>/rules.mk`**, delete both the `SRC += <path>.c` line and the
`-DPOLYKYBD_<X>` enable define, and in **`<variant>/keyboard.json`** add the module
(keep the array alphabetical):

```json
"modules": [
    "polykybd/polymod_crc32",
    "polykybd/polymod_<name>",
    "polykybd/polymod_rle"
],
```

Consumers now gate on the generated **`COMMUNITY_MODULE_POLYMOD_<NAME>_ENABLE`**.
Keep any *separate* policy define (e.g. `POLYKYBD_LTR559_DRIVE`) — that is a different
question from "is the driver present", and splitting them is part of the value.

## 3. Convert consumers to the module's hooks

If the subsystem has an init and/or a periodic task that `poly_keymap.c` calls
explicitly, move them into the module:

```c
void keyboard_post_init_polymod_<name>(void) {
    keyboard_post_init_polymod_<name>_kb();   // must call the _kb link yourself
    <thing>_init();
}

void housekeeping_task_polymod_<name>(void) {
    housekeeping_task_polymod_<name>_kb();
    <thing>_task();
}
```

…and **delete the explicit calls** from `poly_keymap.c` — keeping both double-runs
the work. This is safe because `quantum/keyboard.c` dispatches `*_modules()` **before**
`_kb`/`_user`; re-confirm that if the QMK version has moved:

```bash
grep -n "post_init_modules\|housekeeping_task_modules" quantum/keyboard.c
```

A consumer that used the init's **return value** (`if (x_init()) uprint(...)`) should
switch to the availability accessor (`if (x_available())`), which the earlier module
hook has already populated.

Guard the hook block with `#ifndef <NAME>_UNIT_TEST` so the test build (§4) can link
the driver without the generated `community_modules.h`.

## 4. Add a mocked unit-test suite

`quantum/wear_leveling/tests/` is the pattern. Mock the **bus/backing store**, not the
subsystem — the tests should drive the *real* code.

`tests/rules.mk`:
```make
POLYMOD_<NAME>_PATH := modules/polykybd/polymod_<name>
polymod_<name>_DEFS := -D<NAME>_UNIT_TEST
polymod_<name>_SRC := \
	$(POLYMOD_<NAME>_PATH)/polymod_<name>.c \
	$(POLYMOD_<NAME>_PATH)/tests/<mock>.cpp \
	$(POLYMOD_<NAME>_PATH)/tests/<name>_tests.cpp \
	$(PLATFORM_PATH)/timer.c \
	$(PLATFORM_PATH)/test/timer.c
polymod_<name>_INC := \
	$(POLYMOD_<NAME>_PATH) \
	$(POLYMOD_<NAME>_PATH)/tests
```

`tests/testlist.mk`: `TEST_LIST += polymod_<name>` (⚠️ no `-` in test names).

Register both, next to the `quantum/*/tests/` lines:
- `builddefs/testlist.mk` → `include modules/polykybd/polymod_<name>/tests/testlist.mk`
- `builddefs/build_test.mk` → `include modules/polykybd/polymod_<name>/tests/rules.mk`

Expose a test-only reset (the driver is a singleton over file statics):
```c
#ifdef <NAME>_UNIT_TEST
void <thing>_reset_for_test(void);
#endif
```

Run it:
```bash
git submodule update --init --depth 1 --no-recommend-shallow lib/googletest
export QMK_HOME=$PWD && export PATH="/root/.qmk_venv/bin:$PATH"
make test:polymod_<name>
```

**Test the behaviours that were learned on hardware** — the ones living in comments
that a refactor would silently break: bounded retries, refusal paths, wire byte order,
sample-validity rules, windowing/averaging. Those are the tests worth having.

## 5. Verify — and mutation-test the tests

```bash
export QMK_HOME=$PWD && export PATH="/root/.qmk_venv/bin:$PATH"
qmk compile -kb polykybd/split72 -km default
qmk compile -kb polykybd/split42 -km default
qmk lint --strict --keyboard polykybd/split72
qmk lint --strict --keyboard polykybd/split42
clang-format --dry-run -Werror modules/polykybd/polymod_<name>/*.[ch] \
                               modules/polykybd/polymod_<name>/tests/*.[ch]pp
make test:polymod_<name>
```

Then **break the driver on purpose and confirm the right test fails** — two mutations
are enough (e.g. swap a byte order; delete a bound). A green suite against a
deliberately broken driver is measuring nothing, and this is the only cheap proof it
isn't. Restore from a copy afterwards and re-run to confirm green.

## Output

Report: the coupling verdict + evidence; what moved; which defines changed; whether
explicit calls became hooks; the test count and what they pin; the mutation-test
result; and build/lint status for **both** variants. State plainly that hardware
behaviour is unverified unless someone flashed it.

## Pitfalls

- ⚠️ **Don't keep the explicit init/task calls "just in case"** once the hooks exist —
  that double-probes and double-polls.
- ⚠️ **Don't invent a new `-DPOLYKYBD_<X>` enable.** The build already emits
  `COMMUNITY_MODULE_POLYMOD_<NAME>_ENABLE`; a second define drifts out of sync.
- ⚠️ **Don't run the local formatter over the moved `.c`.** The container's
  clang-format is a different version from CI's and will rewrite files CI accepts —
  which both adds churn and breaks the rename. Format only what the lint job named,
  and confirm skew by testing the base-branch copy (CLAUDE.md § CI).
- ⚠️ **A `.c` that keeps `#include QMK_KEYBOARD_H` is not extractable** — that pulls
  the whole keyboard in. If §0 finds one, the decoupling is the task, not the move.
- ⚠️ **`git mv` before editing**, not after, or the rename detection is lost and the
  reviewer sees a wall of green.
- Keep the module **single-purpose**. Two subsystems that merely ship together are two
  modules.
