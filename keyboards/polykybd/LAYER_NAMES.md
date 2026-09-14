# Layer names over the wire

Moved out of `CLAUDE.md` 2026-09-14. Verbatim.

### Layer names over the wire (`layer_names.c`, HID cmd 35, protocol v14+)

The host layout editor labels its layer tabs. It used to read those labels from
`PolyKybdHost/polyhost/res/layer_names.yaml`, a build-time artifact generated from
this repo's `layers.h` — and that generator's default source path had been dead
through **two** renames, so nothing regenerated it and nothing failed. The committed
file still listed 14 layers ending `EMJ0`/`EMJ1`, a split this firmware had not had
in a very long time (found 2026-08-26). The editor was mislabelling tabs against an
enum that no longer existed.

**A name the keyboard states itself cannot drift from the keyboard**, which is the
whole point of cmd 35. Three things are worth knowing:

- ⚠️ **The count is NOT a second opinion.** It is the same
  `DYNAMIC_KEYMAP_UPDATE_MAX_LAYER_COUNT` that cmd 17 already reports, echoed back so
  the reply parses standalone. Do not "improve" it to
  `DYNAMIC_KEYMAP_LAYER_COUNT` (12): layers at or above the write cap are served from
  flash and have no editor tab to label, so naming them would invite the editor to
  draw tabs it cannot write to.
- **All three name widths live in ONE record** (`layer_names.c`). split72's status
  OLED wants the full name, split42's 32 px panel wants ≤5 chars, and the wire wants
  ≤8 — and before this those were three hand-kept lists, two of which carried a
  "keep in sync with the other" comment. That is the guard shape this repo keeps
  getting caught by, so a layout now cannot be added without giving it every form.
  A `_Static_assert` pins the named set to exactly the remappable range;
  mutation-checked by dropping `_UL` from the table, which fails the build with the
  assert's own message.
- **Names are capped at 8 chars, so "Colemak DH" ships as `ColemkDH`.** The cap is
  the host tab label's budget; the emitter clamps rather than trusting the table, so
  an over-long name can never run into the next record.
- ⚠️ **The TOTAL is what makes the reply decodable, and it is why the length is not
  in the records.** The host reads byte 0, keeps reading until it holds that many
  bytes, and only then splits on the NULs — so termination is arithmetic rather than
  a scan, and the report's zero fill is never examined. Two encodings were tried
  first and shipped briefly; both are worse, and the reasons generalise:
  - **Fixed-width 8-byte records** give the same arithmetic length but cost 65 bytes
    and therefore a **second report**, for a command whose whole payload otherwise
    fits one.
  - **Terminated records with no total** force the decoder to find the end by
    scanning, and the only way to tell a real terminator from the zero fill is "an
    empty name means padding" — which makes an **UNNAMED layer** (`poly_layer_name_wire()`
    returning NULL, i.e. a bare terminator) indistinguishable from the fill and
    silently truncates the list. That is the case that decided it.
  - ⚠️ **The claim that a terminated form is UNSAFE against truncation is FALSE, and
    was asserted here for a while on the strength of a bad test fixture.** The fixture
    fed the decoder a **short non-final report**, which the emit loop cannot produce —
    a short report is always the last one. Measured across every reachable failure
    mode (last report lost, first lost, reordered, nothing arrives), fixed-width,
    naive-terminated and total-prefixed behave **identically**. Robustness is a wash
    under real HID behaviour, because whole 64-byte reports arrive or nothing does;
    the encoding choice is about size, report count, and whether an unnamed layer is
    expressible. Don't re-derive a robustness argument here without checking which
    scenarios the firmware can actually emit.
- **`LAYER_NAMES_PAYLOAD_MAX` is `_Static_assert`ed to stay under 255**, since the
  total is one byte. Mutation-checked by widening `POLY_LAYER_NAME_MAX` to 32, which
  fails the build with the assert's own message. At 12 layers × 8 chars it is 110.
- **Names are capped at 8 chars, so "Colemak DH" ships as `ColemkDH`.** The cap is
  the host tab label's budget; the emitter clamps rather than trusting the table, so
  an over-long name can never run past the buffer.

