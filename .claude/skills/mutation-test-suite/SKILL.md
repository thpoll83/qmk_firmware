---
name: mutation-test-suite
description: Prove a googletest suite actually detects breakage before trusting it — deliberately break the code under test N ways, confirm each mutation is caught, and confirm the INTENDED test is the one that fails. Use right after writing or extending a `make test:<name>` suite, when reviewing a PR that adds tests, when a suite has never failed and you want to know whether it can, or when asked "are these tests any good / do they actually test anything". NOT for finding bugs in the code (that is what the tests are for) and NOT for firmware behaviour on hardware (see diagnose-hil-failure / measure-firmware-perf).
---

# Mutation-test a googletest suite

A suite that stays green against a deliberately broken implementation is
measuring nothing. `CLAUDE.md` already requires this ("Mutation-test the suite
before trusting it") but does not say how — and **two** separate traps make the
harness **fail open**, reporting every mutation as "still green", which is the
exact result that means your tests are worthless, for all of them at once. Both
are called out below; neither announces itself.

The output is a table: mutation → caught? → by which test.

## 0. Environment

```bash
cd /home/user/qmk_firmware
export QMK_HOME=$PWD
export PATH="/root/.qmk_venv/bin:$PATH"     # qmk is NOT on PATH in the container
make test:<name>                            # baseline must be GREEN before you start
```

If the baseline is red, stop — you are debugging, not mutation-testing.

## 1. Choose mutations that map to real mistakes

Aim for **4–7**. Each should be a plausible edit someone could actually make, and
each should have **one test you expect to catch it**. Write that expectation down
*before* running — an unexpected pass is the finding.

The productive categories, with worked examples from this repo:

| Category | Example |
|---|---|
| **Drop a guard** | remove the `!status_ok` check in `fw_up_classify_commit_failure` |
| **Invert a comparison** | `recorded != SYNC_ACK` as the refusal test |
| **Reorder precedence** | let the probe outrank an explicit refusal |
| **Whitelist → blacklist** | `sync_succeeded` listing failures instead of successes |
| **Enumerate instead of complement** | `sync_is_link_fault` listing its non-fault siblings |
| **Weaken a constant** | an ack value 1 bit from another; `SYNC_BUSY` as `0x00` |
| **Always-succeed** | a CRC check that returns true unconditionally |

⚠️ **The mutation must be in the code under test, not in the test.** Editing an
assertion proves nothing.

## 2. Run the sweep

```bash
SRC=keyboards/polykybd/base/sync_ack.h        # the file being mutated
SUITE=fw_up_verdict
cp "$SRC" /tmp/mut.bak

run() {  # prints the failing test names, or nothing if the suite stayed green
    make test:$SUITE 2>&1 \
      | sed 's/\x1b\[[0-9;]*m//g' \
      | grep -E '^\[  FAILED  \] [A-Za-z]' \
      | sed 's/ ([0-9]* ms)$//' | sort -u
}

mutate() {  # apply, and PROVE it applied — see the second fail-open trap below
    perl -0pi -e "$1" "$SRC"
    if git diff --quiet "$SRC"; then
        echo "MUTATION DID NOT APPLY — pattern did not match. Aborting."
        return 1
    fi
}

# --- one mutation ---
mutate 's/return \!got_reply \|\| ack == SYNC_CRC32_ERR;/return false;/' \
    && echo "M1 caught by: $(run | tr '\n' ' ')"
cp /tmp/mut.bak "$SRC"        # ALWAYS restore before the next mutation
```

⚠️ **Use `perl -0pi -e`, not `sed -i`, when the code contains `|`.** A C `||`
collides with `sed`'s `s|…|…|` delimiter and the command dies with
`unknown option to \`s'` — which brings us to the second trap.

⚠️ **`sed 's/\x1b\[[0-9;]*m//g'` is not optional.** gtest prints
`\e[0;32m[  FAILED  ]`, so a regex anchored on a leading `[` matches **nothing**
and every mutation reads as "still green". That is a **fail-open** harness: it
reports the one result that means your tests are worthless, for every mutation,
which is itself the tell that the *detector* is broken and not the suite. This
cost a full round on 2026-08-17.

## 3. Read the result correctly

For each mutation, three outcomes:

- **Caught by the expected test** — the assertion is doing its job.
- **Caught by a different test** — fine, but ask whether the expected test is
  actually asserting what its name claims.
- **Not caught at all** — the real finding. Either add the missing test, or
  conclude the mutation is genuinely unobservable (say which, don't hand-wave).

Restore the source and re-run the suite green at the end. Verify with
`git status --short` that nothing is left mutated — shipping a mutation is the
one way this skill can do harm.

## 4. Report

```
MUTATION TEST — <suite>, N mutations
  M1 <what was broken>              → caught by <TestName>
  M2 <what was broken>              → caught by <TestName>
  M3 <what was broken>              → NOT CAUGHT  ← finding
  ...
  baseline restored, suite green (git status clean)
```

Put the list in the PR body. It is the evidence that the tests are worth their
line count, and it is what a reviewer cannot easily reproduce.

## Pitfalls

- **The ANSI escape trap above** — the single most likely way this goes wrong,
  and it fails silently in the direction of "everything is fine".
- ⚠️ **A mutation that never applied looks exactly like a mutation that was not
  caught** — the second fail-open path, hit while dogfooding this very skill
  (2026-08-18). A failed `sed`/`perl` prints its error on stderr, which is easy
  to miss in a loop, leaves the source untouched, and the suite then passes
  because *nothing was broken*. `run()` prints nothing, and "caught by: " with an
  empty value reads identically to "NOT CAUGHT". Both fail-open paths share one
  shape: **the harness reports the alarming result for a reason that has nothing
  to do with the tests.** Always assert the edit landed (`git diff --quiet` is
  enough) before believing the run.
- **Verify the harness before trusting a sweep.** If *all* mutations report
  "still green", suspect the detector, not the suite. One manual mutation checked
  by eye is the 30 s way to confirm the pipeline works before believing a clean
  sweep.
- **Restore between mutations**, not just at the end — otherwise M3 is being
  tested against M1+M2+M3 and a later "caught" tells you nothing about M3.
- **Don't mutate a test file.** Only the code under test.
- **A test that fails for every mutation** is probably too broad to localise a
  regression; that is worth noting even though it is technically "catching".
- **Don't commit the mutations.** `git status --short` before you finish.
