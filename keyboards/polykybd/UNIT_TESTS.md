# Unit tests (`make test:<name>`)

QMK's googletest harness, how a standalone PolyKybd suite is wired into it, what is
worth extracting to make testable, and the fail-open traps in mutation-testing one.
Moved out of `CLAUDE.md` on 2026-09-10: ~14 KB read while you are adding or fixing
a suite.

The `mutation-test-suite` skill is the recipe for checking a suite is worth
anything; this file is the wiring and the traps.

⚠️ **`make test:<name>` with a name that is not in `TEST_LIST` exits 0 and prints
NOTHING** — no "unknown target", no output, just a clean prompt. Judge a run by the
`[  PASSED  ] N tests.` line, never by the exit code.

---

### Unit tests (`make test:<name>`)

QMK has a googletest harness; a **standalone** test (one that links a subsystem
against mocks, rather than booting a whole fake keyboard) is the right shape for
module code. `quantum/wear_leveling/tests/` is the model to copy — it mocks its
backing store the way a driver test should mock its bus.

```bash
git submodule update --init --depth 1 --no-recommend-shallow lib/googletest  # needs add_repo qmk/googletest first
export QMK_HOME=$PWD && export PATH="/root/.qmk_venv/bin:$PATH"
make test:polymod_ltr559          # ~1 s, 19 tests — the LTR-559 driver vs a mock I2C bus
make test:fw_up_verdict           # ~1 s, 27 tests — the flash-staging COMMIT decision layer
```

⚠️ **`make test:<name>` with a name that is not in `TEST_LIST` exits 0 and prints
NOTHING** — no "unknown target", no test output, just a clean prompt. It is the same
silent-green failure the CI workflow's zero-suites guard exists to catch, one level
down and with nothing guarding it: `make test:os_hints` "passed" twice before the
missing output was noticed (2026-08-18); the suite is `polykybd_os_hints`. **Judge the
run by the `[  PASSED  ] N tests.` line, never by the exit code** — a real run always
prints one. `grep -rn "TEST_LIST +=" --include=testlist.mk .` is the authoritative
list of registered names — they live in `base/tests/testlist.mk`,
`hints/tests/testlist.mk` and each `modules/polykybd/polymod_*/tests/`; neither a
count nor an enumeration is kept here, because this sentence once listed four
suites while eleven existed.

✅ **These run in CI — via `polykybd-unit-test.yml`, NOT upstream's `unit_test.yml`.**
That distinction is the whole point: upstream's workflow filters on `builddefs/ quantum/
platforms/ tmk_core/ tests/`, and a PolyKybd change touches none of them, so for as long
as these suites existed **CI never ran a single one of them** — 46 tests, hand-run only,
on a PR board that otherwise looks comprehensively green. Do **not** "fix" that by adding
our paths to `unit_test.yml`: it is stock upstream and would conflict at the next
catch-up merge.
- **The suite names are DERIVED, not hardcoded.** The workflow greps every
  `*polykybd*/testlist.mk` that `builddefs/testlist.mk` includes and reads their
  `TEST_LIST +=` lines, so **a third suite needs no workflow edit** — register it in the
  two builddefs files (see below) and CI picks it up. Same reasoning as
  `sync_is_link_fault()` refusing to enumerate its siblings: a list that must be kept in
  sync is a list that goes stale silently.
- ⚠️ **It fails when it discovers ZERO suites**, and again if the loop runs zero. A test
  job that quietly runs nothing and reports green is strictly worse than no job — it is
  the exact failure this workflow was added to remove, so it must not be able to
  recreate it one level up. The loop also continues past a failing suite, so one run
  names every broken suite rather than just the first.
- ⚠️ **`ghcr.io/qmk/qmk_cli` runs steps under POSIX `sh` (dash), not bash** — the log
  header says `shell: sh -e {0}`. Write every `run:` in that container POSIX-clean, and
  **test it with `dash`, not your login shell**; `bash script.sh` passing proves nothing.
  Two traps, and the second is the dangerous one:
  - `done <<< "$list"` is a bash **herestring**: dash won't parse it at all —
    `Syntax error: redirection unexpected`, exit 2. Loud, so it is the good case, but
    it is what made this workflow's first run red.
  - `printf … | while read` is the tempting POSIX fix and is **worse**: it parses, but
    POSIX runs the loop body in a **subshell**, so counters and accumulators are
    discarded at the `done`. A failing suite would leave the failure list empty and the
    job would report **green** — which is why the count-zero guard above is not
    redundant paranoia; it is the only thing that catches this.
  - `for t in $LIST` (unquoted, from an `env:`) keeps state in the current shell and
    works in every POSIX shell. Safe here because suite names are makefile identifiers
    — the harness rejects even a `-`, so they can never contain whitespace.

- **`fw_up_verdict` is the pattern for testing DECISION logic** (as opposed to
  `polymod_ltr559`, which is the pattern for a driver vs a mock bus). It covers the
  COMMIT-failure classification, the ack vocabulary, the STATUS-snapshot CRC guard and
  the font-pack status byte. **Getting it testable required decoupling first, and that
  decoupling was worth doing on its own terms** — the two smells it exposed:
  - `split_sync.h` conflated a **protocol contract** (4 ack bytes + `sync_succeeded`)
    with the sync **payload structs**, which need `config.h`/`state.h`/`mru.h`. So
    asking "is this ack a success?" pulled in the whole keyboard config, and
    `sync_succeeded()` — the helper guarding every `send_to_bridge` call site, and
    itself the subject of a field bug — had no test for years. The vocabulary now
    lives in dependency-free **`base/sync_ack.h`**, re-exported by `split_sync.h` so
    all consumers are unchanged.
  - `fw_up_slave_refused_commit()` mixed RPC transport, CRC validation, the decision
    and `uprintf` in one function; the decision was the only part with a bug history
    and the only part unreachable. It is now pure in **`base/fw_up_verdict.c`**, with
    the I/O and the four diagnostic lines left in `split_fw_up.c`. This mirrors what
    the host repo does deliberately (`polyhost/core/decisions.py`,
    `decide_stale_bundles`, `classify_commit_reply`) — the firmware just never had
    the seams.
  - ⚠️ `base/fw_up_verdict.c` is listed in the **shared** `POLY_SRC` in
    `keyboards/polykybd/rules.mk`, NOT with the other `base/*.c` in the per-variant
    `rules.mk` — its consumer `split_fw_up.c` is in that shared list, so a variant
    that forgot the line would just fail to link.
- **A host fixture can never catch THIS end emitting the wrong byte.** PolyKybdHost has
  had `classify_commit_reply` under test for a while, but those tests encode the
  firmware's reply bytes as fixtures — so they only catch the *host* misreading a
  status, which is the opposite direction from the bug that actually shipped. Both ends
  of the font-pack COMMIT contract are now pinned: `fontpack_commit_status()` is a pure
  `static inline` in `hid_fontpack.h` (which is why that header now includes
  stdint/stdbool instead of `quantum.h` — nothing in it needed quantum.h).

Wiring a new one needs **two** registrations plus one non-obvious source list:

- `builddefs/testlist.mk` — `include <path>/tests/testlist.mk` (which does
  `TEST_LIST += <name>`). ⚠️ Test names **cannot contain `-`**; the makefile
  hard-errors.
- `builddefs/build_test.mk` — `include <path>/tests/rules.mk`, alongside the
  `quantum/*/tests/rules.mk` lines. This defines `<name>_SRC/_INC/_DEFS`.
  - ⚠️ **A test under `keyboards/` must NOT name that file `rules.mk` — CI reads it as
    a KEYBOARD.** `qmk ci-validate-keyboard-targets` globs `keyboards/**/rules.mk` and
    flags every hit whose path lacks a directory named `keymaps`, `common` or `lib` and
    which has no `keyboard.json` beneath it (`lib/python/qmk/cli/ci/
    validate_keyboard_targets.py`, 17 lines — read it, it is the whole rule). There is
    no exemption for tests, because upstream keeps none under `keyboards/`. The failure
    is `keyboards/polykybd/base/tests::Legacy target detected` and it is a **separate
    lint-job step from `qmk lint`**, so `qmk lint --strict` passing tells you nothing
    about it. `keyboards/polykybd/base/tests/` therefore uses **`test_rules.mk`**; the
    `build_test.mk` include names the file explicitly, so the name is free.
    (`testlist.mk` is unaffected — only `rules.mk` is globbed.) Cost a CI round
    2026-08-17.
  - ⚠️ **So run the WHOLE lint job locally, not just `qmk lint`.** The recipe in the CI
    section above already lists all of it; the two `ci-validate-*` commands are the
    ones easy to skip, and they are ~1 s each:
    ```bash
    qmk ci-validate-keyboard-targets && qmk ci-validate-aliases   # both must exit 0
    ```
- ⚠️ **A standalone test must put the timer in its own `_SRC`.**
  `platforms/common.mk` adds `platforms/timer.c` + `platforms/test/timer.c` to `SRC`,
  which only the **full-keyboard** harness consumes — so a standalone test links with
  `undefined reference to timer_read32 / timer_elapsed32` until you list both files
  yourself.
- ⚠️ **`set_time()` / `advance_time()` have no header.** They are defined only in
  `platforms/test/timer.c`; every test that drives the clock forward-declares them
  (see `quantum/sequencer/tests/sequencer_tests.cpp`).
- **Mutation-test the suite before trusting it — the `mutation-test-suite` skill is
  the recipe.** Break the thing on purpose (swap a
  byte order, delete a bound) and confirm the *expected* test fails — the same
  discipline as "verify against the rendered glyph shape, not a transform∘inverse
  round-trip". A suite that passes against a deliberately broken driver is measuring
  nothing. `fw_up_verdict` was validated this way against 7 mutations (dropped
  `!status_ok` guard, `recorded != SYNC_ACK` as the refusal test, probe outranking an
  explicit refusal, `sync_succeeded` as a blacklist, a 1-bit-spaced ack value, a slave
  refusal reported as retryable, and a CRC check that always passes) — each caught by
  the intended test.
  - ⚠️ **A suite that expresses every expectation IN TERMS OF the constant under
    test cannot catch a change to that constant — it moves both sides of the
    comparison at once.** Measured 2026-09-06 on `polykybd_ai_light`, whose fade
    holds for a minute: every case was written as `HOLD - 1`, `HOLD + FADE`, and
    so on, which reads as thorough and is, for the *curve*. Mutating
    `AI_LIGHT_HOLD_MS` from 60000 to **1000** left all 11 tests green — a light
    that goes out after one second instead of a minute, i.e. the whole
    user-visible requirement, unpinned. The fix is one test carrying the
    **literal** (`EXPECT_EQ(60000u, AI_LIGHT_HOLD_MS)` plus two samples either
    side of it), on the same reasoning as the host repo pinning `WINDOWS_APP_ID`:
    a stated requirement needs something that states the number, and only an
    implementation detail may be expressed relative to itself. Symbolic
    expectations are still right for everything else — the point is that at least
    one case has to be anchored outside the symbol.
  - ⚠️ **Probe a bound WELL PAST it, not one unit past — one unit is where the
    arithmetic still looks right.** The same suite clamps a ramp at the end of its
    window and tested `HOLD + FADE` and `HOLD + FADE + 1`. Deleting the clamp
    passed both: at +1 the unclamped expression rounds to the same 0 it should
    be, and the underflow only becomes visible further out. The escaped mutation
    is a light that comes back on **hours later at a wrong brightness**, which is
    exactly the shape nobody would find by hand. Sweep the decade — `2×`, an
    hour, a day, `0xFFFFFFFF` — for any bound whose far side is unbounded.
  - ✅ **A `_Static_assert` is mutation-checked in SECONDS with a standalone
    translation unit — do not reach for `qmk compile`.** The assert usually rests
    on a chain of `#define`s and nothing else, so copy that chain into a throwaway
    `.c`, add the assert and a `main`, and compile it with the host `gcc`. Verified
    on `FW_HAND_STAMP_OFFSET`'s alignment assert (#282, 2026-09-09): the shipped
    values pass, and adding 512 to `FW_APPLY_LOG_BYTES` fails with the assert's own
    message — the whole loop in about two seconds, against ~4 minutes for a
    firmware build per mutation.
    ```bash
    cat > /tmp/a.c <<'EOF'
    #define FW_RESOURCE_OFFSET   0x400000UL
    #define FW_APPLY_LOG_BYTES   (8UL * 4096UL)          /* mutate me: + 512UL */
    #define FW_APPLY_LOG_OFFSET  (FW_RESOURCE_OFFSET - FW_APPLY_LOG_BYTES)
    #define FW_CRASH_LOG_OFFSET  (FW_APPLY_LOG_OFFSET - 4096UL)
    #define FW_HAND_STAMP_OFFSET (FW_CRASH_LOG_OFFSET - 4096UL)
    _Static_assert(FW_HAND_STAMP_OFFSET % 4096UL == 0, "not sector-aligned");
    int main(void){ return 0; }
    EOF
    gcc -o /dev/null /tmp/a.c            # must PASS as shipped, FAIL when mutated
    ```
    ⚠️ **Mutate a constant the assert DEPENDS on, not the assert itself.** Editing
    the condition proves only that the compiler evaluates it; moving an input proves
    the assert would catch the edit somebody will actually make. And copy the chain
    verbatim — a retyped one that happens to stay aligned passes for the wrong reason.
  - ⚠️ **Strip ANSI escapes before grepping gtest output, or the mutation harness
    FAILS OPEN.** gtest prints `\e[0;32m[  FAILED  ]`, so a regex anchored on a leading
    `[` matches nothing and **every** mutation reads as "still green" — i.e. the
    harness reports the exact result that means "your tests are worthless", for all of
    them, which is itself the tell that the detector and not the suite is broken. Pipe
    through `sed 's/\x1b\[[0-9;]*m//g'` first. Cost a full round (2026-08-17); a
    single manual mutation is the 30 s way to confirm the harness works before
    trusting a sweep.
  - ⚠️ **A second fail-open path, same shape: a mutation that never APPLIED.** A
    failed `sed`/`perl` (a C `||` collides with `sed`'s `s|…|…|` delimiter) leaves
    the source untouched, the suite passes because nothing was broken, and the
    empty "caught by:" reads identically to "not caught". Assert the edit landed
    (`git diff --quiet` on the mutated file) before believing the run. Hit while
    dogfooding the skill, 2026-08-18.
  - ⚠️ **…and that `git diff --quiet` guard is itself fail-open unless it
    compares the PRE-MUTATION baseline.** Diffing against **HEAD** only works
    while the tree is clean: mutate code you have not committed yet — the normal
    case, since you mutation-test a suite right after writing it — and the guard
    sees your own feature diff, reports "changed", and passes for every
    mutation whether or not any applied. So the check meant to catch a
    non-applied mutation is exactly the one that stops working when you need it.
    Copy the file first and compare to the copy:
    ```bash
    cp path/to/src.c /tmp/base.c                      # pre-mutation baseline
    ...apply mutation...
    diff -q /tmp/base.c path/to/src.c >/dev/null; rc=$?
    case $rc in
      0) echo "MUTATION DID NOT APPLY - result meaningless" >&2; exit 1 ;;
      1) ;;                                           # applied, carry on
      *) echo "diff failed ($rc) - baseline unreadable?" >&2; exit 1 ;;
    esac
    ...run suite, restore with: cp /tmp/base.c path/to/src.c
    ```
    ⚠️ **The guard has to EXIT, not just print** — a third instance of the same
    family, caught in review of this very note (CodeRabbit, #221). A bare
    `diff … && echo "DID NOT APPLY"` returns 0 and the loop carries on to report
    the mutation as "not caught", with the warning buried in a screen of gtest
    output. And `diff` has **three** exit codes — `0` same, `1` differs, `2`
    could not read a file — so `else`-ing on "not 0" silently treats a missing
    baseline as a successful mutation. Hence the `case`.
    ⚠️ That restore **overwrites whatever is in the file**, and by this note's
    own premise the tree is uncommitted — so there is no git copy to recover
    from. Do not edit the source between mutating it and restoring it.
    Hit on the host repo 2026-08-20 (Python, same shape — the family is not
    C-specific). The run happened to be sound because every mutation *did* apply
    and turned the suite red, but that was luck: the guard could not have told
    me otherwise. **A fail-open guard that is only correct on a clean tree is a
    fail-open guard.**
