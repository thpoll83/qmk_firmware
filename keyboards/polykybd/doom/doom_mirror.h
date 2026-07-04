// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Slave lockstep mirror — shared data model (DOOM_FEASIBILITY.md "split-UART
// lockstep"; the transport the upstream piconet.c I2C layer is replaced by).
//
// One-way, master-sim-authoritative: the master's game stays a completely
// untouched single-player session; every ticcmd it builds is published through
// the engine's piconet seam (d_loop.c piconet_new_local_tic) into the TX ring
// below, drained by core0 (doom_tick) over the USER_SYNC_DOOM_MIRROR split
// transaction. The slave keeps a ROLLING WINDOW of the received cmds — it
// buffers continuously from engine boot, so when the master's G_DoNewGame
// fires a START(tic T) the cmds for T (built a couple of tics BEFORE the
// master ran tic T) are already in the window. On START the slave becomes a
// chocolate-doom "drone": it builds no local tics and runs exactly the
// received ones (d_loop.c D_StartDoomMirror), which — both sims starting from
// G_InitNew's M_ClearRandom with identical WHX data — keeps it in input
// lockstep with the master.
//
// Shared by qmk_shim.c (core1 engine side), doom_mode.c (core0 pump) and
// split_sync.c (slave RX handler); the struct itself lives in the game arena
// (DOOM_ARENA_MIRROR_OFF) so it exists only while game mode holds the pool,
// and is zeroed with the rest of the shared block on every session entry.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// One engine ticcmd_t (DOOM_TINY asserts sizeof == 8, incl. the ingame bit).
#define DOOM_MIRROR_CMD_BYTES 8

// Master TX ring: ~1.8 s at 35 tics/s. Overflow = the bridge was down for
// that long -> the mirror is declared broken for the rest of the session
// (exit + re-enter game mode resets everything). Power of two.
#define DOOM_MIRROR_TX_LEN 64
// Slave rolling window: ~3.6 s. The consumer (once a START engaged it) stays
// within a handful of tics of the writer, so overwrites of unconsumed slots
// only happen when the slave engine has stalled — detected and flagged.
#define DOOM_MIRROR_RX_LEN 128

// Message kinds (doom_mirror_msg_t.kind).
#define DOOM_MIRROR_MSG_TIC   1 // count cmds, tic = absolute tic of cmds[0]
#define DOOM_MIRROR_MSG_START 2 // skill/epi/map + tic = first level gametic
#define DOOM_MIRROR_MSG_BREAK 3 // master mirror broke -> slave back to pad

#define DOOM_MIRROR_MSG_MAX_CMDS 7

// The USER_SYNC_DOOM_MIRROR payload — fixed size (72 B == RPC_M2S_BUFFER_SIZE),
// leading crc32 over the rest, like every other split_sync transaction.
typedef struct {
    uint32_t crc32;
    uint8_t  kind;
    uint8_t  count;             // TIC: number of valid cmds[]
    uint8_t  skill, epi;        // START
    uint8_t  map;               // START
    uint8_t  _pad[3];
    uint32_t tic;               // TIC: tic of cmds[0]; START: start gametic
    uint8_t  cmds[DOOM_MIRROR_MSG_MAX_CMDS][DOOM_MIRROR_CMD_BYTES];
} doom_mirror_msg_t;

// The arena-resident mailbox. Ring heads are MONOTONIC counters in absolute
// tic units (slot = tic % LEN) — maketic/gametic start at 0 on every session
// (the whole shared block is memset at entry), so a cmd's ring index IS its
// tic number. Each field group has exactly one writer:
//   tx_*      master: core1 writes cmds/w/overflow, core0 advances r
//   ctl_out_* master: core1 writes, core0 reads (seq handshake). A SINGLE
//             latest-wins control channel — START and BREAK share it so a
//             stale BREAK can never be delivered after a fresher START.
//   rx_*      slave:  core0 (split handler) writes cmds/w/r, core1 reads
//   start_in_*/break_in slave: core0 writes, core1 consumes (seq handshake)
typedef struct {
    // master -> bridge
    volatile uint32_t tx_w, tx_r;
    volatile uint8_t  tx_overflow;      // transport died / ring desynced: the
                                        // mirror is dead for this session
    uint8_t           tx_cmds[DOOM_MIRROR_TX_LEN][DOOM_MIRROR_CMD_BYTES];
    volatile uint32_t ctl_out_seq;
    volatile uint8_t  ctl_out_kind;     // DOOM_MIRROR_MSG_START / _BREAK
    volatile uint32_t ctl_out_tic;
    volatile uint8_t  ctl_out_skill, ctl_out_epi, ctl_out_map;
    // bridge -> slave
    volatile uint32_t rx_w, rx_r;
    volatile uint8_t  rx_broken;        // gap / consumer overrun: frozen until
                                        // the next START re-arms
    uint8_t           rx_cmds[DOOM_MIRROR_RX_LEN][DOOM_MIRROR_CMD_BYTES];
    volatile uint32_t start_in_seq;
    volatile uint32_t start_in_tic;
    volatile uint8_t  start_in_skill, start_in_epi, start_in_map;
    volatile uint8_t  break_in;
} doom_mirror_t;

#ifdef __cplusplus
}
#endif
