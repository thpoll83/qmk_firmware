#! /bin/bash

#echo "Enter Latin/Greek font size (eg. 14):"
#read size_lat
size_lat=14

#echo "Enter Cyrillic font size (eg. 16):"
#read size_cyr
size_cyr=16

#echo "Enter Hebrew font size (eg. 16):"
#read size_hbr
size_hbr=16

#echo "Enter Hebrew Punctuation font size (eg. 22):"
#read size_hbr_dts
size_hbr_dts=22

#echo "Enter Korean font size (eg. 21):"
#read size_kr
size_kr=21

#echo "Enter Japanese font size (eg. 15):"
#read size_jp
size_jp=15

#echo "Enter Arabic font size (eg. 16):"
#read size_ar
size_ar=16

echo "Noto Sans..."
fontconvert "-ffonts/noto-sans/NotoSans-Regular.ttf" "-s${size_lat}" -v _Base_ 0x20 0x7e > "base/fonts/generated/0NotoSans_Regular_Base_${size_lat}pt.h"
fontconvert "-ffonts/noto-sans/NotoSans-Regular.ttf" "-s${size_lat}" -r44 -v _SupAndExtA_  0xa1 0x17e > "base/fonts/generated/1NotoSans_Regular_SupAndExtA_${size_lat}pt.h"
fontconvert "-ffonts/noto-sans/NotoSans-Regular.ttf" "-s${size_lat}" -v _LetterMod_ 0x2c6 0x2dd > "base/fonts/generated/NotoSans_Regular_LetterMod_${size_lat}pt.h"
fontconvert "-ffonts/noto-sans/NotoSans-Regular.ttf" "-s${size_lat}" -v _Greek_  0x384 0x385 0x391 0x3a1 0x3a3 0x3c9 > "base/fonts/generated/NotoSans_Regular_Greek_${size_lat}pt.h"
fontconvert "-ffonts/noto-sans/NotoSans-Regular.ttf" "-s${size_cyr}" -v _Cyrillic_  0x401 0x46b 0x490 0x4bb 0x4d8 0x4e9 > "base/fonts/generated/NotoSans_Regular_Cyrillic_${size_cyr}pt.h"

echo "Hebrew..."
fontconvert "-ffonts/Noto_Sans_Hebrew/static/NotoSansHebrew-Medium.ttf" "-s${size_hbr_dts}" -v _HebrewDots_  0x5b0 0x5c7 > "base/fonts/generated/NotoSans_Medium_HebrewDots_${size_hbr_dts}pt.h"
fontconvert "-ffonts/Noto_Sans_Hebrew/static/NotoSansHebrew-Medium.ttf" "-s${size_hbr}" -v _Hebrew_  0x5d0 0x5ea 0x5f0 0x5f4 > "base/fonts/generated/NotoSans_Medium_Hebrew_${size_hbr}pt.h"
fontconvert "-ffonts/noto-sans/NotoSans-Regular.ttf" "-s${size_lat}" -r44 -v _SZ_  0x1E9E 0x1E9E > "base/fonts/generated/NotoSans_Regular_SZ_${size_lat}pt.h"

echo "Japanese..."
fontconvert "-ffonts/noto-sans-jp/NotoSansJP-Regular.ttf" "-s${size_jp}" -v _KangXi_ 0x2f00 0x2f00 0x2f08 0x2f08 0x2f17 0x2f18 0x2f1d 0x2f25 0x2f2b 0x2f2d 0x2f38 0x2f3f 0x2f47 0x2f4a 0x2f54 0x2f55 0x2f65 0x2f65 0x2f75 0x2f75 0x2fa6 0x2fa6 > "base/fonts/generated/7NotoSansJP_Regular_KangXi_${size_jp}pt.h"
fontconvert "-ffonts/noto-sans-jp/NotoSansJP-Regular.ttf" "-s${size_jp}" -v _Cjk4e2d_ 0x4e2d 0x4e2d > "base/fonts/generated/7NotoSansJP_Regular_Cjk4e2d_${size_jp}pt.h"
fontconvert "-ffonts/noto-sans-jp/NotoSansJP-Regular.ttf" "-s${size_jp}" -v _Cjk5eff_ 0x5eff 0x5eff > "base/fonts/generated/7NotoSansJP_Regular_Cjk5eff_${size_jp}pt.h"
fontconvert "-ffonts/noto-sans-jp/NotoSansJP-Regular.ttf" "-s${size_jp}" -v _Cjk96e3_ 0x96e3 0x96e3 > "base/fonts/generated/7NotoSansJP_Regular_Cjk96e3_${size_jp}pt.h"
fontconvert "-ffonts/noto-sans-jp/NotoSansJP-Regular.ttf" "-s${size_jp}" -v _Hiragana_ 0x3041 0x3096 0x3099 0x309f > "base/fonts/generated/NotoSansJP_Regular_Hiragana_${size_jp}pt.h"
fontconvert "-ffonts/noto-sans-jp/NotoSansJP-Regular.ttf" "-s${size_jp}" -v _Punct_ 0x3001 0x3002 0x3008 0x3011 > "base/fonts/generated/NotoSansJP_Regular_Punct_${size_jp}pt.h"
fontconvert "-ffonts/noto-sans-jp/NotoSansJP-Regular.ttf" "-s${size_jp}" -v _Punct2_ 0x30fb 0x30fc > "base/fonts/generated/NotoSansJP_Regular_Punct2_${size_jp}pt.h"

echo "Korean..."
fontconvert "-ffonts/noto-serif-kr/NotoSerifKR-Medium.otf" "-s${size_kr}" -r51 -v _Vowels_ 0x1161 0x1169 0x116d 0x116e 0x1172 0x1175 > "base/fonts/generated/NotoSerifKR_Regular_Vowels_${size_kr}pt.h"
fontconvert "-ffonts/noto-serif-kr/NotoSerifKR-Medium.otf" "-s${size_kr}" -v _Consonants_ 0x1100 0x1112 > "base/fonts/generated/NotoSerifKR_Regular_Consonants_${size_kr}pt.h"

echo "Arabic..."
fontconvert "-ffonts/noto-sans-arabic/static/NotoSansArabic/NotoSansArabic-Regular.ttf" "-s${size_ar}" -r40 -v _Isolated_ 0x60c 0x60c 0x61b 0x61b 0x61f 0x669 > "base/fonts/generated/NotoSansAR_Regular_Isolated_${size_ar}pt.h"
fontconvert "-ffonts/noto-sans-arabic/static/NotoSansArabic/NotoSansArabic-Regular.ttf" "-s${size_ar}" -r40 -v _FormsB_ 0xfef5 0xfef5 0xfefb 0xfefb > "base/fonts/generated/NotoSansAR_Regular_FormsB_${size_ar}pt.h"

echo "Emojis..."
fontconvert "-ffonts/noto-sans/NotoSans-Regular.ttf" "-s${size_lat}" -v _CurrencySigns_  0x20aa 0x20ac 0x20b4 0x20b4 0x2116 0x2116 > "base/fonts/generated/2NotoSans_Regular_CurrencySigns_${size_lat}pt.h"
fontconvert "-ffonts/Noto_Emoji/static/NotoEmoji-Medium.ttf" "-s20" -r50 -v _World_ -n0x11000 0x1F310 0x1F310 > "base/fonts/generated/3NotoEmoji_Medium_World_20pt.h"
fontconvert "-ffonts/Noto_Emoji/static/NotoEmoji-Medium.ttf" "-s20" -r50 -v _Meh_ -n0x11000 0x1F410 0x1F410 > "base/fonts/generated/3NotoEmoji_Medium_Meh_20pt.h"
fontconvert "-ffonts/Noto_Emoji/static/NotoEmoji-Medium.ttf" "-s20" -r50 -v _Settings_ -n0x11000 0x1F527 0x1F527 > "base/fonts/generated/3NotoEmoji_Medium_Settings_20pt.h"
fontconvert "-ffonts/Noto_Emoji/static/NotoEmoji-Medium.ttf" "-s18" -r48 -v _Hyper_ -n0x11000 0x1F4AB 0x1F4AB > "base/fonts/generated/3NotoEmoji_Medium_Hyper_18pt.h"
fontconvert "-ffonts/Noto_Emoji/static/NotoEmoji-Medium.ttf" "-s20" -r50 -v _Light_ -n0x11000 0x1F4A1 0x1F4A1 > "base/fonts/generated/6NotoEmoji_Medium_Light_20pt.h"
fontconvert "-ffonts/Noto_Emoji/static/NotoEmoji-Medium.ttf" "-s16" -r45 -v _Brightness_ -n0x11000 0x1F311 0x1F318 > "base/fonts/generated/5NotoEmoji_Medium_Brightness_16pt.h"
fontconvert "-ffonts/Noto_Emoji/static/NotoEmoji-Medium.ttf" "-s20" -r50 -v _AI_ -n0x11000 0x1F300 0x1F300 > "base/fonts/generated/6NotoEmoji_Medium_AI_20pt.h"
fontconvert "-ffonts/Noto_Emoji/static/NotoEmoji-Medium.ttf" "-s14" -r50 -v _FileOpen_ -n0x11000 0x1F4C2 0x1F4C2 > "base/fonts/generated/4NotoEmoji_Medium_FileOpen_14pt.h"
fontconvert "-ffonts/Noto_Emoji/static/NotoEmoji-Medium.ttf" "-s18" -r50 -v _LockFind_ -n0x11000 0x1F50E 0x1F50E 0x1F512 0x1F512 > "base/fonts/generated/5NotoEmoji_Medium_LockFind_18pt.h"
fontconvert "-ffonts/Noto_Emoji/static/NotoEmoji-Medium.ttf" "-s18" -r50 -v _ClipCpy_ -n0x11000 0x1F4CB 0x1F4CB > "base/fonts/generated/4NotoEmoji_Medium_Copy_18pt.h"
fontconvert "-ffonts/Noto_Emoji/static/NotoEmoji-Medium.ttf" "-s12" -r50 -v _Note_ -n0x11000 0x1f4dd 0x1f4dd > "base/fonts/generated/5NotoEmoji_Medium_Note_12pt.h"
fontconvert "-ffonts/Noto_Emoji/static/NotoEmoji-Medium.ttf" "-s20" -r50 -v _Emojis0_ -n0x10000 0x1f600 0x1f64f > "base/fonts/generated/6NotoEmoji_Medium_Emoji0_20pt.h"
fontconvert "-ffonts/Noto_Emoji/static/NotoEmoji-Medium.ttf" "-s20" -r50 -v _Emojis1_ -n0xfd00 0x1f440 0x1f453 0x1f47b 0x1f496 0x1f4a1 0x1f4b1 > "base/fonts/generated/7NotoEmoji_Medium_Emoji1 _20pt.h"
fontconvert "-ffonts/Noto_Emoji/static/NotoEmoji-Medium.ttf" "-s20" -r50 -v _Emojis2_ -n0x10100 0x1F912 0x1F919 > "base/fonts/generated/7NotoEmoji_Medium_Emoji2_20pt.h"

# ── Emoji category fonts — all SMP with -n0x10000, BMP without -n ────────────
# Used by emoji/emoji_layer.c; cp_to_font_idx() assumes offset 0x10000 for SMP.
echo "Emoji category fonts..."
NOTO_EMOJI="fonts/Noto_Emoji/static/NotoEmoji-Medium.ttf"
NOTO_SYM2="fonts/Noto_Sans_Symbols_2/NotoSansSymbols2-Regular.ttf"

# Cat 0 extra: faces 0x1F910-0x1F917 (0x1F600-0x1F64F already in Emoji0)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjSmileys2_ -n0x10000 0x1F910 0x1F917 > "base/fonts/generated/8NotoEmoji_Medium_EmjSmileys2_20pt.h"

# Cat 1: gestures & body
# Single range only — 0x1F64C-0x1F64F are already covered by _Emojis0_ (0x1F600-0x1F64F).
# A multi-range gestures font would cover 0xF440-0xF64F with skip entries for the gap,
# shadowing EmjLove (0xF48B-0xF4B1) and EmjObjects (0xF4BB-0xF52E) with empty glyphs.
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjGestures_ -n0x10000 0x1F440 0x1F450 > "base/fonts/generated/8NotoEmoji_Medium_EmjGestures_20pt.h"

# Cat 0 (fantasy/creature faces 0x1F47B-0x1F482) + Cat 2 (dancer 0x1F483, syringe/pill 0x1F489-0x1F48A) + Cat 3 (💄0x1F484, 💅0x1F485)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjFantasy_ -n0x10000 0x1F47B 0x1F48A > "base/fonts/generated/8NotoEmoji_Medium_EmjFantasy_20pt.h"

# Cat 2: people & jobs — buildings, clothing, people, dancer, syringe/pill
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjBuildings_ -n0x10000 0x1F3E0 0x1F3F0 > "base/fonts/generated/8NotoEmoji_Medium_EmjBuildings_20pt.h"
# Single range covers clothing (0x1F451-0x1F465) and people/characters (0x1F466-0x1F47A)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjPeople_ -n0x10000 0x1F451 0x1F47A > "base/fonts/generated/8NotoEmoji_Medium_EmjPeople_20pt.h"

# Cat 3: love & hearts — BMP 0x2764 + SMP 0x1F48B-0x1F49F (single range includes colored hearts)
fontconvert "-f$NOTO_SYM2" -s20 -r50 -v _EmjHeart_ 0x2764 0x2764 > "base/fonts/generated/8NotoSansSymbols2_EmjHeart_20pt.h"
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjLove_ -n0x10000 0x1F48B 0x1F49F > "base/fonts/generated/8NotoEmoji_Medium_EmjLove_20pt.h"
# 💪 0x1F4AA (flexed biceps) — cat 1 (Gestures & Body); single glyph between EmjLove and EmjObjects
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjMuscle_ -n0x10000 0x1F4AA 0x1F4AA > "base/fonts/generated/8NotoEmoji_Medium_EmjMuscle_20pt.h"

# Cat 4: animals
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjAnimals_ -n0x10000 0x1F400 0x1F43E > "base/fonts/generated/8NotoEmoji_Medium_EmjAnimals_20pt.h"

# Cat 5: nature & plants
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjPlants_ -n0x10000 0x1F330 0x1F344 > "base/fonts/generated/8NotoEmoji_Medium_EmjPlants_20pt.h"

# Cat 6: weather — BMP 0x2600-0x2603, 0x2614, 0x26A1 + SMP 0x1F300-0x1F321
fontconvert "-f$NOTO_SYM2" -s20 -r50 -v _EmjWeatherBMP_ 0x2600 0x2603 0x2614 0x2614 0x26A1 0x26A1 > "base/fonts/generated/2NotoSansSymbols2_EmjWeatherBMP_20pt.h"
# ⛄⛅⛈ (0x26C4/5/8) — separate file so the gap 0x26A1-0x26C3 doesn't shadow ⚽⚾ (0x26BD-0x26BE)
fontconvert "-f$NOTO_SYM2" -s20 -r50 -v _EmjWeatherBMP2_ 0x26C4 0x26C8 > "base/fonts/generated/2NotoSansSymbols2_EmjWeatherBMP2_20pt.h"
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjWeatherSMP_ -n0x10000 0x1F300 0x1F321 > "base/fonts/generated/8NotoEmoji_Medium_EmjWeatherSMP_20pt.h"
# Cat 6 addition: zodiac signs ♈-♓ (BMP 0x2648-0x2653)
# PREFIX 1 required: 0x2648-0x2653 fall inside EmjWeatherBMP gap range (first=0x2600, last=0x26A1)
# prefix 1 sorts before prefix 2, so zodiac font wins the lookup
fontconvert "-f$NOTO_SYM2" -s20 -r50 -v _Zodiac_ 0x2648 0x2653 > "base/fonts/generated/1NotoSansSymbols2_Regular_Zodiac_20pt.h"
# Cat 6 addition: clock faces 🕐-🕧 (SMP 0x1F550-0x1F567)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjClocks_ -n0x10000 0x1F550 0x1F567 > "base/fonts/generated/8NotoEmoji_Medium_EmjClocks_20pt.h"

# Cat 7: food & drink
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjFood_ -n0x10000 0x1F347 0x1F37F > "base/fonts/generated/8NotoEmoji_Medium_EmjFood_20pt.h"

# Cat 8: travel — BMP 0x2708 + SMP 0x1F680-0x1F6BE
fontconvert "-f$NOTO_SYM2" -s20 -r50 -v _EmjTravelBMP_ 0x2708 0x2708 > "base/fonts/generated/2NotoSansSymbols2_EmjTravelBMP_20pt.h"
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjTravel_ -n0x10000 0x1F680 0x1F6BE > "base/fonts/generated/8NotoEmoji_Medium_EmjTravel_20pt.h"

# Cat 9: sports & entertainment — BMP 0x26BD-0x26BE + SMP 0x1F3A0-0x1F3CA
fontconvert "-f$NOTO_SYM2" -s20 -r50 -v _EmjSportsBMP_ 0x26BD 0x26BE > "base/fonts/generated/8NotoSansSymbols2_EmjSportsBMP_20pt.h"
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjSports_ -n0x10000 0x1F3A0 0x1F3CA > "base/fonts/generated/8NotoEmoji_Medium_EmjSports_20pt.h"

# Cat 10: tools & objects
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjObjects_ -n0x10000 0x1F4BB 0x1F52E > "base/fonts/generated/8NotoEmoji_Medium_EmjObjects_20pt.h"

# Cat 11: celebrations
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjCelebrate_ -n0x10000 0x1F380 0x1F393 > "base/fonts/generated/8NotoEmoji_Medium_EmjCelebrate_20pt.h"

#fontconvert "-ffonts/Noto_CEmoji/NotoColorEmoji-Regular.ttf" "-s20" -r50 -g -v _Emojis2_ -n0x10000 0x1f600 0x1f64f > "base/fonts/generated/7test-NotoEmoji_Medium_Emoji2_20pt.h"
#fontconvert "-ffonts/Noto_CEmoji/NotoColorEmoji-Regular.ttf" "-s20" -r50 -g -v _Flags_ -n0x10100 0x1F1E8 0x1F1FC > "base/fonts/generated/8NotoColorEmoji_Regular_Flags_20pt.h"

echo "Symbols..."
fontconvert "-ffonts/Noto_Sans_Symbols/static/NotoSansSymbols-Regular.ttf" "-s20" -r50 -v _Technical_ 0x2387 0x2388 0x238b 0x238b 0x2399 0x2399 > "base/fonts/generated/3NotoSansSymbols_Regular_Technical_20pt.h"
fontconvert "-ffonts/Noto_Sans_Symbols_2/NotoSansSymbols2-Regular.ttf" "-s20" -r50 -v _Technical2_ 0x2318 0x2318 0x2325 0x2326 0x232B 0x232B > "base/fonts/generated/3NotoSansSymbols2_Regular_Technical_20pt.h"
fontconvert "-ffonts/Noto_Sans_Symbols_2/NotoSansSymbols2-Regular.ttf" "-s20" -r50 -v _SymbolsAndShapes_ 0x25AC 0x25AC 0x2611 0x2611 > "base/fonts/generated/3NotoSansSymbols2_Regular_SymbolsAndShapes_20pt.h"
fontconvert "-ffonts/Noto_Sans_Symbols_2/NotoSansSymbols2-Regular.ttf" "-s20" -r50 -v _Chess_ 0x2654 0x265F > "base/fonts/generated/7NotoSansSymbols2_Regular_Chess_20pt.h"
fontconvert "-ffonts/Noto_Sans_Symbols_2/NotoSansSymbols2-Regular.ttf" "-s20" -r50 -v _Arrows_ 	0x2B6F 0x2B73 0x2B7E 0x2B7E 0x2B8C 0x2B8C 0x2B8E 0x2B8E 0x2BA0 0x2BA0 > "base/fonts/generated/3NotoSansSymbols2_Regular_Arrows_20pt.h"
fontconvert "-ffonts/Noto_Sans_Symbols_2/NotoSansSymbols2-Regular.ttf" "-s20" -r50 -v _DiamondCut_ 0x2702 0x2702 0x2756 0x2756 > "base/fonts/generated/3NotoSansSymbols2_Regular_DiamondCut_20pt.h"
fontconvert "-ffonts/Noto_Sans_Symbols_2/NotoSansSymbols2-Regular.ttf" "-s20" -r50 -v _Util_ -n0x11000 0x1F568 0x1F56A 0x1F5A9 0x1F5AB 0x1F5B3 0x1F5BC 0x1F5CA 0x1F5CA > "base/fonts/generated/4NotoSansSymbols2_Regular_Util_20pt.h"
fontconvert "-ffonts/Noto_Sans_Symbols_2/NotoSansSymbols2-Regular.ttf" "-s18" -r50 -v _Window_ -n0x11000 0x1F5D5 0x1F5DB > "base/fonts/generated/5NotoSansSymbols2_Regular_Window_18pt.h"


cd base/fonts
echo -e "#pragma once\n" > gfx_used_fonts.h
ls -1 generated/*pt.h | while read line; do echo '#include "'${line}'"'; done >> gfx_used_fonts.h
echo -e '#include "gfx_icons.h"\n' >> gfx_used_fonts.h
echo -e '#include "FreeSansBold24pt7b.h"\n' >> gfx_used_fonts.h
echo -e 'const GFXfont* const ALL_FONTS [] = {' >> gfx_used_fonts.h
echo -e '  &IconsFont,' >> gfx_used_fonts.h
cat  generated/*pt.h | grep -oP 'const GFXfont \K\w+' | while read line; do echo "  &"${line}","; done >> gfx_used_fonts.h
echo -e '};\n' >> gfx_used_fonts.h
echo -e '#define ALL_FONT_SIZE (sizeof(ALL_FONTS) / sizeof(GFXfont*))' >> gfx_used_fonts.h
