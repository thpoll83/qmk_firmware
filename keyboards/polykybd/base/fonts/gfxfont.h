// PolyKybd column-native glyph font structures.
//
// These are STRUCTURALLY identical to the Adafruit-GFX GFXglyph/GFXfont, but the
// `bitmap` byte layout is DIFFERENT: PolyKybd stores every glyph COLUMN-MAJOR in
// OLED page format (one byte = 8 VERTICAL pixels), NOT the Adafruit row-major
// layout (one byte = 8 horizontal pixels). Per glyph the bitmap is `width` columns
// of `cb = (height + 7) >> 3` page-bytes each (so per-glyph length is `width * cb`,
// NOT `(width*height+7)/8`), indexed `bitmap[bitmapOffset + xx*cb + (yy>>3)]` with
// bit `(yy & 7)` (LSB = top of the 8-px page). This matches the keycap OLED buffer
// (GET_BUFFER_OFFSET/SET_PIXEL in disp_array.c), so a glyph draws as whole vertical
// byte blits instead of a per-pixel raster from flash.
//
// The distinct type NAMES (`PolyColGfx`/`PolyColGlyph`) make that column layout
// unmissable at every reader/emitter. `GFXfont`/`GFXglyph` remain as compatibility
// aliases so existing declarations and the generated headers keep compiling during
// the migration — but a `GFXfont` here is column-major too, NOT Adafruit row-major.

#ifndef _GFXFONT_H_
#define _GFXFONT_H_

#include <stdint.h>

/// Column-native font data stored PER GLYPH
typedef struct {
  uint16_t bitmapOffset; ///< Pointer into PolyColGfx->bitmap (column-major bytes)
  int8_t width;         ///< Bitmap dimensions in pixels
  int8_t height;        ///< Bitmap dimensions in pixels
  int8_t xAdvance;      ///< Distance to advance cursor (x axis)
  int8_t xOffset;        ///< X dist from cursor pos to UL corner
  int8_t yOffset;        ///< Y dist from cursor pos to UL corner
} PolyColGlyph;

/// Column-native data stored for FONT AS A WHOLE
typedef struct {
  uint8_t *bitmap;      ///< Glyph bitmaps, concatenated — COLUMN-MAJOR (OLED pages)
  PolyColGlyph *glyph;  ///< Glyph array
  uint32_t first;   ///< Unicode codepoint extents (first char) — 32-bit so SMP
                    ///< codepoints (> 0xFFFF, e.g. emoji at 0x1F600) are stored
                    ///< directly, with no Private-Use-Area shift
  uint32_t last;    ///< Unicode codepoint extents (last char)
  int8_t yAdvance; ///< Newline distance (y axis)
} PolyColGfx;

// Compatibility aliases — a GFXfont/GFXglyph in the PolyKybd firmware is a
// column-native PolyColGfx/PolyColGlyph (see the header note above).
typedef PolyColGlyph GFXglyph;
typedef PolyColGfx   GFXfont;

#endif // _GFXFONT_H_
