---
name: add-gated-hid-command
description: Add a new PolyKybd HID command, or a new flag byte on an existing one, end to end across the firmware and the host — the `PROTOCOL_VERSION` bump, the `FEATURE_MIN_PROTOCOL` gate, the device/core/RPC/CLI/GUI wiring, tests on both sides, both-variant builds, the CLAUDE.md protocol entry, and the right bump label. Use when asked to "add a HID command", "add cmd NN", "let the host set/read X on the keyboard", "add a flag to cmd NN", "bump the protocol", or whenever a host feature needs the firmware to do something it cannot do today. NOT for a host-only change (no firmware round trip), NOT for the overlay/font-pack bulk transports (those are dispatched independently of the protocol version), and NOT for changing what an existing command already does without a wire change.
---

# Add a protocol-gated HID command or flag

The host connects across a **range** of firmware protocols and disables features
individually, so every device-facing command needs a gate. The rules are real and
enumerated — in `keyboards/polykybd/CLAUDE.md` ("Every new device-facing command
MUST be version-gated — no exceptions") and in `PolyKybdHost/CLAUDE.md` — but they
live in prose in two files, and the firmware CLAUDE.md notes the gate "has been
forgotten twice". This skill is the checklist.

An **ungated** command does not fail cleanly: it connects, then NACKs at runtime on
an older keyboard, instead of greying out its menu entry.

## 0. Decide the shape first

| You want | Shape | Protocol bump? |
|---|---|---|
| A new command the firmware has never had | new `case NN` | **yes**, and a `supports()` gate |
| A new flag/byte on an existing command | extra payload byte | **yes** — an old firmware IGNORES it, which is the dangerous direction |
| A new *value* in an already open-ended set (e.g. another glyph script) | none | **no** — see `GlyphScript`, deliberately open |
| Bulk overlay / font-pack / profiler traffic | dispatched independently | **no** |

⚠️ **Open vs closed ranges are a real design decision, not a default.** An unknown
`GlyphScript` index is accepted and degrades to the normal legend, which is what
lets the host offer faces a keyboard lacks. An unknown `GlyphSize` is NACKed,
because it would otherwise persist as a setting that silently renders small.
`tests/device/poly_kybd_capabilities_test.py` pins that contrast on purpose.

⚠️ **A new payload byte is backwards-compatible on the wire in ONE direction only.**
The host zero-pads every report (`hid_helper.send`), so an old *host* sends 0 and
an old *firmware* reads 0 — fine if you choose 0 to mean the legacy behaviour. But
an old *firmware* also **ignores** your new byte and does the legacy thing while
the caller believes otherwise. That asymmetry is why the flag is gated even though
the wire is compatible. Worked example: cmd 20's VOLATILE flag (protocol 17), where
an old firmware would have **persisted** a mode the caller explicitly asked not to
store.

## 1. Firmware (`qmk_firmware`)

```bash
export QMK_HOME=$PWD && export PATH="/root/.qmk_venv/bin:$PATH"   # container: qmk is off PATH
```

1. `keyboards/polykybd/hid_com.c` — add the `case NN:` (or read the new byte).
   Reply with `hid_reply(data, NN, ok)` + `raw_hid_send`; NACK an out-of-range value
   unless the range is deliberately open.
2. `keyboards/polykybd/config.h` — `PROTOCOL_VERSION` += 1.
3. If it needs to reach the **slave**, bridge it and check the ack with
   `sync_succeeded()` — never bool-test `send_to_bridge()`, every return is non-zero.
4. If it persists, add the field to `poly_eeconf_t` / `poly_sync_t` and follow the
   suspend-only dirty-flag model — no direct EEPROM write, and note that
   `eeprom_update_byte` already skips an unchanged value.
5. Build **both** variants — `config.h` is shared:

```bash
qmk compile -kb polykybd/split72 -km default && qmk compile -kb polykybd/split42 -km default
```

⚠️ Never run two `qmk compile` at once — they share one `.build/` tree and the
collision presents as a bogus `undefined reference`.

## 2. Host (`PolyKybdHost`)

1. `polyhost/device/command_ids.py` — the `Cmd` entry (and any enum the payload uses).
2. `polyhost/device/poly_kybd.py` —
   - a `<FEATURE>_MIN_PROTOCOL = N` constant **with a comment saying what it gates**,
   - the `FEATURE_MIN_PROTOCOL["<feature>"]` entry,
   - the getter/setter, guarded by `self.supports("<feature>")` returning a
     "firmware too old" `(False, msg)` — not an exception.
3. `polyhost/_version.py` — `__protocol__` = the same N.
4. `polyhost/device/poly_kybd_mock.py` — mirror the signature, or every mock-backed
   test breaks on the new keyword.
5. `polyhost/core/poly_core.py` — the core method (`_device_call` for a read,
   `worker.submit` for fire-and-forget).
6. `polyhost/server/protocol.py` + `control_server.py` — the `M_*` method,
   `polyhost/client/remote_core.py` — the mirror. **A daemon-mode GUI is a client, so
   anything not mirrored is unreachable out of the box.**
7. `polyhost/cli/polyctl.py` — the subcommand.
8. `polyhost/host.py` — the menu entry, gated on `self.supports("<feature>")`.

⚠️ **`keeb.supports()` lazily does device I/O** (`query_version_info()` when
`protocol_version` is unset). Anywhere off the worker — a background thread, the
no-I/O status snapshot — use the pure `protocol_supports(self.keeb.protocol_version,
"<feature>")` instead.

## 3. Tests (both sides, and the wire byte)

```bash
# host
.venv/bin/python -m unittest tests.device.poly_kybd_cmd_test \
    tests.device.poly_kybd_capabilities_test tests.core.<your>_test
xvfb-run -a .venv/bin/python scripts/run_tests.py --timeout 240   # full suite, 65-90 s
# firmware, if the logic is pure enough to extract (see base/fw_up_verdict.c)
make test:<name>
```

- **Pin the payload BYTES** in `poly_kybd_cmd_test.py` (`self._payload(...)[:4]`),
  including the flag byte's legacy value — that is the only test that catches the
  host and firmware disagreeing about the wire.
- **Pin the GATE** in `poly_kybd_capabilities_test.py`: the threshold, and a
  refusal below it. ⚠️ When you added a FLAG to an existing command, also pin that
  the **unflagged** form still works below the threshold — the gate is on the flag,
  not on the command, which is ancient. A brand-new command has no unflagged form:
  there, everything below the threshold refuses.
- **Mutation-test** what you added (`mutation-test-suite`). A gate that is never
  exercised is a gate that does not exist.

## 4. Land it

- `CLAUDE.md` (firmware) — append a `**vN** adds …` paragraph to the HID protocol
  list, saying what the command does, which direction the compatibility runs, and
  anything a reader would otherwise re-derive.
- ⚠️ **Label `bump:minor` (or whatever fits) at OPEN, and NOT `bump:protocol`** when
  you bumped `PROTOCOL_VERSION` in-source — the label would double-bump it. A label
  applied later races the merge and loses; `create_pull_request` cannot set labels,
  so follow it immediately with `issue_write` + `labels:`.
- **Ship the two repos together**, and say so in both PR bodies. Which order is safe
  depends on the change; work it out explicitly rather than assuming.

## Verify before you call it done

```bash
grep -n "PROTOCOL_VERSION" keyboards/polykybd/config.h        # firmware N
grep -n "__protocol__" ../PolyKybdHost/polyhost/_version.py   # host N — must match
grep -n "<feature>" ../PolyKybdHost/polyhost/device/poly_kybd.py   # constant + table + guard
```

## Pitfalls

- **The gate is the easy thing to forget** — it is also the whole point. A command
  without a `FEATURE_MIN_PROTOCOL` entry connects and then NACKs at runtime.
- **Forgetting `__protocol__` no longer rejects the keyboard**, which is worse than
  the old behaviour: it silently leaves the feature disabled and pins the status to
  "update the host app".
- **A `RemoteCore` mirror is not optional.** Daemon-by-default means the tray is a
  client; an un-mirrored method is missing for every normal user and present for you.
- **Don't bump the protocol for a new value in an open-ended set.** Check whether the
  firmware already accepts unknown values in that range before adding a version.
- **Don't add a getter that returns a cached value** where the caller means "what is
  running right now" — `polyctl fw version` was a cache once, and it lied straight
  after a flash.
