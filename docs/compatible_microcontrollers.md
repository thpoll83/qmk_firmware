# Compatible Microcontrollers

QMK runs on any USB-capable AVR or ARM microcontroller with enough flash space - generally 32kB+ for AVR, and 64kB+ for ARM. With significant disabling of features, QMK may *just* squeeze into 16kB AVR MCUs.

## Atmel AVR

The following use [LUFA](https://www.fourwalledcubicle.com/LUFA.php) as the USB stack:

* [ATmega16U2](https://www.microchip.com/wwwproducts/en/ATmega16U2) / [ATmega32U2](https://www.microchip.com/wwwproducts/en/ATmega32U2)
* [ATmega16U4](https://www.microchip.com/wwwproducts/en/ATmega16U4) / [ATmega32U4](https://www.microchip.com/wwwproducts/en/ATmega32U4)
  * SparkFun Pro Micro (and clones)
  * PJRC Teensy 2.0
  * Adafruit Feather 32U4
* [AT90USB64](https://www.microchip.com/wwwproducts/en/AT90USB646) / [AT90USB128](https://www.microchip.com/wwwproducts/en/AT90USB1286)
  * PJRC Teensy++ 2.0
* [AT90USB162](https://www.microchip.com/wwwproducts/en/AT90USB162)

Certain MCUs which do not have native USB will use [V-USB](https://www.obdev.at/products/vusb/index.html) instead:

* [ATmega32A](https://www.microchip.com/wwwproducts/en/ATmega32A)
* [ATmega328P](https://www.microchip.com/wwwproducts/en/ATmega328P)
* [ATmega328](https://www.microchip.com/wwwproducts/en/ATmega328)

## ARM

You can also use any ARM chip with USB that [ChibiOS](https://www.chibios.org) supports. Most have plenty of flash. Known to work are:

### STMicroelectronics (STM32)

 * [STM32F0x2](https://www.st.com/en/microcontrollers-microprocessors/stm32f0x2.html)
 * [STM32F103](https://www.st.com/en/microcontrollers-microprocessors/stm32f103.html)
   * Bluepill (with STM32duino bootloader)
 * [STM32F303](https://www.st.com/en/microcontrollers-microprocessors/stm32f303.html)
   * QMK Proton-C
 * [STM32F401](https://www.st.com/en/microcontrollers-microprocessors/stm32f401.html)
   * WeAct Blackpill
 * [STM32F405](https://www.st.com/en/microcontrollers-microprocessors/stm32f405-415.html)
 * [STM32F407](https://www.st.com/en/microcontrollers-microprocessors/stm32f407-417.html)
 * [STM32F411](https://www.st.com/en/microcontrollers-microprocessors/stm32f411.html)
   * WeAct Blackpill
 * [STM32F446](https://www.st.com/en/microcontrollers-microprocessors/stm32f446.html)
 * [STM32G431](https://www.st.com/en/microcontrollers-microprocessors/stm32g4x1.html)
 * [STM32G474](https://www.st.com/en/microcontrollers-microprocessors/stm32g4x4.html)
 * [STM32L412](https://www.st.com/en/microcontrollers-microprocessors/stm32l4x2.html)
 * [STM32L422](https://www.st.com/en/microcontrollers-microprocessors/stm32l4x2.html)
 * [STM32L432](https://www.st.com/en/microcontrollers-microprocessors/stm32l4x2.html)
 * [STM32L433](https://www.st.com/en/microcontrollers-microprocessors/stm32l4x3.html)
 * [STM32L442](https://www.st.com/en/microcontrollers-microprocessors/stm32l4x2.html)
 * [STM32L443](https://www.st.com/en/microcontrollers-microprocessors/stm32l4x3.html)

### WestBerryTech (WB32)

 * [WB32F3G71xx](http://www.westberrytech.com)

### NXP (Kinetis)

 * [MKL26Z64](https://www.nxp.com/products/processors-and-microcontrollers/arm-microcontrollers/general-purpose-mcus/kl-series-cortex-m0-plus/kinetis-kl2x-72-96-mhz-usb-ultra-low-power-microcontrollers-mcus-based-on-arm-cortex-m0-plus-core:KL2x)
   * PJRC Teensy LC
 * [MK20DX128](https://www.nxp.com/products/processors-and-microcontrollers/arm-microcontrollers/general-purpose-mcus/k-series-cortex-m4/k2x-usb/kinetis-k20-50-mhz-full-speed-usb-mixed-signal-integration-microcontrollers-based-on-arm-cortex-m4-core:K20_50)
 * [MK20DX256](https://www.nxp.com/products/processors-and-microcontrollers/arm-microcontrollers/general-purpose-mcus/k-series-cortex-m4/k2x-usb/kinetis-k20-72-mhz-full-speed-usb-mixed-signal-integration-microcontrollers-mcus-based-on-arm-cortex-m4-core:K20_72)
   * PJRC Teensy 3.2
 * [MK66FX1M0](https://www.nxp.com/products/processors-and-microcontrollers/arm-microcontrollers/general-purpose-mcus/k-series-cortex-m4/k6x-ethernet/kinetis-k66-180-mhz-dual-high-speed-full-speed-usbs-2mb-flash-microcontrollers-mcus-based-on-arm-cortex-m4-core:K66_180)
   * PJRC Teensy 3.6

### Raspberry PI RP2040

The following drivers are currently supported on RP2040 MCUs.

| System           | Support                                        |
| ---------------- | ---------------------------------------------- |
| ADC driver       | Support planned (no ETA)                       |
| Audio            | Support planned (no ETA)                       |
| I2C driver       | :heavy_check_mark:                             |
| SPI driver       | :heavy_check_mark:                             |
| WS2812 driver    | :heavy_check_mark: using `PIO` driver          |
| External EEPROMs | :heavy_check_mark: using `I2C` or `SPI` driver |
| EEPROM emulation | Support planned (no ETA)                       |
| serial driver    | :heavy_check_mark: using `SIO` or `PIO` driver |
| UART driver      | Support planned (no ETA)                       |

#### GPIO pin nomenclature

<img alt="Raspberry Pi Pico Pinout" src="assets/Pi_Pico_Pinout.png" width="48%"/>
<img alt="Adafruit KB2040 Pinout" src="assets/KB2040_Pinout.png" width="48%"/>

!> The GPIO pins of the RP2040 are not 5V tolerant!

To address individual pins QMK uses the `GPx` abbreviation, the `x` stands for the GPIO number of the pin. This number can be found on the official pinout diagramm of your board. Note that these GPIO numbers not necessarily match the number you see printed on the board. For instance the Raspberry Pi Pico uses numbers from 1 to 40 for their pins, but these are not identical the internal GPIO numbers. So if you want to use the pin 11 of the Pico for your keyboard, you would reefer to it as `GP8` in the config files.

#### Double-tap reset boot-loader entry

The double-tap reset mechanism is an alternate way in QMK to enter the embedded mass storage UF2 boot-loader of the RP2040. It works by a fast double-tap of the reset pin on start up, which is similar to the behavior of AVR Pro Micros. This feature is not activated by default and has to be configured. To activate it add the following options to your keyboards `config.h` file:

```c
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET // Activates the double-tap behavior
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_TIMEOUT 200U // Timeout window in ms in which the double tap can occur.
#define RP2040_BOOTLOADER_DOUBLE_TAP_RESET_LED_MASK 0U // Specify a optional status led which blinks when entering the bootloader
```

#### Split keyboard support

Split keyboards are fully supported using the [serial driver](serial_driver.md) in both Full-duplex and Half-duplex configurations. For this to work two driver subsystems are supported by the RP2040, the hardware UART based `SIO` and the Programmable IO based `PIO` driver.

| Feature                       | [SIO Driver](serial_driver.md#the-sio-driver) | [PIO Driver](serial_driver.md#the-pio-driver) |
| ----------------------------- | --------------------------------------------- | --------------------------------------------- |
| Half-Duplex operation         |                                               | :heavy_check_mark:                            |
| Full-Duplex operation         | :heavy_check_mark:                            | :heavy_check_mark:                            |
| `TX` and `RX` pin swapping    |                                               | :heavy_check_mark:                            |
| Any GPIO as `TX` and `RX` pin | Only UART capable pins                        | :heavy_check_mark:                            |
| Simple configuration          |                                               | :heavy_check_mark:                            |

The `PIO` driver is much more flexible then the `SIO` driver, the only "downside" is the usage of `PIO` resources which in turn are not available for avanced user programs.

## Atmel ATSAM

There is limited support for one of Atmel's ATSAM microcontrollers, that being the [ATSAMD51J18A](https://www.microchip.com/wwwproducts/en/ATSAMD51J18A) used by the [Massdrop keyboards](https://github.com/qmk/qmk_firmware/tree/master/keyboards/massdrop). However, it is not recommended to design a board with this microcontroller as the support is quite specialized to Massdrop hardware.

## RISC-V

### GigaDevice

[ChibiOS-Contrib](https://github.com/ChibiOS/ChibiOS-Contrib) has support for the GigaDevice [GD32VF103 series](https://www.gigadevice.com/products/microcontrollers/gd32/risc-v/mainstream-line/gd32vf103-series/) microcontrollers and provides configurations for the [SiPeed Longan Nano](https://longan.sipeed.com/en/) development board that uses this microcontroller. It is largely pin and feature compatible with STM32F103 and STM32F303 microcontrollers.
