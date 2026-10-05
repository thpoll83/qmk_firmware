// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Pure playlist + timeline for the showroom DEMO MODE (KC_DEMO on the settings layer).
//
// The demo plays a ~15 minute playlist on a loop until someone holds Esc: typing that
// shows legends change under Shift, a language tour, the menus, the glyph scripts, and
// a minute of the configured idle animation between activities. In the plain demo nothing
// reaches the host: it is a show on the keycaps, not typing. The key demo (KC_DEMO_KEYS,
// below) plays the same show and also types the TYPE segments into the host.
//
// Deliberately free of quantum.h, the display stack and the keymap: the arithmetic here
// (where the cycle is, which character is down, when Shift is held) is the part a unit
// test can reach (`make test:polykybd_demo_plan`). The plan speaks in ABSTRACT views and
// looks; anim/demo_mode.c maps them to layers, languages and glyph scripts. Same seam as
// base/tutorial_plan.c.
//
// ⚠️ Both halves run this. The master chooses the segment and syncs its index; each half
// then times the segment from its OWN receipt of that index, because the two MCUs share
// no time base. That is why every function here takes "ms into the segment" rather than
// a wall clock.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// ---- what a segment does ----------------------------------------------------
enum demo_kind {
    DEMO_TYPE = 0,   // "type" `text`: key presses invert keycaps, Shift changes legends
    DEMO_SHOW,       // hold a view (a layer or a menu) on screen, optionally paging tabs
    DEMO_IDLE,       // hand the board to the configured idle style, then wake it
    DEMO_KIND_COUNT
};

// Which board the keycaps show. The binding maps each to a layer and to the key that
// opens it, so the opening key can be shown pressed.
enum demo_view {
    DEMO_VIEW_BASE = 0,
    DEMO_VIEW_FN,          // held, like MO(_FL)
    DEMO_VIEW_NUM,         // held, like MO(_NL)
    DEMO_VIEW_INTL,        // held, like MO(_ADDLANG1)
    DEMO_VIEW_LANG_MENU,   // tapped open, like KC_LANG; pages through the regions
    DEMO_VIEW_EMOJI,       // tapped open, like TO(_EMJ); pages through the categories
    DEMO_VIEW_UTIL,        // the utilities layer
    DEMO_VIEW_SETTINGS,    // the settings layer (where KC_DEMO itself lives)
    DEMO_VIEW_COUNT
};

// Legend override for the segment: the board's own language, a board-only language
// preview (never reported to the host), or a glyph script. The binding skips a look the
// flashed fonts cannot draw, so a board without the font pack still demos in Latin.
enum demo_look {
    DEMO_LOOK_OWN = 0,
    DEMO_LOOK_GERMAN,
    DEMO_LOOK_FRENCH,
    DEMO_LOOK_GREEK,
    DEMO_LOOK_RUSSIAN,
    DEMO_LOOK_UKRAINIAN,
    DEMO_LOOK_ARABIC,
    DEMO_LOOK_HEBREW,
    DEMO_LOOK_HINDI,
    DEMO_LOOK_THAI,
    DEMO_LOOK_JAPANESE,
    DEMO_LOOK_KOREAN,
    DEMO_LOOK_GEORGIAN,
    DEMO_LOOK_ARMENIAN,
    DEMO_LOOK_TENGWAR,
    DEMO_LOOK_RUNES,
    DEMO_LOOK_AUREBESH,
    DEMO_LOOK_CIRTH,
    DEMO_LOOK_SGA,
    DEMO_LOOK_IBMVGA,
    DEMO_LOOK_C64,
    DEMO_LOOK_AMIGA,
    DEMO_LOOK_BRAILLE,
    DEMO_LOOK_COUNT
};

typedef struct {
    uint8_t     kind;      // enum demo_kind
    uint8_t     view;      // enum demo_view (TYPE segments use DEMO_VIEW_BASE)
    uint8_t     look;      // enum demo_look
    uint8_t     pages;     // SHOW on a paged menu: tabs stepped through after the first
    uint32_t    hold_ms;   // SHOW/IDLE: the whole segment; TYPE: dwell after the last key
    const char *text;      // TYPE only: printable US-ASCII
} demo_seg_t;

// ---- timing (ms) --------------------------------------------------------------
#define DEMO_KEY_MS         150u   // base slot per character (~80 wpm before pauses)
#define DEMO_KEY_JITTER_MS   60u   // + 0..59, deterministic per position: no metronome
#define DEMO_SPACE_EXTRA_MS  60u   // after a space
#define DEMO_COMMA_EXTRA_MS 220u   // after , ; :
#define DEMO_STOP_EXTRA_MS  450u   // after . ! ?
#define DEMO_DOWN_MS         85u   // how long a key is shown pressed
#define DEMO_SHIFT_LEAD_MS   45u   // Shift goes down this long before a shifted key
#define DEMO_TAP_MS         140u   // a tapped menu key (Lang, Emoji) shown pressed
#define DEMO_PAGE_MS       2600u   // dwell on each tab of a paged menu
#define DEMO_EXIT_HOLD_MS  2000u   // hold Esc this long to leave the demo

// ---- the playlist -------------------------------------------------------------
extern const demo_seg_t demo_playlist[];
extern const uint8_t    demo_playlist_len;

// ---- arithmetic ---------------------------------------------------------------
// HID usage (QMK basic keycode, 0x04..0x38) for a US-ASCII character, and whether it
// needs Shift. 0 = the character cannot be typed (it then becomes a pause).
uint8_t  demo_ascii_usage(char c, bool *shift);

// Length of character slot `i` of `text` (the pause after it included).
uint32_t demo_char_slot_ms(const char *text, uint16_t i);
// Time to type all of `text`, without the segment's trailing dwell.
uint32_t demo_type_ms(const char *text);
// Whole segment length.
uint32_t demo_seg_ms(const demo_seg_t *s);
// Whole playlist length — one loop of the demo.
uint32_t demo_cycle_ms(void);

// Where a cycle is `t` ms after it started. `t` wraps on demo_cycle_ms().
typedef struct {
    uint8_t  seg;    // index into demo_playlist
    uint32_t into;   // ms into that segment
} demo_pos_t;
demo_pos_t demo_locate(uint32_t t);

// What a TYPE segment has pressed `into` ms after it started.
typedef struct {
    char ch;           // the character whose slot this is; 0 = typing is over
    bool key_down;     // its key is shown pressed right now
    bool shift_down;   // Shift is held right now (for the whole slot of a shifted char)
} demo_keys_t;
demo_keys_t demo_type_keys(const char *text, uint32_t into);

// ---- key demo (KC_DEMO_KEYS): the keystrokes the HOST receives -------------------
// The key demo plays the same playlist and also types each TYPE segment into whatever
// has focus, as a typing test for a text editor. It sends the key the board shows
// pressed and never a modifier: no Shift, no Ctrl/Alt/GUI, no layer key. So a shifted
// character arrives as its unshifted key ('H' -> h, '(' -> 9), and a long run cannot
// switch windows, close a tab or trigger a shortcut.
//
// A segment's strokes are its characters plus one Enter after the last, so each line
// lands on its own line in the editor. Stroke `i` (0..demo_type_strokes()-1) goes down
// at demo_stroke_press_ms() — the moment the board inverts that key — and the binding
// lifts it DEMO_DOWN_MS later.
#define DEMO_USAGE_ENTER 0x28u

// The usage the host receives for `c`, Shift dropped. 0 = send nothing: an untypable
// character, and Tab, which moves focus out of the editor.
uint8_t  demo_host_usage(char c);
// Characters + the closing Enter; 0 for NULL.
uint16_t demo_type_strokes(const char *text);
// ms into the segment when stroke `i` goes down (the Enter: when typing ends).
uint32_t demo_stroke_press_ms(const char *text, uint16_t i);
// The usage stroke `i` sends (DEMO_USAGE_ENTER for the last one).
uint8_t  demo_stroke_usage(const char *text, uint16_t i);

// SHOW: which tab of a paged menu is up (0..pages), and whether the key that opened the
// view is shown pressed (held views: the whole segment; tapped menus: DEMO_TAP_MS).
uint8_t demo_show_page(const demo_seg_t *s, uint32_t into);
bool    demo_view_is_held(uint8_t view);
bool    demo_show_trigger_down(const demo_seg_t *s, uint32_t into);
