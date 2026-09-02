/* PolyKybd Doom dev-harness linker script (POLYKYBD_DOOM=yes builds only).
 *
 * Copy of platforms/chibios/boards/common/ld/RP2040_FLASH_TIMECRIT.ld with one
 * structural change: the INCLUDE of RP2040_rules_data_with_timecrit.ld is
 * inlined below so the .bss rule can be modified -- the doom engine's
 * zero-initialised statics (.bss + vintage COMMON tentative definitions,
 * ~21 KB across the whole game core) are placed in a dedicated .doom_shared
 * block that IS the keycap overlay pool (base/overlay.c aliases it under
 * POLYKYBD_DOOM). RAM is otherwise fully committed; sharing is safe because
 * the pool and the game are mutually exclusive by design (the pool is
 * blank-and-resend state, the study's "borrow, don't cut").
 *
 * Consequences encoded here:
 *  - .doom_shared is NOLOAD and OUTSIDE __bss_base__..__bss_end__, so crt0
 *    never zeroes it: doom_enter() memsets the whole block before the engine
 *    starts (fresh zero-init statics on every entry), and the overlay
 *    accessors never read it before the host writes it.
 *  - The block is padded to exactly the pool size (226,800 =
 *    NUM_OVERLAYS * NUM_VARIATIONS * 360); if the engine statics ever outgrow
 *    the pool the link fails ("cannot move location counter backwards")
 *    instead of silently shrinking the arena.
 */
/*
    ChibiOS - Copyright (C) 2006..2021 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

/*
 * RP2040 memory setup.
 */
MEMORY
{
    flash0 (rx) : org = 0x00000000, len = 16k   /* ROM                  */
    flash1 (rx) : org = 0x10000000, len = DEFINED(FLASH_LEN) ? FLASH_LEN : 2048k /* XIP */
    flash2 (rx) : org = 0x00000000, len = 0
    flash3 (rx) : org = 0x00000000, len = 0
    flash4 (rx) : org = 0x00000000, len = 0
    flash5 (rx) : org = 0x00000000, len = 0
    flash6 (rx) : org = 0x00000000, len = 0
    flash7 (rx) : org = 0x00000000, len = 0
    ram0   (wx) : org = 0x20000000, len = 256k  /* SRAM0 striped        */
    ram1   (wx) : org = 0x00000000, len = 256k  /* SRAM0 non striped    */
    ram2   (wx) : org = 0x00000000, len = 0
    ram3   (wx) : org = 0x00000000, len = 0
    ram4   (wx) : org = 0x20040000, len = 4k    /* SRAM4                */
    ram5   (wx) : org = 0x20041000, len = 4k    /* SRAM5                */
    ram6   (wx) : org = 0x00000000, len = 0
    ram7   (wx) : org = 0x20041f00, len = 256   /* SRAM5 boot           */
}

/* For each data/text section two region are defined, a virtual region
   and a load region (_LMA suffix).*/

/* Flash region to be used for exception vectors.*/
REGION_ALIAS("VECTORS_FLASH", flash1);
REGION_ALIAS("VECTORS_FLASH_LMA", flash1);

/* Flash region to be used for constructors and destructors.*/
REGION_ALIAS("XTORS_FLASH", flash1);
REGION_ALIAS("XTORS_FLASH_LMA", flash1);

/* Flash region to be used for code text.*/
REGION_ALIAS("TEXT_FLASH", flash1);
REGION_ALIAS("TEXT_FLASH_LMA", flash1);

/* Flash region to be used for read only data.*/
REGION_ALIAS("RODATA_FLASH", flash1);
REGION_ALIAS("RODATA_FLASH_LMA", flash1);

/* Flash region to be used for various.*/
REGION_ALIAS("VARIOUS_FLASH", flash1);
REGION_ALIAS("VARIOUS_FLASH_LMA", flash1);

/* Flash region to be used for RAM(n) initialization data.*/
REGION_ALIAS("RAM_INIT_FLASH_LMA", flash1);

/* RAM region to be used for Main stack. This stack accommodates the processing
   of all exceptions and interrupts.*/
REGION_ALIAS("MAIN_STACK_RAM", ram4);

/* RAM region to be used for the process stack. This is the stack used by
   the main() function.*/
REGION_ALIAS("PROCESS_STACK_RAM", ram4);

/* RAM region to be used for Main stack. This stack accommodates the processing
   of all exceptions and interrupts.*/
REGION_ALIAS("C1_MAIN_STACK_RAM", ram5);

/* RAM region to be used for the process stack. This is the stack used by
   the main() function.*/
REGION_ALIAS("C1_PROCESS_STACK_RAM", ram5);

/* RAM region to be used for data segment.*/
REGION_ALIAS("DATA_RAM", ram0);
REGION_ALIAS("DATA_RAM_LMA", flash1);

/* RAM region to be used for BSS segment.*/
REGION_ALIAS("BSS_RAM", ram0);

/* RAM region to be used for the default heap.*/
REGION_ALIAS("HEAP_RAM", ram0);

SECTIONS
{
   .flash_begin : {
      __flash_binary_start = .;
   } > flash1

   .boot2 : {
      __boot2_start__ = .;
      KEEP (*(.boot2))
      __boot2_end__ = .;
   } > flash1
}

/* Generic rules inclusion.*/
INCLUDE rules_stacks.ld
INCLUDE rules_stacks_c1.ld
INCLUDE RP2040_rules_code_with_boot2.ld

/* ---- inlined (modified) RP2040_rules_data_with_timecrit.ld ---- */
SECTIONS
{
    .data : ALIGN(4)
    {
        PROVIDE(_textdata = LOADADDR(.data));
        PROVIDE(_data = .);
        __textdata_base__ = LOADADDR(.data);
        __data_base__ = .;
        *(vtable)
        *(.time_critical*)
        . = ALIGN(4);
        *(.data)
        *(.data.*)
        *(.ramtext)
        . = ALIGN(4);
        PROVIDE(_edata = .);
        __data_end__ = .;
    } > DATA_RAM AT > DATA_RAM_LMA

    .bss (NOLOAD) : ALIGN(4)
    {
        __bss_base__ = .;
        *(EXCLUDE_FILE(*doom/engine/*.o) .bss)
        *(EXCLUDE_FILE(*doom/engine/*.o) .bss.*)
        *(EXCLUDE_FILE(*doom/engine/*.o) COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
        PROVIDE(end = .);
    } > BSS_RAM

    /* The overlay pool / doom arena shared block -- see file header.
     *
     * ALIGN(8), not 4: doom_mode.c carves the core1 stack out of the TAIL of
     * this block (__doom_shared_end__ - DOOM_ARENA_STACK_BYTES), and AAPCS
     * requires an 8-byte-aligned stack pointer. The block's size (216000) and
     * the stack size (4096) are both multiples of 8, so the end -- and hence
     * the stack base -- inherits whatever alignment the START has. At ALIGN(4)
     * that could be 4-aligned. The DOOMPACK flavour is unaffected because its
     * pool is pinned at the RAM origin, which is 8-aligned by construction.
     * (Caught in review of #264; doom_mode.c also masks the pointer down, so
     * neither half alone is load-bearing.) */
    .doom_shared (NOLOAD) : ALIGN(8)
    {
        __doom_shared_base__ = .;
        *doom/engine/*.o(.bss)
        *doom/engine/*.o(.bss.*)
        *doom/engine/*.o(COMMON)
        . = ALIGN(4);
        __doom_shared_statics_end__ = .;
        . = __doom_shared_base__ + 216000;
        __doom_shared_end__ = .;
    } > BSS_RAM
}
/* ---- end inlined rules ---- */

INCLUDE rules_memory.ld

SECTIONS
{
   .flash_end : {
      __flash_binary_end = .;
   } > flash1
}
