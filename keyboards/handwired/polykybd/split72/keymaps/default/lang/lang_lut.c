//File content partially generated!
//Execute the following command to do so (via cogapp):
//cog -r lang_lut.c
#include "lang_lut.h"
#include "polykybd.h"

static const uint16_t* lang_plane [ALPHA + NUM + ADDITIONAL][NUM_LANG * 4] = {
    /*[[[cog
    import cog
    import os
    import string
    from openpyxl import load_workbook
    wb = load_workbook(filename = os.path.join(os.path.abspath(os.path.dirname(cog.inFile)), "lang_lut.xlsx"), data_only=True)
    sheet = wb['key_lut']

    #build a list of known glyphs
    named_glyphs = []
    sheet_named_glyphs = wb['named_glyphs']
    for r in range(2,sheet_named_glyphs.max_row+1):
        named_glyphs.append(sheet_named_glyphs.cell(row = r, column = 1).value)

    #build a dict with all languages
    lang_dict = {}
    lang_index = 0
    lang_key = sheet["B1"].value
    while lang_key:
        lang_dict[lang_index] = lang_key
        lang_index = lang_index + 1
        lang_key = sheet.cell(row = 1, column = 2 + lang_index*4).value

    #helper to make the string for a key
    def make_key(text):
        if not text:
            return "NULL"
        else:
            str_text = str(text).strip()
            if str_text.startswith('u"') or str_text in named_glyphs or len(str_text.split())>1:
                return str_text
            return f'u"{str_text}"'


    #helper to make all 3 strings (lower, upper, caps) needed for a key and language
    def make_lang_key(idx, row, prepend_comment, append_comma):
        lang_row = ""
        if prepend_comment:
            lang_row = '/' + f'*  {lang_dict[idx] : <9}*' + '/  '
        for case_idx in range(0, 4):
            text = sheet.cell(row = row, column = 2 + idx*4+case_idx).value

            composed = make_key(text)
            if case_idx != 3 or append_comma:
                composed = composed + ","
                lang_row = f"{lang_row}{composed:<30}"
            else:
                lang_row = f"{lang_row}{composed}"

        return lang_row

    #create actual language lookup table
    key_index = 2
    key_name = sheet["A2"].value
    glyph_code = sheet["D2"].value
    while not key_name.startswith("{"):
        cog.out('/' + f'*{key_name : <11}*' + '/ {')

        for idx in lang_dict:
            cog.outl(f'{make_lang_key(idx, key_index, idx>0, idx < lang_index-1)}')
        cog.outl("},")

        key_index = key_index + 1
        key_name = sheet.cell(row = key_index, column = 1).value

    ]]]*/
    /*KC_A       */ {u"a",                         u"A",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  u"q",                         u"Q",                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         LETTER_AE_SMALL,              
    /*  LANG_KO  */  HANGEUL_MIEUM,                NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_TI,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_SHEEN,                 BACKSLASH,                    ARABIC_SHEEN,                 NULL,                         
    /*  LANG_GR  */  GREEK_SM_ALPHA,               GREEK_ALPHA,                  NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_EF,               CYRILLIC_EF,                  NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_EF,               CYRILLIC_EF,                  NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_EF,               CYRILLIC_EF,                  NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_EF,               CYRILLIC_EF,                  NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_SOFT_SIGN,        CYRILLIC_SOFT_SIGN,           NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_B       */ {u"b",                         u"B",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_YU,                   NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_KO,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_LAM_ALEF,              ARABIC_LAM_ALEF_MADDA,        NULL,                         NULL,                         
    /*  LANG_GR  */  GREEK_SM_BETA,                GREEK_BETA,                   NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_I,                CYRILLIC_I,                   NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_I,                CYRILLIC_I,                   NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_BY_YA_I,          CYRILLIC_BY_YA_I,             NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_BY_YA_I,          CYRILLIC_BY_YA_I,             NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_EF,               CYRILLIC_EF,                  NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         u"{",                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_C       */ {u"c",                         u"C",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_CHIEUCH,              NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_SO,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_WAW_HAMZA_A,           ARABIC_KASRA,                 ARABIC_WAW_HAMZA_A,           NULL,                         
    /*  LANG_GR  */  GREEK_SM_PSI,                 GREEK_PSI,                    NULL,                         COPYRIGHT_SIGN,               
    /*  LANG_UA  */  CYRILLIC_SM_ES,               CYRILLIC_ES,                  NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_ES,               CYRILLIC_ES,                  NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_ES,               CYRILLIC_ES,                  NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_ES,               CYRILLIC_ES,                  NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_HARD_SIGN,        CYRILLIC_HARD_SIGN,           NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         REGISTERED_SIGN,              
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         CENT_SIGN,                    NULL
    },
    /*KC_D       */ {u"d",                         u"D",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_IEUNG,                NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_SI,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_YEH,                   u"]",                         ARABIC_YEH,                   NULL,                         
    /*  LANG_GR  */  GREEK_SM_DELTA,               GREEK_DELTA,                  NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_VE,               CYRILLIC_VE,                  NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_VE,               CYRILLIC_VE,                  NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_VE,               CYRILLIC_VE,                  NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_VE,               CYRILLIC_VE,                  NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_A,                CYRILLIC_A,                   NULL,                         CYRILLIC_SM_YAT,              
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         LATIN_0110,                   
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         LATIN_0110,                   
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_E       */ {u"e",                         u"E",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         EURO_SIGN,                    
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         EURO_SIGN,                    
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         EURO_SIGN,                    
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         EURO_SIGN,                    
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         EURO_SIGN,                    
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         EURO_SIGN,                    
    /*  LANG_KO  */  HANGEUL_TIKEUT,               HANGEUL_SSANGTIKEUT,          NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_I,                   HIRAGANA_SMALL_I,             NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_THEH,                  u"\v" ARABIC_DAMMA,           ARABIC_THEH,                  NULL,                         
    /*  LANG_GR  */  GREEK_SM_EPSILON,             GREEK_EPSILON,                NULL,                         EURO_SIGN,                    
    /*  LANG_UA  */  CYRILLIC_SM_U,                CYRILLIC_U,                   NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_U,                CYRILLIC_U,                   NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_U,                CYRILLIC_U,                   NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_U,                CYRILLIC_U,                   NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_IE,               CYRILLIC_IE,                  NULL,                         CYRILLIC_SM_BIG_YUS,          
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         EURO_SIGN,                    
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         EURO_SIGN,                    NULL
    },
    /*KC_F       */ {u"f",                         u"F",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_RIEUL,                NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_HA,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_BEH,                   u"[",                         ARABIC_BEH,                   NULL,                         
    /*  LANG_GR  */  GREEK_SM_PHI,                 GREEK_PHI,                    NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_A,                CYRILLIC_A,                   NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_A,                CYRILLIC_A,                   NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_A,                CYRILLIC_A,                   NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_A,                CYRILLIC_A,                   NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_O,                CYRILLIC_O,                   NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_G       */ {u"g",                         u"G",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_HIEUH,                NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_KI,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_LAM,                   NULL,                         NULL,                         NULL,                         
    /*  LANG_GR  */  GREEK_SM_GAMMA,               GREEK_GAMMA,                  NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_PE,               CYRILLIC_PE,                  NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_PE,               CYRILLIC_PE,                  NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_PE,               CYRILLIC_PE,                  NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_PE,               CYRILLIC_PE,                  NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_ZHE,              CYRILLIC_ZHE,                 NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_H       */ {u"h",                         u"H",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_O,                    NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_KU,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_ALEF,                  ARABIC_ALEF_HAMZA_A,          ARABIC_ALEF,                  NULL,                         
    /*  LANG_GR  */  GREEK_SM_ETA,                 GREEK_ETA,                    NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_ER,               CYRILLIC_ER,                  NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_ER,               CYRILLIC_ER,                  NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_ER,               CYRILLIC_ER,                  NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_ER,               CYRILLIC_ER,                  NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_GHE,              CYRILLIC_GHE,                 NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_I       */ {u"i",                         u"I",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  I_DOTLESS_SMALL,              u"I",                         NULL,                         u"i",                         
    /*  LANG_KO  */  HANGEUL_YA,                   NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_RI,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_HEH,                   DEVISION_SIGN,                ARABIC_HEH,                   NULL,                         
    /*  LANG_GR  */  GREEK_SM_IOTA,                GREEK_IOTA,                   NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_SHA,              CYRILLIC_SHA,                 NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_SHA,              CYRILLIC_SHA,                 NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_SHA,              CYRILLIC_SHA,                 NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_SHA,              CYRILLIC_SHA,                 NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_ES,               CYRILLIC_ES,                  NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_J       */ {u"j",                         u"J",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_EO,                   NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_MA,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_TEH,                   ARABIC_TATWEEL,               ARABIC_TEH,                   NULL,                         
    /*  LANG_GR  */  GREEK_SM_XI,                  GREEK_XI,                     NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_O,                CYRILLIC_O,                   NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_O,                CYRILLIC_O,                   NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_O,                CYRILLIC_O,                   NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_O,                CYRILLIC_O,                   NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_TE,               CYRILLIC_TE,                  NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_K       */ {u"k",                         u"K",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_A,                    NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_NO,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_NOON,                  ARABIC_COMMA,                 ARABIC_NOON,                  NULL,                         
    /*  LANG_GR  */  GREEK_SM_KAPPA,               GREEK_KAPPA,                  NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_EL,               CYRILLIC_EL,                  NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_EL,               CYRILLIC_EL,                  NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_EL,               CYRILLIC_EL,                  NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_EL,               CYRILLIC_EL,                  NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_EN,               CYRILLIC_EN,                  NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_L       */ {u"l",                         u"L",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_I,                    NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_RI,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_MEEM,                  u"/",                         ARABIC_MEEM,                  NULL,                         
    /*  LANG_GR  */  GREEK_SM_LAMDA,               GREEK_LAMDA,                  NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_DE,               CYRILLIC_DE,                  NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_DE,               CYRILLIC_DE,                  NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_DE,               CYRILLIC_DE,                  NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_DE,               CYRILLIC_DE,                  NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_VE,               CYRILLIC_VE,                  NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         LATIN_0141,                   
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_M       */ {u"m",                         u"M",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         u"\f\f" MICRO_SIGN,           
    /*  LANG_FR  */  u",",                         u"?",                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_EU,                   NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_MO,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_WAW,                   GRAVE_ACCENT,                 ARABIC_WAW,                   NULL,                         
    /*  LANG_GR  */  GREEK_SM_MU,                  GREEK_MU,                     NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_SOFT_SIGN,        CYRILLIC_SOFT_SIGN,           NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_SOFT_SIGN,        CYRILLIC_SOFT_SIGN,           NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_SOFT_SIGN,        CYRILLIC_SOFT_SIGN,           NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_SOFT_SIGN,        CYRILLIC_SOFT_SIGN,           NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_PE,               CYRILLIC_PE,                  NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         SECTION,                      
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         MICRO_SIGN
    },
    /*KC_N       */ {u"n",                         u"N",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_U,                    NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_MI,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_ALEF_MAKSURA,          ARABIC_ALEF_MADDA_A,          ARABIC_ALEF_MAKSURA,          NULL,                         
    /*  LANG_GR  */  GREEK_SM_NU,                  GREEK_NU,                     NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_TE,               CYRILLIC_TE,                  NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_TE,               CYRILLIC_TE,                  NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_TE,               CYRILLIC_TE,                  NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_TE,               CYRILLIC_TE,                  NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_HA,               CYRILLIC_HA,                  NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         u"}",                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_O       */ {u"o",                         u"O",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_AE,                   HANGEUL_YAE,                  NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_RA,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_TEH_MARBUTA,           MUL_SIGN,                     ARABIC_TEH_MARBUTA,           NULL,                         
    /*  LANG_GR  */  GREEK_SM_OMICRON,             GREEK_OMICRON,                NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_SHCHA,            CYRILLIC_SHCHA,               NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_SHCHA,            CYRILLIC_SHCHA,               NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_SHORT_U,          CYRILLIC_SHORT_U,             NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_SHORT_U,          CYRILLIC_SHORT_U,             NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_DE,               CYRILLIC_DE,                  NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_P       */ {u"p",                         u"P",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_E,                    HANGEUL_YE,                   NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_SE,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_HAH,                   ARABIC_SEMICOLON,             ARABIC_HAH,                   NULL,                         
    /*  LANG_GR  */  GREEK_SM_PI,                  GREEK_PI,                     NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_ZE,               CYRILLIC_ZE,                  NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_ZE,               CYRILLIC_ZE,                  NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_ZE,               CYRILLIC_ZE,                  NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_ZE,               CYRILLIC_ZE,                  NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_ZE,               CYRILLIC_ZE,                  NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         SECTION,                      
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_Q       */ {u"q",                         u"Q",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         u"@",                         
    /*  LANG_FR  */  u"a",                         u"A",                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         u"@",                         
    /*  LANG_KO  */  HANGEUL_PIEUP,                HANGEUL_SSANGPIEUP,           NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_TA,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_DAD,                   u"\v" ARABIC_FATHA,           ARABIC_DAD,                   NULL,                         
    /*  LANG_GR  */  u";",                         u":",                         u";",                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_SHORT_I,          CYRILLIC_SHORT_I,             NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_SHORT_I,          CYRILLIC_SHORT_I,             NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_SHORT_I,          CYRILLIC_SHORT_I,             NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_SHORT_I,          CYRILLIC_SHORT_I,             NULL,                         NULL,                         
    /*  LANG_BG  */  u",",                         CYRILLIC_SM_YERU,             NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         BACKSLASH,                    
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_R       */ {u"r",                         u"R",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_KIYEOK,               HANGEUL_SSANGKIYEOK,          NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_SU,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_QAF,                   u"\v" ARABIC_DAMMATAN,        ARABIC_QAF,                   CURRENCY_SIGN,                
    /*  LANG_GR  */  GREEK_SM_RHO,                 GREEK_RHO,                    NULL,                         REGISTERED_SIGN,              
    /*  LANG_UA  */  CYRILLIC_SM_KA,               CYRILLIC_KA,                  NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_KA,               CYRILLIC_KA,                  NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_KA,               CYRILLIC_KA,                  NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_KA,               CYRILLIC_KA,                  NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_I,                CYRILLIC_I,                   NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         PILCROW
    },
    /*KC_S       */ {u"s",                         u"S",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         ESZETT,                       
    /*  LANG_KO  */  HANGEUL_NIEUN,                NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_TO,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_SEEN,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_GR  */  GREEK_SM_SIGMA,               GREEK_SIGMA,                  NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_BY_YA_I,          CYRILLIC_BY_YA_I,             NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_YERU,             CYRILLIC_YERU,                NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_YERU,             CYRILLIC_YERU,                NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_YERU,             CYRILLIC_YERU,                NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_YA,               CYRILLIC_YA,                  NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         LATIN_0111,                   
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         ESZETT,                       
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         ESZETT
    },
    /*KC_T       */ {u"t",                         u"T",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_SIOS,                 HANGEUL_SSANGSIOS,            NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_KA,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_FEH,                   NULL,                         NULL,                         NULL,                         
    /*  LANG_GR  */  GREEK_SM_TAU,                 GREEK_TAU,                    NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_IE,               CYRILLIC_IE,                  NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_IE,               CYRILLIC_IE,                  NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_IE,               CYRILLIC_IE,                  NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_IE,               CYRILLIC_IE,                  NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_SHA,              CYRILLIC_SHA,                 NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_U       */ {u"u",                         u"U",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_YEO,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_NA,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_AIN,                   ACUTE_ACCENT,                 ARABIC_AIN,                   NULL,                         
    /*  LANG_GR  */  GREEK_SM_THETA,               GREEK_THETA,                  NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_GHE,              CYRILLIC_GHE,                 NULL,                         CYRILLIC_GHE_W_UPTURN,        
    /*  LANG_RU  */  CYRILLIC_SM_GHE,              CYRILLIC_GHE,                 NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_GHE,              CYRILLIC_GHE,                 NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_GHE,              CYRILLIC_GHE,                 NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_KA,               CYRILLIC_KA,                  NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         EURO_SIGN,                    
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_V       */ {u"v",                         u"V",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_PHIEUPH,              NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_HI,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_REH,                   ARABIC_KASRATAN,              ARABIC_REH,                   NULL,                         
    /*  LANG_GR  */  GREEK_SM_OMEGA,               GREEK_OMEGA,                  NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_EM,               CYRILLIC_EM,                  NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_EM,               CYRILLIC_EM,                  NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_EM,               CYRILLIC_EM,                  NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_EM,               CYRILLIC_EM,                  NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_E,                CYRILLIC_E,                   NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         u"@",                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_W       */ {u"w",                         u"W",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  u"z",                         u"Z",                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_CIEUC,                HANGEUL_SSANGCIEUC,           NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_TE,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_SAD,                   u"\v" ARABIC_FATHATAN,        ARABIC_SAD,                   NULL,                         
    /*  LANG_GR  */  GREEK_SM_FINAL_SIGMA,         GREEK_DIALYTIKA_TONOS,        GREEK_SM_FINAL_SIGMA,         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_TSE,              CYRILLIC_TSE,                 NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_TSE,              CYRILLIC_TSE,                 NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_TSE,              CYRILLIC_TSE,                 NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_TSE,              CYRILLIC_TSE,                 NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_U,                CYRILLIC_U,                   NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         BROKEN_BAR,                   
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_X       */ {u"x",                         u"X",                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_THIEUTH,              NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_SA,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_HAMZA,                 u"\v" ARABIC_SUKUN,           ARABIC_HAMZA,                 NULL,                         
    /*  LANG_GR  */  GREEK_SM_CHI,                 GREEK_CHI,                    NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_CHE,              CYRILLIC_CHE,                 NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_CHE,              CYRILLIC_CHE,                 NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_CHE,              CYRILLIC_CHE,                 NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_CHE,              CYRILLIC_CHE,                 NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_SHORT_I,          CYRILLIC_SHORT_I,             NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         DBL_ANGLE_QMARK_R
    },
    /*KC_Y       */ {u"y",                         u"Y",                         NULL,                         NULL,                         
    /*  LANG_DE  */  u"z",                         u"Z",                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_YO,                   NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_N,                   NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_GHAIN,                 ARABIC_ALEF_HAMZA_B,          ARABIC_GHAIN,                 NULL,                         
    /*  LANG_GR  */  GREEK_SM_UPSILON,             GREEK_UPSILON,                NULL,                         YEN_SIGN,                     
    /*  LANG_UA  */  CYRILLIC_SM_EN,               CYRILLIC_EN,                  NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_EN,               CYRILLIC_EN,                  NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_EN,               CYRILLIC_EN,                  NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_EN,               CYRILLIC_EN,                  NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_SHCHA,            CYRILLIC_SHCHA,               NULL,                         NULL,                         
    /*  LANG_PL  */  u"z",                         u"Z",                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_Z       */ {u"z",                         u"Z",                         NULL,                         NULL,                         
    /*  LANG_DE  */  u"y",                         u"Y",                         NULL,                         NULL,                         
    /*  LANG_FR  */  u"w",                         u"W",                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  HANGEUL_KHIEUKH,              NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_TU,                  HIRAGANA_SMALL_TU,            NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_YEH_HAMZA_A,           u"~",                         ARABIC_YEH_HAMZA_A,           NULL,                         
    /*  LANG_GR  */  GREEK_SM_ZETA,                NULL,                         NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_YA,               CYRILLIC_YA,                  NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_YA,               CYRILLIC_YA,                  NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_YA,               CYRILLIC_YA,                  NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_YA,               CYRILLIC_YA,                  NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_YU,               CYRILLIC_YU,                  NULL,                         NULL,                         
    /*  LANG_PL  */  u"y",                         u"Y",                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         DBL_ANGLE_QMARK_L
    },
    /*KC_1       */ {u"1",                         u"!",                         u"1",                         NULL,                         
    /*  LANG_DE  */  u"1",                         u"!",                         NULL,                         NULL,                         
    /*  LANG_FR  */  u"&",                         u"1",                         NULL,                         NULL,                         
    /*  LANG_ES  */  u"1",                         u"!",                         u"1",                         u"|",                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  u"1",                         u"!",                         u"1",                         u">",                         
    /*  LANG_KO  */  u"1",                         u"!",                         u"1",                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_NU,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  u"1",                         u"!",                         u"1",                         ARABIC_INDIC_1,               
    /*  LANG_GR  */  u"1",                         u"!",                         u"1",                         NULL,                         
    /*  LANG_UA  */  u"1",                         u"!",                         u"1",                         NULL,                         
    /*  LANG_RU  */  u"1",                         u"!",                         u"1",                         NULL,                         
    /*  LANG_BY  */  u"1",                         u"!",                         u"1",                         NULL,                         
    /*  LANG_KZ  */  QUOTE,                        u"!",                         QUOTE,                        NULL,                         
    /*  LANG_BG  */  u"1",                         u"!",                         u"1",                         NULL,                         
    /*  LANG_PL  */  u"1",                         u"!",                         NULL,                         MOD_TILDE,                    
    /*  LANG_RO  */  u"1",                         u"!",                         NULL,                         MOD_TILDE,                    
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u"1",                         u"!",                         u"1",                         SUPER_SCRIPT_1
    },
    /*KC_2       */ {u"2",                         u"@",                         u"2",                         NULL,                         
    /*  LANG_DE  */  u"2",                         QUOTE,                        NULL,                         SUPER_SCRIPT_2,               
    /*  LANG_FR  */  u"\f\f" E_WITH_ACUTE_SMALL,   u"2",                         NULL,                         u"~",                         
    /*  LANG_ES  */  u"2",                         QUOTE,                        u"2",                         u"@",                         
    /*  LANG_PT  */  u"2",                         QUOTE,                        u"2",                         u"@",                         
    /*  LANG_IT  */  u"2",                         QUOTE,                        u"2",                         NULL,                         
    /*  LANG_TR  */  u"2",                         u"'",                         u"2",                         POUND_SIGN,                   
    /*  LANG_KO  */  u"2",                         u"@",                         u"2",                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_HU,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  u"2",                         u"@",                         u"2",                         ARABIC_INDIC_2,               
    /*  LANG_GR  */  u"2",                         u"@",                         u"2",                         u" " SUPER_SCRIPT_2,          
    /*  LANG_UA  */  u"2",                         QUOTE,                        u"2",                         DBL_ANGLE_QMARK_L,            
    /*  LANG_RU  */  u"2",                         QUOTE,                        u"2",                         NULL,                         
    /*  LANG_BY  */  u"2",                         QUOTE,                        u"2",                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_SCHWA,            CYRILLIC_SCHWA,               NULL,                         NULL,                         
    /*  LANG_BG  */  u"2",                         u"?",                         u"2",                         NULL,                         
    /*  LANG_PL  */  u"2",                         QUOTE,                        NULL,                         MOD_CARON,                    
    /*  LANG_RO  */  u"2",                         u"@",                         NULL,                         u" " MOD_CARON,               
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u"2",                         QUOTE,                        u"2",                         SUPER_SCRIPT_2
    },
    /*KC_3       */ {u"3",                         u"#",                         u"3",                         NULL,                         
    /*  LANG_DE  */  u"3",                         SECTION,                      NULL,                         SUPER_SCRIPT_3,               
    /*  LANG_FR  */  QUOTE,                        u"3",                         NULL,                         u"#",                         
    /*  LANG_ES  */  u"3",                         MIDDLE_DOT,                   u"3",                         u"#",                         
    /*  LANG_PT  */  u"3",                         u"#",                         u"3",                         u"\f\f\xa3",                  
    /*  LANG_IT  */  u"3",                         POUND_SIGN,                   u"3",                         NULL,                         
    /*  LANG_TR  */  u"3",                         u"^",                         u"3",                         u"#",                         
    /*  LANG_KO  */  u"3",                         u"#",                         u"3",                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_A,                   HIRAGANA_SMALL_A,             NULL,                         NULL,                         
    /*  LANG_AR  */  u"3",                         u"#",                         u"3",                         ARABIC_INDIC_3,               
    /*  LANG_GR  */  u"3",                         u"#",                         u"3",                         u" " SUPER_SCRIPT_3,          
    /*  LANG_UA  */  u"3",                         NUMERO_SIGN,                  u"3",                         NULL,                         
    /*  LANG_RU  */  u"3",                         NUMERO_SIGN,                  u"3",                         NULL,                         
    /*  LANG_BY  */  u"3",                         NUMERO_SIGN,                  u"3",                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_BY_YA_I,          CYRILLIC_BY_YA_I,             NULL,                         NULL,                         
    /*  LANG_BG  */  u"3",                         u"+",                         u"3",                         NULL,                         
    /*  LANG_PL  */  u"3",                         u"#",                         NULL,                         u"^",                         
    /*  LANG_RO  */  u"3",                         u"#",                         NULL,                         u"^",                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u"3",                         u"#",                         u"3",                         SUPER_SCRIPT_3
    },
    /*KC_4       */ {u"4",                         u"$",                         u"4",                         NULL,                         
    /*  LANG_DE  */  u"4",                         u"$",                         NULL,                         NULL,                         
    /*  LANG_FR  */  u"'",                         u"4",                         NULL,                         u"{",                         
    /*  LANG_ES  */  u"4",                         u"$",                         u"4",                         u"~",                         
    /*  LANG_PT  */  u"4",                         u"$",                         u"4",                         u"\f\f\xa7",                  
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  u"4",                         u"+",                         u"4",                         u"$",                         
    /*  LANG_KO  */  u"4",                         u"$",                         u"4",                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_U,                   HIRAGANA_SMALL_U,             NULL,                         NULL,                         
    /*  LANG_AR  */  u"4",                         u"$",                         u"4",                         ARABIC_INDIC_4,               
    /*  LANG_GR  */  u"4",                         u"$",                         u"4",                         POUND_SIGN,                   
    /*  LANG_UA  */  u"4",                         u";",                         u"4",                         NULL,                         
    /*  LANG_RU  */  u"4",                         u";",                         u"4",                         NULL,                         
    /*  LANG_BY  */  u"4",                         u";",                         u"4",                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_EN_W_DESC,        CYRILLIC_EN_W_DESC,           NULL,                         NULL,                         
    /*  LANG_BG  */  u"4",                         QUOTE,                        u"4",                         NULL,                         
    /*  LANG_PL  */  u"4",                         CURRENCY_SIGN,                NULL,                         MOD_BREVE,                    
    /*  LANG_RO  */  u"4",                         u"$",                         u"",                          MOD_BREVE,                    
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u"4",                         u"$",                         u"4",                         QUATER_SIGN
    },
    /*KC_5       */ {u"5",                         u"%",                         u"5",                         NULL,                         
    /*  LANG_DE  */  u"5",                         u"%",                         NULL,                         NULL,                         
    /*  LANG_FR  */  u"(",                         u"5",                         NULL,                         u"[",                         
    /*  LANG_ES  */  u"5",                         u"%",                         u"5",                         u" " EURO_SIGN,               
    /*  LANG_PT  */  u"5",                         u"%",                         u"5",                         u" " EURO_SIGN,               
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  u"5",                         u"%",                         u"5",                         HALF_SIGN,                    
    /*  LANG_KO  */  u"5",                         u"%",                         u"5",                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_E,                   HIRAGANA_SMALL_E,             NULL,                         NULL,                         
    /*  LANG_AR  */  u"5",                         u"%",                         u"5",                         ARABIC_INDIC_5,               
    /*  LANG_GR  */  u"5",                         u"%",                         u"5",                         SECTION,                      
    /*  LANG_UA  */  u"5",                         u"%",                         u"5",                         NULL,                         
    /*  LANG_RU  */  u"5",                         u"%",                         u"5",                         NULL,                         
    /*  LANG_BY  */  u"5",                         u"%",                         u"5",                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_GHE_W_STROKE,     CYRILLIC_GHE_W_STROKE,        u"5",                         NULL,                         
    /*  LANG_BG  */  u"5",                         u"%",                         u"5",                         NULL,                         
    /*  LANG_PL  */  u"5",                         u"%",                         NULL,                         MOD_RING,                     
    /*  LANG_RO  */  u"5",                         u"%",                         NULL,                         MOD_RING,                     
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u"5",                         u"%",                         u"5",                         HALF_SIGN
    },
    /*KC_6       */ {u"6",                         u"^",                         u"6",                         NULL,                         
    /*  LANG_DE  */  u"6",                         u"&",                         NULL,                         NULL,                         
    /*  LANG_FR  */  u"-",                         u"6",                         NULL,                         u"|",                         
    /*  LANG_ES  */  u"6",                         u"&",                         u"6",                         NOT_SIGN,                     
    /*  LANG_PT  */  u"6",                         u"&",                         u"6",                         NULL,                         
    /*  LANG_IT  */  u"6",                         u"&",                         u"6",                         NULL,                         
    /*  LANG_TR  */  u"6",                         u"&",                         u"6",                         NULL,                         
    /*  LANG_KO  */  u"6",                         u"^",                         u"6",                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_O,                   HIRAGANA_SMALL_O,             NULL,                         NULL,                         
    /*  LANG_AR  */  u"6",                         u"^",                         u"6",                         ARABIC_INDIC_6,               
    /*  LANG_GR  */  u"6",                         u"^",                         u"6",                         u"\f\f" PILCROW,              
    /*  LANG_UA  */  u"6",                         u":",                         u"6",                         NULL,                         
    /*  LANG_RU  */  u"6",                         u":",                         u"6",                         NULL,                         
    /*  LANG_BY  */  u"6",                         u":",                         u"6",                         NULL,                         
    /*  LANG_KZ  */  u",",                         u";",                         u",",                         NULL,                         
    /*  LANG_BG  */  u"6",                         EQUALS,                       u"6",                         NULL,                         
    /*  LANG_PL  */  u"6",                         u"&",                         NULL,                         MOD_OGONEK,                   
    /*  LANG_RO  */  u"6",                         u"^",                         NULL,                         MOD_OGONEK,                   
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u"6",                         u"&",                         u"6",                         THREE_QUATER_SIGN
    },
    /*KC_7       */ {u"7",                         u"&",                         u"7",                         NULL,                         
    /*  LANG_DE  */  u"7",                         u"/",                         NULL,                         u"{",                         
    /*  LANG_FR  */  u"\f\f" E_WITH_GRAVE_SMALL,   u"7",                         NULL,                         ACUTE_ACCENT,                 
    /*  LANG_ES  */  u"7",                         u"/",                         u"7",                         NULL,                         
    /*  LANG_PT  */  u"7",                         u"/",                         u"7",                         u"{",                         
    /*  LANG_IT  */  u"7",                         u"/",                         u"7",                         NULL,                         
    /*  LANG_TR  */  u"7",                         u"/",                         u"7",                         u"{",                         
    /*  LANG_KO  */  u"7",                         u"&",                         u"7",                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_YA,                  HIRAGANA_SMALL_YA,            NULL,                         NULL,                         
    /*  LANG_AR  */  u"7",                         u"&",                         u"7",                         ARABIC_INDIC_7,               
    /*  LANG_GR  */  u"7",                         u"&",                         u"7",                         NULL,                         
    /*  LANG_UA  */  u"7",                         u"?",                         u"7",                         NULL,                         
    /*  LANG_RU  */  u"7",                         u"?",                         u"7",                         NULL,                         
    /*  LANG_BY  */  u"7",                         u"?",                         u"7",                         NULL,                         
    /*  LANG_KZ  */  u".",                         u":",                         u".",                         NULL,                         
    /*  LANG_BG  */  u"7",                         u":",                         u"7",                         NULL,                         
    /*  LANG_PL  */  u"7",                         u"/",                         NULL,                         u"`",                         
    /*  LANG_RO  */  u"7",                         u"&",                         NULL,                         u"`",                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u"7",                         u"_",                         u"7",                         POUND_SIGN
    },
    /*KC_8       */ {u"8",                         u"*",                         u"8",                         NULL,                         
    /*  LANG_DE  */  u"8",                         u"(",                         NULL,                         u"[",                         
    /*  LANG_FR  */  u"_",                         u"8",                         NULL,                         BACKSLASH,                    
    /*  LANG_ES  */  u"8",                         u"(",                         u"8",                         NULL,                         
    /*  LANG_PT  */  u"8",                         u"(",                         u"8",                         u"[",                         
    /*  LANG_IT  */  u"8",                         u"(",                         u"8",                         NULL,                         
    /*  LANG_TR  */  u"8",                         u"(",                         u"8",                         u"[",                         
    /*  LANG_KO  */  u"8",                         u"*",                         u"8",                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_YU,                  HIRAGANA_SMALL_YU,            NULL,                         NULL,                         
    /*  LANG_AR  */  u"8",                         u"*",                         u"8",                         ARABIC_INDIC_8,               
    /*  LANG_GR  */  u"8",                         u"*",                         u"8",                         CURRENCY_SIGN,                
    /*  LANG_UA  */  u"8",                         u"*",                         u"8",                         NULL,                         
    /*  LANG_RU  */  u"8",                         u"*",                         u"8",                         NULL,                         
    /*  LANG_BY  */  u"8",                         u"*",                         u"8",                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_STRAIGHT_U,       CYRILLIC_STRAIGHT_U,          NULL,                         NULL,                         
    /*  LANG_BG  */  u"8",                         u"/",                         u"8",                         NULL,                         
    /*  LANG_PL  */  u"8",                         u"(",                         NULL,                         MOD_DOT_ACCENT,               
    /*  LANG_RO  */  u"8",                         u"*",                         NULL,                         MOD_DOT_ACCENT,               
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u"8",                         u"(",                         u"8",                         u"{"
    },
    /*KC_9       */ {u"9",                         u"(",                         u"9",                         NULL,                         
    /*  LANG_DE  */  u"9",                         u")",                         NULL,                         u"]",                         
    /*  LANG_FR  */  u"\f\f" C_WITH_CEDILLA_SMALL, u"9",                         NULL,                         u"^",                         
    /*  LANG_ES  */  u"9",                         u")",                         u"9",                         NULL,                         
    /*  LANG_PT  */  u"9",                         u")",                         u"9",                         u"]",                         
    /*  LANG_IT  */  u"9",                         u")",                         u"9",                         NULL,                         
    /*  LANG_TR  */  u"9",                         u")",                         u"9",                         u"]",                         
    /*  LANG_KO  */  u"9",                         u"(",                         u"9",                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_YO,                  HIRAGANA_SMALL_YO,            NULL,                         NULL,                         
    /*  LANG_AR  */  u"9",                         u"(",                         u"9",                         ARABIC_INDIC_9,               
    /*  LANG_GR  */  u"9",                         u"(",                         u"9",                         u"\f\f" BROKEN_BAR,           
    /*  LANG_UA  */  u"9",                         u"(",                         u"9",                         NULL,                         
    /*  LANG_RU  */  u"9",                         u"(",                         u"9",                         NULL,                         
    /*  LANG_BY  */  u"9",                         u"(",                         u"9",                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_STRAIGHT_U_W_STROKE,CYRILLIC_STRAIGHT_U_W_STROKE, NULL,                         NULL,                         
    /*  LANG_BG  */  u"9",                         u"--",                        u"9",                         NULL,                         
    /*  LANG_PL  */  u"9",                         u")",                         NULL,                         ACUTE_ACCENT,                 
    /*  LANG_RO  */  u"9",                         u"(",                         NULL,                         ACUTE_ACCENT,                 
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u"9",                         u")",                         u"9",                         u"}"
    },
    /*KC_0       */ {ZERO,                         u")",                         ZERO,                         NULL,                         
    /*  LANG_DE  */  ZERO,                         EQUALS,                       NULL,                         u"}",                         
    /*  LANG_FR  */  u"\f\f" A_WITH_GRAVE_SMALL,   ZERO,                         NULL,                         u"@",                         
    /*  LANG_ES  */  ZERO,                         EQUALS,                       ZERO,                         NULL,                         
    /*  LANG_PT  */  ZERO,                         EQUALS,                       ZERO,                         u"}",                         
    /*  LANG_IT  */  ZERO,                         EQUALS,                       ZERO,                         NULL,                         
    /*  LANG_TR  */  ZERO,                         EQUALS,                       ZERO,                         u"}",                         
    /*  LANG_KO  */  ZERO,                         u")",                         ZERO,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_WA,                  HIRAGANA_WO,                  NULL,                         NULL,                         
    /*  LANG_AR  */  ZERO,                         u")",                         ZERO,                         ARABIC_INDIC_0,               
    /*  LANG_GR  */  ZERO,                         u")",                         ZERO,                         DEGREE,                       
    /*  LANG_UA  */  ZERO,                         u")",                         ZERO,                         NULL,                         
    /*  LANG_RU  */  ZERO,                         u")",                         ZERO,                         NULL,                         
    /*  LANG_BY  */  ZERO,                         u")",                         ZERO,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_KA_W_DESC,        CYRILLIC_KA_W_DESC,           NULL,                         NULL,                         
    /*  LANG_BG  */  ZERO,                         NUMERO_SIGN,                  ZERO,                         NULL,                         
    /*  LANG_PL  */  ZERO,                         EQUALS,                       NULL,                         MOD_HUNGARUMLAUT,             
    /*  LANG_RO  */  ZERO,                         u")",                         NULL,                         MOD_HUNGARUMLAUT,             
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  ZERO,                         u"'",                         ZERO,                         NULL
    },
    /*KC_ENTER   */ {ARROWS_RETURN,                NULL,                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_GR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_UA  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RU  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_BY  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KZ  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_BG  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_ESCAPE  */ {u"Esc",                       NULL,                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_GR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_UA  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RU  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_BY  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KZ  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_BG  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_BACKSPACE*/ {TECHNICAL_ERASELEFT,          NULL,                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_GR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_UA  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RU  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_BY  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KZ  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_BG  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_TAB     */ {ARROWS_TAB,                   NULL,                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_GR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_UA  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RU  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_BY  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KZ  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_BG  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_SPACE   */ {ICON_SPACE,                   NULL,                         NULL,                         NULL,                         
    /*  LANG_DE  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_GR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_UA  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RU  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_BY  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KZ  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_BG  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_MINUS   */ {u"-",                         u"_",                         u"-",                         NULL,                         
    /*  LANG_DE  */  ESZETT,                       u"?",                         ESZETT,                       BACKSLASH,                    
    /*  LANG_FR  */  u")",                         DEGREE,                       NULL,                         u"]",                         
    /*  LANG_ES  */  u"'",                         u"?",                         u"'",                         NULL,                         
    /*  LANG_PT  */  u"'",                         u"?",                         u"'",                         NULL,                         
    /*  LANG_IT  */  u"'",                         u"?",                         u"'",                         u"^",                         
    /*  LANG_TR  */  u"*",                         u"?",                         u"*",                         BACKSLASH,                    
    /*  LANG_KO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_HO,                  KH_PSOUNDM,                   NULL,                         NULL,                         
    /*  LANG_AR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_GR  */  u"-",                         u"_",                         u"-",                         PLUS_MINUS,                   
    /*  LANG_UA  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RU  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_BY  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_BARRED_O,         CYRILLIC_BARRED_O,            NULL,                         NULL,                         
    /*  LANG_BG  */  u"-",                         u"$",                         u"-",                         NULL,                         
    /*  LANG_PL  */  u"+",                         u"?",                         u"+",                         DIAERESIS,                    
    /*  LANG_RO  */  u"-",                         u"_",                         u"-",                         DIAERESIS u"--",              
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u"/",                         u"?",                         u"/",                         BACKSLASH
    },
    /*KC_EQUAL   */ {EQUALS,                       u"+",                         EQUALS,                       NULL,                         
    /*  LANG_DE  */  GRAVE_ACCENT,                 ACUTE_ACCENT,                 GRAVE_ACCENT,                 NULL,                         
    /*  LANG_FR  */  EQUALS,                       u"+",                         NULL,                         u"}",                         
    /*  LANG_ES  */  INVERTED_EMARK,               INVERTED_QMARK,               INVERTED_EMARK,               NULL,                         
    /*  LANG_PT  */  DBL_ANGLE_QMARK_L,            DBL_ANGLE_QMARK_R,            DBL_ANGLE_QMARK_L,            NULL,                         
    /*  LANG_IT  */  I_WITH_GRAVE_SMALL,           I_WITH_ACUTE_SMALL,           I_WITH_GRAVE,                 I_WITH_CIRCUMF_SMALL,         
    /*  LANG_TR  */  u"-",                         u"_",                         u"-",                         u"|",                         
    /*  LANG_KO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_HE,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_GR  */  EQUALS,                       u"+",                         EQUALS,                       HALF_SIGN,                    
    /*  LANG_UA  */  EQUALS,                       u"+",                         EQUALS,                       NULL,                         
    /*  LANG_RU  */  EQUALS,                       u"+",                         EQUALS,                       NULL,                         
    /*  LANG_BY  */  EQUALS,                       u"+",                         EQUALS,                       NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_SHHA,             CYRILLIC_SHHA,                NULL,                         NULL,                         
    /*  LANG_BG  */  u".",                         EURO_SIGN,                    u".",                         NULL,                         
    /*  LANG_PL  */  u"'",                         u"*",                         u"'",                         CEDILLA,                      
    /*  LANG_RO  */  u"=",                         u"+",                         u"=",                         u"\f\f\f\f" CEDILLA PLUS_MINUS,
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  DEGREE,                       u"~",                         DEGREE,                       CEDILLA
    },
    /*KC_LBRC    */ {u"[",                         u"{",                         u"[",                         NULL,                         
    /*  LANG_DE  */  UMLAUT_U_SMALL,               UMLAUT_U,                     NULL,                         NULL,                         
    /*  LANG_FR  */  u"^",                         DIAERESIS,                    NULL,                         NULL,                         
    /*  LANG_ES  */  GRAVE_ACCENT,                 u"^",                         GRAVE_ACCENT,                 u"[",                         
    /*  LANG_PT  */  u"+",                         u"*",                         u"+",                         DIAERESIS,                    
    /*  LANG_IT  */  E_WITH_GRAVE_SMALL,           E_WITH_ACUTE_SMALL,           E_WITH_GRAVE,                 u"[",                         
    /*  LANG_TR  */  G_WITH_BREVE_SMALL,           G_WITH_BREVE,                 NULL,                         DIAERESIS,                    
    /*  LANG_KO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  KH_VSOUNDM,                   LCORNER_BRKT,                 NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_JEEM,                  u"}",                         ARABIC_JEEM,                  NULL,                         
    /*  LANG_GR  */  u"[",                         u"{",                         u"[",                         DBL_ANGLE_QMARK_L,            
    /*  LANG_UA  */  CYRILLIC_SM_HA,               CYRILLIC_HA,                  NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_HA,               CYRILLIC_HA,                  NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_HA,               CYRILLIC_HA,                  NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_HA,               CYRILLIC_HA,                  NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_TSE,              CYRILLIC_TSE,                 NULL,                         NULL,                         
    /*  LANG_PL  */  LATIN_017C,                   LATIN_0144,                   LATIN_017C,                   DEVISION_SIGN,                
    /*  LANG_RO  */  LATIN_0103,                   LATIN_0102,                   LATIN_0103,                   u"[ {",                       
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  DIAERESIS,                    u"^",                         DIAERESIS,                    NULL
    },
    /*KC_RBRC    */ {u"]",                         u"}",                         u"]",                         NULL,                         
    /*  LANG_DE  */  u"+",                         u"*",                         NULL,                         u"~",                         
    /*  LANG_FR  */  u"$",                         POUND_SIGN,                   NULL,                         CURRENCY_SIGN,                
    /*  LANG_ES  */  u"+",                         u"*",                         u"+",                         u"]",                         
    /*  LANG_PT  */  ACUTE_ACCENT,                 GRAVE_ACCENT,                 ACUTE_ACCENT,                 u"]",                         
    /*  LANG_IT  */  u"+",                         u"*",                         u"+",                         u"]",                         
    /*  LANG_TR  */  UMLAUT_U_SMALL,               UMLAUT_U,                     NULL,                         u"~",                         
    /*  LANG_KO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  KH_SEMI_VSOUNDM,              RCORNER_BRKT,                 NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_DAL,                   u"{",                         ARABIC_DAL,                   NULL,                         
    /*  LANG_GR  */  u"]",                         u"}",                         u"]",                         DBL_ANGLE_QMARK_R,            
    /*  LANG_UA  */  CYRILLIC_SM_YI,               CYRILLIC_YI,                  NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_HARD_SIGN,        CYRILLIC_HARD_SIGN,           NULL,                         NULL,                         
    /*  LANG_BY  */  u"'",                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_HARD_SIGN,        CYRILLIC_HARD_SIGN,           NULL,                         NULL,                         
    /*  LANG_BG  */  u";",                         SECTION,                      NULL,                         NULL,                         
    /*  LANG_PL  */  LATIN_015B,                   LATIN_0107,                   NULL,                         MUL_SIGN,                     
    /*  LANG_RO  */  I_WITH_CIRCUMF_SMALL,         I_WITH_CIRCUMF,               I_WITH_CIRCUMF,               u"] }",                       
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u"*",                         u"|",                         u"*",                         NULL
    },
    /*KC_BACKSLASH*/ {BACKSLASH,                    u"|",                         BACKSLASH,                    NULL,                         
    /*  LANG_DE  */  u"#",                         u"'",                         NULL,                         NULL,                         
    /*  LANG_FR  */  u"*",                         MICRO_SIGN,                   NULL,                         NULL,                         
    /*  LANG_ES  */  C_WITH_CEDILLA_SMALL,         C_WITH_CEDILLA,               NULL,                         u"}",                         
    /*  LANG_PT  */  u"~",                         u"^",                         u"~",                         NULL,                         
    /*  LANG_IT  */  U_WITH_GRAVE_SMALL,           U_WITH_ACUTE_SMALL,           U_WITH_GRAVE,                 u"\f" SECTION,                
    /*  LANG_TR  */  u",",                         u";",                         NULL,                         GRAVE_ACCENT,                 
    /*  LANG_KO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_MU,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_THAL,                  u"\v" ARABIC_SHADDA,          ARABIC_THAL,                  NULL,                         
    /*  LANG_GR  */  BACKSLASH,                    u"|",                         BACKSLASH,                    NOT_SIGN,                     
    /*  LANG_UA  */  BACKSLASH,                    u"/",                         NULL,                         NULL,                         
    /*  LANG_RU  */  BACKSLASH,                    u"/",                         NULL,                         NULL,                         
    /*  LANG_BY  */  BACKSLASH,                    u"/",                         NULL,                         NULL,                         
    /*  LANG_KZ  */  BACKSLASH,                    u"/",                         NULL,                         NULL,                         
    /*  LANG_BG  */  u",,",                        u"\xb4\xb4",                  u",,",                        NULL,                         
    /*  LANG_PL  */  O_WITH_ACUTE_SMALL,           LATIN_017A,                   O_WITH_ACUTE_SMALL,           LATIN_017A,                   
    /*  LANG_RO  */  LATIN_00E2,                   LATIN_00C2,                   LATIN_00C2,                   u"\\ |",                      
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u"<",                         u">",                         u"<",                         NULL
    },
    /*KC_NONUS_HASH*/ {u"#",                         u"~",                         u"#",                         NULL,                         
    /*  LANG_DE  */  u"#",                         u"'",                         NULL,                         NULL,                         
    /*  LANG_FR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_GR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_UA  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RU  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_BY  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KZ  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_BG  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  NULL,                         NULL,                         NULL,                         NULL
    },
    /*KC_SEMICOLON*/ {u";",                         u":",                         u";",                         NULL,                         
    /*  LANG_DE  */  UMLAUT_O_SMALL,               UMLAUT_O,                     NULL,                         NULL,                         
    /*  LANG_FR  */  u"m",                         u"M",                         NULL,                         NULL,                         
    /*  LANG_ES  */  N_WITH_TILDE_SMALL,           N_WITH_TILDE,                 NULL,                         NULL,                         
    /*  LANG_PT  */  C_WITH_CEDILLA_SMALL,         C_WITH_CEDILLA,               NULL,                         NULL,                         
    /*  LANG_IT  */  O_WITH_GRAVE_SMALL,           C_WITH_CEDILLA_SMALL,         O_WITH_GRAVE,                 u"@",                         
    /*  LANG_TR  */  S_WITH_CEDILLA_SMALL,         S_WITH_CEDILLA,               NULL,                         ACUTE_ACCENT,                 
    /*  LANG_KO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_RE,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_KAF,                   u":",                         ARABIC_KAF,                   NULL,                         
    /*  LANG_GR  */  GREEK_TONOS,                  GREEK_DIALYTIKA,              GREEK_TONOS,                  GREEK_DIALYTIKA_TONOS,        
    /*  LANG_UA  */  CYRILLIC_SM_ZHE,              CYRILLIC_ZHE,                 NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_ZHE,              CYRILLIC_ZHE,                 NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_ZHE,              CYRILLIC_ZHE,                 NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_ZHE,              CYRILLIC_ZHE,                 NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_EM,               CYRILLIC_EM,                  NULL,                         NULL,                         
    /*  LANG_PL  */  LATIN_0142,                   LATIN_0141,                   NULL,                         u"$",                         
    /*  LANG_RO  */  LATIN_015F,                   LATIN_015E,                   LATIN_015E,                   u"; :",                       
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u"+",                         PLUS_MINUS,                   NULL,                         NULL
    },
    /*KC_QUOTE   */ {u"'",                         QUOTE,                        u"'",                         NULL,                         
    /*  LANG_DE  */  UMLAUT_A_SMALL,               UMLAUT_A,                     NULL,                         NULL,                         
    /*  LANG_FR  */  U_WITH_GRAVE,                 u"%",                         NULL,                         NULL,                         
    /*  LANG_ES  */  ACUTE_ACCENT,                 DIAERESIS,                    ACUTE_ACCENT,                 u"{",                         
    /*  LANG_PT  */  MASC_ORDINAL_IND,             FEM_ORDINAL_IND,              MASC_ORDINAL_IND,             NULL,                         
    /*  LANG_IT  */  A_WITH_GRAVE_SMALL,           O_WITH_ACUTE_SMALL,           A_WITH_GRAVE,                 u"#",                         
    /*  LANG_TR  */  u"i",                         I_WITH_DOT,                   NULL,                         NULL,                         
    /*  LANG_KO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_KE,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_TAH,                   QUOTE,                        ARABIC_TAH,                   NULL,                         
    /*  LANG_GR  */  u"'",                         QUOTE,                        u"'",                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_UA_IE,            CYRILLIC_UA_IE,               NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_E,                CYRILLIC_E,                   NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_E,                CYRILLIC_E,                   NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_E,                CYRILLIC_E,                   NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_CHE,              CYRILLIC_CHE,                 NULL,                         NULL,                         
    /*  LANG_PL  */  LATIN_0105,                   LATIN_0119,                   LATIN_0105,                   ESZETT,                       
    /*  LANG_RO  */  LATIN_0163,                   LATIN_0162,                   LATIN_0162,                   u"’ \"",                      
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u"`",                         u"´",                         u"`",                         NULL
    },
    /*KC_GRAVE   */ {GRAVE_ACCENT,                 u"~",                         GRAVE_ACCENT,                 NULL,                         
    /*  LANG_DE  */  u"^",                         DEGREE,                       u"^",                         NULL,                         
    /*  LANG_FR  */  u"\v" SUPER_SCRIPT_2,         SPACE,                        u"\v" SUPER_SCRIPT_2,         NULL,                         
    /*  LANG_ES  */  MASC_ORDINAL_IND,             FEM_ORDINAL_IND,              MASC_ORDINAL_IND,             BACKSLASH,                    
    /*  LANG_PT  */  BACKSLASH,                    u"|",                         BACKSLASH,                    NULL,                         
    /*  LANG_IT  */  BACKSLASH,                    u"|",                         BACKSLASH,                    NULL,                         
    /*  LANG_TR  */  QUOTE,                        E_WITH_ACUTE,                 NULL,                         u"<",                         
    /*  LANG_KO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_RO,                  NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  u">",                         u"<",                         u">",                         NULL,                         
    /*  LANG_GR  */  GRAVE_ACCENT,                 u"~",                         GRAVE_ACCENT,                 NULL,                         
    /*  LANG_UA  */  GRAVE_ACCENT,                 HRYVNIA_SIGN,                 NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_IO,               CYRILLIC_IO,                  NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_IO,               CYRILLIC_IO,                  NULL,                         NULL,                         
    /*  LANG_KZ  */  u"(",                         u")",                         u"(",                         NULL,                         
    /*  LANG_BG  */  u"(",                         u")",                         u"(",                         NULL,                         
    /*  LANG_PL  */  MOD_OGONEK,                   MOD_DOT_ACCENT,               MOD_OGONEK,                   MOD_DOT_ACCENT,               
    /*  LANG_RO  */  u",,",                        u"\xb4\xb4",                  u",,",                        u"` ~",                       
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u"@",                         SECTION,                      u"@",                         NOT_SIGN
    },
    /*KC_COMMA   */ {COMMA,                        u"<",                         COMMA,                        NULL,                         
    /*  LANG_DE  */  COMMA,                        u";",                         NULL,                         NULL,                         
    /*  LANG_FR  */  u";",                         u".",                         NULL,                         NULL,                         
    /*  LANG_ES  */  COMMA,                        u";",                         COMMA,                        NULL,                         
    /*  LANG_PT  */  COMMA,                        u";",                         COMMA,                        NULL,                         
    /*  LANG_IT  */  COMMA,                        u";",                         COMMA,                        DBL_ANGLE_QMARK_L,            
    /*  LANG_TR  */  UMLAUT_O_SMALL,               UMLAUT_O,                     NULL,                         NULL,                         
    /*  LANG_KO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_NE,                  CJK_IDEOG_COMMA,              NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_WAW,                   COMMA,                        ARABIC_WAW,                   NULL,                         
    /*  LANG_GR  */  COMMA,                        u"<",                         COMMA,                        NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_BE,               CYRILLIC_BE,                  NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_BE,               CYRILLIC_BE,                  NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_BE,               CYRILLIC_BE,                  NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_BE,               CYRILLIC_BE,                  NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_ER,               CYRILLIC_ER,                  NULL,                         NULL,                         
    /*  LANG_PL  */  COMMA,                        u";",                         COMMA,                        u"<",                         
    /*  LANG_RO  */  COMMA,                        u";",                         COMMA,                        u"< " DBL_ANGLE_QMARK_L,      
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u",",                         u";",                         NULL,                         NULL
    },
    /*KC_DOT     */ {u".",                         u">",                         u".",                         NULL,                         
    /*  LANG_DE  */  u".",                         u":",                         NULL,                         NULL,                         
    /*  LANG_FR  */  u":",                         u"/",                         NULL,                         NULL,                         
    /*  LANG_ES  */  u".",                         u":",                         u".",                         NULL,                         
    /*  LANG_PT  */  u".",                         u":",                         u".",                         NULL,                         
    /*  LANG_IT  */  u".",                         u":",                         u".",                         DBL_ANGLE_QMARK_R,            
    /*  LANG_TR  */  C_WITH_CEDILLA_SMALL,         C_WITH_CEDILLA,               NULL,                         NULL,                         
    /*  LANG_KO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_RU,                  CJK_IDEOG_FSTOP,              NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_ZAIN,                  u".",                         ARABIC_ZAIN,                  NULL,                         
    /*  LANG_GR  */  u".",                         u">",                         u".",                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_YU,               CYRILLIC_YU,                  NULL,                         NULL,                         
    /*  LANG_RU  */  CYRILLIC_SM_YU,               CYRILLIC_YU,                  NULL,                         NULL,                         
    /*  LANG_BY  */  CYRILLIC_SM_YU,               CYRILLIC_YU,                  NULL,                         NULL,                         
    /*  LANG_KZ  */  CYRILLIC_SM_YU,               CYRILLIC_YU,                  NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_EL,               CYRILLIC_EL,                  NULL,                         NULL,                         
    /*  LANG_PL  */  u".",                         u":",                         u".",                         u">",                         
    /*  LANG_RO  */  u".",                         u":",                         u".",                         u"> " DBL_ANGLE_QMARK_R,      
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u".",                         u":",                         u".",                         MIDDLE_DOT
    },
    /*KC_SLASH   */ {u"/",                         u"?",                         u"/",                         NULL,                         
    /*  LANG_DE  */  u"-",                         u"_",                         u"-",                         NULL,                         
    /*  LANG_FR  */  u"!",                         SECTION,                      NULL,                         NULL,                         
    /*  LANG_ES  */  u"-",                         u"_",                         u"-",                         NULL,                         
    /*  LANG_PT  */  u"-",                         u"_",                         u"-",                         NULL,                         
    /*  LANG_IT  */  u"-",                         u"_",                         u"-",                         DEGREE,                       
    /*  LANG_TR  */  u".",                         u":",                         u".",                         NULL,                         
    /*  LANG_KO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  HIRAGANA_ME,                  KATAKANA_MIDDLE_DOT,          NULL,                         NULL,                         
    /*  LANG_AR  */  ARABIC_ZAH,                   ARABIC_QMARK,                 ARABIC_ZAH,                   NULL,                         
    /*  LANG_GR  */  u"/",                         u"?",                         u"/",                         NULL,                         
    /*  LANG_UA  */  u".",                         COMMA,                        u".",                         NULL,                         
    /*  LANG_RU  */  u".",                         COMMA,                        u".",                         NULL,                         
    /*  LANG_BY  */  u".",                         COMMA,                        u".",                         NULL,                         
    /*  LANG_KZ  */  NUMERO_SIGN,                  u"?",                         NUMERO_SIGN,                  NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_BE,               CYRILLIC_BE,                  NULL,                         NULL,                         
    /*  LANG_PL  */  u"-",                         u"_",                         u"-",                         NULL,                         
    /*  LANG_RO  */  u"/",                         u"?",                         u"/",                         NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u"-",                         u"=",                         u"-",                         NULL
    },
    /*KC_NONUS_BACKSLASH*/ {u"<",                         u">",                         u"<",                         NULL,                         
    /*  LANG_DE  */  u"<",                         u">",                         NULL,                         u"|",                         
    /*  LANG_FR  */  u"<",                         u">",                         NULL,                         u"|",                         
    /*  LANG_ES  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_PT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_IT  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_TR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_KO  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_JA  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_AR  */  BACKSLASH,                    u"|",                         BACKSLASH,                    NULL,                         
    /*  LANG_GR  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_UA  */  CYRILLIC_SM_GHE_W_UPTURN,     CYRILLIC_GHE_W_UPTURN,        NULL,                         NULL,                         
    /*  LANG_RU  */  BACKSLASH,                    u"/",                         NULL,                         NULL,                         
    /*  LANG_BY  */  BACKSLASH,                    u"/",                         NULL,                         NULL,                         
    /*  LANG_KZ  */  BACKSLASH,                    u"|",                         NULL,                         NULL,                         
    /*  LANG_BG  */  CYRILLIC_SM_I_W_GRAVE,        CYRILLIC_I_W_GRAVE,           NULL,                         NULL,                         
    /*  LANG_PL  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_RO  */  BACKSLASH,                    u"|",                         BACKSLASH,                    NULL,                         
    /*  LANG_CN  */  NULL,                         NULL,                         NULL,                         NULL,                         
    /*  LANG_NL  */  u"]",                         u"[",                         u"]",                         BROKEN_BAR
    },
    //[[[end]]]
};

/*[[[cog
#count settings keys
settings_index = key_index
while key_name:
    settings_index = settings_index + 1
    key_name = sheet.cell(row = settings_index, column = 1).value
#reset to first settings line
key_name = sheet.cell(row = key_index, column = 1).value

cog.outl(f'static const int8_t poly_settings [SETTINGS_NUM][NUM_LANG * 4] = {{');

def make_settings(idx, row, append_comma):
    lang_row = '/' + f'*  {lang_dict[idx]}*' + '/  '
    for case_idx in range(0, 4):
        text = sheet.cell(row = row, column = 2 + idx*4+case_idx).value

        if text:
            num = text#int(str(text))
            if case_idx != 3 or append_comma:
                lang_row = f"{lang_row}{num},"
            else:
                lang_row = f"{lang_row}{num}"
        else:
            if case_idx != 3 or append_comma:
                lang_row = f"{lang_row}0,"
            else:
                lang_row = f"{lang_row}0"
    return lang_row

while key_name:
    cog.outl('\t/' + f'/ {key_name}\n\t{{')

    for idx in lang_dict:
        cog.outl(f'\t\t{make_settings(idx, key_index, idx < lang_index-1)}')
    if key_index+1==settings_index:
        cog.outl("\t}")
    else:
        cog.outl("\t},")

    key_index = key_index + 1
    key_name = sheet.cell(row = key_index, column = 1).value

]]]*/
static const int8_t poly_settings [SETTINGS_NUM][NUM_LANG * 4] = {
	// {letter.hoffset}
	{
		/*  LANG_EN*/  0,-1,0,0,
		/*  LANG_DE*/  0,-1,0,54,
		/*  LANG_FR*/  0,-1,0,50,
		/*  LANG_ES*/  0,-1,0,53,
		/*  LANG_PT*/  0,-1,0,50,
		/*  LANG_IT*/  0,-1,0,50,
		/*  LANG_TR*/  0,-1,0,50,
		/*  LANG_KO*/  0,33,0,0,
		/*  LANG_JA*/  0,-1,0,0,
		/*  LANG_AR*/  0,38,0,52,
		/*  LANG_GR*/  0,-1,0,50,
		/*  LANG_UA*/  0,-1,0,50,
		/*  LANG_RU*/  0,-1,0,50,
		/*  LANG_BY*/  0,-1,0,50,
		/*  LANG_KZ*/  0,-1,0,50,
		/*  LANG_BG*/  0,-1,0,50,
		/*  LANG_PL*/  0,-1,0,52,
		/*  LANG_RO*/  0,-1,0,48,
		/*  LANG_CN*/  0,30,0,50,
		/*  LANG_NL*/  0,-1,0,50
	},
	// {letter.voffset}
	{
		/*  LANG_EN*/  0,-1,0,0,
		/*  LANG_DE*/  0,-1,0,13,
		/*  LANG_FR*/  0,-1,0,13,
		/*  LANG_ES*/  0,-1,0,13,
		/*  LANG_PT*/  0,-1,0,13,
		/*  LANG_IT*/  0,-1,0,13,
		/*  LANG_TR*/  0,-1,0,12,
		/*  LANG_KO*/  0,15,0,0,
		/*  LANG_JA*/  0,-1,0,0,
		/*  LANG_AR*/  0,0,0,13,
		/*  LANG_GR*/  0,-1,0,12,
		/*  LANG_UA*/  0,-1,0,12,
		/*  LANG_RU*/  0,-1,0,13,
		/*  LANG_BY*/  0,-1,0,13,
		/*  LANG_KZ*/  0,-1,0,13,
		/*  LANG_BG*/  0,-1,0,12,
		/*  LANG_PL*/  0,-1,0,12,
		/*  LANG_RO*/  0,-1,0,13,
		/*  LANG_CN*/  0,15,0,13,
		/*  LANG_NL*/  0,-1,0,11
	},
	// {num.hoffset}
	{
		/*  LANG_EN*/  0,35,0,0,
		/*  LANG_DE*/  0,27,0,54,
		/*  LANG_FR*/  0,28,0,50,
		/*  LANG_ES*/  0,28,0,50,
		/*  LANG_PT*/  0,30,0,50,
		/*  LANG_IT*/  0,30,0,50,
		/*  LANG_TR*/  0,28,0,52,
		/*  LANG_KO*/  0,35,0,0,
		/*  LANG_JA*/  0,-1,0,0,
		/*  LANG_AR*/  0,27,0,52,
		/*  LANG_GR*/  0,27,0,50,
		/*  LANG_UA*/  0,30,0,50,
		/*  LANG_RU*/  0,30,0,50,
		/*  LANG_BY*/  0,30,0,50,
		/*  LANG_KZ*/  0,32,0,50,
		/*  LANG_BG*/  0,30,0,50,
		/*  LANG_PL*/  0,30,0,52,
		/*  LANG_RO*/  0,25,0,50,
		/*  LANG_CN*/  0,30,0,50,
		/*  LANG_NL*/  0,28,0,50
	},
	// {num.voffset}
	{
		/*  LANG_EN*/  0,0,0,0,
		/*  LANG_DE*/  0,0,0,13,
		/*  LANG_FR*/  0,0,0,13,
		/*  LANG_ES*/  0,0,0,13,
		/*  LANG_PT*/  0,0,0,13,
		/*  LANG_IT*/  0,0,0,13,
		/*  LANG_TR*/  0,0,0,12,
		/*  LANG_KO*/  0,0,0,0,
		/*  LANG_JA*/  0,-1,0,0,
		/*  LANG_AR*/  0,0,0,13,
		/*  LANG_GR*/  0,0,0,12,
		/*  LANG_UA*/  0,0,0,12,
		/*  LANG_RU*/  0,0,0,13,
		/*  LANG_BY*/  0,0,0,13,
		/*  LANG_KZ*/  0,0,0,13,
		/*  LANG_BG*/  0,0,0,12,
		/*  LANG_PL*/  0,0,0,12,
		/*  LANG_RO*/  0,0,0,13,
		/*  LANG_CN*/  0,0,0,13,
		/*  LANG_NL*/  0,0,0,11
	},
	// {sym.hoffset}
	{
		/*  LANG_EN*/  0,35,0,0,
		/*  LANG_DE*/  0,29,0,54,
		/*  LANG_FR*/  0,28,0,50,
		/*  LANG_ES*/  0,28,0,53,
		/*  LANG_PT*/  0,30,0,50,
		/*  LANG_IT*/  0,30,0,52,
		/*  LANG_TR*/  0,28,0,52,
		/*  LANG_KO*/  0,35,0,0,
		/*  LANG_JA*/  0,35,0,0,
		/*  LANG_AR*/  0,38,0,52,
		/*  LANG_GR*/  0,27,0,50,
		/*  LANG_UA*/  0,30,0,50,
		/*  LANG_RU*/  0,30,0,50,
		/*  LANG_BY*/  0,30,0,50,
		/*  LANG_KZ*/  0,32,0,50,
		/*  LANG_BG*/  0,30,0,50,
		/*  LANG_PL*/  0,30,0,52,
		/*  LANG_RO*/  0,25,0,48,
		/*  LANG_CN*/  0,30,0,50,
		/*  LANG_NL*/  0,28,0,50
	},
	// {sym.voffset}
	{
		/*  LANG_EN*/  0,0,0,0,
		/*  LANG_DE*/  0,0,0,13,
		/*  LANG_FR*/  0,0,0,13,
		/*  LANG_ES*/  0,0,0,13,
		/*  LANG_PT*/  0,0,0,13,
		/*  LANG_IT*/  0,0,0,13,
		/*  LANG_TR*/  0,0,0,12,
		/*  LANG_KO*/  0,0,0,0,
		/*  LANG_JA*/  0,0,0,0,
		/*  LANG_AR*/  0,0,0,13,
		/*  LANG_GR*/  0,0,0,12,
		/*  LANG_UA*/  0,0,0,12,
		/*  LANG_RU*/  0,0,0,13,
		/*  LANG_BY*/  0,0,0,13,
		/*  LANG_KZ*/  0,0,0,13,
		/*  LANG_BG*/  0,0,0,12,
		/*  LANG_PL*/  0,0,0,12,
		/*  LANG_RO*/  0,0,0,13,
		/*  LANG_CN*/  0,0,0,13,
		/*  LANG_NL*/  0,0,0,11
	}
//[[[end]]]
};

int8_t get_setting(uint8_t setting, uint8_t lang, uint8_t variation) {
    return poly_settings[setting][lang*4+variation];
}

const uint16_t* translate_keycode_only_shift(uint8_t used_lang, uint16_t keycode) {
    uint16_t index = keycode - KC_A;

    if(keycode==KC_NONUS_BACKSLASH) {
        index = ALPHA + NUM + ADDITIONAL - 1;
    } else if((keycode < KC_A || keycode > KC_SLASH)) {
        return NULL;
    }
    return  lang_plane[index][used_lang*4 + VAR_SHIFT];
}

const uint16_t* translate_keycode_only_altgr(uint8_t used_lang, uint16_t keycode) {
    uint16_t index = keycode - KC_A;

    if(keycode==KC_NONUS_BACKSLASH) {
        index = ALPHA + NUM + ADDITIONAL - 1;
    } else if((keycode < KC_A || keycode > KC_SLASH)) {
        return NULL;
    }
    return  lang_plane[index][used_lang*4 + VAR_ALTGR];
}

const uint16_t* translate_keycode(uint8_t used_lang, uint16_t keycode, bool shift, bool caps_lock) {
    uint16_t index = keycode - KC_A;

    if(keycode==KC_NONUS_BACKSLASH) {
        index = ALPHA + NUM + ADDITIONAL - 1;
    } else if((keycode < KC_A || keycode > KC_SLASH)) {
        return NULL;
    }

    const uint16_t* lower_case = lang_plane[index][used_lang*4 + VAR_SMALL];
    if(lower_case==NULL) {
        lower_case = lang_plane[index][0]; //english as backup
        used_lang = 0;
    }

    if(caps_lock) {
        const uint16_t* caps_case = lang_plane[index][used_lang*4 + VAR_CAPS];
        if(caps_case!=NULL) {
            if(!shift) {
                return caps_case;
            }
        } else {
           shift = !shift;
        }
    }

    if(shift) {
        const uint16_t* upper_case = lang_plane[index][used_lang*4 + VAR_SHIFT];
        if(upper_case!=NULL) {
            return upper_case;
        }
    }
    return lower_case;
}


/*[[[cog
    latin_sheet = wb['latin_sup_ex']
    d = dict.fromkeys(string.ascii_uppercase, 0)
    d.update(dict.fromkeys(string.ascii_lowercase, 0))

    max_variation_index = 0
    letter_index = 1
    current_letter = latin_sheet[f"A{letter_index}"].value
    current_code = latin_sheet[f"B{letter_index+1}"].value
    while current_letter:
        variations =  [ ]
        variation_index = 1
        while current_code:
            variations.append(current_code)
            max_variation_index = max(max_variation_index, variation_index)
            variation_index = variation_index + 1
            current_code = latin_sheet[f"{chr(ord('A')+variation_index)}{letter_index+1}"].value

        d.update({current_letter : variations})
        letter_index = letter_index + 2
        current_letter = latin_sheet[f"A{letter_index}"].value
        current_code = latin_sheet[f"B{letter_index+1}"].value

    last_letter = latin_sheet[f"A{letter_index-2}"].value
    last_index = ord(last_letter)

    cog.outl(f"const uint16_t* latin_ex_map[26*2][{max_variation_index}] PROGMEM = {{")
    idx = 0
    for k, values in d.items():
        delim = ", "
        if values == 0:
            values = []
        else:
            values = [f'u"\\x{x}"' for x in values]

        values.extend(["NULL"] *(10- len(values)))
        cog.out("  /")
        cog.out(f"* [{idx}] {ord(k)}/{k} *")
        cog.out(f"/ {{ {delim.join(values)} }}")
        if ord(k)!=last_index:
            cog.outl(",")
        idx = idx + 1
]]]*/
const uint16_t* latin_ex_map[26*2][10] PROGMEM = {
  /* [0] 65/A */ { u"\xC0", u"\xC1", u"\xC2", u"\xC3", u"\xC4", u"\xC5", u"\xC6", u"\x100", u"\x102", u"\x104" },
  /* [1] 66/B */ { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [2] 67/C */ { u"\xC7", u"\x106", u"\x108", u"\x10A", u"\x10C", NULL, NULL, NULL, NULL, NULL },
  /* [3] 68/D */ { u"\xD0", u"\x10E", u"\x110", NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [4] 69/E */ { u"\xC8", u"\xC9", u"\xCA", u"\xCB", u"\x112", u"\x114", u"\x116", u"\x118", u"\x11A", NULL },
  /* [5] 70/F */ { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [6] 71/G */ { u"\x11C", u"\x11E", u"\x120", u"\x122", NULL, NULL, NULL, NULL, NULL, NULL },
  /* [7] 72/H */ { u"\x124", u"\x126", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [8] 73/I */ { u"\xEC", u"\xED", u"\xEE", u"\xEF", u"\x129", u"\x12B", u"\x12D", u"\x12F", u"\x131", u"\x133" },
  /* [9] 74/J */ { u"\x134", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [10] 75/K */ { u"\x136", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [11] 76/L */ { u"\x139", u"\x13B", u"\x13D", u"\x13F", u"\x141", NULL, NULL, NULL, NULL, NULL },
  /* [12] 77/M */ { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [13] 78/N */ { u"\xD1", u"\x143", u"\x145", u"\x147", u"\x14A", NULL, NULL, NULL, NULL, NULL },
  /* [14] 79/O */ { u"\xD2", u"\xD3", u"\xD4", u"\xD5", u"\xD6", u"\x14C", u"\x14E", u"\x150", u"\x152", u"\xD8" },
  /* [15] 80/P */ { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [16] 81/Q */ { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [17] 82/R */ { u"\x154", u"\x156", u"\x158", NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [18] 83/S */ { u"\x1E9E", u"\x15A", u"\x15C", u"\x15E", u"\x160", NULL, NULL, NULL, NULL, NULL },
  /* [19] 84/T */ { u"\xDE", u"\x162", u"\x164", u"\x166", NULL, NULL, NULL, NULL, NULL, NULL },
  /* [20] 85/U */ { u"\xD9", u"\xDA", u"\xDB", u"\xDC", u"\x168", u"\x16A", u"\x16C", u"\x16E", u"\x170", u"\x172" },
  /* [21] 86/V */ { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [22] 87/W */ { u"\x174", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [23] 88/X */ { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [24] 89/Y */ { u"\xDD", u"\x176", u"\x178", NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [25] 90/Z */ { u"\x179", u"\x17B", u"\x17D", NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [26] 97/a */ { u"\xE0", u"\xE1", u"\xE2", u"\xE3", u"\xE4", u"\xE5", u"\xE6", u"\x101", u"\x103", u"\x105" },
  /* [27] 98/b */ { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [28] 99/c */ { u"\xE7", u"\x107", u"\x109", u"\x10B", u"\x10D", NULL, NULL, NULL, NULL, NULL },
  /* [29] 100/d */ { u"\xF0", u"\x10F", u"\x111", NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [30] 101/e */ { u"\xE8", u"\xE9", u"\xEA", u"\xEB", u"\x113", u"\x115", u"\x117", u"\x119", u"\x11B", NULL },
  /* [31] 102/f */ { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [32] 103/g */ { u"\x11D", u"\x11F", u"\x121", u"\x123", NULL, NULL, NULL, NULL, NULL, NULL },
  /* [33] 104/h */ { u"\x125", u"\x127", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [34] 105/i */ { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [35] 106/j */ { u"\x135", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [36] 107/k */ { u"\x137", u"\x138", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [37] 108/l */ { u"\x13A", u"\x13C", u"\x13E", u"\x140", u"\x142", NULL, NULL, NULL, NULL, NULL },
  /* [38] 109/m */ { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [39] 110/n */ { u"\xF1", u"\x144", u"\x146", u"\x148", u"\x14B", u"\x149", NULL, NULL, NULL, NULL },
  /* [40] 111/o */ { u"\xF2", u"\xF3", u"\xF4", u"\xF5", u"\xF6", u"\x14D", u"\x14F", u"\x151", u"\x153", u"\xF8" },
  /* [41] 112/p */ { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [42] 113/q */ { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [43] 114/r */ { u"\x155", u"\x157", u"\x159", NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [44] 115/s */ { u"\xDF", u"\x15B", u"\x15D", u"\x15F", u"\x161", NULL, NULL, NULL, NULL, NULL },
  /* [45] 116/t */ { u"\xFE", u"\x163", u"\x165", u"\x167", NULL, NULL, NULL, NULL, NULL, NULL },
  /* [46] 117/u */ { u"\xF9", u"\xFA", u"\xFB", u"\xFC", u"\x169", u"\x16B", u"\x16D", u"\x16F", u"\x171", u"\x173" },
  /* [47] 118/v */ { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [48] 119/w */ { u"\x175", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [49] 120/x */ { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [50] 121/y */ { u"\xFD", u"\x177", u"\xFF", NULL, NULL, NULL, NULL, NULL, NULL, NULL },
  /* [51] 122/z */ { u"\x17A", u"\x17C", u"\x17E", NULL, NULL, NULL, NULL, NULL, NULL, NULL }
//[[[end]]]
};

/*[[[cog
#cog.outl(f"static const uint16_t latin_rev_ex_map[26*2][{max_variation_index}] PROGMEM = {{")
#idx = 0
#for k, values in d.items():
#    delim = ", "
#    if values == 0:
#        values = []
#    else:
#        values = [f'{rev_lookup[x]}' for x in values]
#
#    values.extend(["0"] *(10- len(values)))
#    cog.out("  /")
#    cog.out(f"* [{idx}] {ord(k)}/{k} *")
#    cog.out(f"/ {{ {delim.join(values)} }}")
#    if ord(k)!=last_index:
#        cog.outl(",")
#    idx = idx + 1
]]]*/
// //[[[end]]]
// };
