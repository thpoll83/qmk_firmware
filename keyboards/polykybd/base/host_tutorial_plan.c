// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_tutorial_plan.h"

#include <string.h>

bool host_tut_parse_text(const uint8_t *data, uint8_t length, host_tut_text_t *out) {
    if (data == NULL || out == NULL) return false;
    if (length > HOST_TUT_REPORT_BYTES) length = HOST_TUT_REPORT_BYTES;
    host_tut_text_t t;
    memset(&t, 0, sizeof t);
    uint8_t pos = HOST_TUT_IDX_ARG;
    for (uint8_t i = 0; i < 2u; ++i) {
        if (pos >= length) return false;
        const uint8_t n = data[pos++];
        if (n > HOST_TUT_TEXT_BYTES || (uint16_t)pos + n > length) return false;
        for (uint8_t k = 0; k < n; ++k) {
            const uint8_t ch = data[pos + k];
            if (ch < 0x20u || ch > 0x7Eu) return false;
        }
        memcpy(&t.buf[t.len[0]], &data[pos], n);   // line 1 starts after line 0
        t.len[i] = n;
        pos      = (uint8_t)(pos + n);
    }
    *out = t;
    return true;
}

bool host_tut_text_empty(const host_tut_text_t *t) {
    return t == NULL || (t->len[0] == 0u && t->len[1] == 0u);
}

const char *host_tut_text_line(const host_tut_text_t *t, uint8_t i) {
    return &t->buf[i == 0u ? 0u : t->len[0]];
}

bool host_tut_parse_spotlight(const uint8_t *data, uint8_t length, uint8_t rows, uint8_t cols,
                              bool *on, uint8_t mask[HOST_TUT_MASK_BYTES]) {
    if (data == NULL || on == NULL || mask == NULL) return false;
    if (length < HOST_TUT_IDX_ARG + 1u + HOST_TUT_MASK_BYTES) return false;
    const uint16_t bits = (uint16_t)rows * cols;
    if (bits > HOST_TUT_MASK_BYTES * 8u) return false;
    const uint8_t flags = data[HOST_TUT_IDX_ARG];
    if ((flags & (uint8_t)~HOST_TUT_SPOT_FLAG_ON) != 0u) return false;
    const uint8_t *m = &data[HOST_TUT_IDX_ARG + 1u];
    for (uint16_t b = bits; b < HOST_TUT_MASK_BYTES * 8u; ++b) {
        if (m[b / 8u] & (uint8_t)(1u << (b % 8u))) return false;
    }
    *on = (flags & HOST_TUT_SPOT_FLAG_ON) != 0u;
    memcpy(mask, m, HOST_TUT_MASK_BYTES);
    return true;
}

bool host_tut_mask_has(const uint8_t mask[HOST_TUT_MASK_BYTES], uint8_t cols, uint8_t row,
                       uint8_t col) {
    const uint16_t b = (uint16_t)row * cols + col;
    if (mask == NULL || col >= cols || b >= HOST_TUT_MASK_BYTES * 8u) return false;
    return (mask[b / 8u] & (uint8_t)(1u << (b % 8u))) != 0u;
}

bool host_tut_parse_arm(const uint8_t *data, uint8_t length, uint8_t rows, uint8_t cols,
                        uint8_t layers, host_tut_conds_t *out) {
    if (data == NULL || out == NULL || length <= HOST_TUT_IDX_ARG) return false;
    host_tut_conds_t c;
    memset(&c, 0, sizeof c);
    c.count = data[HOST_TUT_IDX_ARG];
    if (c.count > HOST_TUT_MAX_CONDS) return false;
    if ((uint16_t)HOST_TUT_IDX_ARG + 1u + 3u * c.count > length) return false;
    for (uint8_t i = 0; i < c.count; ++i) {
        const uint8_t *p = &data[HOST_TUT_IDX_ARG + 1u + 3u * i];
        host_tut_cond_t k = {p[0], p[1], p[2]};
        switch (k.kind) {
            case HOST_TUT_COND_KEY:
                if (k.a >= rows || k.b >= cols) return false;
                break;
            case HOST_TUT_COND_LAYER:
                if (k.a >= layers || k.a >= 32u || k.b != 0u) return false;
                break;
            case HOST_TUT_COND_KEYCODE:
                break;
            default:
                return false;
        }
        c.cond[i] = k;
    }
    *out = c;
    return true;
}

void host_tut_note_key(host_tut_conds_t *c, uint8_t row, uint8_t col, uint16_t keycode,
                       bool pressed) {
    if (c == NULL || !pressed) return;
    for (uint8_t i = 0; i < c->count && i < HOST_TUT_MAX_CONDS; ++i) {
        const host_tut_cond_t *k = &c->cond[i];
        const bool hit =
            (k->kind == HOST_TUT_COND_KEY && k->a == row && k->b == col) ||
            (k->kind == HOST_TUT_COND_KEYCODE && (uint16_t)(k->a | ((uint16_t)k->b << 8)) == keycode);
        if (hit) c->matched |= (uint8_t)(1u << i);
    }
}

void host_tut_note_layers(host_tut_conds_t *c, uint32_t layers_on) {
    if (c == NULL) return;
    for (uint8_t i = 0; i < c->count && i < HOST_TUT_MAX_CONDS; ++i) {
        const host_tut_cond_t *k = &c->cond[i];
        if (k->kind == HOST_TUT_COND_LAYER && k->a < 32u && (layers_on & (1ul << k->a))) {
            c->matched |= (uint8_t)(1u << i);
        }
    }
}

bool host_tut_expired(uint32_t last_ms, uint32_t now_ms) {
    return (uint32_t)(now_ms - last_ms) >= HOST_TUT_WATCHDOG_MS;
}

uint8_t host_tut_reveal_progress(uint32_t elapsed_ms, uint32_t run_ms) {
    if (run_ms == 0u || elapsed_ms >= run_ms) return 255u;
    return (uint8_t)((elapsed_ms * 255u) / run_ms);
}

static uint32_t fnv1a(uint32_t h, const uint8_t *p, uint16_t n) {
    for (uint16_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

uint32_t host_tut_state_crc(const host_tut_text_t *text, bool spot_on,
                            const uint8_t mask[HOST_TUT_MASK_BYTES]) {
    uint32_t h = 2166136261u;
    if (text != NULL) {
        h = fnv1a(h, text->len, 2u);
        uint16_t used = (uint16_t)text->len[0] + text->len[1];
        if (used > HOST_TUT_TEXT_BYTES) used = HOST_TUT_TEXT_BYTES;
        h = fnv1a(h, (const uint8_t *)text->buf, used);
    }
    const uint8_t on = spot_on ? 1u : 0u;
    h = fnv1a(h, &on, 1u);
    if (mask != NULL) h = fnv1a(h, mask, HOST_TUT_MASK_BYTES);
    return h;
}

uint8_t host_tut_sendable(uint8_t dirty, bool reveal_on) {
    return reveal_on ? (uint8_t)(dirty & (uint8_t)~HOST_TUT_DIRTY_SPOT) : dirty;
}
