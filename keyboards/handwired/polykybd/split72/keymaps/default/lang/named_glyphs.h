//File content partially generated!
//Execute the following command to do so (via cogapp):
//cog -r named_glyphs.h
#pragma once

/*[[[cog
    import cog
    import os
    from openpyxl import load_workbook
    wb = load_workbook(filename = os.path.join(os.path.abspath(os.path.dirname(cog.inFile)), "lang_lut.xlsx"), data_only=True)
    sheet = wb['named_glyphs']

    glyph_index = 2
    glyph_key = sheet["A2"].value
    glyph_code = sheet["D2"].value
    while glyph_key and glyph_code:
        cog.outl(f"#define {glyph_key : <28}\tu\"\\x{glyph_code}\"")

        glyph_index = glyph_index + 1
        glyph_key = sheet[f"A{glyph_index}"].value
        glyph_code = sheet[f"D{glyph_index}"].value
    ]]]*/
#define SPACE                       	u"\x0020"
#define QUOTE                       	u"\x0022"
#define COMMA                       	u"\x002C"
#define ZERO                        	u"\x0030"
#define EQUALS                      	u"\x003D"
#define BACKSLASH                   	u"\x005C"
#define GRAVE_ACCENT                	u"\x0060"
#define ICON_LAYER                  	u"\x0080"
#define ICON_UP                     	u"\x0081"
#define ICON_DOWN                   	u"\x0082"
#define ICON_LEFT                   	u"\x0083"
#define ICON_RIGHT                  	u"\x0084"
#define ICON_SPACE                  	u"\x0085"
#define ICON_LMB                    	u"\x0086"
#define ICON_MMB                    	u"\x0087"
#define ICON_RMB                    	u"\x0088"
#define ICON_VOL_UP                 	u"\x0089"
#define ICON_VOL_DOWN               	u"\x008A"
#define ICON_BACKSPACE              	u"\x008B"
#define ICON_NUMLOCK_OFF            	u"\x008C"
#define ICON_NUMLOCK_ON             	u"\x008D"
#define ICON_CAPSLOCK_OFF           	u"\x008E"
#define ICON_CAPSLOCK_ON            	u"\x008F"
#define ICON_SHIFT                  	u"\x0090"
#define ICON_SWITCH_ON              	u"\x0091"
#define ICON_SWITCH_OFF             	u"\x0092"
#define ICON_BONGO_CAT              	u"\x0093"
#define INVERTED_EMARK              	u"\x00A1"
#define POUND_SIGN                  	u"\x00A3"
#define CURRENCY_SIGN               	u"\x00A4"
#define YEN_SIGN                    	u"\x00A5"
#define BROKEN_BAR                  	u"\x00A6"
#define SECTION                     	u"\x00A7"
#define DIAERESIS                   	u"\x00A8"
#define GREEK_DIALYTIKA             	u"\x00A8"
#define REGISTERED_SIGN             	u"\x00A9"
#define FEM_ORDINAL_IND             	u"\x00AA"
#define DBL_ANGLE_QMARK_L           	u"\x00AB"
#define NOT_SIGN                    	u"\x00AC"
#define COPYRIGHT_SIGN              	u"\x00AE"
#define DEGREE                      	u"\x00B0"
#define PLUS_MINUS                  	u"\x00B1"
#define SUPER_SCRIPT_2              	u"\x00B2"
#define SUPER_SCRIPT_3              	u"\x00B3"
#define ACUTE_ACCENT                	u"\x00B4"
#define MICRO_SIGN                  	u"\x00B5"
#define PILCROW                     	u"\x00B6"
#define MIDDLE_DOT                  	u"\x00B7"
#define MASC_ORDINAL_IND            	u"\x00BA"
#define DBL_ANGLE_QMARK_R           	u"\x00BB"
#define HALF_SIGN                   	u"\x00BD"
#define ONE_HALF                    	u"\x00BD"
#define INVERTED_QMARK              	u"\x00BF"
#define A_WITH_GRAVE                	u"\x00C0"
#define A_WITH_GRAVE                	u"\x00C0"
#define UMLAUT_A                    	u"\x00C4"
#define C_WITH_CEDILLA              	u"\x00C7"
#define E_WITH_GRAVE                	u"\x00C8"
#define E_WITH_ACUTE                	u"\x00C9"
#define I_WITH_GRAVE                	u"\x00CD"
#define I_WITH_ACUTE                	u"\x00CD"
#define I_WITH_CIRCUMF              	u"\x00CE"
#define N_WITH_TILDE                	u"\x00D1"
#define O_WITH_GRAVE                	u"\x00D2"
#define O_WITH_ACUTE                	u"\x00D3"
#define UMLAUT_O                    	u"\x00D6"
#define MUL_SIGN                    	u"\x00D7"
#define U_WITH_GRAVE                	u"\x00D9"
#define U_WITH_GRAVE                	u"\x00D9"
#define U_WITH_ACUTE                	u"\x00DA"
#define UMLAUT_U                    	u"\x00DC"
#define ESZETT                      	u"\x00DF"
#define A_WITH_GRAVE_SMALL          	u"\x00E0"
#define A_WITH_GRAVE_SMALL          	u"\x00E0"
#define UMLAUT_A_SMALL              	u"\x00E4"
#define LETTER_AE_SMALL             	u"\x00E6"
#define C_WITH_CEDILLA_SMALL        	u"\x00E7"
#define E_WITH_GRAVE_SMALL          	u"\x00E8"
#define E_WITH_ACUTE_SMALL          	u"\x00E9"
#define I_WITH_GRAVE_SMALL          	u"\x00EC"
#define I_WITH_ACUTE_SMALL          	u"\x00ED"
#define I_WITH_CIRCUMF_SMALL        	u"\x00EE"
#define N_WITH_TILDE_SMALL          	u"\x00F1"
#define O_WITH_GRAVE_SMALL          	u"\x00F2"
#define O_WITH_ACUTE_SMALL          	u"\x00F3"
#define UMLAUT_O_SMALL              	u"\x00F6"
#define DEVISION_SIGN               	u"\x00F7"
#define U_WITH_GRAVE_SMALL          	u"\x00F9"
#define U_WITH_GRAVE_SMALL          	u"\x00F9"
#define U_WITH_ACUTE_SMALL          	u"\x00FA"
#define UMLAUT_U_SMALL              	u"\x00FC"
#define G_WITH_BREVE                	u"\x011E"
#define G_WITH_BREVE_SMALL          	u"\x011F"
#define I_WITH_DOT                  	u"\x0130"
#define I_DOTLESS_SMALL             	u"\x0131"
#define S_WITH_CEDILLA              	u"\x015E"
#define S_WITH_CEDILLA_SMALL        	u"\x015F"
#define GREEK_SM_CHI                	u"\x02C7"
#define GREEK_TONOS                 	u"\x0384"
#define GREEK_DIALYTIKA_TONOS       	u"\x0385"
#define GREEK_ALPHA                 	u"\x0391"
#define GREEK_BETA                  	u"\x0392"
#define GREEK_GAMMA                 	u"\x0393"
#define GREEK_DELTA                 	u"\x0394"
#define GREEK_EPSILON               	u"\x0395"
#define GREEK_ZETA                  	u"\x0396"
#define GREEK_ETA                   	u"\x0397"
#define GREEK_THETA                 	u"\x0398"
#define GREEK_IOTA                  	u"\x0399"
#define GREEK_KAPPA                 	u"\x039A"
#define GREEK_LAMDA                 	u"\x039B"
#define GREEK_MU                    	u"\x039C"
#define GREEK_NU                    	u"\x039D"
#define GREEK_XI                    	u"\x039E"
#define GREEK_OMICRON               	u"\x039F"
#define GREEK_PI                    	u"\x03A0"
#define GREEK_RHO                   	u"\x03A1"
#define GREEK_SIGMA                 	u"\x03A3"
#define GREEK_TAU                   	u"\x03A4"
#define GREEK_UPSILON               	u"\x03A5"
#define GREEK_PHI                   	u"\x03A6"
#define GREEK_CHI                   	u"\x03A7"
#define GREEK_PSI                   	u"\x03A8"
#define GREEK_OMEGA                 	u"\x03A9"
#define GREEK_SM_ALPHA              	u"\x03B1"
#define GREEK_SM_BETA               	u"\x03B2"
#define GREEK_SM_GAMMA              	u"\x03B3"
#define GREEK_SM_DELTA              	u"\x03B4"
#define GREEK_SM_EPSILON            	u"\x03B5"
#define GREEK_SM_ZETA               	u"\x03B6"
#define GREEK_SM_ETA                	u"\x03B7"
#define GREEK_SM_THETA              	u"\x03B8"
#define GREEK_SM_IOTA               	u"\x03B9"
#define GREEK_SM_KAPPA              	u"\x03BA"
#define GREEK_SM_LAMDA              	u"\x03BB"
#define GREEK_SM_MU                 	u"\x03BC"
#define GREEK_SM_NU                 	u"\x03BD"
#define GREEK_SM_XI                 	u"\x03BE"
#define GREEK_SM_OMICRON            	u"\x03BF"
#define GREEK_SM_PI                 	u"\x03C0"
#define GREEK_SM_RHO                	u"\x03C1"
#define GREEK_SM_FINAL_SIGMA        	u"\x03C2"
#define GREEK_SM_SIGMA              	u"\x03C3"
#define GREEK_SM_TAU                	u"\x03C4"
#define GREEK_SM_UPSILON            	u"\x03C5"
#define GREEK_SM_PHI                	u"\x03C6"
#define GREEK_SM_PSI                	u"\x03C8"
#define GREEK_SM_OMEGA              	u"\x03C9"
#define ARABIC_COMMA                	u"\x060C"
#define ARABIC_SEMICOLON            	u"\x061B"
#define ARABIC_QMARK                	u"\x061F"
#define ARABIC_KASHMIRI_YEH         	u"\x0620"
#define ARABIC_HAMZA                	u"\x0621"
#define ARABIC_ALEF_MADDA_A         	u"\x0622"
#define ARABIC_ALEF_HAMZA_A         	u"\x0623"
#define ARABIC_WAW_HAMZA_A          	u"\x0624"
#define ARABIC_ALEF_HAMZA_B         	u"\x0625"
#define ARABIC_YEH_HAMZA_A          	u"\x0626"
#define ARABIC_ALEF                 	u"\x0627"
#define ARABIC_BEH                  	u"\x0628"
#define ARABIC_TEH_MARBUTA          	u"\x0629"
#define ARABIC_TEH                  	u"\x062A"
#define ARABIC_THEH                 	u"\x062B"
#define ARABIC_JEEM                 	u"\x062C"
#define ARABIC_HAH                  	u"\x062D"
#define ARABIC_KHAH                 	u"\x062E"
#define ARABIC_DAL                  	u"\x062F"
#define ARABIC_THAL                 	u"\x0630"
#define ARABIC_REH                  	u"\x0631"
#define ARABIC_ZAIN                 	u"\x0632"
#define ARABIC_SEEN                 	u"\x0633"
#define ARABIC_SHEEN                	u"\x0634"
#define ARABIC_SAD                  	u"\x0635"
#define ARABIC_DAD                  	u"\x0636"
#define ARABIC_TAH                  	u"\x0637"
#define ARABIC_ZAH                  	u"\x0638"
#define ARABIC_AIN                  	u"\x0639"
#define ARABIC_GHAIN                	u"\x063A"
#define ARABIC_KEHEH_2D_A           	u"\x063B"
#define ARABIC_KEHEH_3D_B           	u"\x063C"
#define ARABIC_FARSI_YEH_INV_V      	u"\x063D"
#define ARABIC_FARSI_YEH_2D_A       	u"\x063E"
#define ARABIC_FARSI_YEH_3D_A       	u"\x063F"
#define ARABIC_TATWEEL              	u"\x0640"
#define ARABIC_FEH                  	u"\x0641"
#define ARABIC_QAF                  	u"\x0642"
#define ARABIC_KAF                  	u"\x0643"
#define ARABIC_LAM                  	u"\x0644"
#define ARABIC_MEEM                 	u"\x0645"
#define ARABIC_NOON                 	u"\x0646"
#define ARABIC_HEH                  	u"\x0647"
#define ARABIC_WAW                  	u"\x0648"
#define ARABIC_ALEF_MAKSURA         	u"\x0649"
#define ARABIC_YEH                  	u"\x064A"
#define ARABIC_FATHATAN             	u"\x064B"
#define ARABIC_DAMMATAN             	u"\x064C"
#define ARABIC_KASRATAN             	u"\x064D"
#define ARABIC_FATHA                	u"\x064E"
#define ARABIC_DAMMA                	u"\x064F"
#define ARABIC_KASRA                	u"\x0650"
#define ARABIC_SHADDA               	u"\x0651"
#define ARABIC_SUKUN                	u"\x0652"
#define ARABIC_INDIC_0              	u"\x0660"
#define ARABIC_INDIC_1              	u"\x0661"
#define ARABIC_INDIC_2              	u"\x0662"
#define ARABIC_INDIC_3              	u"\x0663"
#define ARABIC_INDIC_4              	u"\x0664"
#define ARABIC_INDIC_5              	u"\x0665"
#define ARABIC_INDIC_6              	u"\x0666"
#define ARABIC_INDIC_7              	u"\x0667"
#define ARABIC_INDIC_8              	u"\x0668"
#define ARABIC_INDIC_9              	u"\x0669"
#define HANGEUL_KIYEOK              	u"\x1100"
#define HANGEUL_SSANGKIYEOK         	u"\x1101"
#define HANGEUL_NIEUN               	u"\x1102"
#define HANGEUL_TIKEUT              	u"\x1103"
#define HANGEUL_SSANGTIKEUT         	u"\x1104"
#define HANGEUL_RIEUL               	u"\x1105"
#define HANGEUL_MIEUM               	u"\x1106"
#define HANGEUL_PIEUP               	u"\x1107"
#define HANGEUL_SSANGPIEUP          	u"\x1108"
#define HANGEUL_SIOS                	u"\x1109"
#define HANGEUL_SSANGSIOS           	u"\x110A"
#define HANGEUL_IEUNG               	u"\x110B"
#define HANGEUL_CIEUC               	u"\x110C"
#define HANGEUL_SSANGCIEUC          	u"\x110D"
#define HANGEUL_CHIEUCH             	u"\x110E"
#define HANGEUL_KHIEUKH             	u"\x110F"
#define HANGEUL_THIEUTH             	u"\x1110"
#define HANGEUL_PHIEUPH             	u"\x1111"
#define HANGEUL_HIEUH               	u"\x1112"
#define HANGEUL_A                   	u"\x1161"
#define HANGEUL_AE                  	u"\x1162"
#define HANGEUL_YA                  	u"\x1163"
#define HANGEUL_YAE                 	u"\x1164"
#define HANGEUL_EO                  	u"\x1165"
#define HANGEUL_E                   	u"\x1166"
#define HANGEUL_YEO                 	u"\x1167"
#define HANGEUL_YE                  	u"\x1168"
#define HANGEUL_O                   	u"\x1169"
#define HANGEUL_YO                  	u"\x116D"
#define HANGEUL_U                   	u"\x116E"
#define HANGEUL_YU                  	u"\x1172"
#define HANGEUL_EU                  	u"\x1173"
#define HANGEUL_I                   	u"\x1175"
#define EURO_SIGN                   	u"\x20AC"
#define TECHNICAL_COMMAND           	u"\x2318"
#define TECHNICAL_OPTION            	u"\x2325"
#define TECHNICAL_ERASERIGHT        	u"\x2326"
#define TECHNICAL_ERASELEFT         	u"\x232B"
#define BOX_WITH_CHECK_MARK         	u"\x2611"
#define CLIPBOARD_CUT               	u"\x2702"
#define DINGBAT_BLACK_DIA_X         	u"\x2756"
#define ARROWS_CIRCLE               	u"\x2B6F"
#define ARROWS_LEFTSTOP             	u"\x2B70"
#define ARROWS_UPSTOP               	u"\x2B71"
#define ARROWS_RIGHTSTOP            	u"\x2B72"
#define ARROWS_DOWNSTOP             	u"\x2B73"
#define ARROWS_TAB                  	u"\x2B7E"
#define ARROWS_UNDO                 	u"\x2B8C"
#define ARROWS_REDO                 	u"\x2B8E"
#define ARROWS_RETURN               	u"\x2BA0"
#define CJK_IDEOG_COMMA             	u"\x3001"
#define CJK_IDEOG_FSTOP             	u"\x3002"
#define LCORNER_BRKT                	u"\x300C"
#define RCORNER_BRKT                	u"\x300D"
#define HIRAGANA_SMALL_A            	u"\x3041"
#define HIRAGANA_A                  	u"\x3042"
#define HIRAGANA_SMALL_I            	u"\x3043"
#define HIRAGANA_I                  	u"\x3044"
#define HIRAGANA_SMALL_U            	u"\x3045"
#define HIRAGANA_U                  	u"\x3046"
#define HIRAGANA_SMALL_E            	u"\x3047"
#define HIRAGANA_E                  	u"\x3048"
#define HIRAGANA_SMALL_O            	u"\x3049"
#define HIRAGANA_O                  	u"\x304A"
#define HIRAGANA_KA                 	u"\x304B"
#define HIRAGANA_GA                 	u"\x304C"
#define HIRAGANA_KI                 	u"\x304D"
#define HIRAGANA_GI                 	u"\x304E"
#define HIRAGANA_KU                 	u"\x304F"
#define HIRAGANA_GU                 	u"\x3050"
#define HIRAGANA_KE                 	u"\x3051"
#define HIRAGANA_GE                 	u"\x3052"
#define HIRAGANA_KO                 	u"\x3053"
#define HIRAGANA_GO                 	u"\x3054"
#define HIRAGANA_SA                 	u"\x3055"
#define HIRAGANA_ZA                 	u"\x3056"
#define HIRAGANA_SI                 	u"\x3057"
#define HIRAGANA_ZI                 	u"\x3058"
#define HIRAGANA_SU                 	u"\x3059"
#define HIRAGANA_ZU                 	u"\x305A"
#define HIRAGANA_SE                 	u"\x305B"
#define HIRAGANA_ZE                 	u"\x305C"
#define HIRAGANA_SO                 	u"\x305D"
#define HIRAGANA_ZO                 	u"\x305E"
#define HIRAGANA_TA                 	u"\x305F"
#define HIRAGANA_DA                 	u"\x3060"
#define HIRAGANA_TI                 	u"\x3061"
#define HIRAGANA_DI                 	u"\x3062"
#define HIRAGANA_SMALL_TU           	u"\x3063"
#define HIRAGANA_TU                 	u"\x3064"
#define HIRAGANA_DU                 	u"\x3065"
#define HIRAGANA_TE                 	u"\x3066"
#define HIRAGANA_DE                 	u"\x3067"
#define HIRAGANA_TO                 	u"\x3068"
#define HIRAGANA_DO                 	u"\x3069"
#define HIRAGANA_NA                 	u"\x306A"
#define HIRAGANA_NI                 	u"\x306B"
#define HIRAGANA_NU                 	u"\x306C"
#define HIRAGANA_NE                 	u"\x306D"
#define HIRAGANA_NO                 	u"\x306E"
#define HIRAGANA_HA                 	u"\x306F"
#define HIRAGANA_BA                 	u"\x3070"
#define HIRAGANA_PA                 	u"\x3071"
#define HIRAGANA_HI                 	u"\x3072"
#define HIRAGANA_BI                 	u"\x3073"
#define HIRAGANA_PI                 	u"\x3074"
#define HIRAGANA_HU                 	u"\x3075"
#define HIRAGANA_BU                 	u"\x3076"
#define HIRAGANA_PU                 	u"\x3077"
#define HIRAGANA_HE                 	u"\x3078"
#define HIRAGANA_BE                 	u"\x3079"
#define HIRAGANA_PE                 	u"\x307A"
#define HIRAGANA_HO                 	u"\x307B"
#define HIRAGANA_BO                 	u"\x307C"
#define HIRAGANA_PO                 	u"\x307D"
#define HIRAGANA_MA                 	u"\x307E"
#define HIRAGANA_MI                 	u"\x307F"
#define HIRAGANA_MU                 	u"\x3080"
#define HIRAGANA_ME                 	u"\x3081"
#define HIRAGANA_MO                 	u"\x3082"
#define HIRAGANA_SMALL_YA           	u"\x3083"
#define HIRAGANA_YA                 	u"\x3084"
#define HIRAGANA_SMALL_YU           	u"\x3085"
#define HIRAGANA_YU                 	u"\x3086"
#define HIRAGANA_SMALL_YO           	u"\x3087"
#define HIRAGANA_YO                 	u"\x3088"
#define HIRAGANA_RA                 	u"\x3089"
#define HIRAGANA_RI                 	u"\x308A"
#define HIRAGANA_RU                 	u"\x308B"
#define HIRAGANA_RE                 	u"\x308C"
#define HIRAGANA_RO                 	u"\x308D"
#define HIRAGANA_SMALL_WA           	u"\x308E"
#define HIRAGANA_WA                 	u"\x308F"
#define HIRAGANA_WI                 	u"\x3090"
#define HIRAGANA_WE                 	u"\x3091"
#define HIRAGANA_WO                 	u"\x3092"
#define HIRAGANA_N                  	u"\x3093"
#define HIRAGANA_VU                 	u"\x3094"
#define HIRAGANA_SMALL_KA           	u"\x3095"
#define HIRAGANA_SMALL_KE           	u"\x3096"
#define CMB_KH_VSOUNDM              	u"\x3099"
#define CMB_KH_SEMI_VSOUNDM         	u"\x309A"
#define KH_VSOUNDM                  	u"\x309B"
#define KH_SEMI_VSOUNDM             	u"\x309C"
#define HIRAGANA_ITERATIONM         	u"\x309D"
#define HIRAGANA_VITERATIONM        	u"\x309E"
#define HIRAGANA_DIGRAPH_YORI       	u"\x309F"
#define KATAKANA_MIDDLE_DOT         	u"\x30FB"
#define KH_PSOUNDM                  	u"\x30FC"
#define PRIVATE_AI                  	u"\xE300"
#define PRIVATE_WORLD               	u"\xE310"
#define PRIVATE_DISP_BRIGHT         	u"\xE311"
#define PRIVATE_DISP_DARK           	u"\xE315"
#define PRIVATE_DISP_DARKER         	u"\xE316"
#define PRIVATE_DISP_HALF           	u"\xE317"
#define PRIVATE_DISP_BRIGHTER       	u"\xE318"
#define PRIVATE_HOME                	u"\xE3E0"
#define PRIVATE_MEH                 	u"\xE410"
#define PRIVATE_LIGHT               	u"\xE4A1"
#define PRIVATE_HYPER               	u"\xE4AB"
#define FILE_OPEN                   	u"\xE4C2"
#define CLIPBOARD_COPY              	u"\xE4CB"
#define PRIVATE_NOTE                	u"\xE4DD"
#define PRIVATE_FIND                	u"\xE50E"
#define PRIVATE_LOCK                	u"\xE512"
#define PRIVATE_SETTINGS            	u"\xE527"
#define PRIVATE_MUTE                	u"\xE568"
#define PRIVATE_VOL_DOWN            	u"\xE569"
#define PRIVATE_VOL_UP              	u"\xE56A"
#define PRIVATE_CALC                	u"\xE5A9"
#define PRIVATE_FLOPPY              	u"\xE5AB"
#define PRIVATE_PC                  	u"\xE5B3"
#define PRIVATE_HDD                 	u"\xE5B4"
#define PRIVATE_SCREEN              	u"\xE5B5"
#define PRIVATE_PRINTER             	u"\xE5B6"
#define PRIVATE_FAX                 	u"\xE5B7"
#define PRIVATE_CD                  	u"\xE5B8"
#define PRIVATE_DOCUMENT            	u"\xE5B9"
#define PRIVATE_MIXED_DOCUMENT      	u"\xE5BA"
#define PRIVATE_IMAGE_DOCUMENT      	u"\xE5BB"
#define PRIVATE_IMAGE               	u"\xE5BC"
#define CLIPBOARD_PASTE             	u"\xE5CA"
#define PRIVATE_MINIMIZE            	u"\xE5D5"
#define PRIVATE_MAXIMIZE            	u"\xE5D6"
#define PRIVATE_WINDOW              	u"\xE5D7"
#define PRIVATE_RELOAD              	u"\xE5D8"
#define PRIVATE_DELETE              	u"\xE5D9"
#define PRIVATE_FONT_INC            	u"\xE5DA"
#define PRIVATE_FONT_DEC            	u"\xE5DB"
#define PRIVATE_EMOJI_1F600         	u"\xF600"
#define PRIVATE_EMOJI_1F601         	u"\xF601"
#define PRIVATE_EMOJI_1F602         	u"\xF602"
#define PRIVATE_EMOJI_1F603         	u"\xF603"
#define PRIVATE_EMOJI_1F604         	u"\xF604"
#define PRIVATE_EMOJI_1F605         	u"\xF605"
#define PRIVATE_EMOJI_1F606         	u"\xF606"
#define PRIVATE_EMOJI_1F607         	u"\xF607"
#define PRIVATE_EMOJI_1F608         	u"\xF608"
#define PRIVATE_EMOJI_1F609         	u"\xF609"
#define PRIVATE_EMOJI_1F60A         	u"\xF60A"
#define PRIVATE_EMOJI_1F60B         	u"\xF60B"
#define PRIVATE_EMOJI_1F60C         	u"\xF60C"
#define PRIVATE_EMOJI_1F60D         	u"\xF60D"
#define PRIVATE_EMOJI_1F60E         	u"\xF60E"
#define PRIVATE_EMOJI_1F60F         	u"\xF60F"
#define PRIVATE_EMOJI_1F610         	u"\xF610"
#define PRIVATE_EMOJI_1F611         	u"\xF611"
#define PRIVATE_EMOJI_1F612         	u"\xF612"
#define PRIVATE_EMOJI_1F613         	u"\xF613"
#define PRIVATE_EMOJI_1F614         	u"\xF614"
#define PRIVATE_EMOJI_1F615         	u"\xF615"
#define PRIVATE_EMOJI_1F616         	u"\xF616"
#define PRIVATE_EMOJI_1F617         	u"\xF617"
#define PRIVATE_EMOJI_1F618         	u"\xF618"
#define PRIVATE_EMOJI_1F619         	u"\xF619"
#define PRIVATE_EMOJI_1F61A         	u"\xF61A"
#define PRIVATE_EMOJI_1F61B         	u"\xF61B"
#define PRIVATE_EMOJI_1F61C         	u"\xF61C"
#define PRIVATE_EMOJI_1F61D         	u"\xF61D"
#define PRIVATE_EMOJI_1F61E         	u"\xF61E"
#define PRIVATE_EMOJI_1F61F         	u"\xF61F"
#define PRIVATE_EMOJI_1F620         	u"\xF620"
#define PRIVATE_EMOJI_1F621         	u"\xF621"
#define PRIVATE_EMOJI_1F622         	u"\xF622"
#define PRIVATE_EMOJI_1F623         	u"\xF623"
#define PRIVATE_EMOJI_1F624         	u"\xF624"
#define PRIVATE_EMOJI_1F625         	u"\xF625"
#define PRIVATE_EMOJI_1F626         	u"\xF626"
#define PRIVATE_EMOJI_1F627         	u"\xF627"
#define PRIVATE_EMOJI_1F628         	u"\xF628"
#define PRIVATE_EMOJI_1F629         	u"\xF629"
#define PRIVATE_EMOJI_1F62A         	u"\xF62A"
#define PRIVATE_EMOJI_1F62B         	u"\xF62B"
#define PRIVATE_EMOJI_1F62C         	u"\xF62C"
#define PRIVATE_EMOJI_1F62D         	u"\xF62D"
#define PRIVATE_EMOJI_1F62E         	u"\xF62E"
#define PRIVATE_EMOJI_1F62F         	u"\xF62F"
#define PRIVATE_EMOJI_1F630         	u"\xF630"
#define PRIVATE_EMOJI_1F631         	u"\xF631"
#define PRIVATE_EMOJI_1F632         	u"\xF632"
#define PRIVATE_EMOJI_1F633         	u"\xF633"
#define PRIVATE_EMOJI_1F634         	u"\xF634"
#define PRIVATE_EMOJI_1F635         	u"\xF635"
#define PRIVATE_EMOJI_1F636         	u"\xF636"
#define PRIVATE_EMOJI_1F637         	u"\xF637"
#define PRIVATE_EMOJI_1F638         	u"\xF638"
#define PRIVATE_EMOJI_1F639         	u"\xF639"
#define PRIVATE_EMOJI_1F644         	u"\xF644"
#define PRIVATE_EMOJI_1F645         	u"\xF645"
#define PRIVATE_EMOJI_1F646         	u"\xF646"
#define PRIVATE_EMOJI_1F647         	u"\xF647"
#define PRIVATE_EMOJI_1F648         	u"\xF648"
#define PRIVATE_EMOJI_1F649         	u"\xF649"
#define PRIVATE_EMOJI_1F64A         	u"\xF64A"
#define PRIVATE_EMOJI_1F64B         	u"\xF64B"
#define PRIVATE_EMOJI_1F64C         	u"\xF64C"
#define PRIVATE_EMOJI_1F64D         	u"\xF64D"
#define PRIVATE_EMOJI_1F64E         	u"\xF64E"
#define PRIVATE_EMOJI_1F64F         	u"\xF64F"
#define PRIVATE_EMOJI_1F440         	u"\xF740"
#define PRIVATE_EMOJI_1F441         	u"\xF741"
#define PRIVATE_EMOJI_1F442         	u"\xF742"
#define PRIVATE_EMOJI_1F443         	u"\xF743"
#define PRIVATE_EMOJI_1F444         	u"\xF744"
#define PRIVATE_EMOJI_1F445         	u"\xF745"
#define PRIVATE_EMOJI_1F446         	u"\xF746"
#define PRIVATE_EMOJI_1F447         	u"\xF747"
#define PRIVATE_EMOJI_1F448         	u"\xF748"
#define PRIVATE_EMOJI_1F449         	u"\xF749"
#define PRIVATE_EMOJI_1F44A         	u"\xF74A"
#define PRIVATE_EMOJI_1F44B         	u"\xF74B"
#define PRIVATE_EMOJI_1F44C         	u"\xF74C"
#define PRIVATE_EMOJI_1F44D         	u"\xF74D"
#define PRIVATE_EMOJI_1F44E         	u"\xF74E"
#define PRIVATE_EMOJI_1F44F         	u"\xF74F"
#define PRIVATE_EMOJI_1F450         	u"\xF750"
#define PRIVATE_EMOJI_1F451         	u"\xF751"
#define PRIVATE_EMOJI_1F452         	u"\xF752"
#define PRIVATE_EMOJI_1F453         	u"\xF753"
#define PRIVATE_EMOJI_1F47B         	u"\xF77B"
#define PRIVATE_EMOJI_1F47C         	u"\xF77C"
#define PRIVATE_EMOJI_1F47D         	u"\xF77D"
#define PRIVATE_EMOJI_1F47E         	u"\xF77E"
#define PRIVATE_EMOJI_1F47F         	u"\xF77F"
#define PRIVATE_EMOJI_1F480         	u"\xF780"
#define PRIVATE_EMOJI_1F481         	u"\xF781"
#define PRIVATE_EMOJI_1F482         	u"\xF782"
#define PRIVATE_EMOJI_1F483         	u"\xF783"
#define PRIVATE_EMOJI_1F484         	u"\xF784"
#define PRIVATE_EMOJI_1F485         	u"\xF785"
#define PRIVATE_EMOJI_1F489         	u"\xF789"
#define PRIVATE_EMOJI_1F48A         	u"\xF78A"
#define PRIVATE_EMOJI_1F48B         	u"\xF78B"
#define PRIVATE_EMOJI_1F48C         	u"\xF78C"
#define PRIVATE_EMOJI_1F48D         	u"\xF78D"
#define PRIVATE_EMOJI_1F48E         	u"\xF78E"
#define PRIVATE_EMOJI_1F48F         	u"\xF78F"
#define PRIVATE_EMOJI_1F490         	u"\xF790"
#define PRIVATE_EMOJI_1F491         	u"\xF791"
#define PRIVATE_EMOJI_1F492         	u"\xF792"
#define PRIVATE_EMOJI_1F493         	u"\xF793"
#define PRIVATE_EMOJI_1F494         	u"\xF794"
#define PRIVATE_EMOJI_1F495         	u"\xF795"
#define PRIVATE_EMOJI_1F496         	u"\xF796"
#define PRIVATE_EMOJI_1F4A1         	u"\xF7A1"
#define PRIVATE_EMOJI_1F4A2         	u"\xF7A2"
#define PRIVATE_EMOJI_1F4A3         	u"\xF7A3"
#define PRIVATE_EMOJI_1F4A4         	u"\xF7A4"
#define PRIVATE_EMOJI_1F4A5         	u"\xF7A5"
#define PRIVATE_EMOJI_1F4A6         	u"\xF7A6"
#define PRIVATE_EMOJI_1F4A7         	u"\xF7A7"
#define PRIVATE_EMOJI_1F4A8         	u"\xF7A8"
#define PRIVATE_EMOJI_1F4A9         	u"\xF7A9"
#define PRIVATE_EMOJI_1F4AA         	u"\xF7AA"
#define PRIVATE_EMOJI_1F4AB         	u"\xF7AB"
#define PRIVATE_EMOJI_1F4AC         	u"\xF7AC"
#define PRIVATE_EMOJI_1F4AD         	u"\xF7AD"
#define PRIVATE_EMOJI_1F4AE         	u"\xF7AE"
#define PRIVATE_EMOJI_1F4AF         	u"\xF7AF"
#define PRIVATE_EMOJI_1F4B0         	u"\xF7B0"
#define PRIVATE_EMOJI_1F4B1         	u"\xF7B1"
#define PRIVATE_EMOJI_1F912         	u"\xF812"
#define PRIVATE_EMOJI_1F913         	u"\xF813"
#define PRIVATE_EMOJI_1F914         	u"\xF814"
#define PRIVATE_EMOJI_1F915         	u"\xF815"
#define PRIVATE_EMOJI_1F916         	u"\xF816"
#define PRIVATE_EMOJI_1F917         	u"\xF817"
#define PRIVATE_EMOJI_1F918         	u"\xF818"
#define PRIVATE_EMOJI_1F919         	u"\xF819"
#define ARABIC_LAM_ALEF_MADDA       	u"\xFEF5"
#define ARABIC_LAM_ALEF             	u"\xFEFB"
//[[[end]]]
