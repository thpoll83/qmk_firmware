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
# Latin Extended-B (0x0180-0x024F) — emoji-layer "Latin" tab (cat 11).
# Latin Extended-A (0x0100-0x017E) is already covered by _SupAndExtA_ above.
fontconvert "-ffonts/noto-sans/NotoSans-Regular.ttf" "-s${size_lat}" -r44 -v _LatinExtB_  0x180 0x24f > "base/fonts/generated/1NotoSans_Regular_LatinExtB_${size_lat}pt.h"
fontconvert "-ffonts/noto-sans/NotoSans-Regular.ttf" "-s${size_lat}" -v _LetterMod_ 0x2c6 0x2dd > "base/fonts/generated/NotoSans_Regular_LetterMod_${size_lat}pt.h"
fontconvert "-ffonts/noto-sans/NotoSans-Regular.ttf" "-s${size_lat}" -v _Greek_  0x384 0x385 0x391 0x3a1 0x3a3 0x3c9 > "base/fonts/generated/NotoSans_Regular_Greek_${size_lat}pt.h"
fontconvert "-ffonts/noto-sans/NotoSans-Regular.ttf" "-s${size_cyr}" -v _Cyrillic_  0x401 0x46b 0x490 0x4bb 0x4d8 0x4e9 > "base/fonts/generated/NotoSans_Regular_Cyrillic_${size_cyr}pt.h"

echo "Hebrew..."
fontconvert "-ffonts/Noto_Sans_Hebrew/static/NotoSansHebrew-Medium.ttf" "-s${size_hbr_dts}" -v _HebrewDots_  0x5b0 0x5c7 > "base/fonts/generated/NotoSans_Medium_HebrewDots_${size_hbr_dts}pt.h"
fontconvert "-ffonts/Noto_Sans_Hebrew/static/NotoSansHebrew-Medium.ttf" "-s${size_hbr}" -v _Hebrew_  0x5d0 0x5ea 0x5f0 0x5f4 > "base/fonts/generated/NotoSans_Medium_Hebrew_${size_hbr}pt.h"
fontconvert "-ffonts/noto-sans/NotoSans-Regular.ttf" "-s${size_lat}" -r44 -v _SZ_  0x1E9E 0x1E9E > "base/fonts/generated/NotoSans_Regular_SZ_${size_lat}pt.h"

echo "Japanese..."
fontconvert "-ffonts/noto-sans-jp/NotoSansJP-Regular.ttf" "-s${size_jp}" -w400 -v _KangXi_ 0x2f00 0x2f00 0x2f08 0x2f08 0x2f17 0x2f18 0x2f1d 0x2f25 0x2f2b 0x2f2d 0x2f38 0x2f3f 0x2f47 0x2f4a 0x2f54 0x2f55 0x2f65 0x2f65 0x2f75 0x2f75 0x2fa6 0x2fa6 > "base/fonts/generated/7NotoSansJP_Regular_KangXi_${size_jp}pt.h"
fontconvert "-ffonts/noto-sans-jp/NotoSansJP-Regular.ttf" "-s${size_jp}" -w400 -v _Cjk4e2d_ 0x4e2d 0x4e2d > "base/fonts/generated/7NotoSansJP_Regular_Cjk4e2d_${size_jp}pt.h"
fontconvert "-ffonts/noto-sans-jp/NotoSansJP-Regular.ttf" "-s${size_jp}" -w400 -v _Cjk5eff_ 0x5eff 0x5eff > "base/fonts/generated/7NotoSansJP_Regular_Cjk5eff_${size_jp}pt.h"
fontconvert "-ffonts/noto-sans-jp/NotoSansJP-Regular.ttf" "-s${size_jp}" -w400 -v _Cjk96e3_ 0x96e3 0x96e3 > "base/fonts/generated/7NotoSansJP_Regular_Cjk96e3_${size_jp}pt.h"
fontconvert "-ffonts/noto-sans-jp/NotoSansJP-Regular.ttf" "-s${size_jp}" -w400 -v _Hiragana_ 0x3041 0x3096 0x3099 0x309f > "base/fonts/generated/NotoSansJP_Regular_Hiragana_${size_jp}pt.h"
fontconvert "-ffonts/noto-sans-jp/NotoSansJP-Regular.ttf" "-s${size_jp}" -w400 -v _Punct_ 0x3001 0x3002 0x3008 0x3011 > "base/fonts/generated/NotoSansJP_Regular_Punct_${size_jp}pt.h"
fontconvert "-ffonts/noto-sans-jp/NotoSansJP-Regular.ttf" "-s${size_jp}" -w400 -v _Punct2_ 0x30fb 0x30fc > "base/fonts/generated/NotoSansJP_Regular_Punct2_${size_jp}pt.h"

echo "Korean..."
fontconvert "-ffonts/noto-serif-kr/NotoSerifKR-Medium.otf" "-s${size_kr}" -w400 -r51 -v _Vowels_ 0x1161 0x1169 0x116d 0x116e 0x1172 0x1175 > "base/fonts/generated/NotoSerifKR_Regular_Vowels_${size_kr}pt.h"
fontconvert "-ffonts/noto-serif-kr/NotoSerifKR-Medium.otf" "-s${size_kr}" -w400 -v _Consonants_ 0x1100 0x1112 > "base/fonts/generated/NotoSerifKR_Regular_Consonants_${size_kr}pt.h"

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

# Cat 0 extra: faces 0x1F910-0x1F917 (0x1F600-0x1F64F already in Emoji0, incl. 🙁🙂🙃🙄)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjSmileys2_ -n0x10000 0x1F910 0x1F919 > "base/fonts/generated/8NotoEmoji_Medium_EmjSmileys2_20pt.h"
# Cat 0 addition: Unicode 9 faces 🤠-🤯 (0x1F920-0x1F92F); fits between EmjSmileys2 and EmjPeople2
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjFaces_ -n0x10000 0x1F920 0x1F92F > "base/fonts/generated/8NotoEmoji_Medium_EmjFaces_20pt.h"
# Cat 0 addition: Unicode 11-13 faces 🥰-🥺 (0x1F970-0x1F97A); gap 0x1F977-0x1F979 harmless
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjFaces2_ -n0x10000 0x1F970 0x1F97A > "base/fonts/generated/8NotoEmoji_Medium_EmjFaces2_20pt.h"

echo "Emoji category fonts: gestures & body..."
# Cat 1: gestures & body
# Single range only — 0x1F64C-0x1F64F are already covered by _Emojis0_ (0x1F600-0x1F64F).
# A multi-range gestures font would cover 0xF440-0xF64F with skip entries for the gap,
# shadowing EmjLove (0xF48B-0xF4B1) and EmjObjects (0xF4BB-0xF52E) with empty glyphs.
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjGestures_ -n0x10000 0x1F440 0x1F450 > "base/fonts/generated/8NotoEmoji_Medium_EmjGestures_20pt.h"
# Cat 1 addition: new hand gestures 🤚-🤟 (0x1F91A-0x1F91F); fits between EmjSmileys2 (ends 0xF919) and EmjFaces (starts 0xF920)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjHands2_ -n0x10000 0x1F91A 0x1F91F > "base/fonts/generated/8NotoEmoji_Medium_EmjHands2_20pt.h"
# Cat 1 addition: 🖐 raised hand with fingers splayed (0x1F590); gap between EmjObjects end (0xF52E) and Emojis0 start (0xF600)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjRaised_ -n0x10000 0x1F590 0x1F590 > "base/fonts/generated/8NotoEmoji_Medium_EmjRaised_20pt.h"
# Cat 1: BMP hand gestures.  ☝☞ (0x261D-0x261E, Symbols2) are merged into _SymBmp1_ (see "Symbols").
# ✊✋✌✍ (0x270A-0x270D, NotoEmoji) stay separate; the Symbols2 ranges are split around this island.
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjHandsBMP2_ 0x270A 0x270D > "base/fonts/generated/1NotoSansSymbols2_EmjHandsBMP2_20pt.h"

# Cat 0 (fantasy/creature faces 0x1F47B-0x1F482) + Cat 2 (dancer 0x1F483, syringe/pill 0x1F489-0x1F48A) + Cat 3 (💄0x1F484, 💅0x1F485)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjFantasy_ -n0x10000 0x1F47B 0x1F48A > "base/fonts/generated/8NotoEmoji_Medium_EmjFantasy_20pt.h"

echo "Emoji category fonts: people, clothing, body parts, superheroes..."
# Cat 2: people & jobs — buildings, clothing, people, dancer, syringe/pill
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjBuildings_ -n0x10000 0x1F3E0 0x1F3F0 > "base/fonts/generated/8NotoEmoji_Medium_EmjBuildings_20pt.h"
# Single range covers clothing (0x1F451-0x1F465) and people/characters (0x1F466-0x1F47A)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjPeople_ -n0x10000 0x1F451 0x1F47A > "base/fonts/generated/8NotoEmoji_Medium_EmjPeople_20pt.h"
# Cat 2 addition: newer people 🤰🤱-🤷 (Unicode 9+); extended to 0x1F937 to add prince/tuxedo/Mrs Claus/shrug
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjPeople2_ -n0x10000 0x1F930 0x1F937 > "base/fonts/generated/8NotoEmoji_Medium_EmjPeople2_20pt.h"
# Cat 2 addition: body parts 🦴🦵🦶🦷 (Unicode 12+); gap between EmjAnimals5 end (0xF9AE) and EmjPeople3 start (0xF9D0)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjParts_ -n0x10000 0x1F9B4 0x1F9B7 > "base/fonts/generated/8NotoEmoji_Medium_EmjParts_20pt.h"
# 🧐-🧟 + 🧠 brain (Unicode 10/11/12+, U+1F9D0-1F9E0); gap above EmjParts end (0x1F9B7), below EmjClothing2 start (0x1F9E1)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjPeople3_ -n0x10000 0x1F9D0 0x1F9E0 > "base/fonts/generated/8NotoEmoji_Medium_EmjPeople3_20pt.h"
# 🧡🧢-🧦🧧🧨 orange-heart, clothing, red-envelope, firecracker (Unicode 11+, U+1F9E1-1F9E8); gap above EmjPeople3 end (0x1F9E0), below EmjSciTools start (0x1F9E9)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjClothing2_ -n0x10000 0x1F9E1 0x1F9E8 > "base/fonts/generated/8NotoEmoji_Medium_EmjClothing2_20pt.h"
# Cat 10 addition: science & tools 🧩-🧿 (Unicode 11-12+)
# Gap: above EmjClothing2 end (0xF9E8), below EmjInsects start (0xFAB0)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjSciTools_ -n0x10000 0x1F9E9 0x1F9FF > "base/fonts/generated/8NotoEmoji_Medium_EmjSciTools_20pt.h"
# Cat 2 addition: clothing & medical 🩰-🩺 (Unicode 12+); gap at 0x1FA75-0x1FA77 is harmless
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjClothing_ -n0x10000 0x1FA70 0x1FA7A > "base/fonts/generated/8NotoEmoji_Medium_EmjClothing_20pt.h"
# Cat 2 addition: anatomical body 🫀🫁🫂🫃🫄🫅 (Unicode 13-14+; extended to 0x1FAC5)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjBody2_ -n0x10000 0x1FAC0 0x1FAC5 > "base/fonts/generated/8NotoEmoji_Medium_EmjBody2_20pt.h"
# Cat 0 addition: Unicode 14-15 faces 🫠-🫨 (0x1FAE0-0x1FAE8); above EmjBody2 end (0xFAC5), below EmjHands3 start (0xFAF0)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjFaces3_ -n0x10000 0x1FAE0 0x1FAE8 > "base/fonts/generated/8NotoEmoji_Medium_EmjFaces3_20pt.h"
# Cat 1 addition: Unicode 14-15 hand gestures 🫰-🫸 (0x1FAF0-0x1FAF8); above EmjFaces3 end (0xFAE8)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjHands3_ -n0x10000 0x1FAF0 0x1FAF8 > "base/fonts/generated/8NotoEmoji_Medium_EmjHands3_20pt.h"
# Cat 2 addition: superheroes & accessories 🦸-🦿 (Unicode 11+); fits between EmjParts end (0xF9B7) and EmjFood4 start (0xF9C0)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjPeople4_ -n0x10000 0x1F9B8 0x1F9BF > "base/fonts/generated/8NotoEmoji_Medium_EmjPeople4_20pt.h"

echo "Emoji category fonts: love, hearts, weather, zodiac..."
# Cat 3: love & hearts — BMP ❤ 0x2764 now in merged _SymBmp4_; SMP 0x1F48B-0x1F49F (colored hearts)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjLove_ -n0x10000 0x1F48B 0x1F49F > "base/fonts/generated/8NotoEmoji_Medium_EmjLove_20pt.h"
# Cat 3 addition: white/brown hearts + pinched fingers + pinching hand 🤌🤍🤎🤏 (Unicode 12-13+)
# Indices 0xF90C-0xF90F: below EmjSmileys2 start (0xF910), clean gap
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjHearts_ -n0x10000 0x1F90C 0x1F90F > "base/fonts/generated/8NotoEmoji_Medium_EmjHearts_20pt.h"
# Cat 0/1/10: emotion/objects 💠-💺 (0x1F4A0-0x1F4BA); covers 💪 0x1F4AA (supersedes EmjMuscle)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjEffects_ -n0x10000 0x1F4A0 0x1F4BA > "base/fonts/generated/8NotoEmoji_Medium_EmjEffects_20pt.h"
rm -f "base/fonts/generated/8NotoEmoji_Medium_EmjMuscle_20pt.h"

echo "Emoji category fonts: animals, nature, weather, food..."
# Cat 4: animals
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjAnimals_ -n0x10000 0x1F400 0x1F43F > "base/fonts/generated/8NotoEmoji_Medium_EmjAnimals_20pt.h"
# Cat 4 addition: newer animals 🦁-🦏 (Unicode 9+)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjAnimals2_ -n0x10000 0x1F981 0x1F98F > "base/fonts/generated/8NotoEmoji_Medium_EmjAnimals2_20pt.h"
# Cat 4 addition: newer animals 🦐-🦗 (Unicode 10+)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjAnimals3_ -n0x10000 0x1F990 0x1F997 > "base/fonts/generated/8NotoEmoji_Medium_EmjAnimals3_20pt.h"
# Cat 4 addition: newer animals 🦘-🦠 (Unicode 11+)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjAnimals4_ -n0x10000 0x1F998 0x1F9A0 > "base/fonts/generated/8NotoEmoji_Medium_EmjAnimals4_20pt.h"
# Cat 4 addition: newer animals 🦡-🦭 (Unicode 12-13+)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjAnimals5_ -n0x10000 0x1F9A1 0x1F9AE > "base/fonts/generated/8NotoEmoji_Medium_EmjAnimals5_20pt.h"

# Cat 5: nature & plants
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjPlants_ -n0x10000 0x1F330 0x1F344 > "base/fonts/generated/8NotoEmoji_Medium_EmjPlants_20pt.h"
# Cat 5 addition: insects & nature objects 🪰-🪻 (Unicode 13-15+); extended to 0x1FABF for 🪼🪽🪿(cat4) + 🪻(cat5)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjInsects_ -n0x10000 0x1FAB0 0x1FABC > "base/fonts/generated/8NotoEmoji_Medium_EmjInsects_20pt.h"

echo "Emoji category fonts: weather, zodiac, clocks..."
# Cat 6: weather — BMP 0x2600-0x2603, 0x2614, 0x26A1 + SMP 0x1F300-0x1F32C
# 0x1F310 🌐 globe and 0x1F324-0x1F32C (☁ variants, 🌪🌫🌬) are in the extended SMP range.
# BMP weather glyphs (☀☁☂☃ 0x2600-0x2603, ☔ 0x2614, ⚡ 0x26A1, ⛄⛅⛈ 0x26C4/5/8) are now in
# the merged _SymBmp1_ / _SymBmp2_ / _SymBmp6_ fonts (see "Symbols").
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjWeatherSMP1_ -n0x10000 0x1F300 0x1F321 > "base/fonts/generated/8NotoEmoji_Medium_EmjWeatherSMP1_20pt.h"
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjWeatherSMP2_ -n0x10000 0x1F324 0x1F32C > "base/fonts/generated/8NotoEmoji_Medium_EmjWeatherSMP2_20pt.h"

# Cat 6 addition: zodiac signs ♈-♓ (BMP 0x2648-0x2653, NotoEmoji).
# Stays separate (NotoEmoji); the Symbols2 ranges _SymBmp1_/_SymBmp2_ are split around this island.
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _Zodiac_ 0x2648 0x2653 > "base/fonts/generated/1NotoSansSymbols2_Regular_Zodiac_20pt.h"
# ❄ snowflake (BMP 0x2744, Symbols2) is merged into _SymBmp4_ (see "Symbols").
# Cat 6 addition: clock faces 🕐-🕧 (SMP 0x1F550-0x1F567)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjClocks_ -n0x10000 0x1F550 0x1F567 > "base/fonts/generated/8NotoEmoji_Medium_EmjClocks_20pt.h"

echo "Emoji category fonts: food & drink..."
# Cat 7: food & drink
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjFood_ -n0x10000 0x1F347 0x1F37F > "base/fonts/generated/8NotoEmoji_Medium_EmjFood_20pt.h"
# Cat 7 addition: newer foods 🥐-🥞 (Unicode 9+)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjFood2_ -n0x10000 0x1F950 0x1F95E > "base/fonts/generated/8NotoEmoji_Medium_EmjFood2_20pt.h"
# Cat 7 addition: newer foods 🥟-🥯 (Unicode 10/11+, extended to include 🥬🥭🥮🥯)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjFood3_ -n0x10000 0x1F95F 0x1F96F > "base/fonts/generated/8NotoEmoji_Medium_EmjFood3_20pt.h"
# Cat 7 addition: newer foods 🧀-🧈 (Unicode 12+)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjFood4_ -n0x10000 0x1F9C0 0x1F9CB > "base/fonts/generated/8NotoEmoji_Medium_EmjFood4_20pt.h"
# Cat 7 addition: newer foods 🫐-🫛 (Unicode 13-15+); extended to 0x1FADB for 🫗🫘🫙🫚🫛
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjFood5_ -n0x10000 0x1FAD0 0x1FADB > "base/fonts/generated/8NotoEmoji_Medium_EmjFood5_20pt.h"

# Cat 8: travel — BMP 0x2708 + SMP 0x1F680-0x1F6C5 (extended to include shower/bath/transit)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjTravelBMP_ 0x2708 0x2708 > "base/fonts/generated/2NotoSansSymbols2_EmjTravelBMP_20pt.h"
# Cat 8 addition: BMP travel places/sports ⛰⛱⛲⛳⛴ (0x26F0-0x26F5 selective)
# Indices 9962-9981: clean gap between EmjWeatherBMP2 end (0x26C8=9928) and DiamondCut start (0x2702=9986)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjTravelBMP2_ 0x26F0 0x26F5 > "base/fonts/generated/8NotoSansSymbols2_EmjTravelBMP2_20pt.h"
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjTravel_ -n0x10000 0x1F680 0x1F6C5 > "base/fonts/generated/8NotoEmoji_Medium_EmjTravel_20pt.h"
# Cat 8 addition: 🛐🛑🛒 (0x1F6D0-0x1F6D2); gap between EmjTravel end (0xF6C5) and next fonts
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjTravelExt_ -n0x10000 0x1F6D0 0x1F6D2 > "base/fonts/generated/8NotoEmoji_Medium_EmjTravelExt_20pt.h"

echo "Emoji category fonts: sports, activities, objects..."
# Cat 9: sports & entertainment — BMP 0x26BD-0x26BE (⚽⚾) now in merged _SymBmp6_; SMP 0x1F3A0-0x1F3CE below
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjSports_ -n0x10000 0x1F3A0 0x1F3CE > "base/fonts/generated/8NotoEmoji_Medium_EmjSports_20pt.h"
# Cat 9 addition: newer activities 🤸🤹🤺🤼🤽🤾 (Unicode 9+)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjSports2_ -n0x10000 0x1F938 0x1F93A > "base/fonts/generated/8NotoEmoji_Medium_EmjSports2_20pt.h"
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjSports3_ -n0x10000 0x1F93C 0x1F93F > "base/fonts/generated/8NotoEmoji_Medium_EmjSports3_20pt.h"
# Cat 9 addition: toys & activities 🪀-🪆 (Unicode 12-13+)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjToys_ -n0x10000 0x1FA80 0x1FA86 > "base/fonts/generated/8NotoEmoji_Medium_EmjToys_20pt.h"

echo "Emoji category fonts: objects, celebrations..."
# Cat 10: tools & objects
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjObjects1_ -n0x10000 0x1F4BB 0x1F4FD > "base/fonts/generated/8NotoEmoji_Medium_EmjObjects1_20pt.h"
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjObjects2_ -n0x10000 0x1F4FF 0x1F52E > "base/fonts/generated/8NotoEmoji_Medium_EmjObjects2_20pt.h"
# Cat 10 addition: household & tools 🪐-🪟 (Unicode 12-13+)
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjHousehold_ -n0x10000 0x1FA90 0x1FA9F > "base/fonts/generated/8NotoEmoji_Medium_EmjHousehold_20pt.h"

# Cat 11: celebrations
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _EmjCelebrate_ -n0x10000 0x1F380 0x1F393 > "base/fonts/generated/8NotoEmoji_Medium_EmjCelebrate_20pt.h"

#fontconvert "-ffonts/Noto_CEmoji/NotoColorEmoji-Regular.ttf" "-s20" -r50 -g -v _Emojis2_ -n0x10000 0x1f600 0x1f64f > "base/fonts/generated/7test-NotoEmoji_Medium_Emoji2_20pt.h"
#fontconvert "-ffonts/Noto_CEmoji/NotoColorEmoji-Regular.ttf" "-s20" -r50 -g -v _Flags_ -n0x10100 0x1F1E8 0x1F1FC > "base/fonts/generated/8NotoColorEmoji_Regular_Flags_20pt.h"

echo "Symbols..."
fontconvert "-ffonts/Noto_Sans_Symbols/static/NotoSansSymbols-Regular.ttf" "-s20" -r50 -v _Technical_ 0x2387 0x2388 0x238b 0x238b 0x2399 0x2399 > "base/fonts/generated/3NotoSansSymbols_Regular_Technical_20pt.h"
fontconvert "-ffonts/Noto_Sans_Symbols_2/NotoSansSymbols2-Regular.ttf" "-s20" -r50 -v _Technical2_ 0x2318 0x2318 0x2325 0x2326 0x232B 0x232B > "base/fonts/generated/3NotoSansSymbols2_Regular_Technical_20pt.h"
# Symbols2 BMP fonts — full contiguous Misc-Symbols + Dingbats coverage (0x25AC-0x27BF),
# DISJOINT ranges split around the interleaved NotoEmoji islands (Zodiac 0x2648-53,
# ToolsBMP 0x2692-9C, TravelBMP2 0x26F0-F5, TravelBMP 0x2708, HandsBMP2 0x270A-0D). Overlap-free,
# no prefix trick. Replace SymbolsAndShapes / EmjWeatherBMP(2) / EmjHandsBMP / Chess / DiamondCut /
# EmjSnowflake / EmjSportsBMP / EmjHeart (all NotoSansSymbols2). Glyphs in the gaps (I-Ching,
# road signs, …) are rendered but simply not referenced by any emoji category.
#   _SymBmp1_ 0x25AC, 0x2600-0x2647   _SymBmp2_ 0x2654-0x2691   _SymBmp6_ 0x269D-0x26EF
#   _SymBmp3_ 0x26F6-0x2707           _SymBmp5_ 0x2709          _SymBmp4_ 0x270E-0x27BF
# Tools/alchemical 0x2692-0x269C (⚒⚓⚔⚕⚖⚗⚙⚛⚜) are absent from NotoSansSymbols2 — sourced from NotoEmoji.
fontconvert "-ffonts/Noto_Sans_Symbols_2/NotoSansSymbols2-Regular.ttf" "-s20" -r50 -v _SymBmp1_ 0x25AC 0x25AC 0x2600 0x2647 > "base/fonts/generated/1NotoSansSymbols2_Regular_SymBmp1_20pt.h"
fontconvert "-ffonts/Noto_Sans_Symbols_2/NotoSansSymbols2-Regular.ttf" "-s20" -r50 -v _SymBmp2_ 0x2654 0x2691 > "base/fonts/generated/1NotoSansSymbols2_Regular_SymBmp2_20pt.h"
fontconvert "-ffonts/Noto_Sans_Symbols_2/NotoSansSymbols2-Regular.ttf" "-s20" -r50 -v _SymBmp6_ 0x269D 0x26EF > "base/fonts/generated/1NotoSansSymbols2_Regular_SymBmp6_20pt.h"
fontconvert "-ffonts/Noto_Sans_Symbols_2/NotoSansSymbols2-Regular.ttf" "-s20" -r50 -v _SymBmp3_ 0x26F6 0x2707 > "base/fonts/generated/1NotoSansSymbols2_Regular_SymBmp3_20pt.h"
fontconvert "-ffonts/Noto_Sans_Symbols_2/NotoSansSymbols2-Regular.ttf" "-s20" -r50 -v _SymBmp5_ 0x2709 0x2709 > "base/fonts/generated/1NotoSansSymbols2_Regular_SymBmp5_20pt.h"
fontconvert "-ffonts/Noto_Sans_Symbols_2/NotoSansSymbols2-Regular.ttf" "-s20" -r50 -v _SymBmp4_ 0x270E 0x27BF > "base/fonts/generated/1NotoSansSymbols2_Regular_SymBmp4_20pt.h"
fontconvert "-f$NOTO_EMOJI" -s20 -r50 -v _ToolsBMP_ 0x2692 0x269C > "base/fonts/generated/1NotoEmoji_Medium_ToolsBMP_20pt.h"
fontconvert "-ffonts/Noto_Sans_Symbols_2/NotoSansSymbols2-Regular.ttf" "-s20" -r50 -v _Arrows_ 	0x2B6F 0x2B73 0x2B7E 0x2B7E 0x2B8C 0x2B8C 0x2B8E 0x2B8E 0x2BA0 0x2BA0 > "base/fonts/generated/3NotoSansSymbols2_Regular_Arrows_20pt.h"
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
