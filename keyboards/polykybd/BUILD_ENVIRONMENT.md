# Build environment setup (PolyKybd firmware)

Moved out of `CLAUDE.md` on 2026-09-10: this is the once-per-container setup —
toolchain, qmk CLI, submodules — needed when a build is being STOOD UP, not on
every session. The traps that bite mid-session (the `.bin`-not-`.uf2` deliverable,
the sha-in-the-filename rule, never two `qmk compile` at once, the off-PATH `qmk`,
the silently-reset checkout, the shallow clone, `-Wcast-align`) stayed in
`CLAUDE.md` § *Building & flashing*.

⚠️ **Every recipe here has failed in a new way at least once** — the dead codeload
tarball route, submodule clones that fail for all five modules and then succeed one
at a time, a `lib/*` dir full of files that is still uninitialised. Believe the error
in front of you over the history in this file, and check `git submodule status`
rather than assuming a loop worked.

---

- **Toolchain**: `sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi` → `arm-none-eabi-gcc` (13.2.x). This is what `qmk setup` installs on Debian/Ubuntu; the PyPI `qmk` package is only the bootstrapper (`config/clone/console/env/setup`) and does **not** bundle the compiler. There is no `bin/qmk` in this fork — the full CLI lives in `lib/python`.
- **qmk CLI**: `pip install qmk` (use a venv if system pip errors building `halo` — a Debian setuptools quirk), then `qmk config user.qmk_home=<repo>` (or `export QMK_HOME=<repo>`) so it discovers `compile`/`flash` from the repo's `lib/python`, plus `pip install -r requirements.txt`.
- **Submodules** (empty in a fresh clone): `make git-submodule`. The minimum for split72 is `lib/chibios lib/chibios-contrib lib/pico-sdk lib/printf lib/lufa` (printf and lufa are needed even on RP2040 — `quantum/logging` and the ChibiOS USB stack pull them in).
  - ⚠️ **In a web/remote container `make git-submodule` (and `qmk git-submodule`) 403s** — the injected git proxy only serves the session's *authorized* repos, and `qmk/*` aren't in it, so the submodule clone is rejected. **This is NOT a real "build unavailable" — do not give up here.** The fix is **`add_repo`**, once per submodule repo: call it for `qmk/ChibiOS`, `qmk/ChibiOS-Contrib`, `qmk/lufa`, `qmk/printf`, `qmk/pico-sdk` and it answers `read_available` ("the git proxy serves anonymous git reads of public GitHub repos") **without attaching anything**. From then on the ordinary command just works — no tarballs, no manual unpacking:
    ```bash
    git submodule update --init --depth 1 --no-recommend-shallow lib/chibios   # …and the other four
    ```
    ⚠️ **The old `codeload.github.com` tarball recipe is DEAD — it now returns 403**, with a JSON body telling you to use `add_repo` (2026-08-11; it was documented here as "allowed (200), verified 2026-06-25", so believe the error, not this file's history). It also fails *quietly* in a pipeline: `curl -sSL … | tar xz` prints only `gzip: stdin: not in gzip format` while the shell reports success, so a loop over five submodules can look like it worked. `curl -w "HTTP=%{http_code}"` is the check.
  - ⚠️ **An upstream merge BUMPS the submodule pins, and nothing checks them out for you.** The 0.33.13 merge moved `lib/chibios` `8bd61b80→6170ddf9` and `lib/chibios-contrib` `8d863d9e→5a9ad82b`. Re-run the init above **after** the merge (`git submodule status` shows the `-`/`+` prefixes), or you link a new QMK against an old ChibiOS — which compiles cleanly and fails at runtime.
  - ⚠️ **A `lib/*` dir can be FULL OF FILES and still be uninitialised — leftover
    extracted tarballs from the dead codeload recipe, pinned to the wrong revision.**
    This is a third state beyond "empty clone" and "pin bumped", and it looks healthy:
    `ls lib/chibios` shows a complete tree, so the natural conclusion is that
    submodules are fine. The tells: **`git submodule status` prefixes it `-`** (not
    initialised) and **`lib/<m>/.git` does not exist**. The build then dies on a
    *version* mismatch rather than a missing file — the signature is
    ```text
    ./lib/chibios/os/hal/include/hal.h:136:2: error: #error "obsolete or unknown configuration file"
    ```
    ⚠️ **But those two tells are NOT proof the tree cannot build — the BUILD is the
    authority, so compile BEFORE reaching for `rm -rf`.** On 2026-09-22 all five
    modules showed the `-` prefix with no `lib/<m>/.git`, exactly the state described
    above, and `qmk compile` came back clean for split72 AND split42, twice. Acting on
    the prefix alone would have deleted a working `lib/` tree and spent the session
    re-cloning through a proxy that had already failed five times once. The cheap check
    is the compile itself: the `#error "obsolete or unknown configuration file"` above
    is the signature that matters, and it costs one build to ask. Same rule as this
    file's opening line — believe the error in front of you, not the history here.

    Fix, once a build has actually failed that way: `rm -rf` the stale dirs and re-init
    properly (after `add_repo`, above):
    ```bash
    rm -rf lib/chibios lib/chibios-contrib lib/pico-sdk lib/printf lib/lufa
    for m in lib/chibios lib/chibios-contrib lib/printf lib/lufa lib/pico-sdk; do
        git submodule update --init --depth 1 --no-recommend-shallow $m
    done
    ```
    ⚠️ `make`'s own auto-`git-submodule` step does **not** rescue this: it tries to
    clone into the non-empty dir, prints `destination path … already exists and is not
    an empty directory`, and carries on to a doomed build (2026-08-12).
  - ⚠️ **In a FRESH container that loop can fail for EVERY module — retry them one at
    a time.** Run back-to-back straight after the five `add_repo` calls (2026-09-02),
    all five died with `fatal: clone of '<url>' failed / Failed to clone '<path>' a
    second time, aborting`, while `git ls-remote` against the same URL succeeded — so
    the remote was reachable and it is not an authorisation failure. Re-running
    `lib/printf` **alone** then worked first time, and the other four followed once
    each was retried individually:
    ```bash
    for m in lufa chibios chibios-contrib pico-sdk; do
        rm -rf .git/modules/lib/$m lib/$m     # a failed clone leaves a half-state
        git submodule update --init --depth 1 --no-recommend-shallow lib/$m
        sleep 5
    done
    git submodule status lib/*                # every line must start with a SPACE
    ```
    ⚠️ **Cause unestablished — do not theorise one.** The plausible candidates (the
    proxy's 429 concurrency cap, authorisation needing a moment to propagate after
    `add_repo`) were not tested, and this file's own history is full of confident
    mechanisms that turned out wrong. Record the remedy, not a story. The tell is
    cheap: `git submodule status` prefixes an uninitialised module `-`, so check it
    rather than assuming the loop worked.

## Building & flashing

**The ARM toolchain is installable in the dev / remote container — do not claim it is unavailable.** Verified end-to-end (`split72:default` → `.uf2`, exit 0) on 2026-05-29.

- **Toolchain, qmk CLI and submodules** — the once-per-container setup is
  [`keyboards/polykybd/BUILD_ENVIRONMENT.md`](BUILD_ENVIRONMENT.md):
  `gcc-arm-none-eabi`, `pip install qmk` + `QMK_HOME`, and the submodule init, whose
  three failure modes are each written up there (the git proxy 403 that `add_repo`
  fixes, a `lib/*` dir that is full of files and still uninitialised, and a fresh
  container where all five clones fail and then succeed retried one at a time).
  ⚠️ `git submodule status` must show a **leading space** on every line; an
  uninitialised module is prefixed `-`, and a build against one dies on
  `#error "obsolete or unknown configuration file"` rather than a missing file.
  ⚠️ An upstream merge BUMPS the pins — re-init after merging or you link new QMK
  against old ChibiOS, which compiles cleanly and fails at runtime.
- **Build**: `qmk compile -kb polykybd/split72 -km default` (or `make polykybd/split72:default`). Output `.uf2` lands in the repo root and `.build/`.
- **Deliverable for testing is the `.bin`, NOT the `.uf2`** — the user flashes over HID via PolyKybdHost's firmware updater (`polyhost/device/hid_fw_up.py`), which takes the raw RP2040 image: `arm-none-eabi-objcopy -O binary .build/<target>.elf .build/<target>.bin`. The `.uf2` is only for manual bootloader-drive recovery.
  - ⚠️ **Put the commit sha in the FILENAME — every test build reports the same
    `FW_VERSION`, so they are otherwise indistinguishable once flashed.** `FW_VERSION`
    only moves on the post-merge auto-bump, so a session with several hardware rounds
    hands over N files that all answer `0.13.1` to `polyctl fw version` and carry
    near-identical names (`…_fix` / `…_legend` / `…_invert`). That cost a full round
    (2026-08-13): a correct build was reported as "I did not see the new behavior",
    and the only way to settle it was to md5 the delivered file against a fresh
    rebuild and grep the image for a changed string literal. `split72_<sha>_<slug>.bin`
    takes the ambiguity away. Better still, when a change alters something **visible
    on a keycap**, say which pixel tells the builds apart — that is a check the user
    can run without any tooling.
  - ⚠️ **A branch-built `.bin` reports a DIFFERENT `FW_VERSION` from the one CI and
    the HIL rig show for the SAME commit — and that is normal, not a stale build.**
    CI builds the PR *merged into* its base, so it picks up every auto-bump that has
    landed on `PolyKybd` since the branch was cut; a local `qmk compile` builds the
    branch alone. On 2026-08-22 the delivered image answered `0.15.7` while the rig
    logged `Split72 0.15.10 P13` on commit `d8bb98ca` — a 12-commit base drift. It
    reads exactly like handing over the wrong file, so **settle it by diffing, not by
    rebuilding**:
    ```bash
    git log --oneline HEAD..origin/PolyKybd                 # what the branch lacks
    git diff --name-only HEAD...origin/PolyKybd             # EVERY path, not just ours
    git diff HEAD...origin/PolyKybd -- keyboards/polykybd/config.h   # only FW_VERSION?
    ```
    If the only firmware delta is `config.h`'s version string, the `.bin` carries every
    real change and just names itself older. If it is more than that, the branch is
    genuinely behind and the test build is missing base fixes — merge before delivering.
    ⚠️ **Read the CONTENT and the UNRESTRICTED path list — `--stat` scoped to
    `keyboards/polykybd` proves neither half of that sentence.** `--stat` reports line
    counts, so `config.h | 2 +-` is equally consistent with a version bump and with a
    changed `#define` beside it; and the image links this fork's **patched upstream
    files** too, which a PolyKybd-scoped diff hides — `keyboards/polykybd/
    UPSTREAM_PATCHES.md` is the maintained list of them (today `usb_descriptor.h`,
    `usb_main.c`, `oled_driver.c`, `transport.h`, `rp2040.c`), and a catch-up merge
    landing on `PolyKybd` is exactly what moves them. Read that file rather than
    hardcoding the set here — it is the thing that stays current. Even then this is a
    drift check, not proof of binary equivalence: if anything outside `config.h` shows
    up, rebuild on the merged base rather than reasoning about whether it mattered.
- **Docker is NOT usable** in the remote container (no daemon) — use the native toolchain above, not the qmk docker image.
- ⚠️ **NEVER run two `qmk compile` invocations at once — every flavour of a board
  shares ONE `.build/` tree, and the collision presents as a CODE error.** Backgrounding
  the pack build and starting the monolith beside it made the pack link die on
  `undefined reference to doom_shim_menu_key_tile` (2026-09-03) — a symbol the pack
  flavour genuinely does not define locally, so it reads exactly like a real
  missing-shim bug rather than two builds overwriting each other's objects in
  `.build/obj_polykybd_split72_default`. Serially, both link clean and nothing else
  changes. `build_pack.sh` is safe because it sequences the two flavours itself.
- The `firmware-size-diff` skill builds HEAD vs working tree and diffs sizes / `.text`.
- ⚠️ **In the session container `qmk` is at `/root/.qmk_venv/bin/qmk` and is NOT on
  `PATH`.** `build_pack.sh` (and anything else shelling out to `qmk`) dies with
  `qmk: command not found`. Prefix every build:
  `export QMK_HOME=$PWD && export PATH="/root/.qmk_venv/bin:$PATH"`. The
  `deliver-test-firmware` skill wraps this.
- ⚠️ **The checkout can be SILENTLY RESET to an older commit** when the web/remote
  container is reclaimed — your commits survive on `origin`, but the working tree
  and `HEAD` roll back, and nothing announces it. It happened **three times** in one
  session (2026-08-01); once it sent a code review chasing a `NUM_VARIATIONS` /
  pool-size mismatch that existed only in the reverted tree. **Run
  `git log --oneline -1` before trusting any grep or "the code says…" conclusion**,
  especially at the start of a turn or after a long build. Restore with:
  ```bash
  git fetch origin <branch> && git reset --hard origin/<branch>
  ```
  Uncommitted work is lost, so push early. This applies to every repo in the
  session, not just this one.
- ⚠️ **The container's clone is SHALLOW, and that makes `git merge-base` return an
  EMPTY STRING rather than an error** — so anything comparing this branch to
  upstream silently produces nonsense instead of failing. Seen 2026-08-11: the
  clone was 198 commits deep, `git merge-base HEAD upstream/master` printed
  nothing, and `git rev-list --count $MB..0.33.13` with the empty variable
  degraded to `HEAD..0.33.13` and reported **29,576 commits**, i.e. "these
  histories are unrelated" — for a fork whose merge base is one of its own
  branches. **Before ANY merge, diff-vs-upstream, or `git describe` reasoning:**
  ```bash
  git rev-parse --is-shallow-repository        # true = every history answer below is a lie
  git fetch origin --unshallow --no-recurse-submodules   # ~1-2 min on this repo
  ```
  `--no-recurse-submodules` matters: the submodule repos aren't proxy-authorized
  (see above), so a plain fetch spews `Could not access submodule 'lib/chibios'`
  and buries the real result. Once unshallowed, the merge base resolved to
  exactly the fork's own `master` — which is the sanity check that it worked.
  - ⚠️ **It breaks a RELEASE RANGE the same way, and that one reads as plausible
    history rather than as nonsense.** `git log <tag>..origin/PolyKybd` on a shallow
    clone walks truncated history and returns a wrong set **with no error** — asked
    for 0.15.2→0.15.14 it returned commits from the **0.9.54** era, complete with
    believable bump-commit boundaries (2026-08-26). The 29,576-commit case above at
    least screams; this one would simply have shipped release notes describing the
    wrong versions. So the `--is-shallow-repository` check belongs in front of ANY
    tag-anchored history question, not just merge-base reasoning — and note the tag
    itself resolves fine (`git rev-parse <tag>` succeeds), so a tag-exists check
    proves nothing. Unshallowing took **45 s** here; the count went 84 commits.
- **`-Wcast-align` is on for PolyKybd's OWN sources, and it exists for the
  HID-apply brick class.** `fw_staging`'s page buffer was `static uint8_t
  page_buf[256]` (alignment 1) word-copied through a `(uint32_t *)` cast; the
  linker put it at a byte offset, the unaligned `STMIA` HardFaulted the M0+ in a
  function that never returns, and it shipped in a release. The warning names
  exactly that — *"cast increases required alignment of target type"* — and with
  `-Werror` already on it is a build failure on the PR that writes it. That is
  the only place a bisect can find this class, because the brick itself was a
  **layout** effect: a macro PR grew `.bss` and moved the buffer, so the guilty
  commit never touched the failing code.
  - ⚠️ **Scoped by path via the `$<` per-recipe filter** (`rules.mk`, the same
    mechanism the doom `EXTRAFLAGS` block uses). `EXTRAFLAGS` otherwise lands on
    **every** compile line — upstream QMK, ChibiOS, pico-sdk — which is the trap
    that kept CodeQL out of this repo.
  - ⚠️ **The WHOLE `doom/` tree is excluded, not just the vendored engine, and
    the reason for our own sources is worth knowing: `doom_arena_at()` returns
    `uint8_t *` because that signature IS the pack ABI** (`doom_pack_abi.h`,
    handed to a **signed** `.plyx`). So every `(doom_mirror_t *)doom_arena_at(…)`
    is a widening cast the check cannot be satisfied about without editing a
    cross-boundary contract — which is not something to do on a warning's
    account. `void *` would be the better type for untyped arena storage; it was
    tried and reverted for exactly that reason. The offsets are
    `_Static_assert`ed 4-aligned in `doom_arena.h` instead, which is the
    substance, and the one such cast **outside** the doom tree (`split_sync.c`'s
    mirror handler) carries a narrow `#pragma` pointing at those asserts.
  - ⚠️ **A path filter that matches nothing FAILS OPEN** — the flag never applies
    and the guard looks installed while doing nothing. Verify by compiling a
    deliberate misalignment in a PolyKybd source and confirming the build
    **fails**, not by reading the make output.
  - **`void *` casts do not warn** (GCC exempts them), so `bridge_helper.c`'s
    split-link CRC store is unaffected — and is separately safe, since every
    caller passes a struct whose first member is a `uint32_t`.
  - **It found two real latent instances of the same shape**, both now asserted
    rather than assumed: `doom_mode.c` casts the core1 **stack pointer** out of a
    `uint8_t *` pool (where a misalignment is worse than the applier's HardFault
    — 8-aligned base, both offsets multiples of 8, which is also what AAPCS
    demands), and every `doom_arena_at()` consumer relies on arena offsets that
    nothing checked. ⚠️ Do **not** launder such a cast through `uintptr_t` to
    silence the warning — that proves nothing and hides the next one.
  - ⚠️ **Verified the hard way, and it earned its keep immediately**: the first
    build with the flag FAILED on `split_sync.c`, which is simultaneously the
    proof that the path filter matches (it would otherwise fail open) and a real
    find. Do not take a clean build as evidence the flag is active — take a build
    that fails on a deliberate misalignment.
  - The clean-up it required was itself worth having: the OLED helpers took a
    `uint32_t[]`, cast it down to `char *` at the call and back up inside, which
    was safe only by convention. They take `uint32_t *` now.

- ⚠️ **When an upstream merge breaks the build, look at the vendored DOOM engine
  FIRST — a new upstream warning lands there, not on our own sources.** QMK builds
  with `-Werror`, so *any* warning upstream adds to `builddefs/common_rules.mk`
  becomes a hard failure in `doom/engine/` (a third-party rp2040-doom snapshot that
  nobody is going to clean up). 0.33.13 added
  `-Wunused-but-set-variable`/`-parameter` — as collateral of **"GCC 16.1
  compatibility fix" (#26216)**, not a deliberate tightening, so the commit subject
  gives no warning — and six `m_menu.c` menu callbacks that take an ignored
  `choice` parameter failed the build. **The fix site is the `-Wno-error`/`-Wno-`
  demotion block in `keyboards/polykybd/rules.mk`** (the doom-only `EXTRAFLAGS`),
  which already carried the `-variable` half of that exact pair; add the sibling
  there rather than editing vendored code. Keep the demotions doom-scoped so
  PolyKybd's own sources still get the warning.

- ✅ **The `.rodata` assembler warning on EVERY build is EXPECTED, and it is OURS —
  even though it names a stock upstream file.** Every build of either variant prints
  `Warning: setting incorrect section attributes for .rodata`, attributed to
  **`quantum/keymap_introspection.c`** — which is stock upstream and contains no
  section attribute at all. It compiles the keymap (`#include KEYMAP_C`), so the
  warning is reported against it while originating in
  `split{42,72}/keymaps/default/keymap.c`:
  `__attribute__((section(".rodata"))) led_config_t g_led_config`. That override
  keeps the table (296 B on split72) in flash instead of RAM; `g_led_config` cannot
  be `const` (it must match upstream's `extern led_config_t g_led_config`), so GCC
  emits `.section .rodata,"aw"` — writable — while `.rodata` already exists as `"a"`.
  The first attributes win, so it really is read-only in flash. Verify rather than
  trust: `arm-none-eabi-objdump -t <elf> | grep g_led_config` must show `.rodata` at a
  `0x10xxxxxx` (flash) address, not `0x20xxxxxx` (RAM). **Nothing to fix** — dropping
  the attribute costs the RAM it saves, and adding `const` clashes with the extern.
  - ⚠️ **This is the exact INVERSE of the security-scanner rule** in `REVIEW_CONVENTIONS.md` ("a finding on
    an upstream path is probably UPSTREAM's"): here the *file named* is upstream's and
    the *cause* is ours. `diff`ing `keymap_introspection.c` against upstream proves it
    identical and proves nothing about the warning. When a diagnostic names an
    inherited file, check what that file **includes** before concluding it is not ours.
