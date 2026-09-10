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
    Fix: `rm -rf` the stale dirs and re-init properly (after `add_repo`, above):
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
