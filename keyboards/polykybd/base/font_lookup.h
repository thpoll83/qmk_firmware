// Pure font lookup + legend measurement — no display, no SPI, no quantum.h.
//
// Extracted from disp_array.c so the glyph resolver and the display-list-aware
// bounding-box interpreter can be unit-tested on the host (the same seam as
// fw_up_verdict / macro_decode / legend_plan: the arithmetic here is the part
// with a bug history — the '!' substitution, the op-argument skipping, the
// per-glyph MID fallback — and it was only reachable through the SPI driver).
//
// disp_array.h re-exports this header, so every existing consumer of
// kdisp_gfx_glyph / kdisp_gfx_glyph_font / kdisp_gfx_text_bbox is unchanged.
#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "fonts/gfxfont.h"

// Host-built tests have no AVR/ARM progmem macros; QMK's progmem.h maps the
// accessors to plain reads there. Firmware builds get them from quantum.h before
// this header is reached (same pattern as glyph_meta.h).
#ifndef pgm_read_byte
#    include "progmem.h"
#endif

static inline GFXglyph *pgm_read_glyph_ptr(const GFXfont *font, uint32_t c) {
#ifdef __AVR__
    return &(((GFXglyph *)pgm_read_pointer(&font->glyph))[c]);
#else
    // expression in __AVR__ section may generate "dereferencing type-punned
    // pointer will break strict-aliasing rules" warning In fact, on other
    // platforms (such as STM32) there is no need to do this pointer magic as
    // program memory may be read in a usual way So expression may be simplified
    return font->glyph + c;
#endif  //__AVR__
}

static inline uint8_t *pgm_read_bitmap_ptr(const GFXfont *font) {
#ifdef __AVR__
    return (uint8_t *)pgm_read_pointer(&font->bitmap);
#else
    // expression in __AVR__ section generates "dereferencing type-punned pointer
    // will break strict-aliasing rules" warning In fact, on other platforms (such
    // as STM32) there is no need to do this pointer magic as program memory may
    // be read in a usual way So expression may be simplified
    return font->bitmap;
#endif  //__AVR__
}

// Floor division by two. Glyph offsets are negative (above the baseline) and C
// truncates toward zero, which would round those the wrong way and put a lowercase
// glyph 1px off its run's baseline. Written out rather than `>> 1` because a right
// shift of a negative value is only arithmetic by implementation guarantee.
static inline int16_t glyph_half_floor(int16_t v) {
    return (int16_t)((v >= 0) ? (v / 2) : -((int16_t)(((-v) + 1) / 2)));
}

// Locate the glyph for codepoint `ch` in a font set, scanning front-to-back and
// skipping empty (0x0) gap glyphs, exactly like the renderer's own lookup.
// Returns NULL when no font actually covers `ch` (no '!' fallback) — callers use
// this both to test coverage and to read glyph metrics. When out_font != NULL it
// also reports the font that owns the glyph, in the same scan — that is what the
// single-font baseline paths need (kdisp_write_gfx_char baseline-aligns every
// glyph to fonts[0]->yAdvance, so drawing a pack glyph through its owning font
// alone makes that adjustment zero; see the language-flag notes).
const GFXglyph *kdisp_gfx_glyph_font(const GFXfont *const *fonts, uint8_t num_fonts, uint32_t ch, const GFXfont **out_font);

static inline const GFXglyph *kdisp_gfx_glyph(const GFXfont *const *fonts, uint8_t num_fonts, uint32_t ch) {
    return kdisp_gfx_glyph_font(fonts, num_fonts, ch, NULL);
}

// The display-list-aware bounding box with the HINT_MID face pool passed
// explicitly. mid_font/mid_count name the single-font array \x16 switches the
// measurement to (PER GLYPH, falling back to the caller's pool — the
// word-over-icon case); pass NULL/0 to measure with no mid face available, in
// which case every \x16 glyph falls back. disp_array.c's kdisp_gfx_text_bbox is
// a thin wrapper binding the firmware's resident mid face.
//
// Mirrors the draw's cursor rules, op-argument consumption and per-glyph
// yAdvance baseline shift; a MOVE (\x0E) names an ABSOLUTE buffer position
// unknowable at measure time, so its arguments are consumed and the reposition
// itself is ignored. Empty / whitespace-only input reports all-zero.
void kdisp_gfx_text_bbox_in(const GFXfont *const *fonts, uint8_t num_fonts, const GFXfont *const *mid_font, uint8_t mid_count, const uint32_t *text, int8_t *out_xmin, int8_t *out_xmax, int8_t *out_ymin, int8_t *out_ymax);
