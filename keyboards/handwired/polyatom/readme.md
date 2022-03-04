# PolyAtom

Development version of PolyKeyboard-Atom, which uses one OLED display per keycap.
Hardware info at https://github.com/thpoll83/PolyKeyboard/tree/master/poly_kb_atom

Keyboard Maintainer: thpoll83  
Hardware Supported: STM32F407
Hardware Availability: https://stm32-base.org/boards/STM32F407VGT6-STM32F4XX-M.html

# Build Notes

## Clean

$ make clean
or
$ rm -rf .build

## Build
$ make handwired/polyatom/4x5:default
$ qmk compile -kb handwired/polyatom/4x5 -km default

## After merging master into branch update dependencies with

$ make git-submodule

## Check image size

$ size .build//handwired_polyatom_4x5_stm32f407_default.elf -B

# General

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).
