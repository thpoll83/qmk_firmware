---
name: mutation-test-suite
description: Prove a test suite actually detects breakage before trusting it — googletest (`make test:<name>`, firmware) or Python/unittest (PolyKybdHost, polykybd-ctnd) — deliberately break the code under test N ways, confirm each mutation is caught, and confirm the INTENDED test is the one that fails. Use right after writing or extending a `make test:<name>` or `*_test.py` suite, when reviewing a PR that adds tests, when a suite has never failed and you want to know whether it can, or when asked "are these tests any good / do they actually test anything". NOT for finding bugs in the code (that is what the tests are for) and NOT for firmware behaviour on hardware (see diagnose-hil-failure / measure-firmware-perf).
---

# Mutation-test a suite — googletest or Python/unittest

A suite that stays green against a deliberately broken implementation is
measuring nothing. `CLAUDE.md` already requires this ("Mutation-test the suite
before trusting it") but does not say how — and **three** separate traps make the
harness **fail open**, reporting every mutation as "still green" (or nothing at
all), which is the exact result that means your tests are worthless, for all of
them at once. All three are called out below; none announces itself, and each was
hit for real. **If a sweep looks unanimously clean, suspect the harness first.**

The output is a table: mutation → caught? → by which test.

## 0. Environment (googletest / firmware)

For a Python/unittest suite (`PolyKybdHost`, `polykybd-ctnd`) skip to §2b — the
method in §1, §3 and §4 is the same, only the mechanics differ.

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
    if cmp -s "$SRC" /tmp/mut.bak; then
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

## 2b. The Python / unittest variant (PolyKybdHost, polykybd-ctnd)

The discipline is identical; three things change. Used on host#218 to check the
`glyph_script_preview` and `os_theme` fixes (3 mutations, each caught by its own
test).

```bash
cd /home/user/PolyKybdHost
run() {  # file, python-mutation, test-module, expected-failing-test
  f="$1"; mut="$2"; mod="$3"; want="$4"
  cp "$f" /tmp/base.py                                   # PRE-mutation baseline
  python3 - "$f" <<MUT
import io,sys
p=sys.argv[1]; s=io.open(p,encoding='utf-8').read()
$mut
io.open(p,'w',encoding='utf-8').write(s)
MUT
  diff -q /tmp/base.py "$f" >/dev/null; rc=$?
  case $rc in
    0) echo "MUTATION DID NOT APPLY ($f) - result meaningless"; cp /tmp/base.py "$f"; return 1;;
    1) :;;
    *) echo "diff failed ($rc) - baseline unreadable?"; cp /tmp/base.py "$f"; return 1;;
  esac
  out=$(xvfb-run -a env QT_QPA_PLATFORM=offscreen .venv/bin/python -m unittest "$mod" 2>&1 \
        | sed 's/\x1b\[[0-9;]*m//g')
  cp /tmp/base.py "$f"                                   # restore FIRST
  echo "$out" | grep -q '^Ran [0-9]* test' || { echo "NO SUMMARY LINE - harness broken"; return 1; }
  if echo "$out" | grep -qE "^(FAIL|ERROR): $want[ (]"; then echo "CAUGHT   by $want"
  else echo "ESCAPED  ($want did not fail)"; echo "$out" | grep -E '^(FAIL|ERROR):' | head -3; fi
}
```

⚠️ **Pick an outer heredoc delimiter the inner one cannot collide with.** The
mutation is itself a heredoc, so reusing `PY` for both ends the outer one early and
the shell executes half a Python script — which is how the first attempt at writing
*this very section* failed. `MUT` inside, something else outside.

Three differences from the googletest sweep:

1. **Mutate with a Python heredoc, not `sed`/`perl`.** Python source is full of
   quotes and `|`; `s.replace()` on the file text is exact and cannot half-apply.
   ⚠️ The heredoc delimiter is **unquoted** (`<<MUT`, not `<<'MUT'`) — that is what
   interpolates `$mut` into the script, and it is the whole mechanism. Quote it and
   the mutation is never applied: every call then trips the "did not apply" guard,
   which is the guard working but a recipe that cannot run. The cost of unquoting
   is that `$`, backticks and `\` inside the mutation are shell-expanded too, so
   build any literal quote as `chr(34)` and keep `$` out of the mutation text.
2. **The failure line is `^(FAIL|ERROR): <TestName>`**, not gtest's `[  FAILED  ]`
   — and `ERROR:` matters as much as `FAIL:`, since a mutation that breaks an
   import or raises shows up there.
3. **Judge the run by `Ran N tests` existing**, not by the absence of failures.
   ⚠️ In Qt code a mutation can **abort the interpreter**: an exception escaping a
   PyQt slot makes Qt call the excepthook and then `qFatal`, so the process dies
   before unittest prints anything and the grep finds nothing — which reads
   *identically* to "not caught". Same fail-open family as the ANSI-escape trap
   above, one level up. (Measured on `crash_alert_dialog`: narrowing a broad
   `except` to `OSError` aborted mid-suite.)

⚠️ **The `diff` guard must compare a PRE-MUTATION COPY, not `git diff --quiet`
against HEAD.** You mutation-test a suite right after writing it, so the tree is
already dirty with your own feature diff — a HEAD comparison reports "changed" for
every mutation whether or not any applied, and the guard against a non-applied
mutation is exactly the one that stops working when you need it. And it must
**exit**, not just print: a bare `&& echo "DID NOT APPLY"` returns 0 and the loop
carries on to report the mutation as "not caught", with the warning buried in a
screen of test output. `diff` has three exit codes — `0` same, `1` differs, `2`
unreadable — so `else` on "not 0" silently treats a missing baseline as success;
hence the `case`. ⚠️ The restore overwrites the file, and by that premise there is
no committed copy to recover from: **do not edit the source between mutating and
restoring.**

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
  baseline restored, suite green
```

⚠️ **On the Python path "git status clean" is the WRONG completion check** — that
procedure deliberately supports a dirty worktree (you are testing a suite you have
just written), and it restores from `/tmp/base.py`, not from git. Verify instead
that every mutated file is byte-identical to its pre-mutation copy and that no
mutation is left behind: `diff -q /tmp/base.py <file>` per file, plus a green
suite. On the googletest path the tree is usually clean and `git status --short`
is the cheaper check.

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
  to do with the tests.** Always assert the edit landed before believing the run.
- ⚠️ **Compare against the BACKUP (`cmp -s "$SRC" /tmp/mut.bak`), not against git.**
  `git diff --quiet "$SRC"` looks like the natural applied-check and is wrong in the
  normal case: you are usually mutating code you have just written and not yet
  committed, so the file is *already* dirty against HEAD, the guard's condition never
  holds, and `mutate` returns the wrong status — with `mutate … && run`, the entire
  sweep is skipped and prints nothing at all. That is this trap's third disguise, hit
  on 2026-08-18 while using the skill on a fresh classifier: **four mutations, zero
  output, no error.** Silence is not success — if a sweep prints no "caught by" lines,
  the harness broke, not the suite.
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
