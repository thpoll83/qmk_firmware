# PolyAtom (handwired)

Development version of a custom handwired four button key keyboard (PolyKeyboard-Atom).

Keyboard Maintainer: thpoll83  
Hardware Supported: STM32F407
Hardware Availability: https://stm32-base.org/boards/STM32F407VGT6-STM32F4XX-M.html

# Build Notes (only dev1 maintined for now)

$ make handwired/polyatom:dev1
$ qmk compile -kb handwired/polyatom -km dev1

## After merging master into branch update dependencies with

$ make git-submodule

## Check image size

$ size .build/handwired_polyatom_stm32f407_dev1.elf -B


**See each individual board for pin infomation**

Make example for this keyboard (after setting up your build environment):

    make handwired/polyatom:default

See the [build environment setup](https://docs.qmk.fm/#/getting_started_build_tools) and the [make instructions](https://docs.qmk.fm/#/getting_started_make_guide) for more information. Brand new to QMK? Start with our [Complete Newbs Guide](https://docs.qmk.fm/#/newbs).
