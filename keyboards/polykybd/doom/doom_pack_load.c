// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// DoomPack loader — the firmware half of the executable-pack split
// (doom/PACK_DESIGN.md; compiled only under POLYKYBD_DOOM_PACK). Validates
// the PlyX pack flashed at FW_DOOMPACK_SLOT_OFF and dispatches every
// doom_shim_* call through its export table. Refusal is always safe: the
// stub table below answers every call with a no-op, and doom_mode.c then
// runs the fire demo exactly like a missing WHX.
#include QMK_KEYBOARD_H

#include "doom_mode.h"
#include "doom_arena.h" // doom_arena_at (the pool carve handed to the pack)
#include "doom_pack_abi.h"
#include "doom_pack_gate.h" // the unsigned/invalid x interactive/automatic decision

#include "base/fw_staging.h"
#include "bridge_helper.h"   // is_usb_host_side()
#include "state.h"           // get_local_state() -> the synced doom_pack_auth
#include "polymod_crc32.h"

#ifdef FW_REQUIRE_SIGNATURE
// FW-9: the pack is executable code, so it gets the same Ed25519 gate as the
// firmware image — verified HERE, at load time, not at flash COMMIT (flash can
// be rewritten after a COMMIT succeeds, so a "was validated once" flag is not a
// control). The __has_include fallback bridges the vendored Monocypher's move
// into the polymod_monocypher module (#242) — drop it once that lands.
#    if defined(__has_include) && __has_include("monocypher-ed25519.h")
#        include "monocypher-ed25519.h"
#    else
#        include "base/crypto/monocypher-ed25519.h"
#    endif
#    include "base/fw_pubkey.h"
#endif

#include <string.h>

// FW-9: a 64-byte Ed25519 signature over (header || image) — the header too, or
// a signed image could be re-targeted by editing entry_off/ram_base around it —
// trails the image at slot + DOOM_PACK_HDR_SIZE + image_size. Emitted by
// tools/sign_doompack.py; release.yml signs the .plyx beside the .bin.
#define DOOM_PACK_SIG_SIZE 64u

// ── Stub table: every call safe before a pack is loaded ─────────────────────
static void     stub_void(void) {}
static void     stub_set_role(bool m) { (void)m; }
static bool     stub_false(void) { return false; }
static int      stub_int0(void) { return 0; }
static unsigned stub_uint0(void) { return 0; }
static int      stub_face_index(void) { return -1; }
static void     stub_compose_line(uint8_t *line, unsigned y) { (void)line; (void)y; }
static bool     stub_hud_stats(int *h, int *a, int *m) { (void)h; (void)a; (void)m; return false; }
static bool     stub_weapon_state(uint8_t *o, uint8_t *r) { (void)o; (void)r; return false; }
static bool     stub_glyph(uint8_t g, uint8_t *o, uint8_t *w, uint8_t *h) {
    (void)g; (void)o; (void)w; (void)h;
    return false;
}
static int  stub_menu_snapshot(uint16_t *i, int n, int *s) { (void)i; (void)n; (void)s; return 0; }
static bool stub_menu_key_tile(uint8_t vr, uint8_t vc, uint8_t *t, const uint8_t *l, bool a) {
    (void)vr; (void)vc; (void)t; (void)l; (void)a;
    return false;
}
static bool stub_face_oled(uint8_t *b, const uint8_t *l) { (void)b; (void)l; return false; }
static void stub_engine_main(void) {
    // Never launched (doom_engine_start refuses without a loaded pack) —
    // spin defensively rather than return into a dead core1 stack.
    for (;;) {
        __asm volatile("wfe");
    }
}

static volatile uint8_t  s_stub_progress;
static volatile uint32_t s_stub_snd_fire, s_stub_snd_world;

static const doom_pack_api_t s_stub_api = {
    .size            = sizeof(doom_pack_api_t),
    .version         = 0,
    .engine_main     = stub_engine_main,
    .set_role        = stub_set_role,
    .take_frame      = stub_false,
    .release_frame   = stub_void,
    .compose_begin   = stub_void,
    .compose_line    = stub_compose_line,
    .drain_core1_log = stub_void,
    .progress        = &s_stub_progress,
    .gametic         = stub_int0,
    .video_type      = stub_uint0,
    .hud_stats       = stub_hud_stats,
    .weapon_state    = stub_weapon_state,
    .attract_active  = stub_false,
    .quit_requested  = stub_false,
    .slave_view_live = stub_false,
    .slave_wants_map_key = stub_false,
    .mirror_engaged  = stub_false,
    .drone_map_live  = stub_false,
    .tallnum_glyph   = stub_glyph,
    .hufont_glyph    = stub_glyph,
    .menu_snapshot   = stub_menu_snapshot,
    .menu_key_tile   = stub_menu_key_tile,
    .face_index      = stub_face_index,
    .face_oled       = stub_face_oled,
    .snd_fire        = &s_stub_snd_fire,
    .snd_world       = &s_stub_snd_world,
};

// ── Firmware services handed to the pack ────────────────────────────────────
// putchar_ is lib/printf's output funnel (quantum/logging/print.c) — the
// pack's core0 prints land in the same HID console as everything else. The
// pack's core1 never calls this (its printf goes to the pack-internal relay
// ring, drained by drain_core1_log on core0).
extern void putchar_(char c);

static void fw_console_putc(char c) {
    putchar_(c);
}

static const doom_fw_api_t s_fw_api = {
    .size          = sizeof(doom_fw_api_t),
    .version       = 1,
    .arena_at      = doom_arena_at,
    .arena_zone    = doom_arena_zone,
    .pop_key_event = doom_pop_key_event,
    .console_putc  = fw_console_putc,
};

// ── Loader state ─────────────────────────────────────────────────────────────
static const doom_pack_api_t *s_api = &s_stub_api;
static uint32_t               s_arena_off;

const doom_pack_api_t *doom_pack(void) {
    return s_api;
}

bool doom_pack_loaded(void) {
    return s_api != &s_stub_api;
}

uint32_t doom_pack_arena_off(void) {
    return s_arena_off;
}

void doom_pack_unload(void) {
    s_api       = &s_stub_api;
    s_arena_off = 0;
}

// ── FW-9 unsigned-pack prompt ───────────────────────────────────────────────
// A state machine, never a wait. doom_pack_load() runs on the loop that scans the
// matrix, so blocking for the answer would guarantee the keypress is never seen —
// the same trap FW_UP_COMMIT avoids by answering '?' until resolved.
//
// The window matches FW-2's (FW_CONFIRM_WINDOW_MS): long enough to read the
// keycaps and decide, short enough that an unattended board falls back to
// refusing. RAM-only and re-armed per attempt.
enum { PACK_CONFIRM_IDLE = 0, PACK_CONFIRM_PENDING, PACK_CONFIRM_ACCEPTED };

static uint8_t          s_pack_confirm    = PACK_CONFIRM_IDLE;
static uint32_t         s_pack_confirm_at = 0;
static uint32_t         s_pack_confirm_crc = 0;   // which pack the dialog is about
static doom_pack_auth_t s_auth             = {0}; // what was accepted this boot

// ── The refusal is STICKY until an input changes ────────────────────────────
// ⚠️ doom_slave_tick() calls this every housekeeping pass while doom_ctl is set,
// and before FW-9 could prompt, that loop was unreachable with an unsigned pack:
// the master refused, so doom_ctl was never set and the slave never tried. Now
// the master CAN enter, so a slave that refuses re-derives the same answer every
// pass — a 210 KB CRC walk each time, and the memset of the whole pool before it.
//
// On hardware that presented as "the slave rebooted while the master was already
// running DOOM": ~8 s of split-link failures (transport_fail 9 -> 35), including
// the syncs carrying the very authorisation that would have ended the loop, so it
// sustained itself until one got through. Same pack + same authorisation = same
// verdict, so derive it once and latch it.
static bool                 s_refused_valid = false;
static uint32_t             s_refused_crc   = 0;
static uint32_t             s_refused_auth  = 0;
static enum doom_pack_entry s_refused_entry = DOOM_PACK_ENTRY_INTERACTIVE;

// ⚠️ The key must carry EVERY input doom_pack_gate() reads, and `entry` is one of
// them — an unsigned pack is REFUSED automatically and PROMPTED interactively.
// Leaving it out latched the screensaver's refusal under the deliberate entry's
// key, so on a board whose idle style is the DOOM attract demo the very first
// idle timeout silently disarmed the prompt for the rest of the boot: pressing
// KC_IDDQD returned here and never reached the gate. Caught in review of #298.
static void pack_refusal_latch(uint32_t image_crc, uint32_t auth_crc, enum doom_pack_entry entry) {
    s_refused_valid = true;
    s_refused_crc   = image_crc;
    s_refused_auth  = auth_crc;
    s_refused_entry = entry;
}

static bool pack_refusal_still_stands(uint32_t image_crc, uint32_t auth_crc, enum doom_pack_entry entry) {
    return s_refused_valid && s_refused_crc == image_crc && s_refused_auth == auth_crc &&
           s_refused_entry == entry;
}

static void pack_refusal_clear(void) {
    s_refused_valid = false;
}

// ⚠️ The latch is keyed on the pack's declared image_crc, and the SIGNATURE
// TRAILER sits outside it — so re-flashing the same image with a signature
// attached leaves the key identical and a cached refusal would outlive the thing
// it was about. Anything that rewrites the slot therefore drops the latch, and the
// staging finalize calls this on both halves. Also caught in review of #298.
void doom_pack_slot_rewritten(void) {
    pack_refusal_clear();
}

void doom_pack_confirm_arm(uint32_t image_crc) {
    if (s_pack_confirm == PACK_CONFIRM_PENDING) return;  // already asking about this one
    s_pack_confirm     = PACK_CONFIRM_PENDING;
    s_pack_confirm_at  = timer_read32();
    s_pack_confirm_crc = image_crc;
}

bool doom_pack_confirm_pending(void) {
    return s_pack_confirm == PACK_CONFIRM_PENDING;
}

void doom_pack_confirm_answer(bool accept) {
    if (s_pack_confirm != PACK_CONFIRM_PENDING) return;  // first answer wins; ignore stray keys
    if (accept) {
        // Bind the authorisation to the pack it was given for, so re-flashing a
        // different unsigned pack asks again (doom_pack_gate.h).
        s_auth.valid     = true;
        s_auth.image_crc = s_pack_confirm_crc;
        s_pack_confirm   = PACK_CONFIRM_ACCEPTED;
        pack_refusal_clear();   // the answer is exactly the input that changed
    } else {
        s_pack_confirm = PACK_CONFIRM_IDLE;
    }
    printf("doom: unsigned pack %s on the keyboard\n", accept ? "ACCEPTED" : "REJECTED");
}

void doom_pack_confirm_tick(void) {
    // timer_elapsed32() is modular, so this stays correct across the 49.7-day
    // wrap — the trap that once disabled idle for a 25-day window.
    if (s_pack_confirm == PACK_CONFIRM_PENDING &&
        timer_elapsed32(s_pack_confirm_at) >= FW_CONFIRM_WINDOW_MS) {
        s_pack_confirm = PACK_CONFIRM_IDLE;
        printf("doom: unsigned-pack confirmation timed out — refusing\n");
    }
}

// 0 means "nothing accepted this boot". A pack whose image_crc is genuinely 0
// therefore reads as unaccepted on the slave and is refused — fail-closed, which
// is the only direction this may fail in.
uint32_t doom_pack_auth_crc(void) {
    return s_auth.valid ? s_auth.image_crc : 0u;
}

bool doom_pack_confirm_take_accepted(void) {
    if (s_pack_confirm != PACK_CONFIRM_ACCEPTED) return false;
    s_pack_confirm = PACK_CONFIRM_IDLE;   // one-shot: the retry happens once
    return true;
}

// Validate the flashed pack against THIS build's pool and call its init.
// `pool`/`pool_size` are the live borrowed pool — the pack must have been
// linked against exactly this address range (PACK_DESIGN.md §4). Every
// refusal prints its reason once per attempt; the caller falls back to the
// fire demo.
bool doom_pack_load(uint8_t *pool, uint32_t pool_size, enum doom_pack_entry entry) {
    doom_pack_unload();

    const uint8_t         *slot = (const uint8_t *)(XIP_BASE + FW_RESOURCE_OFFSET + FW_DOOMPACK_SLOT_OFF);
    const doom_pack_hdr_t *hdr  = (const doom_pack_hdr_t *)(const void *)slot;

    if (hdr->magic[0] != DOOM_PACK_MAGIC0 || hdr->magic[1] != DOOM_PACK_MAGIC1 ||
        hdr->magic[2] != DOOM_PACK_MAGIC2 || hdr->magic[3] != DOOM_PACK_MAGIC3) {
        printf("doom: no PlyX pack at %p\n", (const void *)slot);
        return false;
    }
    if (hdr->abi != DOOM_PACK_ABI) {
        printf("doom: pack ABI %lu != %u — refuse\n", (unsigned long)hdr->abi, DOOM_PACK_ABI);
        return false;
    }
    if (hdr->image_size > FW_DOOMPACK_SLOT_SIZE - sizeof(*hdr)) {
        printf("doom: pack size %lu overflows the slot — refuse\n", (unsigned long)hdr->image_size);
        return false;
    }
    // Everything above is a constant-time header test. From here on the work is
    // proportional to the image (a ~210 KB CRC walk, and the signature check
    // behind it), so this is where a repeated call has to stop. The auth byte is
    // part of the key because it is the one input that can change underneath an
    // otherwise identical retry.
    const uint32_t auth_crc = is_usb_host_side() ? doom_pack_auth_crc()
                                                : get_local_state()->doom_pack_auth_crc;
    if (pack_refusal_still_stands(hdr->image_crc, auth_crc, entry)) {
        return false;   // same pack, same answer, same entry — logged the first time
    }
    if (hdr->ram_base != (uint32_t)(uintptr_t)pool || hdr->ram_size > pool_size ||
        hdr->arena_off >= hdr->ram_size) {
        // The pack was linked for a different firmware build (the pool
        // moved). Coupled-but-verified by design — re-flash a matching pack.
        printf("doom: pack RAM %08lx+%lu != pool %p+%lu — stale pack, refuse\n",
               (unsigned long)hdr->ram_base, (unsigned long)hdr->ram_size,
               (void *)pool, (unsigned long)pool_size);
        pack_refusal_latch(hdr->image_crc, auth_crc, entry);
        return false;
    }
    // crc32_1byte takes a uint16_t length — chain it over the ~230 KB image
    // (chunked continuation is exact: the seed round-trips through the
    // final/initial complement).
    uint32_t       crc       = 0;
    const uint8_t *body      = slot + sizeof(*hdr);
    uint32_t       remaining = hdr->image_size;
    while (remaining) {
        const uint16_t n = remaining > 0xFFFFu ? 0xFFFFu : (uint16_t)remaining;
        crc = crc32_1byte(body, n, crc);
        body += n;
        remaining -= n;
    }
    if (crc != hdr->image_crc) {
        printf("doom: pack CRC %08lx != %08lx — refuse\n",
               (unsigned long)crc, (unsigned long)hdr->image_crc);
        pack_refusal_latch(hdr->image_crc, auth_crc, entry);
        return false;
    }

#ifdef FW_REQUIRE_SIGNATURE
    // FW-9: authenticate before branching into the image. The CRC above is an
    // integrity check anyone crafting a pack satisfies; this is the authorship
    // check, over header + image so no signed field can be re-targeted.
    //
    // What happens when it does NOT check out is doom_pack_gate()'s call, and it
    // is a table rather than an `if` — see doom_pack_gate.h. In short: a valid
    // pack loads; a TAMPERED one is refused on every path, because offering a
    // keypress there hands an attacker the one thing the physical gate exists to
    // withhold; an UNSIGNED one (a developer build) may raise the same on-keycap
    // A/ACCEPT — R/REJECT dialog the firmware image has had since FW-2, but only
    // on a deliberate entry, since the idle path has nobody to answer it.
    if (hdr->image_size > FW_DOOMPACK_SLOT_SIZE - sizeof(*hdr) - DOOM_PACK_SIG_SIZE) {
        printf("doom: pack leaves no room for its signature — refuse\n");
        return false;
    }
    const uint8_t *sig = slot + sizeof(*hdr) + hdr->image_size;
    // ⚠️ The cheap question first. A blank trailer cannot verify, so paying a
    // SHA-512 over ~210 KB to learn that is waste — and on the SLAVE it is waste
    // taken out of the window in which it must answer split transactions. The
    // 64-byte scan settles the unsigned case, which is precisely the case this
    // prompt exists for. Only a trailer that was actually written gets the crypto.
    enum doom_pack_sig sig_state = DOOM_PACK_SIG_BLANK;
    if (!doom_pack_trailer_is_blank(sig, DOOM_PACK_SIG_SIZE)) {
        sig_state = crypto_ed25519_check(sig, FW_SIGNING_PUBKEY, slot,
                                         sizeof(*hdr) + hdr->image_size) == 0
                        ? DOOM_PACK_SIG_VALID
                        : DOOM_PACK_SIG_INVALID;
    }

    // The answer is given on the MASTER's keycaps; the slave hears it over the
    // split link (poly_sync_t.doom_pack_auth) because its own RAM never saw the
    // keypress. state.h carries why delegating that verdict is sound — and note
    // the gate still judges an INVALID signature locally on both halves, so this
    // widens nothing but the "unsigned developer build" case.
    doom_pack_auth_t auth = s_auth;
    if (!is_usb_host_side()) {
        // ⚠️ The master's answer names the pack it was given for, and this half
        // honours it only for the pack IT holds. The two slots are written by one
        // host command, but that write can land here and fail there (or the
        // reverse) with nothing downstream reporting it — and a bare "something
        // was accepted" then ran whatever this half happened to hold. A mismatch
        // falls through to the gate as "not authorised", which on the slave's
        // automatic entry means refuse.
        const uint32_t accepted = get_local_state()->doom_pack_auth_crc;
        auth.valid     = (accepted != 0u && accepted == hdr->image_crc);
        auth.image_crc = hdr->image_crc;
    }

    switch (doom_pack_gate(sig_state, entry, &auth, hdr->image_crc)) {
        case DOOM_PACK_LOAD:
            if (sig_state != DOOM_PACK_SIG_VALID) {
                // Say it every session, not just at the prompt. An authorised
                // unsigned pack is a developer state, and a log that only
                // mentions it once is how it gets forgotten on a board that is
                // later handed to somebody else.
                printf("doom: unsigned pack ACCEPTED on the keyboard this boot — running it\n");
            }
            break;
        case DOOM_PACK_PROMPT:
            // Refuse THIS attempt and raise the dialog. The answer cannot be
            // waited for here: this runs on the loop that scans the matrix, so a
            // busy-wait guarantees the keypress is never seen — the same reason
            // FW_UP_COMMIT answers '?' instead of blocking. doom_mode.c re-enters
            // once the answer lands.
            printf("doom: pack is unsigned — asking on the keycaps (A = accept, R = reject)\n");
            doom_pack_confirm_arm(hdr->image_crc);
            pack_refusal_latch(hdr->image_crc, auth_crc, entry);
            return false;
        case DOOM_PACK_REFUSE:
        default:
            printf("doom: pack %s — refuse (FW-9: flash a release-signed .plyx)\n",
                   sig_state == DOOM_PACK_SIG_BLANK ? "is unsigned" : "signature is INVALID");
            pack_refusal_latch(hdr->image_crc, auth_crc, entry);
            return false;
    }
#else
    (void)entry;
#endif

    // Entry: image offset -> XIP address, Thumb bit set. init runs the pack
    // crt0 (.data copy + .bss zero inside the pool — virgin engine statics
    // AND .data every session, which the monolith's doom_shim_set_role reset
    // list only approximates) and returns the export table.
    doom_pack_init_fn init =
        (doom_pack_init_fn)(uintptr_t)((uint32_t)(uintptr_t)(slot + sizeof(*hdr)) + hdr->entry_off + 1u);
    const doom_pack_api_t *api = init(&s_fw_api);
    if (api == NULL || api->size < sizeof(doom_pack_api_t)) {
        printf("doom: pack init refused (api %p size %lu) — stub stays\n",
               (const void *)api, api ? (unsigned long)api->size : 0ul);
        return false;
    }
    s_api       = api;
    s_arena_off = hdr->arena_off;
    pack_refusal_clear();   // it loaded; a later refusal must be derived afresh
    printf("doom: pack v%lu loaded (%lu B, arena_off %lu)\n",
           (unsigned long)hdr->version, (unsigned long)hdr->image_size,
           (unsigned long)s_arena_off);
    return true;
}
