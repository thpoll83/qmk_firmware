/* PolyKybd DoomPack-flavour linker script (POLYKYBD_DOOM_PACK=yes builds).
 *
 * Copy of platforms/chibios/boards/common/ld/RP2040_FLASH_TIMECRIT.ld with one
 * structural change: the keycap overlay pool (base/overlay.c `overlays`,
 * section .overlay_pool) is PINNED AT THE RAM ORIGIN (0x20000000) instead of
 * floating inside .bss. That makes the DoomPack's RAM base a build-independent
 * CONSTANT: the engine pack (doom/pack/) links its statics at the pool
 * address, so with the pool pinned, a flashed .plyx keeps working across
 * firmware rebuilds — only ABI/feature changes ever require a new pack
 * (doom/PACK_DESIGN.md §4a). The loader still verifies hdr.ram_base against
 * the live pool as belt-and-braces.
 *
 * The pool block is NOLOAD and OUTSIDE __bss_base__..__bss_end__, so crt0
 * never zeroes it — same semantics as the monolith's .doom_shared: the
 * overlay usage bits gate every read and the reset/refill paths memset it,
 * and doom_session_start() memsets the whole pool at game entry. The block
 * is padded to exactly the pool size (226,800 = NUM_OVERLAYS *
 * NUM_VARIATIONS * 360); if the array ever outgrows it the link fails
 * loudly ("cannot move location counter backwards").
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
    /* The keycap overlay pool, pinned at the RAM origin -- see file header. */
    .overlay_pool (NOLOAD) : ALIGN(4)
    {
        __overlay_pool_base__ = .;
        *(.overlay_pool)
        . = __overlay_pool_base__ + 216000;
        __overlay_pool_end__ = .;
    } > BSS_RAM

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
        *(.bss)
        *(.bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
        PROVIDE(end = .);
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
