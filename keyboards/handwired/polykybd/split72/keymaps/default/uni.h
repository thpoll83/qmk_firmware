#pragma once

#include "lang/named_glyphs.h"

const uint32_t unicode_map[] PROGMEM = {
    /*[[[cog
    import cog
    import os
    import string
    from openpyxl import load_workbook
    wb = load_workbook(filename = os.path.join(os.path.abspath(os.path.dirname(cog.inFile)), "lang/lang_lut.xlsx"), data_only=True)
    sheet = wb['named_glyphs']

    idx = 0
    glyph_index = 1
    glyph_key = sheet[f"A{glyph_index}"].value
    glyph_code = sheet[f"B{glyph_index}"].value
    rev_lookup = {}

    while glyph_key and not glyph_key.startswith("LATIN_00C0"):
        glyph_key = sheet[f"A{glyph_index}"].value
        glyph_code = sheet[f"B{glyph_index}"].value
        glyph_index = glyph_index + 1
    while glyph_key and glyph_key.startswith("LATIN_0"):
        cog.outl(f'[{idx}] = 0x{glyph_code},')
        rev_lookup[glyph_code] = idx
        glyph_key = sheet[f"A{glyph_index}"].value
        glyph_code = sheet[f"B{glyph_index}"].value
        glyph_index = glyph_index + 1
        idx = idx + 1
    #capital sz
    cog.outl(f'[{idx}] = 0x1E9E,')
    rev_lookup["1E9E"] = idx
    idx = idx + 1
    first_emoji = idx

    while glyph_key and not glyph_key.startswith("PRIVATE_EMOJI"):
        glyph_key = sheet[f"A{glyph_index}"].value
        glyph_code = sheet[f"B{glyph_index}"].value
        glyph_index = glyph_index + 1

    while glyph_key and glyph_key.startswith("PRIVATE_EMOJI"):
        cog.outl(f'[{idx}] = 0x{glyph_code},')
        glyph_key = sheet[f"A{glyph_index}"].value
        glyph_code = sheet[f"B{glyph_index}"].value
        glyph_index = glyph_index + 1
        idx = idx + 1

    ]]]*/
    [0] = 0xC0,
    [1] = 0xC1,
    [2] = 0xC2,
    [3] = 0xC3,
    [4] = 0xC4,
    [5] = 0xC5,
    [6] = 0xC6,
    [7] = 0xC7,
    [8] = 0xC8,
    [9] = 0xC9,
    [10] = 0xCA,
    [11] = 0xCB,
    [12] = 0xCC,
    [13] = 0xCD,
    [14] = 0xCE,
    [15] = 0xCF,
    [16] = 0xD0,
    [17] = 0xD1,
    [18] = 0xD2,
    [19] = 0xD3,
    [20] = 0xD4,
    [21] = 0xD5,
    [22] = 0xD6,
    [23] = 0xD7,
    [24] = 0xD8,
    [25] = 0xD9,
    [26] = 0xDA,
    [27] = 0xDB,
    [28] = 0xDC,
    [29] = 0xDD,
    [30] = 0xDE,
    [31] = 0xDF,
    [32] = 0xE0,
    [33] = 0xE1,
    [34] = 0xE2,
    [35] = 0xE3,
    [36] = 0xE4,
    [37] = 0xE5,
    [38] = 0xE6,
    [39] = 0xE7,
    [40] = 0xE8,
    [41] = 0xE9,
    [42] = 0xEA,
    [43] = 0xEB,
    [44] = 0xEC,
    [45] = 0xED,
    [46] = 0xEE,
    [47] = 0xEF,
    [48] = 0xF0,
    [49] = 0xF1,
    [50] = 0xF2,
    [51] = 0xF3,
    [52] = 0xF4,
    [53] = 0xF5,
    [54] = 0xF6,
    [55] = 0xF7,
    [56] = 0xF8,
    [57] = 0xF9,
    [58] = 0xFA,
    [59] = 0xFB,
    [60] = 0xFC,
    [61] = 0xFD,
    [62] = 0xFE,
    [63] = 0xFF,
    [64] = 0x100,
    [65] = 0x101,
    [66] = 0x102,
    [67] = 0x103,
    [68] = 0x104,
    [69] = 0x105,
    [70] = 0x106,
    [71] = 0x107,
    [72] = 0x108,
    [73] = 0x109,
    [74] = 0x10A,
    [75] = 0x10B,
    [76] = 0x10C,
    [77] = 0x10D,
    [78] = 0x10E,
    [79] = 0x10F,
    [80] = 0x110,
    [81] = 0x111,
    [82] = 0x112,
    [83] = 0x113,
    [84] = 0x114,
    [85] = 0x115,
    [86] = 0x116,
    [87] = 0x117,
    [88] = 0x118,
    [89] = 0x119,
    [90] = 0x11A,
    [91] = 0x11B,
    [92] = 0x11C,
    [93] = 0x11D,
    [94] = 0x11E,
    [95] = 0x11F,
    [96] = 0x120,
    [97] = 0x121,
    [98] = 0x122,
    [99] = 0x123,
    [100] = 0x124,
    [101] = 0x125,
    [102] = 0x126,
    [103] = 0x127,
    [104] = 0x128,
    [105] = 0x129,
    [106] = 0x12A,
    [107] = 0x12B,
    [108] = 0x12C,
    [109] = 0x12D,
    [110] = 0x12E,
    [111] = 0x12F,
    [112] = 0x130,
    [113] = 0x131,
    [114] = 0x132,
    [115] = 0x133,
    [116] = 0x134,
    [117] = 0x135,
    [118] = 0x136,
    [119] = 0x137,
    [120] = 0x138,
    [121] = 0x139,
    [122] = 0x13A,
    [123] = 0x13B,
    [124] = 0x13C,
    [125] = 0x13D,
    [126] = 0x13E,
    [127] = 0x13F,
    [128] = 0x140,
    [129] = 0x141,
    [130] = 0x142,
    [131] = 0x143,
    [132] = 0x144,
    [133] = 0x145,
    [134] = 0x146,
    [135] = 0x147,
    [136] = 0x148,
    [137] = 0x149,
    [138] = 0x14A,
    [139] = 0x14B,
    [140] = 0x14C,
    [141] = 0x14D,
    [142] = 0x14E,
    [143] = 0x14F,
    [144] = 0x150,
    [145] = 0x151,
    [146] = 0x152,
    [147] = 0x153,
    [148] = 0x154,
    [149] = 0x155,
    [150] = 0x156,
    [151] = 0x157,
    [152] = 0x158,
    [153] = 0x159,
    [154] = 0x15A,
    [155] = 0x15B,
    [156] = 0x15C,
    [157] = 0x15D,
    [158] = 0x15E,
    [159] = 0x15F,
    [160] = 0x160,
    [161] = 0x161,
    [162] = 0x162,
    [163] = 0x163,
    [164] = 0x164,
    [165] = 0x165,
    [166] = 0x166,
    [167] = 0x167,
    [168] = 0x168,
    [169] = 0x169,
    [170] = 0x16A,
    [171] = 0x16B,
    [172] = 0x16C,
    [173] = 0x16D,
    [174] = 0x16E,
    [175] = 0x16F,
    [176] = 0x170,
    [177] = 0x171,
    [178] = 0x172,
    [179] = 0x173,
    [180] = 0x174,
    [181] = 0x175,
    [182] = 0x176,
    [183] = 0x177,
    [184] = 0x178,
    [185] = 0x179,
    [186] = 0x17A,
    [187] = 0x17B,
    [188] = 0x17C,
    [189] = 0x17D,
    [190] = 0x17E,
    [191] = 0x1E9E,
    [192] = 0x1F600,
    [193] = 0x1F601,
    [194] = 0x1F602,
    [195] = 0x1F603,
    [196] = 0x1F604,
    [197] = 0x1F605,
    [198] = 0x1F606,
    [199] = 0x1F607,
    [200] = 0x1F608,
    [201] = 0x1F609,
    [202] = 0x1F60A,
    [203] = 0x1F60B,
    [204] = 0x1F60C,
    [205] = 0x1F60D,
    [206] = 0x1F60E,
    [207] = 0x1F60F,
    [208] = 0x1F610,
    [209] = 0x1F611,
    [210] = 0x1F612,
    [211] = 0x1F613,
    [212] = 0x1F614,
    [213] = 0x1F615,
    [214] = 0x1F616,
    [215] = 0x1F617,
    [216] = 0x1F618,
    [217] = 0x1F619,
    [218] = 0x1F61A,
    [219] = 0x1F61B,
    [220] = 0x1F61C,
    [221] = 0x1F61D,
    [222] = 0x1F61E,
    [223] = 0x1F61F,
    [224] = 0x1F620,
    [225] = 0x1F621,
    [226] = 0x1F622,
    [227] = 0x1F623,
    [228] = 0x1F624,
    [229] = 0x1F625,
    [230] = 0x1F626,
    [231] = 0x1F627,
    [232] = 0x1F628,
    [233] = 0x1F629,
    [234] = 0x1F62A,
    [235] = 0x1F62B,
    [236] = 0x1F62C,
    [237] = 0x1F62D,
    [238] = 0x1F62E,
    [239] = 0x1F62F,
    [240] = 0x1F630,
    [241] = 0x1F631,
    [242] = 0x1F632,
    [243] = 0x1F633,
    [244] = 0x1F634,
    [245] = 0x1F635,
    [246] = 0x1F636,
    [247] = 0x1F637,
    [248] = 0x1F638,
    [249] = 0x1F639,
    [250] = 0x1F644,
    [251] = 0x1F645,
    [252] = 0x1F646,
    [253] = 0x1F647,
    [254] = 0x1F648,
    [255] = 0x1F649,
    [256] = 0x1F64A,
    [257] = 0x1F64B,
    [258] = 0x1F64C,
    [259] = 0x1F64D,
    [260] = 0x1F64E,
    [261] = 0x1F64F,
    [262] = 0x1F440,
    [263] = 0x1F441,
    [264] = 0x1F442,
    [265] = 0x1F443,
    [266] = 0x1F444,
    [267] = 0x1F445,
    [268] = 0x1F446,
    [269] = 0x1F447,
    [270] = 0x1F448,
    [271] = 0x1F449,
    [272] = 0x1F44A,
    [273] = 0x1F44B,
    [274] = 0x1F44C,
    [275] = 0x1F44D,
    [276] = 0x1F44E,
    [277] = 0x1F44F,
    [278] = 0x1F450,
    [279] = 0x1F451,
    [280] = 0x1F452,
    [281] = 0x1F453,
    [282] = 0x1F47B,
    [283] = 0x1F47C,
    [284] = 0x1F47D,
    [285] = 0x1F47E,
    [286] = 0x1F47F,
    [287] = 0x1F480,
    [288] = 0x1F481,
    [289] = 0x1F482,
    [290] = 0x1F483,
    [291] = 0x1F484,
    [292] = 0x1F485,
    [293] = 0x1F489,
    [294] = 0x1F48A,
    [295] = 0x1F48B,
    [296] = 0x1F48C,
    [297] = 0x1F48D,
    [298] = 0x1F48E,
    [299] = 0x1F48F,
    [300] = 0x1F490,
    [301] = 0x1F491,
    [302] = 0x1F492,
    [303] = 0x1F493,
    [304] = 0x1F494,
    [305] = 0x1F495,
    [306] = 0x1F496,
    [307] = 0x1F4A1,
    [308] = 0x1F4A2,
    [309] = 0x1F4A3,
    [310] = 0x1F4A4,
    [311] = 0x1F4A5,
    [312] = 0x1F4A6,
    [313] = 0x1F4A7,
    [314] = 0x1F4A8,
    [315] = 0x1F4A9,
    [316] = 0x1F4AA,
    [317] = 0x1F4AB,
    [318] = 0x1F4AC,
    [319] = 0x1F4AD,
    [320] = 0x1F4AE,
    [321] = 0x1F4AF,
    [322] = 0x1F4B0,
    [323] = 0x1F4B1,
    [324] = 0x1F912,
    [325] = 0x1F913,
    [326] = 0x1F914,
    [327] = 0x1F915,
    [328] = 0x1F916,
    [329] = 0x1F917,
    [330] = 0x1F918,
    [331] = 0x1F919,
    //[[[end]]]
};

/*[[[cog
cog.out(f"#define EMJ(e) UM({first_emoji}+e)")
]]]*/
#define EMJ(e) UM(192+e)
//[[[end]]]


const uint16_t* keycode_to_emoji(uint16_t keycode) {
    switch (keycode) {
    /*[[[cog
    wb = load_workbook(filename = os.path.join(os.path.abspath(os.path.dirname(cog.inFile)), "lang/lang_lut.xlsx"), data_only=True)
    sheet = wb['named_glyphs']

    idx = 0
    glyph_index = 1
    glyph_key = sheet[f"A{glyph_index}"].value
    while glyph_key and not glyph_key.startswith("PRIVATE_EMOJI"):
        glyph_key = sheet[f"A{glyph_index}"].value
        glyph_index = glyph_index + 1
    while glyph_key and glyph_key.startswith("PRIVATE_EMOJI"):
        cog.outl(f'case EMJ({idx}): return u" " {glyph_key};')
        glyph_key = sheet[f"A{glyph_index}"].value
        idx = idx + 1
        glyph_index = glyph_index + 1

    ]]]*/
    case EMJ(0): return u" " PRIVATE_EMOJI_1F600;
    case EMJ(1): return u" " PRIVATE_EMOJI_1F601;
    case EMJ(2): return u" " PRIVATE_EMOJI_1F602;
    case EMJ(3): return u" " PRIVATE_EMOJI_1F603;
    case EMJ(4): return u" " PRIVATE_EMOJI_1F604;
    case EMJ(5): return u" " PRIVATE_EMOJI_1F605;
    case EMJ(6): return u" " PRIVATE_EMOJI_1F606;
    case EMJ(7): return u" " PRIVATE_EMOJI_1F607;
    case EMJ(8): return u" " PRIVATE_EMOJI_1F608;
    case EMJ(9): return u" " PRIVATE_EMOJI_1F609;
    case EMJ(10): return u" " PRIVATE_EMOJI_1F60A;
    case EMJ(11): return u" " PRIVATE_EMOJI_1F60B;
    case EMJ(12): return u" " PRIVATE_EMOJI_1F60C;
    case EMJ(13): return u" " PRIVATE_EMOJI_1F60D;
    case EMJ(14): return u" " PRIVATE_EMOJI_1F60E;
    case EMJ(15): return u" " PRIVATE_EMOJI_1F60F;
    case EMJ(16): return u" " PRIVATE_EMOJI_1F610;
    case EMJ(17): return u" " PRIVATE_EMOJI_1F611;
    case EMJ(18): return u" " PRIVATE_EMOJI_1F612;
    case EMJ(19): return u" " PRIVATE_EMOJI_1F613;
    case EMJ(20): return u" " PRIVATE_EMOJI_1F614;
    case EMJ(21): return u" " PRIVATE_EMOJI_1F615;
    case EMJ(22): return u" " PRIVATE_EMOJI_1F616;
    case EMJ(23): return u" " PRIVATE_EMOJI_1F617;
    case EMJ(24): return u" " PRIVATE_EMOJI_1F618;
    case EMJ(25): return u" " PRIVATE_EMOJI_1F619;
    case EMJ(26): return u" " PRIVATE_EMOJI_1F61A;
    case EMJ(27): return u" " PRIVATE_EMOJI_1F61B;
    case EMJ(28): return u" " PRIVATE_EMOJI_1F61C;
    case EMJ(29): return u" " PRIVATE_EMOJI_1F61D;
    case EMJ(30): return u" " PRIVATE_EMOJI_1F61E;
    case EMJ(31): return u" " PRIVATE_EMOJI_1F61F;
    case EMJ(32): return u" " PRIVATE_EMOJI_1F620;
    case EMJ(33): return u" " PRIVATE_EMOJI_1F621;
    case EMJ(34): return u" " PRIVATE_EMOJI_1F622;
    case EMJ(35): return u" " PRIVATE_EMOJI_1F623;
    case EMJ(36): return u" " PRIVATE_EMOJI_1F624;
    case EMJ(37): return u" " PRIVATE_EMOJI_1F625;
    case EMJ(38): return u" " PRIVATE_EMOJI_1F626;
    case EMJ(39): return u" " PRIVATE_EMOJI_1F627;
    case EMJ(40): return u" " PRIVATE_EMOJI_1F628;
    case EMJ(41): return u" " PRIVATE_EMOJI_1F629;
    case EMJ(42): return u" " PRIVATE_EMOJI_1F62A;
    case EMJ(43): return u" " PRIVATE_EMOJI_1F62B;
    case EMJ(44): return u" " PRIVATE_EMOJI_1F62C;
    case EMJ(45): return u" " PRIVATE_EMOJI_1F62D;
    case EMJ(46): return u" " PRIVATE_EMOJI_1F62E;
    case EMJ(47): return u" " PRIVATE_EMOJI_1F62F;
    case EMJ(48): return u" " PRIVATE_EMOJI_1F630;
    case EMJ(49): return u" " PRIVATE_EMOJI_1F631;
    case EMJ(50): return u" " PRIVATE_EMOJI_1F632;
    case EMJ(51): return u" " PRIVATE_EMOJI_1F633;
    case EMJ(52): return u" " PRIVATE_EMOJI_1F634;
    case EMJ(53): return u" " PRIVATE_EMOJI_1F635;
    case EMJ(54): return u" " PRIVATE_EMOJI_1F636;
    case EMJ(55): return u" " PRIVATE_EMOJI_1F637;
    case EMJ(56): return u" " PRIVATE_EMOJI_1F638;
    case EMJ(57): return u" " PRIVATE_EMOJI_1F639;
    case EMJ(58): return u" " PRIVATE_EMOJI_1F644;
    case EMJ(59): return u" " PRIVATE_EMOJI_1F645;
    case EMJ(60): return u" " PRIVATE_EMOJI_1F646;
    case EMJ(61): return u" " PRIVATE_EMOJI_1F647;
    case EMJ(62): return u" " PRIVATE_EMOJI_1F648;
    case EMJ(63): return u" " PRIVATE_EMOJI_1F649;
    case EMJ(64): return u" " PRIVATE_EMOJI_1F64A;
    case EMJ(65): return u" " PRIVATE_EMOJI_1F64B;
    case EMJ(66): return u" " PRIVATE_EMOJI_1F64C;
    case EMJ(67): return u" " PRIVATE_EMOJI_1F64D;
    case EMJ(68): return u" " PRIVATE_EMOJI_1F64E;
    case EMJ(69): return u" " PRIVATE_EMOJI_1F64F;
    case EMJ(70): return u" " PRIVATE_EMOJI_1F440;
    case EMJ(71): return u" " PRIVATE_EMOJI_1F441;
    case EMJ(72): return u" " PRIVATE_EMOJI_1F442;
    case EMJ(73): return u" " PRIVATE_EMOJI_1F443;
    case EMJ(74): return u" " PRIVATE_EMOJI_1F444;
    case EMJ(75): return u" " PRIVATE_EMOJI_1F445;
    case EMJ(76): return u" " PRIVATE_EMOJI_1F446;
    case EMJ(77): return u" " PRIVATE_EMOJI_1F447;
    case EMJ(78): return u" " PRIVATE_EMOJI_1F448;
    case EMJ(79): return u" " PRIVATE_EMOJI_1F449;
    case EMJ(80): return u" " PRIVATE_EMOJI_1F44A;
    case EMJ(81): return u" " PRIVATE_EMOJI_1F44B;
    case EMJ(82): return u" " PRIVATE_EMOJI_1F44C;
    case EMJ(83): return u" " PRIVATE_EMOJI_1F44D;
    case EMJ(84): return u" " PRIVATE_EMOJI_1F44E;
    case EMJ(85): return u" " PRIVATE_EMOJI_1F44F;
    case EMJ(86): return u" " PRIVATE_EMOJI_1F450;
    case EMJ(87): return u" " PRIVATE_EMOJI_1F451;
    case EMJ(88): return u" " PRIVATE_EMOJI_1F452;
    case EMJ(89): return u" " PRIVATE_EMOJI_1F453;
    case EMJ(90): return u" " PRIVATE_EMOJI_1F47B;
    case EMJ(91): return u" " PRIVATE_EMOJI_1F47C;
    case EMJ(92): return u" " PRIVATE_EMOJI_1F47D;
    case EMJ(93): return u" " PRIVATE_EMOJI_1F47E;
    case EMJ(94): return u" " PRIVATE_EMOJI_1F47F;
    case EMJ(95): return u" " PRIVATE_EMOJI_1F480;
    case EMJ(96): return u" " PRIVATE_EMOJI_1F481;
    case EMJ(97): return u" " PRIVATE_EMOJI_1F482;
    case EMJ(98): return u" " PRIVATE_EMOJI_1F483;
    case EMJ(99): return u" " PRIVATE_EMOJI_1F484;
    case EMJ(100): return u" " PRIVATE_EMOJI_1F485;
    case EMJ(101): return u" " PRIVATE_EMOJI_1F489;
    case EMJ(102): return u" " PRIVATE_EMOJI_1F48A;
    case EMJ(103): return u" " PRIVATE_EMOJI_1F48B;
    case EMJ(104): return u" " PRIVATE_EMOJI_1F48C;
    case EMJ(105): return u" " PRIVATE_EMOJI_1F48D;
    case EMJ(106): return u" " PRIVATE_EMOJI_1F48E;
    case EMJ(107): return u" " PRIVATE_EMOJI_1F48F;
    case EMJ(108): return u" " PRIVATE_EMOJI_1F490;
    case EMJ(109): return u" " PRIVATE_EMOJI_1F491;
    case EMJ(110): return u" " PRIVATE_EMOJI_1F492;
    case EMJ(111): return u" " PRIVATE_EMOJI_1F493;
    case EMJ(112): return u" " PRIVATE_EMOJI_1F494;
    case EMJ(113): return u" " PRIVATE_EMOJI_1F495;
    case EMJ(114): return u" " PRIVATE_EMOJI_1F496;
    case EMJ(115): return u" " PRIVATE_EMOJI_1F4A1;
    case EMJ(116): return u" " PRIVATE_EMOJI_1F4A2;
    case EMJ(117): return u" " PRIVATE_EMOJI_1F4A3;
    case EMJ(118): return u" " PRIVATE_EMOJI_1F4A4;
    case EMJ(119): return u" " PRIVATE_EMOJI_1F4A5;
    case EMJ(120): return u" " PRIVATE_EMOJI_1F4A6;
    case EMJ(121): return u" " PRIVATE_EMOJI_1F4A7;
    case EMJ(122): return u" " PRIVATE_EMOJI_1F4A8;
    case EMJ(123): return u" " PRIVATE_EMOJI_1F4A9;
    case EMJ(124): return u" " PRIVATE_EMOJI_1F4AA;
    case EMJ(125): return u" " PRIVATE_EMOJI_1F4AB;
    case EMJ(126): return u" " PRIVATE_EMOJI_1F4AC;
    case EMJ(127): return u" " PRIVATE_EMOJI_1F4AD;
    case EMJ(128): return u" " PRIVATE_EMOJI_1F4AE;
    case EMJ(129): return u" " PRIVATE_EMOJI_1F4AF;
    case EMJ(130): return u" " PRIVATE_EMOJI_1F4B0;
    case EMJ(131): return u" " PRIVATE_EMOJI_1F4B1;
    case EMJ(132): return u" " PRIVATE_EMOJI_1F912;
    case EMJ(133): return u" " PRIVATE_EMOJI_1F913;
    case EMJ(134): return u" " PRIVATE_EMOJI_1F914;
    case EMJ(135): return u" " PRIVATE_EMOJI_1F915;
    case EMJ(136): return u" " PRIVATE_EMOJI_1F916;
    case EMJ(137): return u" " PRIVATE_EMOJI_1F917;
    case EMJ(138): return u" " PRIVATE_EMOJI_1F918;
    case EMJ(139): return u" " PRIVATE_EMOJI_1F919;
    //[[[end]]]
    default: return NULL;
    }
}

uint16_t unicodemap_index_poly(uint16_t keycode) {
    if (keycode >= QK_UNICODEMAP_PAIR) {
        // Keycode is a pair: extract index based on Shift / Caps Lock state
        uint16_t index;

        uint8_t mods = get_mods() | get_weak_mods();
#ifndef NO_ACTION_ONESHOT
        mods |= get_oneshot_mods();
#endif

        bool shift = mods & MOD_MASK_SHIFT;
        bool caps  = host_keyboard_led_state().caps_lock;
        if (shift ^ caps) {
            index = QK_UNICODEMAP_PAIR_GET_SHIFTED_INDEX(keycode);
        } else {
            index = QK_UNICODEMAP_PAIR_GET_UNSHIFTED_INDEX(keycode);
        }

        return index;
    } else {
        // Keycode is a regular index
        return QK_UNICODEMAP_GET_INDEX(keycode);
    }
}

uint32_t unicodemap_get_code_point_poly(uint16_t index) {
    return pgm_read_dword(unicode_map + index);
}


void register_unicodemap_poly(uint16_t index) {
    register_unicode(unicodemap_get_code_point_poly(index));
}

bool process_unicodemap_poly(uint16_t keycode, keyrecord_t *record) {
    if (keycode >= QK_UNICODEMAP && keycode <= QK_UNICODEMAP_PAIR_MAX && record->event.pressed) {
        register_unicodemap_poly(unicodemap_index_poly(keycode));
        return true;
    }
    return false;
}
