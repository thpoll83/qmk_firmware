# CLAUDE.md — qmk_firmware (PolyKybd)

This file provides guidance to Claude Code (claude.ai/code) when working in this QMK fork. The PolyKybd-specific firmware lives at `keyboards/polykybd/`.

For cross-repo context (how this repo relates to `PolyKybdHost/` and `AdafruitGFX/`), see [`../CLAUDE.md`](../CLAUDE.md).

## Code review conventions (all PolyKybd repos)

- **Docstring coverage: ignore CodeRabbit's "Docstring Coverage … threshold 80%"
  pre-merge check.** It is a CodeRabbit default, **not** a project policy, and the check
  is non-blocking. Document new code where a docstring genuinely helps a reader.
- **Verify an AI reviewer's finding against the code before acting on it** — several
  arrive confidently wrong, some refuted by their own evidence. The rule is **verify,
  not dismiss**; the same rounds produce genuinely valuable findings. Reply with the
  counter-evidence, because CodeRabbit stores a repo learning from the thread.
- ⚠️ **A security-scanner finding on a file under `.github/` or `lib/` is probably
  UPSTREAM's, and two checks settle it in minutes: is it stock, and is it reachable?**
  `diff` the path against upstream's raw file (identical is conclusive; a *difference*
  is not proof we own it — check `UPSTREAM_PATCHES.md` first), and for a workflow ask
  upstream whether it exists at all (**404 = ours**; only 5 of 23 are). Then read the
  triggers: a `workflow_call`-only file is dead unless something calls it, and upstream
  gates several on `github.repository == 'qmk/qmk_firmware'`, permanently false here.
  - ⚠️ **The inverse is what makes this worth doing: the scanner looked at the inherited
    file and NOT at ours.** The one genuine injection in the same sweep was in
    `bump-version.yml`, a file we wrote, and nothing flagged it. Treat a report naming
    an upstream path as a prompt to audit the sibling files we own.
  - ⚠️ **A third direction: an inherited upstream POLICY file tells a reviewer to REFUSE
    the files this fork owns.** `.github/copilot-instructions.md` is byte-identical to
    upstream's and scopes review to `keyboards/`, so CodeRabbit defers `release.yml`,
    `qmk-test.yml` and `CLAUDE.md` to a role this repository has nobody to fill. Reply
    with the evidence; do **not** delete the inherited file.
  - **Record the disposition even when nothing changes** — dismissed findings that leave
    no artifact get re-raised in full by the next scan. Tracker:
    `polykybd-ctnd/docs/SECURITY_AUDIT.md`; the `verify-security-finding` skill drives it.
- ⚠️ **A green board is NOT evidence a reviewer read your code.** A quota refusal is a
  real review object carrying the head sha, and a CLEAN CodeRabbit pass creates no
  review object at all. Check `get_reviews` **and** the summary comment's `📥 Commits`
  range; no check run answers this question.
- ⚠️ **After changing a function SIGNATURE, grep THIS FILE for other prose references to
  it** — nothing in CI reads Markdown, so a stale API example ships silently and reads
  as authoritative. Applies to a renamed or re-typed symbol too, and to the sibling
  repos' CLAUDE.md files for a symbol they mirror.

The worked examples, the exact commands and the reviewer field guide are
[`keyboards/polykybd/REVIEW_CONVENTIONS.md`](keyboards/polykybd/REVIEW_CONVENTIONS.md)
and the `triage-pr-review` skill.

## Mirrored skills (`qmk_firmware` ↔ `PolyKybdHost`)

Nine skills exist in **both** repos and are kept **byte-identical**:
`add-gated-hid-command`, `check-mirrored-artifacts`, `cross-repo-pr-sweep`,
`mutation-test-suite`, `polykybd-github-release`, `prune-claude-md`,
`session-retro`, `triage-pr-review`, `update-polykybd-docs`. A skill loads only from the
repos a session has attached, so one describing cross-repo work is unreachable from a
session opened on the other repo alone.

**The rule is copy, never fork**: edit one, `cp` it to the other, and check with

```bash
for s in add-gated-hid-command check-mirrored-artifacts cross-repo-pr-sweep \
         mutation-test-suite polykybd-github-release prune-claude-md \
         session-retro triage-pr-review update-polykybd-docs; do
    cmp -s /home/user/qmk_firmware/.claude/skills/$s/SKILL.md \
           /home/user/PolyKybdHost/.claude/skills/$s/SKILL.md \
      && echo "$s: ok" || echo "$s: DRIFTED"
done
```

⚠️ **A skill under `keyboards/` does NOT load** — five lived at
`keyboards/polykybd/.claude/skills/` and were invisible for months while the pointer
here looked live. A new skill goes at the **repo root** `.claude/skills/`. ⚠️ A move
re-bases every hardcoded `../` depth in a helper script: **derive** the repo root by
walking parents for `keyboards/polykybd`, and **baseline a script before relocating it**
so a post-move failure cannot be confused with one that never worked.

⚠️ **A shared `CLAUDE-SHARED.md` imported by both repos would NOT reduce context** —
imported files load at launch. What *does* reduce it, in increasing order: a `.md` read
on demand; a **path-scoped rule** (`.claude/rules/*.md` with `paths:` frontmatter),
which loads only when a matching file is read; and a **skill**, which costs nothing
until invoked. ⚠️ A path-scoped rule fires on the **Read tool**, not on `cat`/`sed`
through Bash — it supplements a CLAUDE.md pointer, never replaces one. The measurements,
and the `keycap_preview.py` parser bug that surfaced only once its skill became
reachable, are in
[`keyboards/polykybd/SKILLS_AND_CONTEXT.md`](keyboards/polykybd/SKILLS_AND_CONTEXT.md).

## Branching (all PolyKybd repos)

- **Give every branch a name that hints at its content** — a short descriptive slug, so
  the branch list reads as a changelog.
- **Always start new work on a FRESH branch cut from the updated default** (**`PolyKybd`**
  here; `main` in the host and rig repos). Once a PR is merged that branch is done; a
  push to it succeeds silently and orphans the commit.
- ⚠️ **A cross-repo feature can leave one repo with commits PUSHED and NO PR, and
  nothing surfaces it.** Every "is everything saved?" check passes on the repo you
  forgot — the branch is committed, pushed, in sync, and `git status` is clean. Only the
  *absence of a PR* is wrong, and no local command looks for that.
  - ⚠️ **"Commits ahead" cannot answer the question**, on one branch or on all of them:
    a squash-merged branch reads as ahead forever (95 of 525 `claude/*` branches read as
    ahead; 3 had an open PR). **The only reliable signal is whether a PR EXISTS**, which
    is a GitHub query, not a git one.
  - The sweep loop and its five fail-open modes — `~` is `/root`, `origin/HEAD` is
    unset, a stale ref cries wolf, a failed fetch reads as inspected, and it only sees
    the branch you are standing on — are in
    [`keyboards/polykybd/BRANCHING.md`](keyboards/polykybd/BRANCHING.md). Treat a silent
    sweep as "nothing obvious on this branch", never a clean bill of health.

## Building & flashing

**The ARM toolchain is installable in the dev/remote container — do not claim it is
unavailable.** `qmk compile -kb polykybd/split72 -km default`. The once-per-container
setup, the submodule failure modes, the `-Wcast-align` guard and its path filter, and the
vendored-DOOM `-Werror` collateral are in
[`keyboards/polykybd/BUILD_ENVIRONMENT.md`](keyboards/polykybd/BUILD_ENVIRONMENT.md).
Seven rules bind work outside that file:

- **The deliverable for testing is the `.bin`, NOT the `.uf2`** — the user flashes over
  HID. ⚠️ **Put the commit sha in the filename**: every test build reports the same
  `FW_VERSION`, so N hardware rounds hand over N indistinguishable files. That cost a
  full round once. The `deliver-test-firmware` skill wraps it.
- ⚠️ **A branch-built `.bin` reports a DIFFERENT `FW_VERSION` from CI for the SAME
  commit** — CI builds the PR *merged into* its base and picks up every auto-bump since
  the branch was cut. Settle it by **diffing, not rebuilding**, and read the content and
  the unrestricted path list: a PolyKybd-scoped `--stat` proves neither half.
- ⚠️ **NEVER run two `qmk compile` invocations at once** — every flavour shares one
  `.build/` tree and the collision presents as an `undefined reference`, i.e. a code
  error. `build_pack.sh` is safe because it sequences them.
- ⚠️ **In the session container `qmk` is at `/root/.qmk_venv/bin/qmk`, NOT on `PATH`.**
  `export QMK_HOME=$PWD && export PATH="/root/.qmk_venv/bin:$PATH"`.
- ⚠️ **The checkout can be SILENTLY RESET to an older commit** when the container is
  reclaimed — nothing announces it, and it once sent a code review chasing a mismatch
  that existed only in the reverted tree. Run `git log --oneline -1` before trusting any
  grep or "the code says…" conclusion.
- ⚠️ **The container's clone is SHALLOW, and `git merge-base` then returns an EMPTY
  STRING rather than an error** — so a diff-vs-upstream or a tag-anchored release range
  silently produces a wrong answer that looks like plausible history. Run
  `git rev-parse --is-shallow-repository` before ANY such question;
  `git fetch origin --unshallow --no-recurse-submodules` takes ~45 s.
- **Docker is NOT usable** in the remote container (no daemon).

## Continuous integration (PR checks)

The mechanics — the HIL tiers, the FW-APPLY and doom tiers, the paths filters, the
inherited upstream lint, and how to read a job log — are
[`keyboards/polykybd/CI_CHECKS.md`](keyboards/polykybd/CI_CHECKS.md). Seven things every
PR author needs without opening it:

- ⚠️ **The HIL suite has TWO tiers and the default one skips the deepest checks** — the
  startup animation, idle + Eden, the 450-frame split-link soak, and the reboot power
  cycle that is the ONLY thing verifying user state survives a power loss. **Ask for
  `TIER_EXTENDED` on anything touching EEPROM/persisted state, the split link, the
  idle/animation paths, or a release**: the `hil-extended` label, `[hil-extended]` in a
  pushed commit, or a dispatch. The log says which tier ran.
- ⚠️ **A HIL job that never STARTS is not a red one, and it alerts nobody** — an offline
  rig leaves it `queued` with no conclusion and silently arms a release refusal hours
  later. **Read `status` before `conclusion`**, and do not re-run: the rig runs one job
  at a time.
- ⚠️ **A GREEN HIL run can cover NONE of the change you just shipped — read the `SKIP`
  lines, not the conclusion.** The suite announces each opt-in test it declines
  (`[test] SKIP: … (doom suite — re-run with --doom + a signed --plyx-valid …)`), and a
  run full of them still reports success. #298 merged on a green `TIER_EXTENDED` pass
  whose log skipped **all four** doom tests — so the FW-9 gate and its new on-keycap
  prompt, the largest change in the PR, have never run on hardware. **Grep the job log
  for your feature's own test name** before treating a green board as coverage.
- ⚠️ **A green board does NOT mean a rig test from an unmerged `polykybd-ctnd` PR ran** —
  CI force-syncs the station to ctnd `main`, so that test does not exist on the rig.
  Land the ctnd PR first, then re-run HIL; verify by grepping the job log for the test's
  own name.
- **`cppcheck` gates, and is the only reviewer here that is not an LLM** — no quota, no
  file limit, so it cannot go quiet on the PR that needs it. ⚠️ A bare `#` line in
  `.cppcheck-suppressions` kills the whole run **before checking anything** and presents
  as "no findings". Every suppression carries a written reason.
- ⚠️ **Applying N labels in ONE API call fires N workflow runs.** Apply them one call at
  a time when one is a trigger.
- ⚠️ **A `check_suite.completed` wake can name a SUPERSEDED head** — compare the event's
  `head_sha` against the PR's actual head before believing "CI is green".
- ⚠️ **PR CI does NOT build the monolith** (`POLYKYBD_DOOM=yes`) — only the release
  workflow does, and it is the tightest RAM flavour. Build it locally before merging
  anything that adds statics.

The `diagnose-hil-failure` skill classifies a red rig check; `debug-firmware-on-rig`
drives a one-off probe when the graded suite cannot answer the question.

## Releases

Firmware releases are **GitHub Releases** (tag `PolyKybd-fw-vX.Y.Z`; `FW_VERSION` in
`config.h`), created by **publishing** — *not* by pushing a tag, which lands on the
auto-bump `[skip ci]` commit and triggers nothing. Use the `polykybd-github-release`
skill; the mechanics, the `release-notes` branch and `scripts/publish_release.py` are in
[`keyboards/polykybd/RELEASES.md`](keyboards/polykybd/RELEASES.md).

- ⚠️ **Publishing is GATED on a green firmware-APPLY run for the commit being released**
  (`tools/require_fwapply_run.py`). The HID-apply brick shipped because no release
  artifact had ever been applied on hardware — the rig flashes by UF2 over GPIO BOOTSEL,
  which bypasses `fw_staging` entirely. It walks back through ancestors and proves the
  delta to the release commit is **only** the auto-bump. Recovery when it refuses:
  dispatch *Build and HIL Test* on that commit with `tier: fwapply`, then re-run release.
  ⚠️ **A docs-only merge in the way no longer costs that round-trip**: the delta may
  also carry files the build and the rig never read, decided by replaying
  `qmk-test.yml`'s own `paths:` filter rather than a second copy of it, and failing
  closed if that filter cannot be read. `PolyKybd-fw-v0.27.1` published with zero
  assets before this existed. `bump:none` additionally skips the version bump for such
  a PR — a convenience, since the gate no longer depends on anyone remembering it.
- ⚠️ **A `PROTOCOL_VERSION` bump means BOTH artifacts get released, and the check is the
  PUBLISHED versions, not the in-tree ones.** The source-lockstep rule can be perfectly
  satisfied while the releases sit a protocol apart, and nothing downstream catches it —
  the connect gate is not exact-match, so an old host pairs with new firmware and
  silently leaves the new features off. **Publish the host first**, then the firmware.
- ⚠️ **A MISSING bump label is silent, and "before the merge" means AT OPEN.** The label
  is read at merge time; a request in the PR body is documentation, not a label, and one
  applied as the merge happens lands too late (twice, 12 s late once). `create_pull_request`
  cannot set labels — use `issue_write` with `labels:` right after opening. A missing
  label made the live docs promise a version that would never exist, because a docs PR
  ships the moment it merges while a firmware PR only bumps a number and waits.
- ⚠️ **From Claude Code on the web you can neither push tags nor create a release** —
  stage the notes on the branch and hand the user `python scripts/publish_release.py`.

## Firmware overview (`keyboards/polykybd/`)

The firmware runs on a **Raspberry Pi RP2040** (dual-core ARM M0+) and is a heavily customised QMK build. ⚠️ **The clock is 200 MHz by default** (since 0.10.x). It was **125 MHz** before that — never the 133 MHz this file and several code comments used to claim, which was the chip's old *rated maximum*. Nothing in QMK sets the clock; ChibiOS's `hal_lld_init()` (and, earlier in the boot, the double-tap `__late_init`) calls the pico-sdk `clocks_init()`, which reads the compile-time `SYS_CLK_KHZ`, so `rules.mk` sets it. **`-e POLYKYBD_SYS_CLK=125`** opts back out and produces an image **byte-identical** to the pre-200 MHz builds (verified) — the escape hatch if a board ever misbehaves. 200 MHz is the operating point Raspberry Pi certified in 2025 (1200 MHz VCO / 6 / 1), which requires the core voltage raised to **1.15 V** — the vendored pico-sdk predates the SDK's automatic raise and does not compile `hardware_vreg`, so `POLYKYBD_VREG_VSEL` drives it as a register write before the first `clocks_init()` (see `UPSTREAM_PATCHES.md` → `platforms/chibios/bootloaders/rp2040.c`). Peripherals need no rework: SPI (`SPI_DIVISOR`/`CPU_CLOCK`), I2C, the PIO split UART and WS2812 all derive their dividers from the **live** `clock_get_hz(clk_sys)`, and USB is on the separate 48 MHz PLL. The boot banner prints `clk: sys=…Hz vreg_vsel=0x…` so the pairing is verifiable on hardware. The one **fixed** divider is XIP flash — boot2 runs it at `clk_sys/PICO_FLASH_SPI_CLKDIV` (4), i.e. 50 MHz at 200 and 31.25 at 125, both far inside any QSPI part's rating; re-check that list rather than assuming it holds if another clock is ever added. This is **custom hardware with 8 MB of external QSPI flash** (NOT the stock 2 MB). The 8 MB is **partitioned** (see `base/fw_staging.h` for the authoritative map): **0–2 MB running firmware** (the linker `flash1` XIP window), **2–4 MB firmware-update staging**, **4–8 MB resource/overlay data** (`FLASH_TARGET_OFFSET`). So the budget that matters for adding languages/fonts is the **2 MB firmware partition**, of which `split72:default` currently uses ~0.76 MB (~38 %). `FW_STAGING_OFFSET` is kept equal to the linker `flash1` length so a build that exceeds 2 MB fails to *link* rather than silently growing into the staging area (this firmware/staging split was raised from 1 MB → 2 MB in 2026-06 as the image neared the old boundary). ⚠️ **The sectors carved off the TOP of staging (the apply log, the crash archive, the handedness stamp) need an ALIGNMENT assert as well as an overlap one — the overlap asserts do not imply it.** Each is derived by subtraction from the one above (`FW_HAND_STAMP_OFFSET` is `FW_RESOURCE_OFFSET - FW_APPLY_LOG_BYTES - 8192`), so its 4096-alignment rides on constants that can move without any two regions ever overlapping — and `flash_range_erase()` requires the boundary. Caught in review of #282; `fw_staging.c` carries both terms now. The keyboard is split (left + right halves connected via UART) with up to 72 per-keycap OLED displays (72×40 px monochrome, SPI-driven) plus a 128×64 status OLED.

The host software (`PolyKybdHost/`) communicates with this firmware over a custom HID report protocol (64-byte reports, v0.7.0+).

### The dynamic keymap: storage, the write cap, and the one resolver

`DYNAMIC_KEYMAP_LAYER_COUNT` stays **12**, but only layers 0..7 are ever read or written
from EEPROM — `_SL` and up are served from flash. `config.h` rebases the encoder map and
macro buffer on **`DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT`**, reclaiming 672 B. The
reclaim mechanics, the two guards that keep it sound, `poly_keycode_at()`'s role as the
single resolver for both the render and the key-event path, and how to read the real
addresses out of an object file are in
[`keyboards/polykybd/KEYMAP_STORAGE.md`](keyboards/polykybd/KEYMAP_STORAGE.md).
Five rules bind code outside it:

- ⚠️ **The dynamic keymap is indexed BY LAYER NUMBER and QMK does not version it.**
  Remove or reorder a layer and every stored layer above it silently changes meaning —
  no error, no log line, just wrong keys. **Bump `KEYMAP_LAYERS_FL_MERGED` whenever a
  layer is added, removed or reordered — never when a layer's CONTENTS change.**
- ⚠️ **The consequence for a KEYMAP EDIT: on any board that has ever stored a keymap, a
  change to a layer below the write cap is INVISIBLE** — the EEPROM copy wins, and
  nothing says why. Two recoveries, not equivalent: bumping the format stamp reaches
  every board and **WIPES THE USER'S MACROS**; `polyctl keymap set <layer> <row> <col>
  <keycode>` (POSITIONAL args) is non-destructive, one call per changed key.
- ⚠️ **There is NO keymap-reset and no EEPROM-clear anywhere in PolyKybdHost.** Do not
  tell anyone to "reset the keymap from the host app"; that route does not exist.
- **Every mutation goes through a `*_poly` wrapper in `split_sync.c`** — the invariant is
  "all keymap mutation goes through a `_poly` function", not "these four places also
  call the invalidator".
- ⚠️ **`poly_keycode_at()` is the ONE resolver for both paths, so anything derived there
  cannot make a keycap show one thing and type another — but the host layout editor
  CANNOT see it** (it reads straight out of EEPROM). That, not taste, is why the F-row
  alignment is all-or-nothing and self-disabling. Any future runtime derivation on a
  remappable layer faces the same three-way choice.

### `poly_keycode_at()` is the ONE resolver for both the render and the key-event path

`display_keycode_at()` (legend) and `keymap_key_to_keycode()` (action) both bottom out
there, so anything derived inside it **cannot** make a keycap show one thing and type
another. That makes it the correct — and only correct — seam for a runtime keycode
derivation. The F-row alignment (`fl_aligned_keycode`, split72's `POLY_FL_ALIGN_FROW`)
lives there for exactly that reason.

Both callers feed it the **synced** `def_layer` from `get_local_layer()`, not live state
for one and synced for the other. ⚠️ This is the OPPOSITE of the glyph-size key's
deliberate asymmetry, and the difference is what changes at human speed: *modifiers*
change within a single keypress so the action must follow the finger, while `def_layer`
only moves on a deliberate layout switch — so the one-housekeeping-pass lag is
irrelevant and two sources could render F6 on a keycap that types F7.

⚠️ **The host layout editor CANNOT see anything derived there — it reads through
`dynamic_keymap_get_buffer()` (`hid_com.c`), straight out of EEPROM.** So a derivation
on a host-remappable layer would show one keycode in the editor while the board typed
another, with nothing on screen to explain it. That constraint, not taste, is why the
F-row alignment is **all-or-nothing and self-disabling**: it applies only while all 14
slots still hold exactly what was compiled, and any edit hands the whole row back to the
stored values. Two options were rejected — deriving unconditionally (fights the editor)
and moving `_FL` above the write cap (costs remappability entirely). Any future runtime
derivation on a remappable layer faces the same three-way choice.

⚠️ **The derivation is cached** (`fl_row_is_pristine`, 14 dynamic-keymap reads) because
`poly_keycode_at()` runs per keycap per render, not just per keypress. See the `_poly`
wrapper invariant above for how it is invalidated.

### Keyboard variants & the shared keymap (`poly_keymap.c`)

Two hardware variants share one firmware: **`split72`** (72-key, RGB matrix, Cirque
trackpad, 128×64 status OLED) and **`split42`** (42-key CRKBD footprint, renamed from
`corne42` in 2026-06). All behaviour lives in the keyboard-level `poly_keymap.c`; each
variant's `keymap.c` is **data only**, and differences resolve at compile time. Details:
[`keyboards/polykybd/VARIANTS.md`](keyboards/polykybd/VARIANTS.md).

**Consequence:** a feature added to `poly_keymap.c` lands on both keyboards at once —
they can't drift apart. Don't re-introduce per-variant copies of the keymap logic; that
drift is exactly what this extraction fixed (`corne42` had silently fallen ~98 languages
behind).

⚠️ **The two variants also share the MCU SCHEMATIC**, so an MCU-level question is never
answered from `variations/poly_corne/` — that directory contains no processor. A grep
there reports every MCU net as absent and reads as a hardware fact; that is how "split42
has no VBUS_SENSE net" was asserted here when the boards are identical. **An empty grep
is evidence only once you have shown the search covered the thing you asked about.**

### Custom keycodes: where they are handled, and why not on the release edge

**A custom PolyKybd keycode is handled and SWALLOWED in `process_record_user()`, never
left to `post_process_record_user()`.** On an `OSL()` layer a release-edge action fires
up to **THREE** times — `process_action()` re-dispatches the press as a synthetic
release on the same record, the outer `process_record` sees it too, and then the real
release arrives. One tap of `KC_GLYPH_SIZE_UP` stepped the legend size three tiers; an
inc/dec reads as ×3 and a **toggle reads as doing nothing at all**. Returning false from
`process_record_user()` stops the synthetic release being generated, and QMK still
resolves the one-shot. ⚠️ **A REAL keycode cannot use this** — swallowing it would stop
it reaching the host — so the shifts and F-keys stay on the release edge, where they are
idempotent repaints.

⚠️ **Tap-hold settings are `config.h` DEFINES — `PERMISSIVE_HOLD = yes` and
`HOLD_ON_OTHER_KEY_PRESS = yes` sat in both `rules.mk` files and did NOTHING for years**,
while the build stayed green and the source read as configured. **A make variable is
only a feature switch when some `.mk` file translates it:**
`grep -rn "<NAME>" builddefs/` before believing a line in a `rules.mk`.
⚠️ **`HOLD_ON_OTHER_KEY_PRESS` is deliberately NOT defined** — it fires a mod on ordinary
fast rolls, which is exactly what `CHORDAL_HOLD` and `FLOW_TAP_TERM` exist to prevent.
⚠️ **`CHORDAL_HOLD`'s weak hook is what a SPLIT board wants** — override
`chordal_hold_handedness()` with row arithmetic rather than hand-maintaining an 80-entry
table that must be kept in step with the matrix.

Both write-ups: [`keyboards/polykybd/KEYCODE_HANDLING.md`](keyboards/polykybd/KEYCODE_HANDLING.md).

### HID protocol (host → firmware)

64-byte raw HID reports; byte 0 = Report ID, byte 1 = Command ID, byte 2+ = payload. All
responses are prefixed `"P\xNN."` (ACK) or `"P\xNN!"` (NACK). **`PROTOCOL_VERSION`**
(`config.h`, reported in GET_ID) gates host features; the per-version table and the
rationale behind each command are
[`keyboards/polykybd/PROTOCOL_HISTORY.md`](keyboards/polykybd/PROTOCOL_HISTORY.md) —
**read it before changing any of these commands**, because several were shaped by a
contrast with their neighbour that the wire format does not show. The
`add-gated-hid-command` skill drives a new one end to end.

- **Bump `FW_VERSION` + `PROTOCOL_VERSION` and the host's `__protocol__` in lockstep.**
  ⚠️ The connect gate is NOT exact-match, so forgetting the bump no longer rejects the
  keyboard — it silently leaves the new feature disabled. Quieter, and worse.
- ⚠️ **v13's CLOSED range is the deliberate OPPOSITE of v10's open one, one command
  over.** An unknown glyph SCRIPT falls through to the normal legend, so accepting it
  costs nothing and lets the host ship faces a keyboard lacks. An unknown SIZE would be
  stored, synced and persisted while still rendering small. The two HIL tests assert
  opposite things about neighbouring commands **on purpose**.
- ⚠️ **A QMK `*_set_user` hook is a NOTIFICATION, never a setter** — calling one to
  CHANGE state fails in the quietest possible way: the UI moves and the behaviour does
  not. Cmd 20 did this for years, relabelling keycaps while `unicode_config.input_mode`
  never moved. **The tell is a state whose display and effect disagree**; grep for any
  `*_set_user` being CALLED rather than implemented.
- **Cmd `32` (profiler) is present ONLY in a `POLYKYBD_LOOP_PROFILE` build and bumps no
  `PROTOCOL_VERSION`.** Its NACK on a normal build is the deliberate capability signal.
- ⚠️ **v18's idle TIMEOUT (cmd 40) deletes `FADE_OUT_TIME`** — the delay before the
  idle style engages is a per-board setting now (`enum poly_idle_timeout`,
  `base/idle_timeout.h`, six presets 15 s…5 min), read through
  `get_idle_timeout_ms()`. The constant is GONE rather than left to rot, because a
  stale `> FADE_OUT_TIME` would compile and then silently ignore the user's choice.
  `TURN_OFF_TIME` is unchanged and deliberately not scaled by it. It is persisted as
  the enum **biased by one**, so a zero byte means "never chosen" — the property
  `idle_style_fmt` needed a whole second byte to provide.
- ⚠️ **The flat overlay index is the only ADDRESS an upload has, resolved through
  `overlay_map[]` — so `reset_overlay_mapping()`'s identity default is LOAD-BEARING FOR
  WRITES**, not a display convenience. Zeroing it sent every image to slot 0: nearly
  every keycap blank, the whole set piled onto Esc.

**When the BOARD changes something the host caches, the answer is a counter on a reply
the host ALREADY polls** — `['G'][u16 state_generation]` in the GET_ID reply, bumped by
`poly_state_touch()`. The host's reconnect probe sends GET_ID every second forever, so
that reaches it within ~1 s at **zero** additional reports; a dedicated poll or an
unsolicited push both solve a problem that does not exist. ⚠️ **It goes AFTER the `V`
font-pack block** — the host finds that block positionally, and prepending makes every
deployed host re-flash all eight bundles on every connect. ⚠️ **Unsolicited raw HID is
not a drop-in**: nothing reads that interface except a pending command, and the drain
rests on "since v3 the firmware sends no unsolicited replies". **The console may
announce, never define** — it is lossy, does not survive re-enumeration, and any local
process can read it.

### Language list encoding (`lang/iso_lang_country.py`)
The packed list (cmd `27`) maps each 4-char code to two 1-byte indices: the
language's position in the ISO 639-1 table and the country's in ISO 3166-1
alpha-2. `lang/iso_lang_country.py` is the **frozen, append-only** index table —
generated once from the `iso-codes` package then frozen (indices never reorder;
new ISO codes append at the next free slot; private pseudo-codes with no ISO
639-1 entry, e.g. `hw`, live in a reserved block above the standard codes). The
`hid_com.c` case-27 cog imports it and emits the index bytes, so the table is a
**build-time artifact only** — it is *not* compiled into the firmware.
- ⚠️ **Single source of truth across three repos**: this file is byte-identical
  to `PolyKybdHost/polyhost/services/iso_lang_country.py` and
  `polykybd-ctnd/station/iso_lang_country.py`. When it changes, copy it to all
  three (verify with `cmp`); a mismatch silently decodes wrong languages on the
  host/rig. Adding a standard ISO language needs no table change (the code is
  already present); only a new private pseudo-code requires appending an entry.
- Re-run `cog -r hid_com.c` after any change to the list or the table (needs
  `cogapp` + `openpyxl`).

### Display rendering pipeline

Host sends a compressed bitmap → `fill_overlay.c` decompresses (optionally on core1) →
`overlays[idx][360]`; a key event inverts the keycap via the shift-register chip-select;
an app switch swaps all 72 images. How a legend is drawn is
[`LEGEND_RENDERING.md`](keyboards/polykybd/LEGEND_RENDERING.md), where the elements GO is
[`LEGEND_LAYOUT.md`](keyboards/polykybd/LEGEND_LAYOUT.md), the status OLED is
[`STATUS_OLED.md`](keyboards/polykybd/STATUS_OLED.md), and the per-keycap grid, the three
render seams and the settings-gate post-mortem are
[`DISPLAY_PIPELINE.md`](keyboards/polykybd/DISPLAY_PIPELINE.md). Six rules bind code
outside those files:

- ⚠️ **Adding a display-list op is TWO walkers, not one — THREE counting the host.** The
  draw dispatch and the measurement must clear the same flags and skip the same
  arguments, or the bbox describes a legend the draw does not produce. `oled_preview.py`
  is the third edit, and skipping it is silent: a refused op falls back to the keycode
  TEXT, which looks exactly like the op not working.
- ⚠️ **Nudge-run arithmetic is unverifiable by any test in this repo.** Render every
  legend you touched through `PolyKybdHost/tools/oled_preview.py` and require **0**
  pixels outside the 72×40 window.
- ⚠️ **Keep every glyph of one legend in ONE font** — `kdisp_write_gfx_char` baseline-
  aligns by `font->yAdvance - fonts[0]->yAdvance`, so two faces sit on two baselines
  (`a»ñ` put its `a` 7 px high).
- ⚠️ **`render_key()` and `to_static_text()` are a PAIR** — both must normalise the
  keycode the same way, or a key draws its chrome and NO legend. A third seam,
  `update_displays()`, can carry a bespoke per-keycode branch that makes the legend
  DEAD: **grep for your keycode there before believing a legend edit does anything.**
- ⚠️ **"Hidden" is TWO invariants — blank AND inert.** The advanced-row gate covered only
  the drawing half, so a blank Restart keycap still rebooted the board, reaching the
  field as "two crashes in a row". Gate ONE check ABOVE the switch, and when you gate a
  keycode for display, grep `process_record_user()` for it in the same pass.
- ⚠️ **The per-keycap DISPLAY grid is NOT a rectangle, and two physical keys have no OLED
  at all** (74 keys, 72 OLEDs). Anything mapping a key to a display must gate on
  `key_has_display(r,c)` first — a bounds check is not a substitute, since both
  offending keys index in-range phantom slots. ⚠️ **Model placement from the OLED
  chip-select, NOT the RGB `g_led_config` x-order** — they do not match.

### Split synchronisation

Seven custom QMK transaction IDs carry state and overlay data to the slave over UART with
CRC32 validation and up to 10 retries. Sizes, the buffer ceiling arithmetic and the
split42 shmem guard are
[`keyboards/polykybd/SPLIT_SYNC.md`](keyboards/polykybd/SPLIT_SYNC.md).

⚠️ **`RPC_M2S_BUFFER_SIZE` is a SILENT CEILING on every one of them, and it is a CAPACITY
rather than a transfer size.** `transaction_rpc_exec()` returns false **before sending
anything** when a payload exceeds it, and the bulk call sites discard the ack — so a
struct that grows past the cap produces a master that applies the change and a slave that
never hears it, with **nothing in the log**. `state.h` carries `static_assert`s; add one
for any struct that can grow. ⚠️ **The overlay path sits 3 bytes under the old cap** —
one more field, or an `HID_REPORT_SIZE` bump, and an app switch presents as missing
keycap images. Raising the cap costs RAM and nothing else (measured, not reasoned: `.bss`
+32, `.text` identical).

### Firmware staging, the self-apply, and the on-keycap signing prompt (FW-2)

`rules.mk` sets `-DFW_REQUIRE_SIGNATURE`, so an image without a valid Ed25519 signature
is not refused outright — **the board becomes the dialog**, blanking every keycap except
an **A / ACCEPT** and **R / REJECT** pair. The state machine, the render details and the
user-facing story are
[`keyboards/polykybd/FW_STAGING.md`](keyboards/polykybd/FW_STAGING.md) and
`keyboards/polykybd/tools/SIGNING.md`. Seven rules bind code outside them:

- ⚠️ **The self-apply's page buffer must be `uint32_t`.** A `static uint8_t
  page_buf[256]` word-copied through a `(uint32_t *)` cast has alignment 1, and an
  unaligned `STMIA` is a **HardFault on Cortex-M0+** taken inside a function that never
  returns. It shipped, and it bricked boards. The fix is the TYPE, not an `aligned(4)`
  attribute, so no later edit can reintroduce it.
- ⚠️ **When a bisect blames a commit that cannot have touched the failing code, check
  whether it moved the failing code's DATA.** That brick bisected cleanly to a macro PR
  that touches nothing in the applier: it grew `.bss` and shifted the buffer off a word
  boundary, while `fw_staging_do_apply` was byte-identical across the regression. Ten
  rounds were aimed at the code on the strength of that comparison.
  (`arm-none-eabi-nm -S <elf> | grep <buffer>`, then `addr % 4`.)
- ⚠️ **COMMIT must NOT block waiting for the answer** — it runs inside
  `raw_hid_receive()` on the loop that scans the matrix, so a busy-wait guarantees the
  keypress is never seen. It is a state machine answering `?` until resolved.
- ⚠️ **An UNSIGNED artifact gets the prompt; an INVALID one is refused outright.** Opposite
  events: offering a keypress for the second hands an attacker the one thing the physical
  gate exists to withhold. **Accept is physical, cancel may be remote.** The rule now
  covers the DOOM pack too (below), and both prompts share one presentation —
  `poly_sync_t.fw_confirm` carries the KIND (`enum poly_confirm_kind`), and the render
  gate, the key swallow and the `clear_keyboard()` are written once.
- ⚠️ **`clear_keyboard()` before ANY path that swallows keys or does not return**, or the
  host keeps a keycode registered and auto-repeats it until USB drops.
- ⚠️ **A visual cue set on a path that never returns is never painted.** The orange RGB
  cue had, in practice, never been seen — anything that must be *visible* before a
  blocking self-flash has to be flushed by the code that draws it.
- ⚠️ **Signing covers the firmware image AND the `.plyx` engine pack (FW-9), but NOT the
  rest of the resource region** — no signature check there at any target. Do not
  describe the keyboard as "signed firmware, so a malicious flash is covered".
- ⚠️ **The pack's unsigned prompt is gated on the ENTRY, and `build_pack.sh` does not
  sign.** `doom/doom_pack_gate.h` is the table: valid loads; INVALID is refused on every
  path; UNSIGNED prompts only on a deliberate `KC_IDDQD` entry and is refused on the idle
  screensaver, where nobody is there to answer. A locally built `.plyx` therefore always
  takes the prompt route — signing is `sign_doompack.py`, a separate step only
  `release.yml` runs. An accepted pack is remembered **for the boot, bound to its image
  CRC**, and reaches the slave through `poly_sync_t.doom_pack_auth_crc` — the CRC, not a
  flag, because the slave must honour the answer only for the pack IT holds (a partial
  `install_doompack` can leave the halves different, and the GET_ID slot block covers the
  master's slots only, so nothing downstream notices). The slave loads the pack too and
  never sees the keypress. ⚠️ **That binding, and the refusal latch's key, are the two
  places this gate has already been got wrong — the `audit-derived-verdict` skill is the
  checklist for any cached or delegated verdict, here or elsewhere.** ⚠️ The load runs on
  the loop that scans the matrix, so it **raises the prompt and returns false**; `doom_tick()` re-enters once the
  answer lands. Blocking there would guarantee the keypress is never seen — the same trap
  `FW_UP_COMMIT` avoids.

### The Intl layer: latin-variation picker and letter remap (`_ADDLANG1`)

Holding **Intl** shows each letter's selected accented variation; tapping **Ctrl** turns
the number row into a picker, and `KC_LAT_REMAP` reassigns a key to another letter's row
(so `e`, `q`, `j` can carry `é è ê`). Both mechanisms, the storage split and the four
traps behind them are
[`keyboards/polykybd/INTL_LAYER.md`](keyboards/polykybd/INTL_LAYER.md). Five rules
generalise beyond this feature:

- ⚠️ **An unwritten EEPROM byte reads `0x00` here, NOT `0xFF`** — QMK's wear levelling
  normalises cleared bytes to zero, so a map designed to need no migration sentinel read
  back as "every key hosts letter 0" and **every Intl keycap rendered a variation of
  `a`**. Gate such a field on the format version. ⚠️ **A version gate alone does not HEAL
  an already-flashed board**: retire the broken version value rather than reusing it.
- ⚠️ **Gate a release swallow on OWNERSHIP, not on the keycode** — swallowing the release
  of a modifier the user is really holding leaves it registered forever. **Modifiers and
  layer keys must fall through**; swallowing `MO(_ADDLANG1)`'s own release made the mode
  inescapable. The same rule bit twice, one function apart.
- ⚠️ **`render_key()` is only consulted when `to_static_text()` returns NULL**, so a key
  that HAS a legend bypasses a "the board becomes a dialog" mode entirely — including the
  key that opens it. Suppress `text` in `update_displays()` too.
- **The picker LATCHES by registering the REAL modifier**, because the slave only ever
  sees `poly_layer_t.mods` and both flag bytes in `base/com.h` are full.
- **Nothing may overlay this layer** — the letters *are* the payload. The `!add_lang`
  guard must wrap **both** arms of the display_overlays if/else; folding it into the
  first condition looks equivalent and is not.

### Glyph-script override (HID cmd 30, protocol v9+; expanded v10)

An OS-independent override of the language-layer legends with an alternative script
(Tengwar, runes, Aurebesh, IBM VGA, C64, Braille, APL…). The enum, the relocated PUA
blocks, the per-script font licensing and the render hook are
[`keyboards/polykybd/GLYPH_SCRIPT.md`](keyboards/polykybd/GLYPH_SCRIPT.md); the
`add-glyph-script` skill drives adding one.

- **The index is OPEN-ENDED (v10+): cmd 30 accepts any value `0..0xFE`, and an unknown
  one renders the normal legend.** That is what lets the host offer scripts a keyboard
  lacks and lets **new font faces ship without a protocol bump** — adding a `GLYPH_*`
  value needs only the enum entry, the `glyph_script_blocks[]` row, the font and the host
  label. **Don't re-add a range NACK.**
- **Codepoints are RELOCATED, not native** — the `flags` bundle already occupies the CSUR
  PUA, so a raw script codepoint would render a language flag. Each script gets a dense
  private block 0x40 apart, and the firmware needs only the base + dense index.
- **Keep user-facing strings generic** — trademark caveat on the fictional scripts, though
  the fonts themselves are license-clean.

### Layer names over the wire (HID cmd 35, protocol v14+)

The host layout editor used to label its tabs from a generated file whose generator's
source path had been dead through two renames — so it listed 14 layers ending
`EMJ0`/`EMJ1`, a split this firmware had not had in a very long time. **A name the
keyboard states itself cannot drift from the keyboard.** The encoding rationale is
[`keyboards/polykybd/LAYER_NAMES.md`](keyboards/polykybd/LAYER_NAMES.md).

- ⚠️ **The count is NOT a second opinion** — it is the same
  `DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT` cmd 17 reports. Do not "improve" it to
  `DYNAMIC_KEYMAP_LAYER_COUNT`: that invites the editor to draw tabs it cannot write to.
- **All three name widths live in ONE record** (full, ≤5 for split42's panel, ≤8 for the
  wire). They were three hand-kept lists, two carrying a "keep in sync" comment — the
  guard shape this repo keeps getting caught by.
- ⚠️ **The TOTAL is what makes the reply decodable**, and it is why the length is not in
  the records: without it, an **unnamed layer** is indistinguishable from the zero fill.
  ⚠️ The claim that a terminated form is *unsafe against truncation* is **FALSE** and was
  asserted here on a bad fixture — measured across every reachable failure mode, the
  three encodings behave identically. Don't re-derive a robustness argument without
  checking which scenarios the firmware can actually emit.

### The settings-layer RGB row

⚠️ **A LEGEND IS NOT EVIDENCE A KEYCODE DOES ANYTHING — the four RGB effect presets drew
a keycap for years and were dispatched nowhere.** `RGB_M_P`/`RGB_M_B`/`RGB_M_R`/`RGB_M_SW`
are legacy *underglow* mode keycodes with no case in `process_underglow()`, none in
`process_rgb_matrix()`, and `IS_RGB_KEYCODE` dispatched nowhere at all. The keys
rendered, felt real, and did nothing. **Before believing a key works because it has a
legend, grep for a `case` that handles its keycode** — the display pipeline and the
action pipeline share nothing, and this repo has now been caught by that seam in both
directions. Details:
[`keyboards/polykybd/RGB_SETTINGS_ROW.md`](keyboards/polykybd/RGB_SETTINGS_ROW.md).

- ⚠️ **An effect must be enabled on BOTH variants**, because the handler is in the shared
  keymap; read the indices out of the compiled object rather than counting the enum.
- ⚠️ **`val_to_percent()` scaled against 255 while the value is CAPPED at
  `RGB_MATRIX_MAXIMUM_BRIGHTNESS`**, so a fully-lit matrix reported 39% and could never
  reach 100. Saturation genuinely is a `/255` value — same row, different scale.
- ⚠️ **New RGB defaults reach only a FRESH eeconfig.** Say so in release notes rather
  than implying the value moved for everyone.

### Cirque trackpad gesture layer (split72)

- **The custom gesture layer — why the stock ASIC gestures are not used, the pure
  decision FSM, the tunables, and the analysis of running ONE image on either half —
  is [`keyboards/polykybd/TRACKPAD.md`](keyboards/polykybd/TRACKPAD.md).** Three rules
  that bind code outside `cirque_gestures.c` / `base/cirque_gesture_fsm.c`:
  - ⚠️ **The gesture layer is the DEFAULT; `-e POLYKYBD_CIRQUE_RELATIVE=yes` opts
    out.** It was opt-in until 2026-09-15, which meant PR CI and the release
    workflow — both of which build the *default* — produced a flavour no hardware
    round had ever run. **A build flavour only a hand-typed `-e` reaches is covered
    by nothing**; if a tested flavour is not the default, that is a gap, not caution.
  - ⚠️ **`POINTING_DEVICE_ROTATION_90` is RELATIVE-ONLY** (`split72/config.h`). Our
    layer emits deltas already in the pad's physical frame, and
    `pointing_device_adjust_by_defines()` would rotate them a second time — a 90°
    cursor error that reads as a wiring fault.
  - ⚠️ **`POINTING_DEVICE_RIGHT` does NOT bake in a side** — it only selects the
    runtime expression `!is_keyboard_left()` that three gates and both split handlers
    evaluate every boot. So "the pad is on the right" is a build-time *assertion*, not
    a hardware constraint; TRACKPAD.md has what a side-agnostic image would take.

### Community modules (`modules/polykybd/`) and the LTR-559 sensor

Self-contained, keyboard-independent code lives in **QMK community modules** rather than
`keyboards/polykybd/`: `polymod_crc32`, `polymod_rle`, and `polymod_ltr559` — the
**entirely optional** ambient-light + proximity sensor that shares the Cirque I2C bus and
cleanly no-ops when no sensor is fitted. The module mechanics, the sensor's tuning
numbers, the auto-brightness policy and the slave→master backchannel are
[`keyboards/polykybd/COMMUNITY_MODULES.md`](keyboards/polykybd/COMMUNITY_MODULES.md); the
`extract-qmk-module` skill drives a conversion.

- **Declared in `keyboard.json`, not `keymap.json`, and listing a module IS the enable** —
  the build defines `COMMUNITY_MODULE_<NAME>_ENABLE`. No `SRC +=` line, no bespoke `-D`;
  gate consumer code on the generated define.
- ⚠️ **Module hooks run BEFORE `_kb`/`_user`.** That is what makes a self-driving module
  safe, and it is the whole argument for deleting explicit init/task calls in favour of
  hooks — verify it before doing so.
- ⚠️ **Overriding the non-suffixed hook means you must call the `_kb` link yourself**, or
  the keyboard/keymap specialisations are silently dropped.
- **What is worth extracting**: code with no PolyKybd types and no display/protocol
  coupling. **Not** `poly_keymap.c` / `hid_com.c` / the overlay stack — that is the
  product.
- ⚠️ **The LTR-559 is side-agnostic** — do not re-gate it on `is_right_side()`; a
  left-soldered sensor was never read once, in the field. Its proximity baseline is
  **housing-dependent** (~129 open bench, ~325 mounted), so re-check the threshold after
  any housing change.

### Notable QMK features enabled
RGB matrix (72 LEDs, 35 effects), dynamic keymap (9 host-remappable layers), unicode input (Linux/macOS/Windows/BSD), Cirque trackpad (split72 variant), `USE_CORE1` multicore.

⚠️ **VIA is NOT supported and must not be advertised as such.** `VIA_ENABLE` is
unset on both variants — only `DYNAMIC_KEYMAP_ENABLE` is on, and remapping happens
through PolyKybdHost's own layout editor over our raw-HID channel. The one residue
is `poly_keymap.c`'s `#include "quantum/via.h"`, which is where QMK happens to
define the `id_dynamic_keymap_*` command IDs the dynamic keymap uses; that include
is a QMK header path, not a VIA feature. Don't reintroduce "VIA-compatible" wording
in docs, UI strings or comments.

## Font generation

Fonts for the per-keycap OLEDs are generated by `fontconvert` from the
[`AdafruitGFX/`](../AdafruitGFX/CLAUDE.md) repo, config-driven from
`keyboards/polykybd/fonts/fonts.yaml` — whose **list order IS the `ALL_FONTS[]`
priority** (front-to-back, first match wins). `generate_fonts.py` writes one header per
category and composes `gfx_used_fonts.h`; `--check` flags stale headers. Full docs:
[`keyboards/polykybd/fonts/README.md`](keyboards/polykybd/fonts/README.md).

- ⚠️ **Several artifacts are mirrored BYTE-IDENTICALLY into the host repo** —
  `fonts/noto-fonts.yaml`, `base/fonts/generated/fontpack_render_settings.json` and
  `lang_flags.json`. Keep them in sync (`cmp`); the host editor silently pre-fills the
  wrong controls otherwise.
- **Byte-reproducible output requires the pinned `fontconvert` build** (FreeType 2.13.3 /
  HarfBuzz 2.6.7); the distro fast-path build renders ~1 px differently on some glyphs.
- ⚠️ **`parse_gfx_header()` CANONICALISES every glyph's `bitmapOffset`**, so a purely
  cosmetic header change cannot reach the `.plyf` bytes. Without it, reformatting the
  tree changed four shipped bundles and would have forced a reship for zero visual
  change. Don't "simplify" it away.
- **Everything is grid-fitted EXCEPT emoji**, and ⚠️ setting `hinting: auto` on emoji is a
  measured **no-op** — those codepoints match no autohinter script, so 0 of 1156 glyphs
  change and only the provenance comment moves.
- **Adding codepoints to an existing `latin` font is the cheap case** (resident, in no
  bundle: no reship, no `content_version` bump) — and a codepoint **inside** an existing
  span costs only its bitmap, since the record already exists.

The font pack itself — the eight `PlyF` bundles, the slot layout and the HID transport —
is [`keyboards/polykybd/FONT_PACK.md`](keyboards/polykybd/FONT_PACK.md). ⚠️ **`g_all_fonts`
is scanned FRONT TO BACK and resident is always in front**, so adding a whole new
resident FONT shifts every pack font's gidx and forces a full-pack reship; extend the
resident `IconsFont` instead. ⚠️ **Never do heavy work in a split-transaction handler** —
re-CRCing the whole pack inside a ~20 ms RPC callback made the master report a perfect
flash as a CRC failure. ⚠️ **Because FONTPACK writes IN PLACE, a slot is valid as soon as
the last chunk lands — COMMIT is not what makes it so.** The `reship-fontpack-bundle`
skill handles a reship; it needs no `fontconvert`.

## Future language candidates

Adding a language needs (1) a new `LANG_*` entry in `lang/lang_lut.c` (cog-generated from
`lang_lut.xlsx`), (2) `fonts/gen-lang-fonts.sh` re-run for the flag glyph, and (3) the
host's `LANG_REGION` map only if the country code is non-standard. Full mechanics, the
candidate tables and the status of each shipped wave are in
[`keyboards/polykybd/lang/FUTURE_LANGUAGES.md`](keyboards/polykybd/lang/FUTURE_LANGUAGES.md);
the `add-polykybd-language` skill drives the job.

**`NUM_LANG` is 156.** ⚠️ Protocol codes are fixed 2+2 chars, so an ISO-639-2/3 language
needs a **pseudo-code** stored verbatim in the frozen index table (Hawaiian is `hw-US`,
Sorani `ku-IQ`). Adding a standard ISO language needs no table change.

## Hard-won lessons (and where the history lives)

### Troubleshooting principle: mechanical, auditable steps beat clever guesses

When a bug resists the "smart" theories, do the **dumb, exhaustive, fully-auditable
exercise** instead. Five rules, each of which was earned on the split42 rebuild (the
narrative is in
[`keyboards/polykybd/INVESTIGATION_HISTORY.md`](keyboards/polykybd/INVESTIGATION_HISTORY.md)):

1. **One change per commit**, so any subset can be flashed or reverted to bisect. The
   deliverable of an investigation is often the *commit sequence*, not just the fix.
2. **Derive facts from the authoritative source, not memory or an old file** — pins from
   the KiCad schematic, not a stale header.
3. **Suspect what is ABSENT or DISABLED, not only what is present.** A missing subsystem
   or dropped build flag can change timing, linker layout, split transactions or init
   order in ways that break an unrelated-looking feature. Diff a broken variant against a
   working one for *removed* config.
4. ⚠️ **If you claim you copied or reset something, actually do it from the source.** An
   earlier "rebuild" silently reused old files, hid the real diff, and produced "same
   problem as before". A reviewer must be able to verify each step from the git history.
5. **Don't over-narrate conclusions before the test.** State what a build contains and
   what each outcome would imply; let the hardware decide.

### Rules that came out of closed investigations

The narratives moved to
[`keyboards/polykybd/INVESTIGATION_HISTORY.md`](keyboards/polykybd/INVESTIGATION_HISTORY.md)
(11 resolved bugs, the split42 split-link saga, the split-link integrity analysis
and the HIL language-timeout note). **Read it when you need the evidence behind one
of these rules, a root cause to compare a new symptom against, or the reasoning
somebody is about to reverse.** What must stay in context is the rules themselves:

- ⚠️ **Never bool-test `send_to_bridge()` — classify with `sync_succeeded()`.**
  Every return value is non-zero, give-up included, so `if(!send_to_bridge(...))`
  is dead code: the master advanced `global` to `local`, produced no diff, and
  never re-fired the lost sync. The diff IS the retry queue, and only a successful
  sync may advance `global`. The *discarding* sibling applies to the bulk overlay
  sends, which threw the ack away entirely.
- ⚠️ **The split UART has NO payload integrity check of its own.** QMK's transport
  checks a 1-byte handshake token and nothing else; the per-transaction CRC32 in
  `split_sync.c` is the only thing between wire noise and the slave applying
  garbage. Do not remove it on the theory that the transport covers it.
- **Ack byte values are Hamming-spaced (min pairwise distance 4), built as
  complement pairs at popcount 4**, because the 1-byte reply carries no CRC. A
  seventh value must keep distance 4 or single-bit tolerance degrades for the whole
  set. `sync_succeeded()` is a deliberate **whitelist** so a new failure value is a
  failure at every existing call site with no edits.
- **`sync_is_link_fault()` is a COMPLEMENT, not an enumeration of its siblings** —
  a link fault is "nobody answered" or "what reached the slave was corrupt", and
  every other byte means the slave answered with a verdict. Listing the non-fault
  values is the guard shape that goes stale. `nack` is excluded from `err%` for the
  same reason: `SYNC_BUSY` arrives on every erase re-poll of a flash.
- **The split link is full-duplex two-wire** (`SERIAL_USART_FULL_DUPLEX`, TX GP5 /
  RX GP4, `SERIAL_USART_PIN_SWAP` giving the crossover by role at runtime so one
  image serves both halves). Steady-state error rate measured **zero** after the
  migration. ⚠️ `SELECT_SOFT_SERIAL_SPEED` is **0 = 460800 baud**, not the 230400
  the historical half-duplex analysis quotes.
- ⚠️ **split42 needs `POLY_SPLIT_SHMEM_RPC_GUARD`** — an 8-byte pad at the pointing
  member's position in `split_shared_memory_t`, in front of the RPC buffers
  (`transport.h`, tracked in `UPSTREAM_PATCHES.md`). **Do not remove it**: the
  latent writer it guards against was never found.
- **EEPROM persistence is the suspend-only dirty-flag model.** Never write EEPROM
  inside a split-transaction handler (a ~50 ms wear-levelling consolidation erase
  there costs the UART its response window), and never re-add a per-housekeeping
  default-layer drain — `save_all_dirty()` flushes at suspend / reset / store.
- **`g_user_brightness` is the MANUAL brightness and is updated only at deliberate
  set-points**; idle/suspend transients must never be persisted as it. Host-auto
  mode plus its last value persist separately in `poly_eeconf_t.auto_brightness`,
  with a **known bit** so engaging auto before the host pushes cannot bank the
  default as if real.
- **Idle tracking is a `bool` plus a `uint32_t` timestamp**, never a signed
  sentinel: `is_idle_tracking()` / `disable_idle_tracking()` /
  `backdate_last_update()`. The old signed `last_update` silently disabled idle for
  the ~25-day window past uptime 2³¹, and underflowed near boot.
- **An overlay mapping chunk is ONE-SHOT — nothing re-fires it**, unlike the
  periodic state syncs where the diff is the retry queue. The master holds the
  authoritative tables, so a lost chunk arms a repair that drains from
  **housekeeping**, never inline in the HID handler. The two *image* bridges cannot
  be repaired: `resolve_upload_side()` means the master never had the bytes.
- **`DISPLAY_OVERLAYS` belongs in `OVERLAY_SYNCED_STATE_FLAGS`**, and every
  mapping-apply site pairs `set_10bit_overlay_mapping()` with
  `request_disp_refresh()` — on both halves.
- **core1 runs with interrupts masked** (`cpsid i` at the top of `core1_entry`).
  Empirically the only fix for the overlay/ROI hang; the mechanism was never
  established, and it is safe because core1 polls the FIFO rather than waiting on an
  IRQ. Do not remove it on the strength of a theory.

