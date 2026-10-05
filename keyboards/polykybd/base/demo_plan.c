// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "demo_plan.h"
#include "idle_poem_plan.h"   // the poems the typing segments point at

#include <stddef.h>

// ---- the playlist -------------------------------------------------------------
// One loop is ~15 minutes (demo_plan_tests.cpp pins 14..16). Seven activity blocks,
// each followed by about a minute of the idle animation (+ the 10 s fade into it), so a
// passer-by sees the board at work, then resting, then at work again.
//
// ⚠️ The longer typing is the idle screen's POEMS (base/idle_poem_plan.c), pointed at
// rather than copied: the demo used to carry ~900 bytes of its own sentences and code
// lines, and the poems already sit in flash. A poem's '\n' is typed as Enter, so the key
// demo (KC_DEMO_KEYS) writes each poem into the editor line by line.
//
// The short words of the language and script tour stay here: each is spelled for the
// alphabet previewed at that moment, and all of them together are ~150 bytes. They are
// keystrokes on a US layout, not prose in the previewed language: with Greek previewed,
// "kalimera" presses the keys that carry k, a, l, ... and those keycaps show κ, α, λ.
#define SEC(s) ((uint32_t)(s) * 1000u)
// A paged menu: one DEMO_PAGE_MS dwell per tab, plus two seconds on the last one.
#define PAGED(n) ((uint32_t)((n) + 1u) * DEMO_PAGE_MS + 2000u)
// The idle block: the 10 s fade (FADE_TRANSITION_TIME) plus a bit over a minute of the
// idle style itself.
#define IDLE_MINUTE {DEMO_IDLE, DEMO_VIEW_BASE, DEMO_LOOK_OWN, 0, SEC(74), NULL}
// A poem typed on the base layout in the board's own language.
#define POEM(t) {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_OWN, 0, SEC(2), (t)}

const demo_seg_t demo_playlist[] = {
    // 1. Typing — every keycap is a display, and Shift changes all of them.
    POEM(idle_poem_0),
    {DEMO_SHOW, DEMO_VIEW_FN,   DEMO_LOOK_OWN, 0, SEC(5), NULL},
    {DEMO_SHOW, DEMO_VIEW_NUM,  DEMO_LOOK_OWN, 0, SEC(5), NULL},
    POEM(idle_poem_1),
    IDLE_MINUTE,

    // 2. A language tour — the same keys, other alphabets.
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_GREEK,    0, SEC(2), "kalimera kosme"},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_RUSSIAN,  0, SEC(2), "privet mir"},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_ARABIC,   0, SEC(2), "marhaban"},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_HEBREW,   0, SEC(2), "shalom olam"},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_HINDI,    0, SEC(2), "namaste duniya"},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_THAI,     0, SEC(2), "sawasdee"},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_JAPANESE, 0, SEC(2), "konnichiwa"},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_KOREAN,   0, SEC(2), "annyeong"},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_GEORGIAN, 0, SEC(2), "gamarjoba"},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_ARMENIAN, 0, SEC(2), "barev"},
    POEM(idle_poem_2),
    IDLE_MINUTE,

    // 3. The menus — picked on the keyboard itself, no host app needed.
    {DEMO_SHOW, DEMO_VIEW_LANG_MENU, DEMO_LOOK_OWN, 5,  PAGED(5), NULL},
    {DEMO_SHOW, DEMO_VIEW_EMOJI,     DEMO_LOOK_OWN, 8,  PAGED(8), NULL},
    {DEMO_SHOW, DEMO_VIEW_INTL,      DEMO_LOOK_OWN, 0,  SEC(6), NULL},
    {DEMO_SHOW, DEMO_VIEW_UTIL,      DEMO_LOOK_OWN, 0,  SEC(6), NULL},
    {DEMO_SHOW, DEMO_VIEW_SETTINGS,  DEMO_LOOK_OWN, 0,  SEC(6), NULL},
    POEM(idle_poem_3),
    IDLE_MINUTE,

    // 4. Glyph scripts — fantasy and retro faces over the same layout.
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_TENGWAR,  0, SEC(2), "namarie"},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_RUNES,    0, SEC(2), "futhark"},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_CIRTH,    0, SEC(2), "moria"},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_AUREBESH, 0, SEC(2), "a long time ago"},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_SGA,      0, SEC(2), "the cake is a lie"},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_IBMVGA,   0, SEC(2), "C:\\> dir /w"},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_C64,      0, SEC(2), "LOAD \"*\",8,1"},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_AMIGA,    0, SEC(2), "Workbench 1.3"},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_BRAILLE,  0, SEC(2), "touch"},
    POEM(idle_poem_4),
    IDLE_MINUTE,

    // 5. The layers once more, between two poems.
    POEM(idle_poem_0),
    {DEMO_SHOW, DEMO_VIEW_FN,   DEMO_LOOK_OWN, 0, SEC(5), NULL},
    {DEMO_SHOW, DEMO_VIEW_NUM,  DEMO_LOOK_OWN, 0, SEC(5), NULL},
    POEM(idle_poem_3),
    IDLE_MINUTE,

    // 6. European languages and the Intl layer — accents without dead keys.
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_GERMAN,    0, SEC(2), "Guten Tag zusammen"},
    {DEMO_SHOW, DEMO_VIEW_INTL, DEMO_LOOK_GERMAN,    0, SEC(5), NULL},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_FRENCH,    0, SEC(2), "Bonjour tout le monde"},
    {DEMO_SHOW, DEMO_VIEW_INTL, DEMO_LOOK_FRENCH,    0, SEC(5), NULL},
    {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_UKRAINIAN, 0, SEC(2), "dobryi den"},
    {DEMO_SHOW, DEMO_VIEW_EMOJI, DEMO_LOOK_OWN,      3, PAGED(3), NULL},
    POEM(idle_poem_1),
    IDLE_MINUTE,

    // 7. One more poem and the settings, then round again.
    POEM(idle_poem_2),
    {DEMO_SHOW, DEMO_VIEW_NUM,  DEMO_LOOK_OWN, 0, SEC(5), NULL},
    {DEMO_SHOW, DEMO_VIEW_SETTINGS, DEMO_LOOK_OWN, 0, SEC(8), NULL},
    {DEMO_SHOW, DEMO_VIEW_LANG_MENU, DEMO_LOOK_OWN, 2, PAGED(2), NULL},
    IDLE_MINUTE,
};
const uint8_t demo_playlist_len = (uint8_t)(sizeof(demo_playlist) / sizeof(demo_playlist[0]));

// ---- arithmetic ---------------------------------------------------------------

// HID usages, spelled out so this file needs no QMK header. They are the QMK basic
// keycodes (KC_A == 0x04 ...), which is what the binding compares the keymap against.
enum {
    U_A = 0x04, U_1 = 0x1E, U_0 = 0x27, U_ENTER = 0x28, U_TAB = 0x2B, U_SPACE = 0x2C,
    U_MINUS = 0x2D, U_EQUAL = 0x2E, U_LBRC = 0x2F, U_RBRC = 0x30, U_BSLS = 0x31,
    U_SCLN = 0x33, U_QUOT = 0x34, U_GRV = 0x35, U_COMM = 0x36, U_DOT = 0x37, U_SLSH = 0x38,
};

uint8_t demo_ascii_usage(char c, bool *shift) {
    bool sh = false;
    uint8_t u = 0;
    if (c >= 'a' && c <= 'z') {
        u = (uint8_t)(U_A + (c - 'a'));
    } else if (c >= 'A' && c <= 'Z') {
        u = (uint8_t)(U_A + (c - 'A'));
        sh = true;
    } else if (c >= '1' && c <= '9') {
        u = (uint8_t)(U_1 + (c - '1'));
    } else {
        switch (c) {
            case '0':  u = U_0;     break;
            case ' ':  u = U_SPACE; break;
            case '\n': u = U_ENTER; break;
            case '\t': u = U_TAB;   break;
            case '-':  u = U_MINUS; break;
            case '=':  u = U_EQUAL; break;
            case '[':  u = U_LBRC;  break;
            case ']':  u = U_RBRC;  break;
            case '\\': u = U_BSLS;  break;
            case ';':  u = U_SCLN;  break;
            case '\'': u = U_QUOT;  break;
            case '`':  u = U_GRV;   break;
            case ',':  u = U_COMM;  break;
            case '.':  u = U_DOT;   break;
            case '/':  u = U_SLSH;  break;
            case '!':  u = U_1;              sh = true; break;
            case '@':  u = (uint8_t)(U_1 + 1); sh = true; break;
            case '#':  u = (uint8_t)(U_1 + 2); sh = true; break;
            case '$':  u = (uint8_t)(U_1 + 3); sh = true; break;
            case '%':  u = (uint8_t)(U_1 + 4); sh = true; break;
            case '^':  u = (uint8_t)(U_1 + 5); sh = true; break;
            case '&':  u = (uint8_t)(U_1 + 6); sh = true; break;
            case '*':  u = (uint8_t)(U_1 + 7); sh = true; break;
            case '(':  u = (uint8_t)(U_1 + 8); sh = true; break;
            case ')':  u = U_0;     sh = true; break;
            case '_':  u = U_MINUS; sh = true; break;
            case '+':  u = U_EQUAL; sh = true; break;
            case '{':  u = U_LBRC;  sh = true; break;
            case '}':  u = U_RBRC;  sh = true; break;
            case '|':  u = U_BSLS;  sh = true; break;
            case ':':  u = U_SCLN;  sh = true; break;
            case '"':  u = U_QUOT;  sh = true; break;
            case '~':  u = U_GRV;   sh = true; break;
            case '<':  u = U_COMM;  sh = true; break;
            case '>':  u = U_DOT;   sh = true; break;
            case '?':  u = U_SLSH;  sh = true; break;
            default:   u = 0;       break;
        }
    }
    if (shift) *shift = (u != 0) && sh;
    return u;
}

uint32_t demo_char_slot_ms(const char *text, uint16_t i) {
    const char c = text[i];
    // Deterministic jitter: the same text always types with the same rhythm (both halves
    // and the tests agree), and neighbouring keys do not land on a metronome beat.
    uint32_t ms = DEMO_KEY_MS + (((uint32_t)i * 37u + (uint8_t)c * 11u) % DEMO_KEY_JITTER_MS);
    switch (c) {
        case ' ':                     ms += DEMO_SPACE_EXTRA_MS; break;
        case ',': case ';': case ':': ms += DEMO_COMMA_EXTRA_MS; break;
        case '.': case '!': case '?': ms += DEMO_STOP_EXTRA_MS;  break;
        case '\n':                    ms += DEMO_LINE_EXTRA_MS;  break;
        default: break;
    }
    return ms;
}

uint32_t demo_type_ms(const char *text) {
    uint32_t ms = 0;
    if (text == NULL) return 0;
    for (uint16_t i = 0; text[i] != '\0'; ++i) ms += demo_char_slot_ms(text, i);
    return ms;
}

uint32_t demo_seg_ms(const demo_seg_t *s) {
    if (s->kind == DEMO_TYPE) return demo_type_ms(s->text) + s->hold_ms;
    return s->hold_ms;
}

uint32_t demo_cycle_ms(void) {
    uint32_t ms = 0;
    for (uint8_t i = 0; i < demo_playlist_len; ++i) ms += demo_seg_ms(&demo_playlist[i]);
    return ms;
}

demo_pos_t demo_locate(uint32_t t) {
    demo_pos_t     pos   = {0, 0};
    const uint32_t cycle = demo_cycle_ms();
    if (cycle == 0) return pos;
    t %= cycle;
    for (uint8_t i = 0; i < demo_playlist_len; ++i) {
        const uint32_t len = demo_seg_ms(&demo_playlist[i]);
        if (t < len) {
            pos.seg  = i;
            pos.into = t;
            return pos;
        }
        t -= len;
    }
    return pos;   // unreachable: t < cycle
}

demo_keys_t demo_type_keys(const char *text, uint32_t into) {
    demo_keys_t k = {0, false, false};
    if (text == NULL) return k;
    uint32_t start = 0;
    for (uint16_t i = 0; text[i] != '\0'; ++i) {
        const uint32_t slot = demo_char_slot_ms(text, i);
        if (into < start + slot) {
            bool           shift = false;
            const uint8_t  usage = demo_ascii_usage(text[i], &shift);
            const uint32_t off   = into - start;
            // A shifted character holds Shift for its WHOLE slot, so a run of capitals
            // keeps it down across the run, and the key itself goes down a beat later —
            // the order a typist's fingers use, and the order the legends change in.
            const uint32_t lead  = shift ? DEMO_SHIFT_LEAD_MS : 0u;
            k.ch         = text[i];
            k.shift_down = shift;
            k.key_down   = usage != 0 && off >= lead && off < lead + DEMO_DOWN_MS;
            return k;
        }
        start += slot;
    }
    return k;   // past the last character: the dwell, nothing pressed
}

uint8_t demo_show_page(const demo_seg_t *s, uint32_t into) {
    const uint32_t page = into / DEMO_PAGE_MS;
    return (uint8_t)(page < s->pages ? page : s->pages);
}

bool demo_view_is_held(uint8_t view) {
    return view == DEMO_VIEW_FN || view == DEMO_VIEW_NUM || view == DEMO_VIEW_INTL;
}

bool demo_show_trigger_down(const demo_seg_t *s, uint32_t into) {
    if (s->kind != DEMO_SHOW || s->view == DEMO_VIEW_BASE) return false;
    if (demo_view_is_held(s->view)) return true;
    return into < DEMO_TAP_MS;
}

// ---- key demo -----------------------------------------------------------------

uint8_t demo_host_usage(char c) {
    if (c == '\t') return 0;   // Tab moves focus: the run would leave the editor
    return demo_ascii_usage(c, NULL);
}

uint16_t demo_type_strokes(const char *text) {
    if (text == NULL) return 0;
    uint16_t n = 0;
    while (text[n] != '\0') ++n;
    return (uint16_t)(n + 1u);   // + the closing Enter
}

uint32_t demo_stroke_press_ms(const char *text, uint16_t i) {
    uint32_t start = 0;
    uint16_t k     = 0;
    if (text == NULL) return 0;
    for (; k < i && text[k] != '\0'; ++k) start += demo_char_slot_ms(text, k);
    if (text[k] == '\0') return start;   // the Enter, as soon as typing ends
    // The same moment demo_type_keys() shows the key down: after the Shift lead for
    // a shifted character, although the host never receives that Shift.
    bool shift = false;
    demo_ascii_usage(text[k], &shift);
    return start + (shift ? DEMO_SHIFT_LEAD_MS : 0u);
}

uint8_t demo_stroke_usage(const char *text, uint16_t i) {
    const uint16_t n = demo_type_strokes(text);
    if (i + 1u >= n) return i + 1u == n ? (uint8_t)DEMO_USAGE_ENTER : 0u;
    return demo_host_usage(text[i]);
}
