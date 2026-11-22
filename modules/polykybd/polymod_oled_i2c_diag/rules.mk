# Two sources and no polymod_oled_i2c_diag.c: the module build compiles a main
# <name>.c by its absolute path, which keyboards/polykybd/rules.mk cannot give its
# -Wextra flags (POLY_MODULE_WARN_SRC). The generated community_rules.mk
# `-include`s this file.
SRC += modules/polykybd/polymod_oled_i2c_diag/polymod_oled_i2c_diag_hw.c \
       modules/polykybd/polymod_oled_i2c_diag/polymod_oled_i2c_diag_log.c
