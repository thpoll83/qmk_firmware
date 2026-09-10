# CI checks: the HIL tiers, the workflows and reading a red board

What each check does, how to ask for the deeper HIL tiers, why a workflow did or
did not run, and how to read the log when one is red. Moved out of `CLAUDE.md` on
2026-09-10: ~48 KB read when a check is red or when you are editing a workflow.

Two skills cover slices of this: **`diagnose-hil-failure`** classifies a RED rig
check, **`debug-firmware-on-rig`** drives the probe loop. This file is the rest —
the trigger mechanics, the tiers, and the traps in reading a run.

⚠️ **The rig runs the INSTALLED station synced to ctnd `main`, not your branch.**
So a HIL test added in an unmerged `polykybd-ctnd` PR does not exist on the rig:
the suite goes green having never executed it, and the firmware PR's checklist
claims coverage nothing produced. **Grep the HIL job log for the test's own name**
(each prints `[test] PASS: <name>`), never just the job conclusion.

⚠️ **A job that never STARTS is a different state from a red one and alerts
nobody.** Read `status` before `conclusion`: an unfinished run carries no verdict
in either shape, so "not failed" is never "passed".

---

## What gates, and the deeper tiers

- **`Build firmware`** and **`HIL test (split72)`** (the polykybd-ctnd rig) are the
  **real** checks; these are what must go green. Use the `diagnose-hil-failure` skill
  for the HIL side.
  - ⚠️ **The HIL suite has TWO tiers, and the default one deliberately skips the
    deepest checks.** The rig's slow checks — the startup animation, idle engage +
    the Eden screensaver, a 450-frame split-link soak, and a reboot power cycle that
    is the ONLY thing verifying user state survives a power loss — are
    `TIER_EXTENDED` (polykybd-ctnd `station/hil_tests.py`) and add ~50 s, so they run
    only when the run asks. **Ask for them on anything that touches EEPROM/persisted
    state, the split link, the idle/animation paths, or a release.** Three ways, the
    same convention as the `hil-perf` label.
    - ⚠️ **The opt-in labels are the `hil-*` set since 2026-08-29** — `hil-extended`,
      `hil-perf` (the perf job, renamed from `perf`) and `hil-doom` (the FW-9 signed-pack
      job). The bare `perf` / `doom` names, and the `[perf]` / `[doom]` commit markers,
      now fire **NOTHING** — a silent no-op, since the `if:` matches only the new
      strings. Anywhere below still saying "`perf` label" means `hil-perf`.
    - the **`hil-extended`** PR label — it starts its own run: `build` excludes
      `labeled` events (so the auto-labeler cannot re-run the pipeline) with a
      deliberate **exception for this one label**, matched on
      `github.event.label.name`. ⚠️ **Do not try to pick the label up by re-running
      an existing run** — a re-run replays the ORIGINAL event payload, so a label
      added afterwards is invisible and the re-run silently repeats the default
      tier (caught by CodeRabbit on #223; it is also why `hil-perf` works on a label
      and this did not until the exception was added);
    - **`[hil-extended]`** in a commit message — PUSH events only (`head_commit` does
      not exist on a `pull_request` event), i.e. after a merge / at release time;
    - a manual **`workflow_dispatch`** (default-branch copy only).
    The job log says which tier ran (`suite tier: …`), and so does the runner
    (`[runner] suite tier: …`) — read it before concluding a green HIL board covered
    the reboot/link checks, because by default it did not. Locally on the rig:
    `python -m station.test_runner --extended` (or `HIL_EXTENDED=1`), or the touch
    UI's **Extended** toggle beside Run Tests.
  - ✅ **`workflow_dispatch` runs all THREE opt-ins at once — the extended HIL tier,
    the perf measurement AND the FW-9 doom set** — because it satisfies every opt-in
    `if:` at once (each names `github.event_name == 'workflow_dispatch'`).
    ⚠️ **Since the `tier` input (2026-09-01) that is what `tier: all` does, and `all`
    is the DEFAULT**, so an ordinary dispatch still behaves exactly as described here.
    The narrower values (`default`, `extended`, `perf`, `doom`, `fwapply`, `debug`)
    each drive one opt-in, and `tier` is read **only** under
    `github.event_name == 'workflow_dispatch'` — push and pull_request keep their
    label/marker gating untouched. `debug` is the odd one out: it **skips the graded
    suite entirely** (the only `if:` on `hil-test`) and runs just the probe, because a
    debug loop pays the rig cost on every iteration. The whole table was verified by
    simulating the conditions across every trigger, not by reading them — that is the
    only way to check a folded-scalar `if:` short of merging and waiting. They are
    otherwise INDEPENDENT — each opt-in drives only its own job, so `hil-extended`
    alone starts no perf run and `hil-perf` alone leaves the HIL suite on its default
    tier. ⚠️ Dispatch is not the *only* way to get them: the matching labels on a PR, or
    **the markers in one pushed commit message** (`… [hil-extended] [hil-perf] [hil-doom]`),
    do it too. **A dispatch is also the way to exercise the doom set when there is no
    open PR to label** (used 2026-08-29, run #894, right after the trilogy merged). The commit-message form is the natural release-time route — a release push
    is a push — but it only fires where the workflow listens for pushes at all,
    i.e. **`PolyKybd`, and only `PolyKybd`**; the same marker in a commit pushed to
    a `claude/**` branch starts nothing. ⚠️ The trigger used to read `[PolyKybd,
    "PolyKybd/**"]` and that second pattern was **unreachable**: git refuses a ref
    that is a PREFIX of another ref, so `refs/heads/PolyKybd/<x>` cannot exist while
    `refs/heads/PolyKybd` does (`git ls-remote --heads origin 'refs/heads/PolyKybd/*'`
    → 0, ever). It read as a staging-branch escape hatch and was not one — git
    refuses to create the branch, and a differently-named one silently runs nothing.
    Dropped from `qmk-test.yml` and `polykybd-unit-test.yml` (2026-09-01).
    What dispatch buys is needing no label bookkeeping, which also sidesteps
    the two-labels-in-one-call trap below (that fires two runs).
    Whichever route, the combination is safe: `perf-test` has `needs: [build-perf,
    hil-test]`, so the rig runs the suite first and measures afterwards rather than
    interleaving two flashes. Measured on run #805 (2026-08-20): ~3 min of rig time
    for the extended HIL suite plus ~1 min for the perf pass, inside a ~8.5 min
    wall-clock run (the two cloud builds are most of it).
  - ⚠️ **The opt-in condition is a YAML FOLDED SCALAR (`>-`), and indenting its
    continuation lines for readability breaks it SILENTLY.** `HIL_EXTENDED` is one
    `${{ … }}` expression spread over four lines. A folded scalar joins lines with a
    space **only while they share one indent level**; a line indented *deeper* than the
    first is treated as more-indented content and keeps its **literal newline**. So the
    prettier-looking form — first line at the block indent, the `contains(...)` clauses
    indented under it — embeds newlines inside the expression and the value stops being
    a valid GitHub expression. Nothing warns: the workflow still parses as YAML, the
    step still runs, and `HIL_EXTENDED` just comes out wrong, so the run quietly executes
    the **default** tier while the label says otherwise. Keep every continuation line at
    the *same* indent as the first (that is why the block looks under-indented), and
    verify rather than eyeball it:
    ```python
    import yaml
    d = yaml.safe_load(open(".github/workflows/qmk-test.yml").read())
    v = d["jobs"]["hil-test"]["env"]["HIL_EXTENDED"]
    assert "\n" not in v, repr(v)
    print(d[True]["pull_request"])   # the `on:` block — see the d[True] note below
    ```
    ⚠️ **Reading the `on:` block needs `d[True]`, not `d["on"]`** — PyYAML resolves the
    bare key `on:` to the **boolean** `True` (YAML 1.1 truthiness), so `d["on"]` raises
    `KeyError` on every GitHub workflow. `jobs` is an ordinary string key; mixing the two
    up raises `KeyError: 'jobs'`, which is how the first version of this very snippet was
    wrong. **Two other blocks use the same `>-` shape and deserve the same check**:
    the `build` job's `if:` (the `hil-extended` label exception) and `build-perf`'s
    (the `hil-perf` opt-in) — a folded `if:` that gains a newline evaluates to a string
    rather than a boolean, so the job silently stops matching its trigger. The
    `build-doom` `if:` (the `hil-doom` opt-in) is the same shape and deserves the same
    check.
  - **The FW-9 doom set is a THIRD opt-in tier (`hil-doom`), and it signs its test
    artifact with an EPHEMERAL key — never the production one.** `build-doom` +
    `doom-test` (mirroring `build-perf`/`perf-test`) build a doom-flavour HIL image
    pair + a matching signed `.plyx`, then the rig drives the load-time Ed25519 gate in
    `doom_pack_load.c` (accept / tampered / unsigned — `TIER_DOOM` in the ctnd suite).
    ⚠️ **The production `FW_SIGNING_KEY` is confined to `release.yml` and must NOT be
    used by a PR-triggered workflow.** So `build-doom` **generates a throwaway Ed25519
    keypair** (`gen_signing_key.py`, which rewrites `base/fw_pubkey.h`), builds the HIL
    images with that disposable pubkey baked in, and signs the pack with the ephemeral
    seed (`sign_doompack.py --privkey`, asserting the size grew by exactly +64). The
    signature code runs against a real — but disposable — key, so the real key never
    touches a PR run; the images are throwaway (a non-shipping pubkey) and the next
    normal run reflashes the shipping image. This is the pattern for any future
    signature-gated HIL test. The accept path is reachable because the `.plyx`'s
    `ram_base` = the pack-flavour ELF's `__overlay_pool_base__`, pinned at `0x20000000`
    by the DOOMPACK ldscript, so a same-commit pack passes the RAM-pairing gate and
    *reaches* the Ed25519 check.
    - ⚠️ **The `.plyx` from `build_pack.sh` is ROOT-OWNED (docker), so signing it
      in place FAILS on the host runner** — `PermissionError [Errno 13]` at
      `sign_doompack.py`'s `write_bytes`. The pack is emitted by the `qmkfm/qmk_cli`
      **container** (runs as root) into the mounted workspace; the next step is a host
      `run:` as the `runner` user and `sign_doompack.py` rewrites the file in place, so
      it needs write access it doesn't have. `release.yml` never hit this because it
      signs the host-produced `.bin` and only *reads* (`cp`) the `.plyx`. Fix:
      `sudo chown "$(id -u):$(id -g)" "$PLYX"` before signing (GitHub-hosted runners
      have passwordless sudo). Found on the first real `hil-doom` run (dispatch #894,
      2026-08-29); build-doom failed at signing so `doom-test` was `skipped` (it
      `needs: build-doom`).
  - ⚠️ **The zizmor `artipacked` / `persist-credentials: false` warning is MOOT on
    these build jobs, and setting it can BREAK the build.** `build`/`build-perf`/
    `build-doom` upload only **named-glob** artifacts (`*.uf2`, `*.plyx`), never the
    repo root or `.git`, so a persisted checkout token cannot ride along in an
    artifact — the exfiltration path the warning describes does not exist here.
    Meanwhile `persist-credentials: false` interacts with `submodules: recursive`
    (recursive submodule auth can depend on the persisted credential), which is exactly
    what these builds need for the ChibiOS/pico-sdk tree. So the build jobs deliberately
    omit it; `build-doom` carries `permissions: contents: read` instead (least
    privilege, matching the rig test jobs). CodeRabbit accepted this and withdrew the
    finding (#244, 2026-08-29). ⚠️ Pinning `docker://qmkfm/qmk_cli` to an immutable
    digest is a *real* hardening but a **repo-wide** one — 10 uses in `qmk-test.yml` +
    more in `release.yml` — so it is a deliberate all-uses-at-once change (ideally with
    Dependabot), never a piecemeal one-job edit.

---

- **The FW-APPLY set (`build-fwapply` + `fwapply-test`) is the fourth tier, and it
  is the ONLY one that runs unasked — on every push to `PolyKybd`.** It builds a
  HIL image pair, signs the master `.bin` with an **ephemeral** key
  (`gen_signing_key.py` rewrites `base/fw_pubkey.h`, same pattern as `build-doom`
  — the production `FW_SIGNING_KEY` stays confined to `release.yml`), then has the
  rig drive a real HID update through **APPLY** and confirm the board comes back.
  It is also reachable by the `hil-fwapply` label, `[hil-fwapply]` in a pushed
  commit, or `tier: fwapply`.
  - ⚠️ **Why it is not opt-in, when everything else is: the HID-apply brick
    (qmk#258) shipped in a RELEASE, and it was a LAYOUT effect.** `fw_staging`'s
    page buffer was `static uint8_t` (alignment 1) and word-copied; a macro PR grew
    `.bss`, shifted it off a word boundary, and the unaligned `STMIA` HardFaulted
    on the M0+ inside a function that never returns — recoverable only over
    BOOTSEL. So **the guilty PR never touched the applier**, `fw_staging_do_apply`
    was byte-identical across the regression, and the bisect blamed the wrong
    commit. Any PR can move `.bss`, so per-PR is not where this class is catchable;
    what IS catchable is dating it to a **merge** that can still be bisected. A
    release-time-only check would name the release that bricks, with no bisect and
    a release to redo.
  - **The cost is bounded**: the rig already runs a HIL cycle per merge, so this
    adds a cloud build plus one apply+reboot, not a new cadence. PRs are untouched
    — the tier stays opt-in there, so no PR pipeline gets slower.
  - **The rig's post-apply SLAVE check is a separate opt-in on the same signals**
    (`hil-fwapply` label, `[hil-fwapply]` in a pushed commit, `tier: fwapply`/`all`
    dispatch), carried as **`HIL_RESLAVE`** in the step's `env:`. An apply converts
    the rig's slave into a second master — it installs the master's bridged bytes,
    and the halves run per-side images by construction — so the link check inside
    the apply can only report UNVERIFIED there. With the opt-in, ctnd re-flashes
    `*_hil_right.uf2` and measures, which answers the answerable half: **can the
    APPLIED master image bring a split link back up.**
    - ⚠️ **OFF on a plain push, deliberately.** This job runs on every merge, and
      `require_fwapply_run.py` gates publishing on its conclusion being `success`
      — so anything that can fail here can refuse a release, and the rig-side
      check has never executed. Prove it with a dispatch, then decide.
    - ⚠️ **Passed as an ENV VAR, not ctnd's `--reflash-slave` flag.** CI runs the
      INSTALLED station synced to ctnd `main`, so a station predating the feature
      would die on an unknown argparse flag — failing the release gate. An unknown
      env var is ignored, so an old station just skips the check. That also
      removes the merge-order constraint between the two repos.
  - ⚠️ **OPEN (2026-09-04): the apply job went red on the merge of #274 with the
    link dead, and the cause is NOT established — do not theorise one from this
    entry.** Run 992, job `101021479366`: the master came back on the right
    version and every other assertion passed, then the post-apply soak measured
    `57142 tx crc_err=0 nack=0 transport_fail=57142 giveup=17995 err=100.0%` and
    the test failed with *"the master came back but the split link did not"*.
    Two facts that bound it, and nothing further was determined:
    - **`HIL_RESLAVE` was OFF** (a plain push, no `hil-fwapply` label or marker),
      so the slave was never re-flashed and the rig **graded** the link rather
      than reporting UNVERIFIED — i.e. it did not observe the two-masters state
      that the structural note above says an apply necessarily produces on this
      rig. Whether that is because the enumeration read `unknown` (which that path
      treats as one master) was not settled.
    - **The merged change cannot plausibly produce 57k transport failures**: in a
      non-crash-test build its only live delta is at most two extra bounded RPC
      attempts per link-up.
    100% `transport_fail` with `crc_err=0` is the same signature as the
    2026-09-03 field report AND as the benign two-masters case, which is exactly
    why it cannot be read off the numbers alone. The next step is a dispatch with
    `tier: fwapply` (which also turns `HIL_RESLAVE` on) to see whether it
    reproduces.
  - ⚠️ **`--apply-bin` is destructive by design and safe only because the image is
    the one already running** — the rig checks that pairing rather than assuming
    it. And it is safe *at all* only because a brick on the rig is self-recovering
    over GPIO BOOTSEL.
  - ⚠️ **An ephemeral-key image REFUSES a production-signed image over HID** (its
    baked pubkey does not match, so the keyboard raises the A/ACCEPT prompt, which
    the rig cancels). Benign on the rig, which recovers itself — but it is why an
    ephemeral-key build must never be handed to a user to flash.
  - ⚠️ **"On every push" is only as good as GitHub DELIVERING the push event, and it
    does not always — measured, on the very merge that added this tier.** Merging
    #264 (`43cc2559`, 2026-09-01) created **no push-event workflow run at all**:
    not `qmk-test`, not `cppcheck`, not `polykybd-unit-test`. That last one is the
    decisive part — its **`push` trigger has no `paths:` filter** (just
    `branches: [PolyKybd]`), and it had fired on #263's `CLAUDE.md`-only merge hours
    earlier, yet stayed silent on a merge carrying nine C files. So this was not a
    paths filter, and not ours. ⚠️ **Read that as "the PUSH trigger", not the
    workflow** — this note said "no `paths:` filter whatsoever" until 2026-09-02, and
    that is false of its `pull_request` trigger, which filters on
    `keyboards/polykybd/**`, `modules/polykybd/**`, the two `builddefs/*_test*.mk`
    and itself. The conclusion about the dropped push event is unaffected (a merge to
    `PolyKybd` IS a push, and that trigger really is unfiltered), but the sentence
    misleads anyone reasoning about a **PR**: on a docs-only PR the unit tests are
    correctly silent, and reading them as "should always fire" turns a working filter
    into a phantom second dropped-delivery incident. Found while checking exactly
    that on #269. Ruled out
    too: no GitHub Actions incident that day (the one Git Operations incident ran
    15:00–16:01 UTC, hours earlier); the merge was a **direct click** on *Merge
    pull request* by the repo owner — confirmed with them, not inferred — so
    neither the GITHUB_TOKEN no-recursive-runs rule nor the reported
    auto-merge-skips-CI-on-the-target-branch behaviour applies; and every previous
    merge is followed by the same `[skip ci]` auto-bump at +13–16 s, so the bump
    cannot be it either.
    ⚠️ **`merged_by` alone CANNOT settle that** — an auto-merge is still attributed
    to whoever enabled it, so the field reads identically either way. The only
    signals are the timing (auto-merge fires within seconds of the last check
    turning green; this one landed 10 minutes later) and asking the person.
    The run object simply never existed. GitHub's own troubleshooting says a
    workflow that does not trigger "silently does nothing", and dropped runs under
    load are a documented class — so treat this as a **delivery** failure, not a
    config bug, and do not go looking for a mechanism in the workflow.
    - **The design already absorbs it, which is the reassuring half.** The release
      gate (`tools/require_fwapply_run.py`) refuses to publish firmware no apply
      run has covered, so a dropped push event costs a **manual dispatch at release
      time**, not a bricked release. That is the gate doing exactly its job.
    - **Recovery: dispatch *Build and HIL Test* with `tier: fwapply`.** Verified
      2026-09-01, run #959: build ✅, HIL suite ✅ 2m55, apply round-trip ✅ 3m40
      (its first-ever execution).
      ⚠️ **WHICH ref you dispatch on decides whether the gate can ever see it, and
      "the branch" is only right IMMEDIATELY after the merge.** A dispatch attaches
      the run to whatever its `ref` points at. Dispatching on `PolyKybd` therefore
      covers the release sha **only while the tip is still that version's own bump
      commit** — true if you do it right after the merge, as above, and false as
      soon as another PR lands. It is not a general recipe, because the two ends
      move in opposite directions: `publish_release.py` tags the **oldest** commit
      declaring the version (`commit_for_version()`, deliberately not the head),
      while `require_fwapply_run.py` only ever walks **ancestors** of the release
      sha, bounded by `MAX_BUMP_COMMITS`. A run on a *descendant* is invisible to
      it, so a late branch-tip dispatch produces a green run the gate still refuses.
      **Once the branch has moved on, dispatch on the release TAG instead** — the
      API takes a branch *or* a tag, and the tag exists by then (publishing creates
      it, and the gate runs on `release: published`), so the run attaches to the
      release sha itself. ⚠️ Untested here, and it carries its own trap: a dispatch
      runs the workflow **as of that ref**, so a tag predating the `tier` input has
      no `fwapply` value to select. (Caught by Greptile on #265 — the original
      wording generalised one true observation into a rule that only held on the
      day it was written.)
    - ⚠️ **So don't read "runs unasked on every merge" as a guarantee.** After a
      merge you care about, confirm a run actually exists for the merge sha —
      `actions_list list_workflow_runs` filtered `event: push` — rather than
      assuming. A missing run looks identical to a repo where nothing was
      configured.
    - ⚠️ **Add `branch:` to that query and it returns a STALE SUBSET — measured, and
      it reads exactly like the dropped-delivery failure above.** On 2026-09-04
      `list_workflow_runs` with `event: push` **and** `branch: PolyKybd` reported
      `total_count` **25**, newest run dated **2026-08-24**; the same call with
      `event: push` alone reported **217**, newest run **992** twelve minutes old.
      Both were asked seconds apart, so this is not a race. Since `qmk-test.yml`
      pushes only on `PolyKybd`, the filter is a no-op that should change nothing —
      which is what makes it dangerous: an empty-looking result for the merge you
      just made is the exact signature of a dropped push event, and I nearly filed
      a second incident off it. **Use `event: push` with no branch filter, and
      confirm the run you find carries the merge commit's own sha** (`head_sha`)
      rather than trusting the listing's shape.
    - ⚠️ **A THIRD look-alike, and the one to rule out FIRST because it is a
      single field: a MERGE CONFLICT stops every `pull_request` workflow from
      running at all.** Such a workflow runs against the merge ref
      (`refs/pull/N/merge`), which GitHub cannot build for a conflicted PR — so no
      run is queued, no check appears, and the PR looks exactly like one whose
      event was dropped. Measured on host#216 (2026-09-07): CodeQL had fired
      within ~30 s on each of three earlier pushes and then produced nothing for
      **two** consecutive heads over 45 minutes, which read as a dropped delivery
      confirmed by repetition; `mergeable_state` was `"dirty"` the whole time,
      from an auto-bump on `main` touching the line above the one the branch had
      changed. Merging the base in started CodeQL within seconds.
      **So `pull_request_read` `get` and read `mergeable_state` BEFORE reasoning
      about a missing run** — it is one call, it distinguishes the two outright,
      and unlike a dropped delivery a conflict is yours to fix now.
      ⚠️ **Measured a second time on 2026-09-10, in THIS repo, and it is worse
      here: qmk#276 lost `Build firmware`, `cppcheck`, `PolyKybd unit tests` AND
      `HIL test` while `triage` (a `pull_request_target` workflow) and Sourcery
      both fired normally.** So the PR looked alive — two checks present, green —
      with every gate that matters simply absent. A `pull_request_target` run is
      built from the base, not the merge ref, which is exactly why it survives a
      conflict and why its presence is not evidence the others were even queued.
      - ⚠️ **The `check_suite.completed` wake actively points the wrong way here.**
        Its own note says cancelled suites, suites with no runs, the App's own
        suites and legacy statuses are not covered — so on a PR whose CI *cannot
        run* it still says nothing is running or failed, which reads as "CI is
        fine, carry on". It is evidence about third-party suites only, never about
        whether the PR is in a state that can be built.

---

- ✅ **The DEBUG LOOP: a firmware bug can now be chased on the rig with nobody
  flashing a `.bin`.** Dispatch `qmk-test.yml` on a branch with **`tier: debug`,
  `probe: <name>`** and the rig builds that branch, flashes both halves, runs a
  probe committed at `keyboards/polykybd/tools/hil_probes/<name>.py`, and pipes
  the firmware's own console into the job log as `[qmk] …` lines. Edit, push,
  dispatch, read the log. **The `debug-firmware-on-rig` skill drives the whole
  loop** (when a probe beats the graded suite, the dispatch call, reading the
  `[qmk]` lines back out, and the measured cost of a turn); the rig side is
  `polykybd-ctnd/station/probe.py` (`--probe`) and the probe format + pitfalls
  are in `keyboards/polykybd/tools/hil_probes/README.md`. Verified end to end
  2026-09-01, run `33559310727`: `Debug probe (split72)` green with every other
  job — `HIL test (split72)` included — correctly skipped, 5m21s wall clock of
  which 4m03s is the cloud build. This existed in pieces before — the rig has echoed `[qmk]` lines
  into the log all along — and what was missing was any way to run an *arbitrary*
  question instead of the fixed suite.
  - ⚠️ **DISPATCH-ONLY, and that `if:` IS the security control.** Triggering a
    dispatch needs write access, so a fork PR can never reach the rig through it —
    which matters because the rig is a self-hosted runner for a **public** repo
    (HIL-2). **Do not add a `hil-debug` label trigger**: a label is applied to a PR
    whose head may be a fork. The containment check in `station/probe.py` is *not*
    this control and its own docstring says so; it is operational (CI has already
    checked out the whole repo and the build jobs have already run code from it).
  - **Dispatch with `ref:` set to a branch runs that branch's workflow and builds
    that branch's firmware**, so a probe iterates entirely unmerged. That is also
    the answer to "workflow_dispatch only exists on the default branch": the
    *entry* must be there, the *code* need not.
  - ⚠️ **The console cannot see a flash window.** QMK drops output nobody drains
    and during a flash nothing does, so a probe observes before and after an
    update, never during. A gap in the `[qmk]` timestamps spanning a flash is
    expected, not a symptom.
  - ✅ **A brick is self-recovering on the rig** — it asserts BOOTSEL over GPIO,
    and BOOTSEL/UF2 bypasses `fw_staging` entirely. So the rig is the right, and
    the only, place to exercise the firmware-apply path: the failure this whole
    area guards against cannot strand the hardware.
  - **A probe is disposable.** Delete it when the bug closes, or promote it into
    `station/hil_tests.py` if the question is worth asking forever.

---

## When the rig is not there

- ⚠️ **A HIL job that never STARTS is a different state from a red one, it alerts
  nobody, and it blocks the RELEASE gate hours later.** When the self-hosted rig
  runner is offline, `HIL test (split72)` sits `status: queued` with no
  `conclusion`, no log, no annotation and no timeout — the PR board shows a
  spinner, not a failure, so nothing about it looks wrong. Measured 2026-09-08:
  queued 07:34Z, still queued when the PR merged at 12:10Z, 4.5 hours later.
  `diagnose-hil-failure` classifies a RED check and has nothing to say about this.
  Two consequences:
  - **Read `status` before `conclusion`.** An unfinished run carries no verdict in
    either shape: the MCP `actions_list` response omits `conclusion` entirely (the
    trap the note below records), while the REST API returns `"conclusion": null`.
    So "not failed" is never "passed" — and a job whose `started_at` is hours old
    while its status is still `queued` was never picked up by a runner at all.
  - ⚠️ **It silently arms a release refusal.** The FW-APPLY tier runs on every push
    to `PolyKybd`, so an offline rig means the merge's own apply run hangs too —
    and `tools/require_fwapply_run.py` refuses to publish a release the apply tier
    has not covered. The failure surfaces at publish time, on a commit that looked
    fine when it merged. The recovery is the one that note already prescribes:
    dispatch *Build and HIL Test* with `tier: fwapply` on the right ref once the
    rig is back, and remember the ref rules there (the branch tip only works while
    it IS the release commit; otherwise dispatch on the tag).
  - ✅ **PROVE it is the rig and not your PR by COUNTING queued runs, not by
    reasoning about the diff.** `actions_list list_workflow_runs` on `qmk-test.yml`
    and look for `status: queued` across heads: several PRs stuck at once means
    the runner, and **a `workflow_dispatch` on `PolyKybd` stuck alongside them is
    conclusive** — nothing about a feature branch can hold up a manual run on the
    base. Measured 2026-09-09: four runs queued across three heads (two PRs plus a
    dispatch), every one with a green `Build firmware` and a HIL job that never
    started.
  - ⚠️ **Do NOT re-run, and do not read the drive-to-green rules as requiring
    one.** The rig executes one job at a time, so a re-run queues a fifth job
    behind the four already waiting and cannot make an absent runner appear. What
    the rules do require is saying it once: a single comment naming the check, the
    evidence that it is not this PR's, and what you are not doing about it. Then
    silence until the state changes.
  - **It can self-resolve, so an outage is not automatically a person's problem.**
    The same 2026-09-09 outage ran 05:14→07:18Z (~2h05m) and cleared with no
    intervention; the queued jobs then ran in order and passed. Between that and
    the 4.5 h case above there is no useful timeout to assume — keep a check-in
    scheduled rather than declaring the rig dead or waiting on it in the loop.

---

## Which changes start a run

- **A change that cannot alter the firmware does NOT run the build or the rig —
  `qmk-test.yml` path-filters both its `push` and `pull_request` triggers.** Markdown
  since 2026-08-21, then `scripts/` and `.claude/`, then the sibling workflow files
  (#253, 2026-08-29). The rig executes one job at a time, so before this a
  comment-only PR occupied it for a full flash-and-test cycle per push and delayed
  every real build queued behind it (#224 burned three rig runs and three review
  slots that way; #251 later did the same as a workflow-only PR). What follows —
  the last one is still the one that would bite:
  - **`qmk-test.yml` has NO `concurrency:` group, so a push mid-run does not CANCEL
    the in-flight run — it queues a second one behind it on the single-job rig.**
    Worth knowing because it decides "push the fix now or wait?": nothing is aborted,
    so the running job still finishes and still tells you whether the code is sound,
    but you spend a second full flash-and-test cycle. Verified before pushing a
    docs-only follow-up on #267 (`grep -n concurrency .github/workflows/qmk-test.yml`
    → no match); runs 964 and 965 then both queued on the same head. ⚠️ The older
    run's conclusion attaches to the **superseded sha**, so it stops being the PR's
    reported status even though it is the one that actually exercised the code.
  - ⚠️ **It is a `paths` list with `!` negations, NOT `paths-ignore`, and it cannot
    be either one.** The `!` prefix works ONLY in `paths`, and the two filters may
    not both be used for one event — so "exclude the sibling workflows but still
    verify this one" is inexpressible with `paths-ignore`. The list is `**`,
    `!**.md`, `!scripts/**`, `!.claude/**`, `!.github/workflows/**`,
    `.github/workflows/qmk-test.yml`, identical on both triggers.
  - ⚠️ **ORDER IS LOAD-BEARING — the LAST matching pattern decides.** The leading
    `**` is what makes an ordinary source file match at all, and the trailing
    `.github/workflows/qmk-test.yml` is what keeps the gate verifying its own edits.
    Move that line above `!.github/workflows/**`, or drop it, and a change to this
    workflow silently stops being built and rig-tested; drop the `**` and ordinary
    firmware sources stop triggering anything. Both were confirmed by mutating the
    list and re-running a simulation of GitHub's matcher, which is the only way to
    check this without merging and waiting.
  - **A mixed docs+code — or workflow+code — PR still runs the gate in full**, since
    the workflow runs when AT LEAST ONE changed file is included. Nothing can be
    smuggled in behind a README or a CI edit.
  - ⚠️ **`!.claude/**` is anchored at the REPO ROOT, so anything under
    `keyboards/**/.claude/` is NOT excluded — and a RENAME is matched on both its old
    and its new path.** Measured on #286 (2026-09-09), the PR that moved the five
    unreachable skills out of `keyboards/polykybd/.claude/skills/`: every changed file
    was a `.md` or a `.claude/skills/**` script, so the PR body asserted it would start
    no build and no rig run — and `Build firmware` plus `HIL test (split72)` both ran
    (and passed) off the `previous_filename` side of the renames, which sits under
    `keyboards/` and matches the leading `**`. The filter is doing its job; the wrong
    part was reading "`.claude/**` is excluded" as "any `.claude/` directory". Read a
    rename as TWO paths, and check the anchor before predicting a skip:
    `pull_request_read` `get_files` prints `previous_filename` for each one.
    - ⚠️ **And once ONE file in the PR matches, EVERY later push re-runs the gate —
      the `pull_request` paths filter is evaluated over the WHOLE PR's changed
      files, not the push's.** Measured on the same #286 an hour later: a commit
      touching only `CLAUDE.md` plus three files under `.claude/skills/`, i.e.
      nothing but excluded paths, still started `Build firmware` and the rig,
      because the PR still carried the renames above. So the skip you can predict
      is per-PR, not per-push, and a docs-only follow-up on a PR that once touched
      firmware costs a full flash-and-test cycle — which is what the "stop pushing
      cosmetic commits while the important PR waits for the rig" rule is really
      about. `git show --stat HEAD` proving your commit is clean says nothing;
      `get_files` on the PR is the query that answers it.
  - **The exclusion is scoped to `.github/workflows/**`, not all of `.github/`.**
    Nothing under `.github/` is a build input today — there is no `uses: ./...`
    anywhere in `qmk-test.yml`, every action is external — but the narrower scope
    leaves a composite action added later under `.github/actions/` still triggering
    a run, which is the safe direction.
  - **`workflow_dispatch` has no paths filter**, so a manual run — including the
    both-tiers route above — works on any commit regardless.
  - ⚠️ **A path-filtered `pull_request` trigger applies to `labeled` too**, so
    adding `hil-extended` or `hil-perf` to a docs-only PR now starts nothing at all.
    That is the intent (there is no firmware there to measure), but it is a silent
    no-op rather than an error.
  - ⚠️ **This only works because neither check is a REQUIRED status check.** A
    workflow that never runs never reports, so if `Build firmware` / `HIL test
    (split72)` are ever added to branch protection, a docs-only PR would deadlock
    the merge button. The fix then is a paths-filter job feeding `if:` conditions —
    a *skipped job* satisfies a required check, a never-started workflow does not.
    (Not verifiable from a Claude Code session: no MCP tool reads branch-protection
    settings. The indirect evidence is that PRs report `mergeable_state: clean`
    while their checks are still in flight, which would read `blocked` if any check
    were required.)

- ⚠️ **A workflow yields TWO check runs — `push` and `pull_request` — whenever the
  branch matches BOTH triggers, and re-running one does NOT touch the other.**
  Here that mostly doesn't happen: `qmk-test.yml` pushes only on `PolyKybd`,
  `unit_test.yml` only on `master`/`develop`, and `lint`/`labeler`
  are PR-only — so a `claude/**` PR gets a single run per workflow. **The sibling
  repos differ**: wincompose's `build.yml` pushes on `main` *and* `claude/**` on
  top of `pull_request`, so every branch PR there carries two, and that is where
  this bit (2026-08-01). Both runs build the same commit, so a code fix clears
  both — but a fix that lives **outside the commit** (a branch/tag/repo-state
  change) has to be re-run per run, and `rerun_failed_jobs` takes a **run id**, so
  it only ever fixes the one you named. On wincompose#3 re-running one turned that
  check green and the PR was reported green off it while the other — same commit,
  same failure — stayed red. **Before calling a PR green, look at every check run,
  not the one you just acted on.**

---

## The inherited upstream lint

- ⚠️ **A red `lint` on ONE keyboard with `The file "…" should not exist!` is a
  TRACKED-but-GITIGNORED file — real, and it fails every PR that touches that
  keyboard until someone removes it.** `lib/python/qmk/cli/lint.py` calls
  `git_get_ignored_files()` = **`git ls-files -c -o -i --exclude-from=.gitignore
  keyboards/<kb>/`**, so any committed file the root `.gitignore` matches fails the
  keyboard. The classic trap is **images**: `.gitignore` has `*.png` with only
  `!docs/public/**.png` exempt, so a screenshot/render force-added under
  `keyboards/` is committed *and* ignored. Two split42 evidence PNGs did exactly
  this from 2026-07-17 (`11f37c17`) and turned `lint` red on **nine** consecutive
  commits of an unrelated PR. Fixes: delete the file (blobs stay recoverable via
  `git show <sha>:<path>`), move it to `docs/public/`, or add a negation
  (`!keyboards/polykybd/**/*.png`) — all three verified to make `qmk lint --strict`
  pass. **This contradicts the "lint passes green on every normal commit" line
  above** — that holds only while no such file exists.

- ⚠️ **An upstream-merge PR lints UPSTREAM's keyboards too, so it can go red on
  files this fork does not maintain — and a stable tag inherits a new lint rule
  WITHOUT its post-tag fixes.** `lint.yml` lints every keyboard with a changed file
  outside `keymaps/`, and a catch-up merge puts most of upstream's tree in that set
  (~60 keyboards for the 161-commit 0.33.13 merge). 0.33.13 added a **license-header
  check** to `qmk lint --strict` (`_has_license()` — crudely, the first line must
  start with `/*` or `//`; there is no ignore mechanism), which failed **6**
  keyboards: 1 ours (`split72/keymaps/revision2`, genuinely missing) and 5
  upstream's. Three of those five were fixed upstream in `14774c8482` (#26382) —
  **7 commits AFTER tag `0.33.13`** — so merging the tag brought the rule but not
  the fix; the other two are still unfixed on upstream master today, and are green
  upstream only because upstream lints just the keyboards *its* PR touches.
  - The condition is **self-clearing**: once the merge lands, those keyboards are
    in the base branch, so later PRs no longer see them as changed.
  - Resolution used for 0.33.13 (2026-08-11): cherry-pick upstream's own fix where
    one exists (byte-identical afterwards ⇒ no conflict at the next merge), and for
    the rest add `// Copyright <year> <author>` + SPDX taking the **real** author and
    year from `git log --diff-filter=A` on each file. Don't invent attribution.
  - ⚠️ **Enumerate ALL the failures before fixing any** — `qmk info -l` prints a
    keyboard-layout ASCII diagram per keyboard, so the CI log tail is mostly art and
    any excerpt of it is a partial list. Fixing the 5 files a truncated view showed
    left **13** more in `handwired/onekey` and cost an extra CI round. Run the job's
    loop locally and collect every `☒` line first.

- **Reproduce the whole `lint` job locally instead of reading the CI log** — it is
  ~5 s and definitive. (The GitHub MCP `get_job_logs` *does* work — see the
  tail-size note below — but a local run is faster and gives the whole picture):
  ```bash
  export QMK_HOME=$PWD
  qmk lint --strict --keyboard polykybd/split42     # and split72
  git ls-files -c -o -i --exclude-from=.gitignore keyboards/polykybd/   # must be empty
  qmk format-text $(git diff --name-only --diff-filter=d origin/PolyKybd...HEAD)
  git diff --quiet -- $(git diff --name-only --diff-filter=d origin/PolyKybd...HEAD) \
      && echo "format clean"        # the job's second half: any diff = "Requires Formatting"
  ```
  ⚠️ **The `-o` in that command lists UNTRACKED ignored files too, and locally that
  is mostly your own build output** — `keyboards/polykybd/tools/__pycache__`, and
  after a `doom/pack/build_pack.sh` run the ~100 files under
  `keyboards/polykybd/doom/pack/build/`. CI checks out clean, so it never sees any
  of them. **What CI actually fails on is a TRACKED ignored file**, so when the list
  is noisy re-run it without `-o`:
  ```bash
  git ls-files -c -i --exclude-from=.gitignore keyboards/polykybd/   # THIS must be empty
  ```
  ⚠️ **That `-o` noise is convincing enough to cause a MISDIAGNOSIS — it did,
  while this very note was being written (2026-08-19).** Running `git add -A` and
  then the `-c -o -i` check printed the whole `doom/pack/build/` tree, which read as
  "`git add -A` just staged 100 ignored files"; the first draft of this bullet said
  exactly that. It is **false** — `git add -A` honours `.gitignore` and cannot stage
  an ignored file without `-f` (verify in 20 s in a throwaway repo). The files were
  listed by `-o`, as untracked, before and after the add alike. **The tell is
  `git diff --cached --name-only`** — what is *actually* staged — not an
  `ls-files` variant that mixes tracked and untracked in one list.
  Still prefer staging the paths you edited by name: `-A` picks up unrelated
  working-tree changes, and it *does* stage a modification to an ignored file that
  is already tracked, which is the state CI fails on.
  - ⚠️ **WHICH formatter runs is decided by the changed PATHS, and clang-format does
    NOT cover `keyboards/**`.** An earlier version of this file said "the lint job runs
    `format-c` as well as `format-text`" and told you to clang-format every C file you
    touched. That is wrong for keyboard work, and following it means reformatting files
    CI never looks at. The actual wiring (read the three workflows, verified
    2026-08-17):
    - **`lint.yml`** ("PR Lint keyboards") triggers on `keyboards/**` and runs
      **`qmk format-text` only**, then fails any changed file that `git diff` shows as
      modified — that is where `File '…' Requires Formatting` comes from.
    - **`format.yml`** ("PR Lint Format") is the one that runs clang-format, and its
      `paths:` are `drivers/ lib/arm_atsam/ lib/lib8tion/ lib/python/ modules/
      platforms/ quantum/ tests/ tmk_core/` — **no `keyboards/`**.
    - **`format_push.yml`** only fires on pushes to `master`/`develop`, i.e. the
      upstream-mirror branches, never on `PolyKybd` or a `claude/**` PR.
    - So the 2026-08-12 clang-format failure was real but path-specific: that change
      extracted the LTR-559 driver into **`modules/`**, which `format.yml` does cover.
      **Rule: clang-format only what you put under those paths.**
  - ⚠️ **Corollary — do NOT clang-format a new file under `keyboards/`.** The
    container's clang-format 18 disagrees with the prevailing style there on ~every
    file (`.clang-format` sets `ColumnLimit: 1000` and `AlignConsecutive*: true`, so it
    unwraps hand-wrapped signatures and collapses aligned `#define` columns). The
    version-skew test in the next bullet needs a base version to compare against, which
    a new file does not have — the check that replaces it is **whether the same
    objection reproduces on a neighbouring committed file**. It does: the aligned
    `#define SYNC_ACK_SIG    0b…` block is flagged identically in
    `origin/PolyKybd:keyboards/polykybd/split_sync.h`, which CI has been passing for
    months.
  - `qmk format-text` itself cannot be run in the container — it needs **`dos2unix`,
    which is not installed** (`FileNotFoundError: 'dos2unix'`). Its whole job is line
    endings + a trailing newline, so check those by hand:
    `grep -qP '\r' <file>` (must not match) and `[ -n "$(tail -c1 <file>)" ]` (must be
    false, i.e. the file ends in a newline).
  - ⚠️ **The container's clang-format is a DIFFERENT VERSION from CI's, so it flags
    files CI accepts — do not "fix" those.** Local is clang-format 18; it wanted to
    reformat a file the lint job had passed. **The test is whether the same file is
    also flagged on the base branch**: if it is, it is version skew, not a finding —
    reformatting it adds churn CI never asked for and (on a moved file) destroys
    git's rename detection.
    ```bash
    git show origin/PolyKybd:<path> > /tmp/base_copy.c && cp .clang-format /tmp/
    (cd /tmp && clang-format --dry-run -Werror base_copy.c)   # flagged too => skew
    ```
    Only reformat what the CI log named.

---

## Reading the logs

- **`get_job_logs` works — ask for 150–350 `tail_lines`.** An earlier version of
  this file claimed it "caps its response at ~2 KB *regardless of `tail_lines`*";
  that is **wrong** (`tail_lines: 330` returned ~15 KB, 2026-08). The real problem
  is *what fills the tail*: every job ends with **post-job cleanup** — on a
  submodule-heavy repo that is ~60 lines of `git config`/`submodule foreach`
  spam — and a failing tool often dumps diagnostics **after** its own error
  (GitVersion prints a 100-commit graph, so its exception sat ~150 lines above the
  end). `tail_lines: 40` and `125` both landed squarely in that noise and cost
  four wasted calls before 330 reached the actual message. Prefer
  `failed_only: true` with a **run** id to find the job, then a generous
  `tail_lines` on the **job** id.
  - ⚠️ **On a HIL/fwapply job no PRACTICAL tail reaches the interesting part, and
    the AVERAGE line rate is the statistic that misleads you about it.** The rig echoes every
    `[qmk] …` line the keyboard prints. Measured on the red `Firmware apply
    round-trip (split72)` of run 992 (job `101021479366`, 2026-09-04): the whole
    job is **33,422 lines over 266 s**, i.e. ~126 lines/s *averaged* — but the
    console floods hardest at the end, so the **last 2600 lines span 1.87 s**
    (~1400 lines/s) and the last 500 span 0.63 s. The apply sequence a tail is
    fetched for had happened roughly **four minutes** earlier, so reaching it
    needs a tail in the tens of thousands of lines. Same "what fills the tail"
    problem as the cleanup spam above, except that there the noise is ~60 lines
    and here it is the whole log — asking for more lines is not the fix.
    - ✅ **The escape hatch is `return_content: false`** — `get_job_logs` then
      returns a `logs_url` (a time-limited blob link) instead of content, so
      `curl` it and `grep`/measure the **whole** log in the shell, at no context
      cost. That is how the numbers above were obtained, and it is strictly
      better than any tail on a job this noisy.
    - Otherwise read the rig's own verdict lines — the graded
      `[test] PASS/FAIL: <name>` lines and the `Split link:` summary carry the
      diagnosis — or drive the question with a probe (`tier: debug`), which
      prints only what it asks for.

---

## Performance measurement

- **`Performance measurement (split72)` is OPT-IN and never gates.** It builds a
  second pair of HIL images with `-e POLYKYBD_LOOP_PROFILE=yes` and has the rig
  measure main-loop timing, overlay cost (bridge/render/rest) and HID latency, then
  posts a table to the job summary + a PR comment and compares against the baseline
  committed in `polykybd-ctnd` (`perf/baselines/split72.json`). Trigger it with the
  **`hil-perf` PR label** (the way to measure a PR), **`[hil-perf]` in a commit message**
  (PUSH events only — `head_commit` doesn't exist on a `pull_request` event, so it
  does nothing on a PR), or a manual **`workflow_dispatch`** (only available once
  the workflow is on the default branch). ⚠️ The label needs `labeled` in the
  workflow's `pull_request` `types:` — it is **not** in GitHub's default set, so
  without it labelling an open PR fires no run at all; `build`/`hil-test` opt out
  of label events so the auto-labeler can't re-run the whole pipeline.
  ⚠️ It **never fails on a regression** (wall-clock numbers on
  shared hardware — a flaky red check is one people learn to ignore); only a
  *measurement* failure (wrong build flashed, device dead) fails the job. Ordered
  `needs: [build-perf, hil-test]` + `always()` so the two rig jobs can't interleave
  flashes, but a red HIL suite still yields a perf number — often exactly what
  explains a timing-related HIL failure. **Use this instead of asking the user to
  flash a build and paste the console log.** See
  `keyboards/polykybd/profiling/README.md` (§ on-demand control, HID cmd 32) and the
  `polykybd-ctnd` CLAUDE.md § Performance measurement.

- **An upstream merge is the canonical case for the `hil-perf` label.** A catch-up merge
  bumps the ChibiOS / pico-sdk pins and pulls core QMK changes (split transport, USB
  stack, scheduler) — exactly the things that can move main-loop timing with **no
  PolyKybd source changed** — and it is also the PR CodeRabbit skips outright (>100
  files), so `Build firmware` + `HIL test` are the only other verification and
  neither measures timing. The job is report-only, so it cannot redden an already
  unreviewable PR. Apply `hil-perf` as its **own** label call (N labels in one call fire
  N runs — see the labeling note above). ⚠️ **Then move the baseline — but only if
  something actually moved.** `perf/baselines/split72.json` is compared against,
  never auto-updated, so a real shift left unrecorded becomes a phantom regression
  on every later PR; equally, re-baselining on noise creates a phantom regression in
  the other direction. **"Idle — worst iteration" is a max-of-window sample and
  swings ~2× run to run** — the 0.33.13 dispatch read 1.88 ms against a 3.85 ms
  baseline ("-51%") on a window whose histogram was 4055 iterations `<1 ms` + 110 in
  `1-2 ms` and *nothing above 2 ms*, i.e. the old value was one outlier iteration,
  while the main-loop rate over the same window moved -1.3%. Trust the rate/total
  rows; treat the worst-iteration rows as anecdotes. **0.33.13 measured
  performance-neutral** (everything within ~1%), so its baseline was deliberately
  left in place.
