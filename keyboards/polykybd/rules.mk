
# pico-sdk host header uses K&R-style empty () prototype; suppress the warning it triggers
CFLAGS += -Wno-strict-prototypes

# OS detection (QMK feature): fingerprints the USB enumeration (wLength pattern) to
# guess the host OS. Used as the AUTO-mode fallback for the active-OS state — the
# only source on machines where the host app can't run (locked-down) or doesn't
# exist (Android, which it reports as Linux). The host push (HID cmd 29) wins when
# present. Do NOT also set OS_DETECTION_DEBUG_ENABLE — it conflicts with
# DYNAMIC_KEYMAP/VIA (see quantum/os_detection.h).
OS_DETECTION_ENABLE = yes

# polykybd.c is the keyboard-named source — QMK compiles it automatically, so
# it must NOT be listed here too (a second copy in SRC causes a duplicate
# definition at link time once the file defines symbols, e.g. the POLYKYBD_HIL
# is_keyboard_master_impl override).
# poly_keymap.c holds the shared keymap logic compiled for every variant
# (split72, split42). Each variant's keymaps/default/keymap.c carries only its
# data (keymaps[] / encoder_map[] / g_led_config).
POLY_SRC := poly_keymap.c side.c state.c split_sync.c split_fw_up.c multicore_exec.c hid_com.c hid_fw_up.c hid_fontpack.c fill_overlay.c poly_util.c matrix_helper.c bridge_helper.c oled_helper.c keycode_helper.c mru.c lang_layer.c os_actions.c anim/startup_anim.c
SRC += $(POLY_SRC)

# Extra compiler warnings for the PolyKybd (non-QMK-core) sources ONLY. QMK already
# builds with -Wall -Werror globally, but -Wall does NOT include -Wtype-limits — the
# warning that would have caught the `uint8_t s < 340` spark-loop overflow that hung
# the keyboard. -Wextra adds it (plus -Wsign-compare, -Wshadow-ish family, etc.).
# Scoped per-object via FILE_SPECIFIC_CFLAGS (the same hook QMK uses for board hooks)
# so QMK core stays quiet. -Werror is inherited from the global CFLAGS, so these are
# hard errors for our files — a build FAILS rather than shipping the bug.
# -Wextra brings in -Wtype-limits (the bug-catcher). The two suppressed categories
# are pure noise for this codebase: -Wunused-parameter fires on QMK's fixed callback
# signatures (the param can't be dropped), and -Wold-style-declaration is GCC ordering
# pedantry (`const static` vs `static const`). Everything else in -Wextra stays a hard
# error (global -Werror), so a real always-true comparison / sign mismatch fails the build.
POLY_WARN_CFLAGS := -Wextra -Wno-unused-parameter -Wno-old-style-declaration
define POLY_APPLY_WARN
$(INTERMEDIATE_OUTPUT)/$(patsubst %.c,%.o,$(patsubst ./%,%,$1)): FILE_SPECIFIC_CFLAGS += $(POLY_WARN_CFLAGS)
endef
$(foreach s,$(POLY_SRC),$(eval $(call POLY_APPLY_WARN,$(s))))

# HIL test station build: fix the split role at compile time per side instead of
# using VBUS detection, because both halves are USB-powered on the rig and the
# two identical boards are told apart only by the test station, which flashes a
# per-side image. Opt-in only — pass one of:
#   -e POLYKYBD_HIL=left    left half  -> forced master
#   -e POLYKYBD_HIL=right   right half -> forced slave (usb_disconnect)
#   -e POLYKYBD_HIL=yes      alias for 'left' (master)
# Applies to all polykybd variants (split72, split42). Normal builds leave it
# unset and keep stock USB_VBUS_PIN detection. The override lives in polykybd.c.
ifneq ($(filter yes left right,$(strip $(POLYKYBD_HIL))),)
    OPT_DEFS += -DPOLYKYBD_HIL
    ifeq ($(strip $(POLYKYBD_HIL)), right)
        OPT_DEFS += -DPOLYKYBD_HIL_SLAVE
    endif
endif

# "Can it run Doom?" easter egg — dev-harness build (see DOOM_FEASIBILITY.md and
# doom/README.md). Opt-in only: `qmk compile ... -e POLYKYBD_DOOM=yes` compiles
# the game-mode scaffold (overlay-pool borrow, keycap blitter, IDDQD trigger).
# Normal builds pay zero bytes — doom_mode.h stubs every hook to an inline no-op.
#
# `-e POLYKYBD_DOOM_PACK=yes` builds the SHIPPING-SHAPE flavour instead
# (doom/PACK_DESIGN.md): the firmware side of the executable-pack split —
# mode machinery + blitter + fire demo + the PlyX pack loader, with the
# engine expected in the DOOMPACK flash slot and every doom_shim_* call
# dispatched through the pack's export table. No engine sources, no custom
# linker script, no printf/alloc wraps (those exist for the in-image engine).
ifeq ($(strip $(POLYKYBD_DOOM_PACK)), yes)
    POLYKYBD_DOOM = yes
    OPT_DEFS += -DPOLYKYBD_DOOM_PACK
endif
ifeq ($(strip $(POLYKYBD_DOOM)), yes)
    OPT_DEFS += -DPOLYKYBD_DOOM
    SRC += doom/doom_mode.c doom/doom_blit.c doom/doom_fire.c
ifeq ($(strip $(POLYKYBD_DOOM_PACK)), yes)
    SRC += doom/doom_pack_load.c
    # Pack flavour pins the overlay pool at the RAM origin so the engine
    # pack's RAM base is a build-independent constant — a flashed .plyx then
    # survives firmware rebuilds. See ld/RP2040_FLASH_TIMECRIT_DOOMPACK.ld.
    MCU_LDSCRIPT = RP2040_FLASH_TIMECRIT_DOOMPACK
else
    # No savegames in v1 (field round 29): the upstream flags drop Load/Save
    # Game from the menus and compile the save/load machinery out. A future
    # extension can restore them by routing the save-slot flash writes
    # through the firmware's staged-write machinery (picoflash.c is stubbed
    # in qmk_shim.c — see doom/README.md "Known v1 limits").
    OPT_DEFS += -DNO_USE_LOAD=1 -DNO_USE_SAVE=1
    # Doom builds use a keyboard-local linker script: the engine's zero-init
    # statics and the overlay pool share one .doom_shared block (RAM is
    # otherwise fully committed). See ld/RP2040_FLASH_TIMECRIT_DOOM.ld.
    MCU_LDSCRIPT = RP2040_FLASH_TIMECRIT_DOOM
    # Route ALL printf output through the core-aware relay in doom/qmk_shim.c:
    # core1 (the game) must never enter QMK's console path — sendchar suspends
    # the calling ChibiOS thread and core1 has none (it wedged the engine boot
    # on real hardware, 2026-07-03). putchar_ is lib/printf's output funnel
    # (quantum/logging/print.c).
    LDFLAGS += -Wl,--wrap=putchar_
    # Same principle for the allocator: core1 allocations go to the zone
    # (upstream wraps malloc/calloc/free via pico_wrap_function +
    # USE_ZONE_FOR_MALLOC), core0 keeps the real newlib heap. Defense-in-depth:
    # no live engine malloc/strdup call site survives the tiny-build #ifdefs
    # today (nm-verified), but newlib's lock/sbrk path is as core1-hostile as
    # the console, so keep it covered. Implementations in doom/qmk_shim.c.
    LDFLAGS += -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=free \
               -Wl,--wrap=realloc -Wl,--wrap=strdup
    SRC += doom/qmk_shim.c
endif
    # First engine slice from the vendored rp2040-doom snapshot (doom/engine/):
    # pure math/data units with no platform backend, compiled as-is to prove the
    # engine sources build inside QMK's flag/include environment.
    # The engine include dirs are appended LAST, so every existing include keeps
    # resolving exactly as before (first -I hit wins — QMK code still finds
    # keyboards/polykybd/config.h first); only the engine's cross-directory
    # includes (src/doom -> src, e.g. "doomtype.h") resolve through these. The
    # engine's own config.h (engine/src/config.h, our generated stand-in for the
    # upstream CMake one) wins inside engine files via same-directory resolution.
    # pico-sdk extras the engine uses beyond QMK's default set: pico_sync
    # semaphores (frame handoff), binary_info (no-op'd), pico_time, the
    # hardware interpolators (pd_render) and the pico divider wrappers.
    DOOM_INC := -Ikeyboards/polykybd/doom/engine/src \
                -Ikeyboards/polykybd/doom/engine/src/doom \
                -Ikeyboards/polykybd/doom/engine/src/pico \
                -Ilib/pico-sdk/src/common/pico_sync/include \
                -Ilib/pico-sdk/src/common/pico_binary_info/include \
                -Ilib/pico-sdk/src/common/pico_time/include \
                -Ilib/pico-sdk/src/rp2_common/hardware_interp/include \
                -Ilib/pico-sdk/src/common/pico_divider/include
    CFLAGS   += $(DOOM_INC)
    # ALL_CXXFLAGS uses CXXFLAGS, not CFLAGS — pd_render.cpp needs the same dirs.
    CXXFLAGS += $(DOOM_INC)
    # The vintage id headers use K&R `()` prototypes (d_think.h actionf_v,
    # d_loop.h). QMK's common_rules.mk force-enables -Wstrict-prototypes AFTER
    # keyboard CFLAGS, but EXTRAFLAGS lands last on the compile line, so this
    # wins. Doom dev builds only — normal builds keep the strict warning.
    # The unused-* trio mirrors upstream's own small_doom_common compile options
    # (the heavily #ifdef'd tiny build leaves dead locals/functions behind).
    # -Wno-error demotions (not full suppressions — the warnings stay visible):
    # the id code returns const objects through non-const wad_file_t* and prints
    # XIP addresses with %p from integer macros; upstream builds without -Werror.
    # strict-prototypes / discarded-qualifiers are C-only diagnostics that
    # cc1plus REJECTS under -Werror, so they're expanded per-recipe: EXTRAFLAGS
    # is referenced from the compile commands where $< is the source file — the
    # $(filter) drops them for .cpp sources (and the harmless link expansion).
    EXTRAFLAGS += $(if $(filter %.cpp,$<),,-Wno-strict-prototypes -Wno-error=discarded-qualifiers) \
                  -Wno-unused-function \
                  -Wno-unused-but-set-variable \
                  -Wno-unused-variable \
                  -Wno-error=format \
                  -Wno-error=maybe-uninitialized \
                  -Wno-error=cpp
    # Force-include the doom_tiny define set. The config.h route alone is not
    # enough: several engine files test feature guards BEFORE their first
    # include (e.g. m_config.c's !NO_USE_BOUND_CONFIG wrap / SDL include), which
    # upstream covers by passing defines on the compiler command line — this
    # does the same. Build-wide but doom-builds-only; verified no QMK/ChibiOS/
    # pico-sdk-compiled source reads any of these macros (PICO_ON_DEVICE=1 is
    # simply true for this target).
    EXTRAFLAGS += -include keyboards/polykybd/doom/engine/src/doom_tiny_defs.h
ifneq ($(strip $(POLYKYBD_DOOM_PACK)), yes)
    SRC += doom/engine/src/tables.c \
           doom/engine/src/m_bbox.c \
           doom/engine/src/m_fixed.c \
           doom/engine/src/d_mode.c \
           doom/engine/src/m_cheat.c \
           doom/engine/src/doom/m_random.c \
           doom/engine/src/doom/doomdef.c \
           doom/engine/src/doom/doomstat.c \
           doom/engine/src/doom/dstrings.c \
           doom/engine/src/doom/d_items.c \
           doom/engine/src/doom/sounds.c
    # Slice 3: memory + WAD layer (zone allocator on the borrowed pool; WHX wad
    # memory-mapped at the XIP resource address — USE_MEMORY_WAD/NO_FILE_ACCESS,
    # so only the memory w_file backend is compiled).
    SRC += doom/engine/src/z_zone.c \
           doom/engine/src/w_wad.c \
           doom/engine/src/w_main.c \
           doom/engine/src/w_file.c \
           doom/engine/src/w_file_memory.c \
           doom/engine/src/d_event.c \
           doom/engine/src/deh_str.c
    # Slice 4a: remaining support layer (game lib minus platform backends and
    # the music stack — i_main.c stays out, QMK owns main()).
    SRC += doom/engine/src/m_argv.c \
           doom/engine/src/m_misc.c \
           doom/engine/src/m_config.c \
           doom/engine/src/m_controls.c \
           doom/engine/src/v_video.c \
           doom/engine/src/sha1.c \
           doom/engine/src/memio.c \
           doom/engine/src/w_checksum.c \
           doom/engine/src/d_loop.c \
           doom/engine/src/d_iwad.c \
           doom/engine/src/aes_prng.c \
           doom/engine/src/net_client.c \
           doom/engine/src/tiny_huff.c \
           doom/engine/src/musx_decoder.c \
           doom/engine/src/image_decoder.c
    # Slice 4b: the whole src/doom game core (the `doom` CMake target list).
    SRC += doom/engine/src/doom/am_map.c \
           doom/engine/src/doom/deh_ammo.c \
           doom/engine/src/doom/deh_bexstr.c \
           doom/engine/src/doom/deh_cheat.c \
           doom/engine/src/doom/deh_doom.c \
           doom/engine/src/doom/deh_frame.c \
           doom/engine/src/doom/deh_misc.c \
           doom/engine/src/doom/deh_ptr.c \
           doom/engine/src/doom/deh_sound.c \
           doom/engine/src/doom/deh_thing.c \
           doom/engine/src/doom/deh_weapon.c \
           doom/engine/src/doom/d_main.c \
           doom/engine/src/doom/d_net.c \
           doom/engine/src/doom/f_finale.c \
           doom/engine/src/doom/f_wipe.c \
           doom/engine/src/doom/g_game.c \
           doom/engine/src/doom/hu_lib.c \
           doom/engine/src/doom/hu_stuff.c \
           doom/engine/src/doom/info.c \
           doom/engine/src/doom/m_menu.c \
           doom/engine/src/doom/p_ceilng.c \
           doom/engine/src/doom/p_doors.c \
           doom/engine/src/doom/p_enemy.c \
           doom/engine/src/doom/p_floor.c \
           doom/engine/src/doom/p_inter.c \
           doom/engine/src/doom/p_lights.c \
           doom/engine/src/doom/p_map.c \
           doom/engine/src/doom/p_maputl.c \
           doom/engine/src/doom/p_mobj.c \
           doom/engine/src/doom/p_plats.c \
           doom/engine/src/doom/p_pspr.c \
           doom/engine/src/doom/p_saveg.c \
           doom/engine/src/doom/p_setup.c \
           doom/engine/src/doom/p_sight.c \
           doom/engine/src/doom/p_spec.c \
           doom/engine/src/doom/p_switch.c \
           doom/engine/src/doom/p_telept.c \
           doom/engine/src/doom/p_tick.c \
           doom/engine/src/doom/p_user.c \
           doom/engine/src/doom/r_bsp.c \
           doom/engine/src/doom/r_data.c \
           doom/engine/src/doom/r_data_whd.c \
           doom/engine/src/doom/r_draw.c \
           doom/engine/src/doom/r_main.c \
           doom/engine/src/doom/r_plane.c \
           doom/engine/src/doom/r_segs.c \
           doom/engine/src/doom/r_sky.c \
           doom/engine/src/doom/r_things.c \
           doom/engine/src/doom/s_sound.c \
           doom/engine/src/doom/statdump.c \
           doom/engine/src/doom/st_lib.c \
           doom/engine/src/doom/st_stuff.c \
           doom/engine/src/doom/wi_stuff.c
    # Slice 5: the column renderer (C++, PICODOOM_RENDER_NEWHOPE).
    SRC += doom/engine/src/pd_render.cpp
    # pico_sync semaphores for the renderer's core0/core1 choreography + their
    # spinlock underpinnings (QMK compiles neither by default). ⚠️ Runtime
    # audit still owed: next_striped_spin_lock_num() hands out SIO spinlocks
    # with no knowledge of ChibiOS's own spinlock use — verify the claimed
    # range before first boot of the rooted engine.
    SRC += lib/pico-sdk/src/common/pico_sync/sem.c \
           lib/pico-sdk/src/common/pico_sync/lock_core.c \
           lib/pico-sdk/src/rp2_common/hardware_sync/sync.c
endif
endif
