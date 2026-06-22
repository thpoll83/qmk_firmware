#pragma once

#include "generated/latin_fonts.h"
#include "generated/hebrew_fonts.h"
#include "generated/jp_fonts.h"
#include "generated/kr_fonts.h"
#include "generated/arabic_fonts.h"
#include "generated/devanagari_fonts.h"
#include "generated/bengali_fonts.h"
#include "generated/telugu_fonts.h"
#include "generated/tamil_fonts.h"
#include "generated/thai_fonts.h"
#include "generated/georgian_fonts.h"
#include "generated/armenian_fonts.h"
#include "generated/bopomofo_fonts.h"
#include "generated/vietnamese_fonts.h"
#include "generated/ethiopic_fonts.h"
#include "generated/canadian_fonts.h"
#include "generated/cherokee_fonts.h"
#include "generated/symbol_fonts.h"
#include "generated/emoji_fonts.h"
#include "generated/emoji_fig_fonts.h"

#include "gfx_icons.h"
#include "FreeSansBold24pt7b.h"

// Functional-minimum fonts compiled into the firmware image. The rest live
// in the external-flash font pack (base/fontpack.h) and are appended at
// runtime into g_all_fonts[]. Unreferenced pack-font data is dropped by the
// linker (--gc-sections), shrinking the image. See fonts/README.md.
const GFXfont* const RESIDENT_FONTS [] = {
  &IconsFont,
  &NotoSans_Regular_Base_14pt7b,
  &NotoSans_Regular_LatinExtB_14pt16b,
  &NotoSans_Regular_Schwa_14pt16b,
  &NotoSans_Regular_SupAndExtA_14pt16b,
  &NotoSans_Regular_CurrencySigns_14pt16b,
  &NotoEmoji_Medium_Hyper_18pt16b,
  &NotoEmoji_Medium_Meh_20pt16b,
  &NotoEmoji_Medium_Settings_20pt16b,
  &NotoEmoji_Medium_World_20pt16b,
  &NotoSansSymbols2_Regular_Arrows_20pt16b,
  &NotoSansSymbols2_Regular_Technical2_20pt16b,
  &NotoSansSymbols_Regular_Technical_20pt16b,
  &NotoSansSymbols2_Regular_GuiKey_20pt16b,
  &NotoSansSymbols2_Regular_Util_20pt16b,
  &NotoEmoji_Medium_Brightness_16pt16b,
  &NotoSans_Regular_Okina_14pt16b,
  &NotoSans_Regular_LatinExtAdd_14pt16b,
  &NotoSans_Regular_Naira_14pt16b,
  &NotoSans_Regular_Cyrillic_16pt16b,
  &NotoSans_Regular_Greek_14pt16b,
  &NotoSans_Regular_LetterMod_14pt16b,
  &NotoSans_Regular_SZ_14pt16b,
};

#define RESIDENT_FONT_COUNT (sizeof(RESIDENT_FONTS) / sizeof(GFXfont*))
