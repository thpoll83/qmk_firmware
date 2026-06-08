# PolyKybd Layout Market Analysis — Ranking Layouts by *Computer Users*, Not Speakers

**Date:** 2026-06-06
**Author:** Research analysis for the PolyKybd language-layout roadmap
**Scope:** Which keyboard-layout languages are most worth supporting *next*, ranked by the size of the **PC/computer-using** market (not raw native-speaker counts), weighted toward layouts not yet supported or planned.

> **Implementation status (2026-06-07):** the top new-gap recommendations have been
> shipped — **zh-TW (Bopomofo/Zhuyin)** `LANG_ZHTW`, **ka-GE (Georgian)** `LANG_KAGE`,
> **hy-AM (Armenian)** `LANG_HYAM`, and **id-ID (Indonesian)** `LANG_IDID` (enum indices
> 50–53). Indonesian was implemented exactly as this report recommends — **as a fold,
> not a distinct layout**: its key column is empty (every key falls back to en-US, i.e.
> plain US QWERTY) and the host maps `id=us`; it exists only so the keyboard can show an
> 🇮🇩 flag and switch the OS locale. zh-TW similarly folds `tw=us` (Bopomofo is an IME on
> a US base). Georgian/Armenian use new NotoSans{Georgian,Armenian} fonts; Bopomofo uses
> NotoSansTC.
>
> **Update (2026-06-07):** also shipped **az-AZ (Azerbaijani Latin)** `LANG_AZAZ`,
> **is-IS (Icelandic)** `LANG_ISIS`, and **vi-VN (Vietnamese TCVN/AltGr)** `LANG_VIVN`
> (enum indices 54–56). az-AZ and is-IS are Latin clones (is-IS needs no new assets;
> az-AZ adds only the schwa ə U+0259 glyph + a one-codepoint font range). vi-VN is the
> *dedicated TCVN* layout — ă â ê ô đ ư ơ ₫ on the number/bracket row plus the five
> combining tone marks (grave/hook/tilde/acute/dot-below) rendered as dotted-circle
> composites at PUA 0xE1A0. (Telex, the more common everyday IME, would instead be a
> no-op fold onto en-US since it types on bare QWERTY — see §3.)
>
> **Update (2026-06-08):** shipped **zh-HK (Hong Kong, Cangjie)** `LANG_ZHHK`
> (enum index 57) — the rank-12 gap. It needed **no new assets**: the Cangjie
> radical legends (Kangxi-radical block U+2F00–2FD5 plus 中/廿/難 at U+4E2D/5EFF/96E3)
> already existed in the tree, because the old **zh-CN column was itself a Cangjie
> radical layout**. zh-HK was seeded from that column, then completed with the last
> missing radical **Z = 重 (U+91CD)** (new `_Cjk91cd_` single-codepoint NotoSansJP
> font + `CJK_91CD` glyph).
>
> **Then zh-CN was re-pointed to Pinyin (2026-06-08):** mainland Chinese is in
> practice typed with a Pinyin IME on a **plain US-QWERTY** board (the keycaps carry
> no Han glyphs), so zh-CN's key column was **emptied** — it now folds onto en-US like
> id-ID, with the host mapping `cn=us` in `forced_country_match.txt`. The Cangjie
> radical legends therefore now live **only on zh-HK** (Zhuyin/Bopomofo on zh-TW),
> which matches real-world keyboards: zh-CN = plain QWERTY (Pinyin), zh-HK = Cangjie
> radicals, zh-TW = Zhuyin. Layout count stays 58 (zh-CN remains a selectable
> language + 🇨🇳 flag; only its keycaps changed).

---

## Executive summary

PolyKybd relabels keycaps for a selected layout; the OS does character generation. The product's value is highest where (a) a large population (b) actually uses computers with keyboards, and (c) types in a layout that is *physically distinct* from the already-supported US/European layouts (so the relabeling carries information). Ranking by **computer users** rather than **speakers** changes the picture sharply:

- **Taiwan (zh-TW, Bopomofo/Zhuyin)** is the single strongest unsupported gap. Only ~23M people, but ~96–97% internet penetration, one of the world's highest desktop-PC usage shares, and a layout (Zhuyin + Cangjie keycap legends) that is *genuinely distinct* from the supported zh-CN Pinyin/QWERTY. Moderate speakers, very high incremental PC value. **New legends, reuses CJK font work.**
- **Vietnam (vi-VN)** has a large, fast-growing IT/PC market (~80M internet users), but the dominant input method (Telex) runs on a **plain US-QWERTY base** — diacritics are typed via the f/w/z/j keys, not printed on caps. Incremental relabeling value is **low** unless the dedicated TCVN AltGr layout is targeted. **High speakers, low-to-moderate incremental PC value.**
- **Indonesia (id-ID)** is huge online (~212M users) but Bahasa Indonesia uses plain Latin with **no special characters** → it folds onto **en-US**. **High speakers, near-zero incremental value — recommend a host-side fold, not a new layout.**
- **Georgian (ka-GE)** and **Armenian (hy-AM)** are the best *new-script* gaps after Taiwan: distinct alphabets (not foldable), strong penetration (~82% / ~80%), each needs a genuinely new font but a single Latin-position-like mapping.
- **Azerbaijani (az-AZ, Latin)** and **Icelandic (is-IS)** are cheap Latin clones (no new font) with solid penetration, low cost, modest reach.

**Recommended near-term priority:** zh-TW (Taiwan Bopomofo) → ka-GE (Georgian) → hy-AM (Armenian) → az-AZ (Azerbaijani Latin) → is-IS (Icelandic), interleaved with the already-planned FUTURE_LANGUAGES batch (es-419, en-GB, de-CH/fr-CH, Thai). Treat Indonesian and plain-QWERTY Vietnamese as **folds**, not new layouts.

---

## 1. Methodology

### 1.1 Speakers vs. computer users

Ranking layouts by **native speakers** over-weights large populations with low PC penetration and under-weights small, highly-digitized markets. A keyboard product is used by people who **physically type on a hardware keyboard** — overwhelmingly a *desktop/laptop* activity. So the relevant denominator is not "speakers" but "people who type on a PC keyboard in this layout."

Three corrections matter:

1. **Mobile-first markets discount heavily.** Across the world, the majority of web traffic is now mobile; per StatCounter, worldwide *desktop* share of web traffic sits in the high-30s percent, with the balance on mobile/tablet ([StatCounter — Desktop vs Mobile Worldwide](https://gs.statcounter.com/platform-market-share/desktop-mobile/worldwide/)). Developed East-Asian and European markets skew much more toward desktop than the global average; many emerging markets (Indonesia, India, much of SE Asia) are heavily mobile-first, which suppresses their *keyboard*-relevant user count well below their internet-user count.
2. **Layout distinctiveness.** A layout only adds value if the keycap legends differ from one already supported. A language written in plain Latin with no extra letters (Indonesian, Malay, Swahili, plain-QWERTY Vietnamese-via-Telex) **folds onto en-US** — zero incremental keycap information.
3. **Fallback behavior.** Some markets nominally have a national layout but in practice type on US-English or an already-supported layout (e.g. many Indonesian and Filipino users on US QWERTY). Those should be discounted toward the layout they actually fall back to.

### 1.2 Proxy metric

For each candidate we estimate a **Computer-User Value (CUV)** proxy:

```text
CUV ≈ (internet users in countries where the layout dominates)
        × (desktop/PC usage share, as a fraction of those users who type on a PC keyboard)
        × (layout-distinctiveness factor d, 0–1: share who actually use the
           distinct layout vs. falling back to US-English / an already-supported layout)
```

- **Internet users** and **penetration %**: DataReportal *Digital 2025* country reports (Kepios / We Are Social / Meltwater), cross-checked against ITU / World Bank where useful.
- **Desktop/PC usage share**: StatCounter "Desktop vs Mobile vs Tablet" platform share per country (web-traffic proxy for keyboard usage; ranges given because StatCounter measures *page views*, not *people*).
- **Distinctiveness factor `d`**: judgment based on the layout's relationship to supported layouts (script, dead keys, AltGr legends), cited per row.

This is deliberately an **order-of-magnitude** instrument. Where StatCounter month-level desktop figures could not be machine-read (the site returns HTTP 403 to automated fetch), desktop share is given as a *range* consistent with StatCounter's published country pages and DataReportal's device-split commentary; these are flagged as estimates.

### 1.3 Sources (primary)

- DataReportal *Digital 2025* country reports: [Taiwan](https://datareportal.com/reports/digital-2025-taiwan), [Vietnam](https://datareportal.com/reports/digital-2025-vietnam), [Indonesia](https://datareportal.com/reports/digital-2025-indonesia), [Hong Kong](https://datareportal.com/reports/digital-2025-hong-kong), [Armenia](https://datareportal.com/reports/digital-2025-armenia), [Azerbaijan](https://datareportal.com/digital-in-azerbaijan), [Iceland](https://datareportal.com/reports/digital-2025-iceland).
- StatCounter Global Stats — platform/desktop share: [Worldwide](https://gs.statcounter.com/platform-market-share/desktop-mobile/worldwide/), [Taiwan](https://gs.statcounter.com/platform-market-share/desktop-mobile-tablet/taiwan), [Vietnam](https://gs.statcounter.com/platform-market-share/desktop-mobile-tablet/viet-nam), [Indonesia](https://gs.statcounter.com/platform-market-share/desktop-mobile-tablet/indonesia/).
- TWNIC 2025 Taiwan Internet Report — household penetration ([Taiwan News summary, Jan 2026](https://www.taiwannews.com.tw/news/6282453)).
- Layout/script references: [Bopomofo (Wikipedia)](https://en.wikipedia.org/wiki/Bopomofo), [Telex input method (Wikipedia)](https://en.wikipedia.org/wiki/Telex_(input_method)), [Vietnamese language and computers (Wikipedia)](https://en.wikipedia.org/wiki/Vietnamese_language_and_computers), [Georgian scripts (Wikipedia)](https://en.wikipedia.org/wiki/Georgian_scripts), [Armenian alphabet (Wikipedia)](https://en.wikipedia.org/wiki/Armenian_alphabet), Indonesian keyboard = US layout ([IndonesianPod101](https://www.indonesianpod101.com/blog/2020/10/16/how-to-type-in-indonesian/)).
- World Bank *Individuals using the Internet (% of population)* for cross-checks.

---

## 2. Gap analysis

Unsupported **and** not-yet-planned candidates with material computer-user value. (Currently-supported 41 layouts and the planned FUTURE_LANGUAGES set are excluded as gaps; the planned set is folded into the final ranking in §3.)

| Candidate | Internet users (2025) | Penetration | Desktop share (est.) | Script / layout vs. supported | Folds? | New font? | `d` | CUV tier |
|---|---|---|---|---|---|---|---|---|
| **Taiwan zh-TW** (Bopomofo/Zhuyin + Cangjie legends) | ~22.1–22.3M | ~95–97% | High (~45–55%) | Traditional-Chinese phonetic legends on QWERTY; **distinct** from zh-CN Pinyin (which uses bare QWERTY). Caps carry ㄅㄆㄇㄈ + Cangjie marks | No | Reuses CJK glyph pipeline; needs Zhuyin symbols (U+3105–312F) + optional Cangjie radicals | 0.9 | **A (highest)** |
| **Georgian ka-GE** (Mkhedruli) | ~3.1M | ~82% | Med–high | 33-letter Georgian alphabet, own block U+10A0–10FF; **not foldable** | No | **Yes — NotoSansGeorgian** | 1.0 | **B** |
| **Armenian hy-AM** (phonetic/Eastern) | ~2.37M | ~80% | Med–high | 39-letter Armenian alphabet, U+0530–058F; **not foldable** | No | **Yes — NotoSansArmenian** | 1.0 | **B** |
| **Azerbaijani az-AZ** (Latin) | ~9.23M | ~89% | Med | Latin + ə ğ ı ö ü ş ç; same family as tr-TR but distinct positions/extra ə | No (distinct from tr) | **No** — Latin-1/Ext-A already present (ə = U+0259 needs check) | 0.7 | **B** |
| **Icelandic is-IS** | ~0.39M | ~99% | High | Latin + áéíóúýþæðö; distinct from da/sv/nn | No | **No** — þ/ð/æ in Latin-1/Ext-A (already in tree) | 0.8 | **C** |
| **Hong Kong zh-HK** (Cangjie) | ~7.1M | ~96% | High | Traditional Chinese via Cangjie radicals on QWERTY | Partially → could share zh-TW legend work | Shares CJK + Cangjie work with zh-TW | 0.6 | **C** (do *with* zh-TW) |
| **Vietnam vi-VN** (Telex on QWERTY) | ~79.8M | ~78.8% | Med (~20–30%, mobile-heavy) | **Telex types on plain US-QWERTY**; diacritics via f/w/z/j. Dedicated TCVN layout exists but is minority | **Mostly folds to en-US** for Telex | No | 0.2 (Telex) / 0.6 (TCVN) | **C / discount** |
| **Indonesia id-ID** | ~212M (DataReportal) / ~229M (APJII) | ~74.6–80.7% | Low (mobile-first) | Plain Latin, **no special characters** | **Folds to en-US** | No | ~0.05 | **Discount (fold)** |

Notes per candidate:

- **Taiwan (zh-TW).** ~22.1M internet users Jan 2025 rising to ~22.3M (≈96.7%) by Oct 2025 ([DataReportal](https://datareportal.com/reports/digital-2025-taiwan); [Statista](https://www.statista.com/statistics/1296415/taiwan-online-population/)); household PC/internet penetration ~93.4% ([Taiwan News / TWNIC](https://www.taiwannews.com.tw/news/6282453)). Bopomofo is "the primary electronic input method for Taiwanese Mandarin," and physical Taiwanese keyboards carry **dual/quad legends — English, Zhuyin, Cangjie, (Dayi)** ([Bopomofo, Wikipedia](https://en.wikipedia.org/wiki/Bopomofo)). This is exactly the case PolyKybd exists for: a layout whose hardware caps differ from US/zh-CN. **Distinct from the supported zh-CN** (which is QWERTY+Pinyin and visually a US board). Highest incremental value despite modest population.
- **Georgian (ka-GE).** ~3.12M users, ~81.9% penetration. Mkhedruli is a unique 33-letter alphabet (U+10A0–10FF); standard layout maps letters phonetically to QWERTY positions ([Georgian scripts](https://en.wikipedia.org/wiki/Georgian_scripts)). Genuinely new script → genuine new keycaps. Needs NotoSansGeorgian.
- **Armenian (hy-AM).** ~2.37M users, ~80% penetration ([DataReportal Armenia](https://datareportal.com/reports/digital-2025-armenia)). 39-letter alphabet; phonetic layout predominant; some letters land on non-alpha keys ([Armenian alphabet](https://en.wikipedia.org/wiki/Armenian_alphabet)). Needs NotoSansArmenian.
- **Azerbaijani (az-AZ).** ~9.23M users, ~89% penetration — the largest Caucasus market online. Latin script close to Turkish but with the extra schwa **ə** and distinct positions, so it is *not* a tr-TR fold. Likely no new font (verify ə/U+0259 coverage). Good value-per-cost.
- **Icelandic (is-IS).** Tiny population (~0.39M) but ~99% penetration. þ/ð/æ already exist in the tree's Latin ranges → near-zero font cost; a cheap "completeness" add.
- **Hong Kong (zh-HK).** ~7.1M users, ~96% penetration. Cangjie is radical-based; best done as a companion to zh-TW since they share Traditional-Chinese font and Cangjie legend work. Lower `d` because many HK users also type Pinyin/English.
- **Vietnam (vi-VN).** Large (~79.8M users) and a fast-growing IT market, but the dominant **Telex** method composes diacritics on a **standard US-QWERTY base** (e.g. `aa`→â, `as`→á; f/w/z/j repurposed) ([Telex, Wikipedia](https://en.wikipedia.org/wiki/Telex_(input_method))). For Telex users the physical caps are just US QWERTY → **low relabeling value**. A dedicated layout would only help the minority **TCVN/AltGr** users. Plus Vietnam skews mobile-first, further discounting PC value. Classic "high speakers, low incremental PC value."
- **Indonesia (id-ID).** ~212M users (DataReportal) / ~229M (APJII) — enormous online. But Bahasa Indonesia "doesn't use special characters, so the QWERTY keyboard is enough" and the Indonesian keyboard "is the same as English (US)" ([IndonesianPod101](https://www.indonesianpod101.com/blog/2020/10/16/how-to-type-in-indonesian/)). **Folds to en-US.** Recommend a host-side `forced_country_match` fold (ID → en-US), *not* a new layout. The headline user count is a trap here.

---

## 3. Ranked recommendation

Combining the FUTURE_LANGUAGES.md candidates **(P)** and the newly-surfaced gaps **(N)**, ranked by computer-user value. Cost notes use the project's own taxonomy (Latin clone = mapping-only; new script = new Noto font + mapping).

| Rank | Layout | Src | One-line rationale | Implementation cost |
|---|---|---|---|---|
| 1 | **zh-TW** Taiwan Bopomofo/Zhuyin | N | ~22M users, ~96% penetration, very high desktop usage, layout *physically distinct* from supported zh-CN — the flagship gap | New legends (Zhuyin U+3105–312F + optional Cangjie); reuses CJK pipeline. **Medium** |
| 2 | **en-GB** British | P | en-GB diverges from en-US (£ " @ # \ ¬); very large, very high-PC English market | Latin clone, mapping-only, no font. **Low** |
| 3 | **es-419** Latin-American Spanish | P | ~400M speakers across high-and-rising-PC LatAm markets; distinct symbol/dead-key positions vs es-ES | Latin clone, mapping-only. **Low** |
| 4 | **ka-GE** Georgian | N | Unique script, ~82% penetration, not foldable — real new-keycap value | **New font** (NotoSansGeorgian) + mapping. **Medium** |
| 5 | **hy-AM** Armenian | N | Unique script, ~80% penetration, not foldable | **New font** (NotoSansArmenian) + mapping. **Medium** |
| 6 | **de-CH / fr-CH** Swiss | P | High-PC, high-income market; own QWERTZ distinct from de-DE/fr-FR | Latin clone(s), mapping-only. **Low** |
| 7 | **th-TH** Thai | P | ~60–70M speakers, distinct script & Kedmanee layout; linear (no Indic reordering) | **New font** (NotoSansThai) + large key map. **Medium-high** |
| 8 | **az-AZ** Azerbaijani (Latin) | N | Largest Caucasus online market (~9.2M, ~89%); distinct from tr-TR (extra ə) | Latin clone (verify ə glyph). **Low** |
| 9 | **fr-BE / nl-BE** Belgian | P | Belgian AZERTY differs from French AZERTY; affluent high-PC market | Latin clone, mapping-only. **Low** |
| 10 | **fr-CA** Canadian French | P | CSA/Multilingual-Standard layout, distinct from fr-FR | Latin clone, mapping-only. **Low** |
| 11 | **is-IS** Icelandic | N | ~99% penetration; cheap completeness add, glyphs already present | Latin clone, no font. **Very low** |
| 12 | **zh-HK** Hong Kong Cangjie | N | ~7M, ~96% penetration; best bundled with zh-TW (shared font/legends) | Shares zh-TW work. **Low-medium if after zh-TW** |
| 13 | **bn / te / ta** Indic batch | P | Large speaker bases; cheap after Hindi InScript lands (shared key map) | New font each; mapping is InScript clone. **Medium (font-bound)** |
| — | **vi-VN** Vietnamese (Telex) | N | High speakers but Telex = plain US-QWERTY base → low keycap value; mobile-first market | Only worth it for minority TCVN layout. **Defer / discount** |
| — | **id-ID** Indonesian | N | ~212M online but plain Latin, no special chars → identical to en-US | **Fold to en-US** host-side; do *not* build a layout |

### Flags called out explicitly

- **High speakers, low incremental PC value (discount):**
  - **Indonesian (id-ID)** — ~212M+ online but folds to en-US. Add as a host-side fold; building a "layout" gives zero keycap information.
  - **Vietnamese-via-Telex (vi-VN)** — ~80M users, but the dominant input method types on bare US-QWERTY; only the minority TCVN layout would benefit from distinct caps. Mobile-first market further discounts it.
- **Moderate speakers, very high incremental PC value (prioritize):**
  - **Taiwan (zh-TW)** — only ~23M people, but near-universal internet, top-tier desktop usage, and a *genuinely distinct* multi-legend layout. Best single addition on the board.
  - **Hong Kong (zh-HK)**, **Iceland (is-IS)** — small populations, very high penetration; cheap and distinctive (zh-HK shares zh-TW work; is-IS is a near-free Latin clone).
  - **Caucasus (ka-GE, hy-AM, az-AZ)** — small-to-mid populations but high penetration and (for ka/hy) unique non-foldable scripts.

---

## 4. Confidence & caveats

- **Desktop-share figures are ranges.** StatCounter's per-country month data could not be fetched programmatically (HTTP 403 to automated requests); the desktop-share columns are estimates consistent with StatCounter's published country pages and DataReportal device-split commentary, not exact monthly reads. The *relative* ordering (Taiwan/HK/Iceland high; Indonesia/Vietnam mobile-first) is robust; the absolute percentages are indicative.
- **Internet-user counts** are DataReportal *Digital 2025* (January 2025 baseline; Taiwan also has an October 2025 update at ~22.3M / 96.7%). Indonesia has two competing figures (DataReportal 212M / 74.6% vs. APJII 229M / 80.7%) — both cited; either way it folds to en-US.
- **Font-coverage claims** (Azerbaijani ə, Icelandic þ/ð/æ already present) should be verified against `named_glyphs.h` before scheduling — the FUTURE_LANGUAGES notes confirm Latin-1 Supplement + Latin Extended-A are fully present, which covers Icelandic; **ə (U+0259, Latin Extended-B)** for Azerbaijani needs an explicit check.
- **`d` (distinctiveness) is a judgment**, not a measurement. The biggest leverage in this whole analysis is `d`: it is what correctly demotes Indonesian (212M but d≈0.05) below Iceland (0.39M but d≈0.8).

---

*Sources are linked inline throughout §1.3 and §2. All user/penetration figures are DataReportal Digital 2025 unless otherwise noted; desktop-share figures are StatCounter-derived ranges as flagged in §4.*
