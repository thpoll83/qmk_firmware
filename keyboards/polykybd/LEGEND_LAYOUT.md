# Per-language legend layout (PolyKybd keycaps)

Where the three elements of a keycap legend go — the base glyph, the Shift preview
and the AltGr hint — and how they are kept from colliding or running off the 72x40
panel. Moved out of `CLAUDE.md` on 2026-09-10: it is needed when you touch
`lang_lut.xlsx`, `render_key()` or `base/legend_plan.c`, and not otherwise.

Each note is kept WHOLE, measurement attached. In this area the evidence IS the
specification — the clamp order decides which edge loses on an over-size glyph, and
`ALTGR_HALF_MIN_INK_H` is 7 because the ink histogram has an empty bin at 8 px — so
a rule separated from its numbers is a rule nobody can re-derive.

**Two skills drive the work these notes describe**: `tune-lang-lut-cells` for a
change across many layouts, and `keycap-layout-preview` for placing and MEASURING a
single element. `PolyKybdHost/tools/oled_preview.py` is both the mirror of the
firmware draw path and the measuring instrument.

⚠️ **Measure overlap as the INTERSECTION of two ink sets**, never as "how many
legend pixels survived" — lit-on-lit loses no pixels and still reads as merged, and
that metric hid a real digit collision for a round.

---

- ⚠️ **The Shift and AltGr hints are laid out as a PAIR, and it took a field report
  to notice they were not.** Both sit right of the base legend — Shift upper, AltGr
  lower — and until 2026-09-02 the only thing holding them apart was their
  per-language VERTICAL offsets. That is fine for a narrow Latin pair and wrong for
  a tall script: swept over all 160 layouts × 49 keys, **24 keys across 19 layouts**
  drew the two through each other — every `ar-*` layout on `KC_F` by 13 px, `bn-BD`
  worst at 6 keys and up to **57 px** on `KC_D`. `en-US` never shows it because its
  letter Shift offset is `HIDE_KEY`, which is exactly why it survived so long.
  - ⚠️ **It reads as a MISSING GLYPH, not as a layout bug** — reported as *"I can see
    wildcards rendered on a d f"*. Those are correct Shift previews (`\` on A, `]`
    on D, `[` on F) landing inside the AltGr glyph's box; `s` and `g` have no Shift
    preview, which is why they looked skipped. Check `overlap_detail` before chasing
    a font.
  - ⚠️ **A per-language OFFSET cannot fix this, so don't reach for `lang_lut.xlsx`.**
    The offsets are per *language* while the room left over is decided by the WIDTH
    of this key's three glyphs — one number would have to satisfy the layout's worst
    key and would crush every other key into the base. `bn-BD` needs a different
    separation on `C` (3 px) than on `D` (57 px).
  - The fix is the same shape as the shift-vs-base logic one branch up: resolve the
    AltGr hint **before drawing**, and when the two ink boxes intersect in **both**
    axes pull the Shift **left** into the gap between the base and the right-clamped
    AltGr — floored at the base's own 2 px margin, and never moved right (which could
    only walk it into the clamp). Generic and glyph-width driven, no per-language code.
  - **Measured, not eyeballed**: `shift^altgr` 24 keys / 398 px → **1 key / 22 px**,
    `base^altgr` 0 either way, `base^shift` unchanged at its one pre-existing 2 px
    key, and the off-panel pixel count **byte-identical per key**. 53 keys move.
    `PolyKybdHost/tools/oled_preview.py` is both the mirror and the measuring
    instrument; its `report=` gives per-element ink boxes, overlap and out-of-bounds.
  - **`bn-BD KC_H` is the key the pull alone could NOT fix** — a 22 px base plus a
    27 px Shift plus a 37 px AltGr will not fit 72 px at full size, so the pull could
    only shrink it 29 → 22 px. Halving the AltGr hint (below) is what closes it.
- ⚠️ **A Shift CELL has TWO jobs, and emptying it to hide the PREVIEW destroys the
  uppercase — the suppression is a build-time BITMAP instead
  (`shift_preview_redundant`, `lang/lang_lut.c`).** The cell is the preview drawn in
  the unshifted view AND the legend drawn while Shift is actually held, so clearing
  it leaves `translate_keycode()` falling back to `lower_case` — and `VAR_CAPS` is
  empty on those keys, so the capital then lives nowhere and the keycap shows `ä`
  with Shift down. Reported from hardware 2026-09-02 after a tuning pass emptied 164
  cells across 52 layouts to hide previews that only repeated the base letter.
  - **The rule is `lang/shift_preview.py`**: a preview is redundant when the base and
    the Shift each resolve to exactly ONE printable glyph and they are a case pair in
    either direction (`ä`/`Ä`, and `Ø`/`ø` where a few layouts store the capital as
    the base). An exact duplicate needs no clause — a caseless character is its own
    upper case. It fails SAFE: anything it cannot resolve keeps its preview, because
    a stray preview is cosmetic and a missing one is information lost.
  - **The case comparison is done in PYTHON at build time**, where the Unicode tables
    are, and emitted as `uint8_t[NUM_LANG][8]` — 1280 B of `.rodata`, no RAM. All 164
    hand-emptied cells are covered by the generic rule (verified), and it finds the
    same shape on 72 more keys: **236 previews suppressed across 75 layouts**, of the
    4388 that draw at all.
  - ⚠️ **The HOST PREVIEW imports that module and feeds it the RAW cells** rather than
    re-deriving anything — `oled_preview.shift_preview_rule()` /
    `shift_preview_cells()`, mirrored into `gen_keycap_tuner.py` so the tuner cannot
    offer offsets for a preview the board never draws. Sharing the *resolver* and not
    just the rule is the point: measured, two resolvers disagreed on **59** of ~3300
    keys. A bit-level check over all 160 × 49 (lang, key) pairs is what proves parity
    — 0 mismatches.
  - ⚠️ **The named-glyph table must be built from column B (HEX INPUT) of the
    `named_glyphs` sheet, NOT column C (CALC DEC).** C is a FORMULA, and openpyxl's
    `data_only` pass returns `None` for a formula with no cached result — the state of
    **942 of ~1400 rows** in this workbook, and the state it has been in on every
    revision checked. Reading C silently drops those names, so their cells resolve to
    `None` and *keep* their preview; that was the whole 59-key disagreement above.
    `named_glyphs.h` is generated from column B, so B is what the firmware draws.
  - ⚠️ **A literal close-comment inside a `/*[[[cog … ]]]*/` block ends the C comment
    early** and the compiler then parses the generator source as C
    (`missing terminating ' character`, `-Werror`, dead build). The emitter builds its
    trailing `/* lang */` marker by concatenation for exactly that reason. It costs
    two build cycles to notice, because cog itself is perfectly happy.
- **EVERY element of a keycap is clamped onto the panel, on ALL FOUR edges, through
  ONE helper — `legend_plan_clamp()` (`base/legend_plan.c`).** The main legend, the
  Shift preview and the AltGr hint all pass their measured ink box through it, so
  they cannot disagree about where the edge is. Two things about this are worth
  knowing before touching any of them:
  - ⚠️ **It replaced an ASYMMETRY, not an absence, and the asymmetry is what made the
    off-panel ink invisible.** The big legend tiers had been clamped on all four edges
    inside `plan_main_legend()` since they shipped, and the two hints had a
    right-edge-only clamp — while the **small base legend had none at all**. So a
    per-language `VAR_SMALL` offset tuned for one script's glyph heights, applied to
    every key of the layout, silently pushed ink off the north or west edge and it was
    simply lost. Measured over all 160 layouts × 49 keys: **420 keys / 3934 px**
    clipped, of which 305 elements were small base legends (worst `th-TH KC_E`, 8 px
    off the west edge) and 138 Shift previews (worst `ku-IQ KC_L`, 8 px off the top).
  - **Nothing is wider than the panel and exactly ONE element is taller** (he-IL's
    43 px standalone nikud on `KC_BACKSLASH`), so the clamp is a no-op for anything
    already inside the window and the change is confined to the keys that were losing
    ink. Measured after: **1 key / 9 px**, which is that one glyph in a 40 px panel.
  - ⚠️ **The clamp order decides which edge loses on an over-size glyph** — E before W
    (west wins), N before S (south wins, so the top clips). Preserved from the big
    path rather than chosen; changing it moves those 9 px and nothing else.
  - **The stagger and the hint pull are re-clamped after they move something**, so the
    panel edge beats a readability nicety. That matters most for the 6 px lift the
    overlap stagger applies to a *small* base — which, now that a small base is
    clamped, could otherwise push a tall legend straight back off the top.
  - ⚠️ **A big per-language offset is therefore a NO-OP past saturation rather than a
    way to hide a glyph.** `HIDE_KEY` (-128) is the hide mechanism and is checked
    before any of this, so nothing that relied on hiding changes; but the keycap tuner
    sliders now stop moving an element at the edge for all three, where before only the
    two hints saturated and the base ran off the panel.
  - **Cost: `.text` +432 B, `.rodata`/`.data`/`.bss` byte-identical, monolith `.heap`
    free unchanged at 2772.** The bboxes were already computed for all three elements,
    so the clamp is a handful of compares per element on data the render path had
    anyway.
- **The AltGr hint is drawn at HALF size on the layouts that ask for it, and WHICH
  layouts is DATA — `{letter|num|sym.altgrhalf}`, rows 62–64 of `lang_lut.xlsx`.** It is a hint —
  what the key would type under a modifier nobody is holding — so on a script whose
  letters fill the keycap it reads better subordinate to the base legend, and a
  full-size script glyph was most of what made the two hints fight over the right-hand
  side. `render_key()` prepends `HINT_SMALL` to the cell and re-measures.
  - ⚠️ **A SIZE test cannot express which layouts, and the measurement is the reason.**
    The intuition is "Arabic and Indic have very large glyphs"; AltGr ink **height does
    not separate them at all** — median **20 px** on Arabic letters against **21 px** on
    Latin ones. What actually differs is that on those layouts the base and the Shift
    hint are wide too, so the row reads crowded. That is a per-**layout** judgement no
    glyph measurement can make, which is why it lives in the spreadsheet — one cell per
    language, flipped by editing the cell, never by tuning a threshold in the C.
    The opt-in is **per category**, the same three-way split the H/V offsets use, so
    `render_key()` picks `half_set` beside `v_set`/`h_set`. As tuned on hardware
    (2026-09-02): **`{letter.altgrhalf}` on 27 layouts** — every Arabic-script one
    (`ar-*`, `fa-IR`, `ur-PK`, `ku-IQ`, `ps-AF`) plus `ne-NP`, `bn-IN`, `bn-BD`,
    `te-IN`, `ta-IN` — and **`{sym.altgrhalf}` on the 18 `ar-*` layouts**, whose
    symbol row is as crowded as their letters. `{num.…}` is set nowhere.
    ⚠️ **`hi-IN` and `mr-IN` were turned OFF again after looking at them** — their
    letter AltGr cells are mostly bare combining marks that the mark guard leaves full
    size anyway, so the flag bought nothing and only made the few real letterforms
    inconsistent with their neighbours. Derive the current set from the spreadsheet
    rather than from this list; it is a judgement per layout and it moves.
    ⚠️ A row nothing uses still has to EXIST — the tuner can only offer a setting the
    spreadsheet carries — so do not "clean up" the all-blank `{num.…}` one.
  - ⚠️ **The one size test that REMAINS is the mark guard, and its threshold IS
    measured.** Halving a glyph that is already tiny destroys it — a Hebrew nikud is
    2×3 px and comes out a dot. Over the **318 distinct AltGr cells** the ink-height
    histogram has an **EMPTY BIN at 8 px**: 44 cells below it (nikud, diaeresis, middle
    dot, hyphen) and 274 letterforms from 9 px up. So `ALTGR_HALF_MIN_INK_H` is 7
    because the data separates itself there, and a host test asserts nothing inks
    exactly 8 px tall — a new language landing in the gap has to **re-derive** the
    constant from the histogram, not nudge it. It carries most of the **Indic** layouts
    on its own: their letter AltGr hints are mostly bare combining marks at a median
    4 px, so they stay full size even though the layout opted in.
  - **Measured**: hint-on-hint overlap 1 key / 22 px → **0**, `.text` +192 B, `.rodata`
    +1920 B (three settings rows at `int8_t[NUM_LANG*4]` = 640 B each), `.data`/`.bss`
    byte-identical and the monolith's `.heap` unchanged (the halving scratch is on the
    stack).
  - ⚠️ **A settings row must be INSERTED, not appended** — cog collects the block by
    walking column A until the first empty cell, and the row after it is the
    `lower/upper/caps/ALT Gr` legend, so anything below that is invisible.
    `lang/_insert_settings_row.py` renumbers the rows underneath.
  - ⚠️ **A row number lives in FOUR places in sheet2.xml and only two are inside
    `<sheetData>`.** `<row r>` and `<c r>` are; **`<mergeCell ref>` and `<hyperlink
    ref>` sit after it**, so a renumbering pass that slices the tail of `sheetData`
    never reaches them. This sheet has **27 merges and 10 hyperlinks anchored on the
    legend row** — exactly the row an insert pushes down — so the old script left them
    one row short. That claim it made about "no merged cells outside row 1" was simply
    never true.
    - **The failure is SILENT and asymmetric between openpyxl's two readers, which is
      what makes it vicious.** `read_only=True` streams row elements and reports the
      sheet as perfect; the eager loader keys on cell refs, materialises a phantom cell
      where the stale hyperlink still points, and hands back its `display` text as that
      cell's value. **cog uses the eager loader**, so the first inserted settings row
      came out with `https://www.branah.com/english` as its en-US offset (2026-09-02).
      Verifying with the streaming reader alone says everything is fine.
    - The script now renumbers both and **runs `_verify_readers_agree()` after every
      write**, comparing the two readers over the edited region. Compare
      **asymmetrically**: eager holding a value streaming lacks is the corruption;
      eager holding `None` where streaming has a value is a pre-existing shared-string
      quirk on the legend row (present in the *untouched* workbook — the docstring used
      to blame inserts for it) and must be tolerated, or the guard can never be green.
  - ⚠️ **Never size a generated table from `key_index` or any other cog variable left
    over from an earlier block.** The Shift-suppression bitmap did
    `num_keys = key_index - 2`, where `key_index` is where the *settings*-block walk
    stopped — so it was 63, not the real 54, and `row = 2 + k` walked into the settings
    rows evaluating the redundancy rule on offsets like `35` and `HIDE`. Harmless by
    luck (`index` maxes at 53, so the stray bits at k >= 54 are unreachable, and all of
    them came out 0 — the suppression count was never wrong), but the array was two
    bytes per language too wide, and adding six settings rows silently took the stride
    from 8 to 9. It derives `num_keys` from the sheet now. **A generated array's
    dimension is a fact about the data; if it moves when you add an unrelated row, the
    derivation is wrong.**
  - ⚠️ **The AltGr view when AltGr is actually HELD is untouched, and must stay so** —
    that branch is at the top of `render_key()` and there the glyph IS the legend, not
    a hint.
  - ⚠️ **`kdisp_write_gfx_char_half` draws NOTHING for a missing glyph** (no `'!'`
    substitution, no advance), where the full-size writer substitutes. Every AltGr cell
    resolves against the full font set today, so this is only reachable on a keyboard
    whose font pack is absent or behind — where the hint silently disappears instead of
    showing `!`. Acceptable for a hint; do not extend the halving to a **base** legend
    on the same reasoning.
