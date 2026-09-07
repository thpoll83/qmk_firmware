// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The flash side of the handedness stamp. The reasoning, the decision table and
// the failure this exists to survive are all in hand_stamp.h.
#include "hand_stamp.h"

#include "quantum.h"          // eeconfig_read_handedness / eeconfig_update_handedness
#include "fw_staging.h"       // FW_HAND_STAMP_OFFSET, fw_staging_core1_lockout_*()
#include "crash_record.h"     // CRASH_PHASE_FLASH breadcrumb
#include "polymod_crc32.h"

#include "hardware/flash.h"
#include "hardware/sync.h"

#include <stddef.h>
#include <string.h>

#define STAMP_PAGES (FLASH_SECTOR_SIZE / FLASH_PAGE_SIZE)

static int8_t s_resolved = -1;      // -1 = not resolved yet, else 0/1
static bool   s_repair_ee = false;  // post_init owes the EEPROM byte a rewrite
static bool   s_ee_repaired = false;
static poly_hand_source_t s_source = POLY_HAND_SRC_EEPROM;
static bool   s_pending    = false;
static bool   s_pending_is_left = false;

// --- the sector -------------------------------------------------------------
static const poly_hand_stamp_t *stamp_page(uint32_t i) {
    return (const poly_hand_stamp_t *)(XIP_BASE + FW_HAND_STAMP_OFFSET + i * FLASH_PAGE_SIZE);
}

static bool stamp_valid(const poly_hand_stamp_t *p) {
    if (p->magic != POLY_HAND_STAMP_MAGIC) return false;
    if (p->is_left > 1) return false;
    return p->crc == crc32_1byte(p, (uint16_t)offsetof(poly_hand_stamp_t, crc), 0);
}

// "The last valid page." A torn page fails its CRC and is skipped, so the
// previous good one still answers -- which is the whole point of appending.
static bool stamp_read(bool *is_left) {
    bool found = false;
    for (uint32_t i = 0; i < STAMP_PAGES; i++) {
        const poly_hand_stamp_t *p = stamp_page(i);
        if (p->magic == 0xFFFFFFFFu) break;   // erased: nothing beyond this
        if (stamp_valid(p)) {
            *is_left = (p->is_left != 0);
            found    = true;
        }
    }
    return found;
}

// `lockout` = park core1 around the write (it serves RLE from XIP, so it must not
// be fetching while the bootrom rewrites flash). MUST be false from
// poly_hand_boot_init(): at pre_init core1 has never been launched, and the
// lockout's release would do a bounded relaunch that leaves post_init's own
// unbounded multicore_launch_core1() handshake blocked forever. Same rule, and
// the same reason, as crash_record.c's flash_guarded().
static void stamp_flash(bool erase, uint32_t off, const uint8_t *page, bool lockout) {
    uint32_t tag = crash_phase_enter(CRASH_PHASE_FLASH, erase ? 2 : 1);
    if (lockout) fw_staging_core1_lockout_begin();
    uint32_t irq = save_and_disable_interrupts();
    if (erase) {
        flash_range_erase(off, FLASH_SECTOR_SIZE);
    } else {
        flash_range_program(off, page, FLASH_PAGE_SIZE);
    }
    restore_interrupts(irq);
    if (lockout) fw_staging_core1_lockout_end();
    crash_phase_leave(tag);
}

static void stamp_write(bool is_left, bool lockout) {
    uint32_t slot = STAMP_PAGES;
    for (uint32_t i = 0; i < STAMP_PAGES; i++) {
        if (stamp_page(i)->magic == 0xFFFFFFFFu) { slot = i; break; }
    }
    if (slot == STAMP_PAGES) {
        // Sixteen changes since the last erase. Only reachable while somebody is
        // reassigning sides repeatedly, i.e. testing.
        stamp_flash(true, FW_HAND_STAMP_OFFSET, NULL, lockout);
        slot = 0;
    }

    poly_hand_stamp_t rec;
    memset(&rec, 0, sizeof(rec));
    rec.magic   = POLY_HAND_STAMP_MAGIC;
    rec.is_left = is_left ? 1 : 0;
    rec.crc     = crc32_1byte(&rec, (uint16_t)offsetof(poly_hand_stamp_t, crc), 0);

    uint8_t page[FLASH_PAGE_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &rec, sizeof(rec));
    stamp_flash(false, FW_HAND_STAMP_OFFSET + slot * FLASH_PAGE_SIZE, page, lockout);
}

// --- lifecycle --------------------------------------------------------------
static void resolve(bool may_migrate) {
    bool s_left = false;
    bool have   = stamp_read(&s_left);
    // Both reads are read-only, so neither can trigger the eeconfig_init() erase
    // that other routes into eeconfig can at this point in the boot.
    poly_hand_decision_t d = poly_hand_decide(have, s_left, eeconfig_is_enabled(), eeconfig_read_handedness());

    s_resolved  = d.is_left ? 1 : 0;
    s_repair_ee = d.repair_ee;

    if (have) {
        s_source = POLY_HAND_SRC_STAMP;
    } else if (d.migrate && may_migrate) {
        stamp_write(d.is_left, /*lockout=*/false);
        s_source = POLY_HAND_SRC_MIGRATED;
    } else {
        s_source = POLY_HAND_SRC_EEPROM;
    }
}

poly_hand_source_t poly_hand_source(void) {
    if (s_resolved < 0) resolve(/*may_migrate=*/false);
    return s_source;
}

bool poly_hand_ee_repaired(void) {
    return s_ee_repaired;
}

void poly_hand_boot_init(void) {
    resolve(/*may_migrate=*/true);
}

bool poly_hand_is_left(void) {
    // Resolve without migrating if something asks before pre_init: a flash write
    // from an unknown point in the boot is not worth the convenience, and
    // poly_hand_boot_init() will do it a moment later.
    if (s_resolved < 0) resolve(/*may_migrate=*/false);
    return s_resolved != 0;
}

void poly_hand_post_init(void) {
    if (!s_repair_ee) return;
    s_repair_ee   = false;
    s_ee_repaired = true;   // the banner reports it; this runs before emit_boot_banner()
    eeconfig_update_handedness(poly_hand_is_left());
}

void poly_hand_set_pending(bool is_left) {
    s_pending         = true;
    s_pending_is_left = is_left;
}

void poly_hand_flush_pending(void) {
    if (!s_pending) return;
    s_pending = false;
    eeconfig_update_handedness(s_pending_is_left);
    stamp_write(s_pending_is_left, /*lockout=*/true);
    s_resolved = s_pending_is_left ? 1 : 0;
}
