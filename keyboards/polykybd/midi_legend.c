// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later
#include "midi_legend.h"

#include <stdbool.h>
#include <stddef.h>
#include "quantum_keycodes.h"
#include "lang/named_glyphs.h"

// The display-list ops these legends use (named_glyphs.h has the full set).
#define OP_MID   0x16u   // rest of the run in the 19 px UI face
#define OP_SMALL 0x10u   // rest of the run at half scale
#define OP_UP    0x0Cu   // cursor 2 px up
#define OP_DOWN  0x05u   // cursor 2 px down
#define OP_RIGHT 0x06u   // cursor 2 px right
#define OP_LEFT  0x08u   // cursor 2 px left

// The start values QMK's midi_init() uses, with the octave one higher as
// keyboard_post_init_user() sets it: shown until the first sync arrives.
#define START_OCTAVE   5u
#define START_VELOCITY 127u

static uint32_t s_buf[MIDI_LEGEND_MAX + 1];
static uint8_t  s_len;

static void put(uint32_t cp) {
    if (s_len < MIDI_LEGEND_MAX) s_buf[s_len++] = cp;
}
static void puts32(const uint32_t* s) {
    while (*s) put(*s++);
}
static void putn(uint32_t cp, uint8_t n) {
    while (n--) put(cp);
}
static void put_str(const char* s) {
    while (*s) put((uint8_t)*s++);
}
static void put_int(int16_t v, bool sign) {
    char d[6];
    uint8_t n = 0;
    if (v < 0) {
        put('-');
        v = (int16_t)-v;
    } else if (sign && v > 0) {
        put('+');
    }
    do {
        d[n++] = (char)('0' + v % 10);
        v      = (int16_t)(v / 10);
    } while (v && n < sizeof(d));
    while (n) put((uint8_t)d[--n]);
}

static const uint32_t* done(void) {
    s_buf[s_len] = 0;
    return s_buf;
}

// MID_TWO_LINE (named_glyphs.h): a small label and the value under it.
static void label(const char* text) {
    put(OP_MID);
    putn(OP_UP, 5);
    put_str(text);
}
static void value_line(void) {
    put('\r');
    put('\v');
    putn(OP_DOWN, 3);
    put(OP_RIGHT);
}

typedef struct {
    uint8_t octave;   // QMK index, 0 = OCTAVE_N2
    int8_t  transpose;
    uint8_t velocity;
    uint8_t channel;  // 0-based
} cfg_t;

static cfg_t unpack(uint32_t raw) {
    cfg_t c;
    if ((raw & MIDI_CFG_VALID) == 0) {
        c.octave    = START_OCTAVE;
        c.transpose = 0;
        c.velocity  = START_VELOCITY;
        c.channel   = 0;
        return c;
    }
    c.octave    = (uint8_t)(raw & 0xFF);
    c.transpose = (int8_t)((raw >> 8) & 0xFF);
    c.velocity  = (uint8_t)((raw >> 16) & 0xFF);
    c.channel   = (uint8_t)((raw >> 24) & 0x0F);
    return c;
}

// The octave MI_C plays at QMK octave index `idx`, in scientific pitch notation.
static int16_t sci_octave(uint8_t idx) {
    return (int16_t)idx - 1;
}

static const uint32_t* const STAFF[12] = {
    ICON_MIDI_STAFF_C,  ICON_MIDI_STAFF_CS, ICON_MIDI_STAFF_D,  ICON_MIDI_STAFF_DS,
    ICON_MIDI_STAFF_E,  ICON_MIDI_STAFF_F,  ICON_MIDI_STAFF_FS, ICON_MIDI_STAFF_G,
    ICON_MIDI_STAFF_GS, ICON_MIDI_STAFF_A,  ICON_MIDI_STAFF_AS, ICON_MIDI_STAFF_B,
};
static const char LETTER[12] = {'C', 'C', 'D', 'D', 'E', 'F', 'F', 'G', 'G', 'A', 'A', 'B'};
static const bool SHARP[12]  = {false, true, false, true, false, false, true, false, true, false, true, false};

// What QMK's midi_compute_note() will send, kept in 0..127 like its uint8_t would
// not be: a note off the end of the MIDI range is shown at the end it fell off.
static uint8_t note_of(uint16_t keycode, cfg_t c) {
    int16_t n = (int16_t)(12 * c.octave + (keycode - QK_MIDI_NOTE_C_0) + c.transpose);
    if (n < 0) n = 0;
    if (n > 127) n = 127;
    return (uint8_t)n;
}

// The staff first (xAdvance 0, it sits at x 42..71), then a big letter with a half-size
// sharp raised beside it and the octave below the sharp. Measured with the courtyard
// model: no letter, sharp or octave box comes within 4 px of the staff's.
static void note_legend(uint8_t note) {
    const uint8_t pc = note % 12;
    puts32(STAFF[pc]);
    put(' ');
    put((uint8_t)LETTER[pc]);
    put(OP_SMALL);
    if (SHARP[pc]) {
        putn(OP_UP, 6);
        put('#');
        putn(OP_DOWN, 6);
        putn(OP_LEFT, 4);
    }
    put_int((int16_t)(note / 12) - 1, false);
}

// A setting that shows its current value: [symbol] LABEL / value.
static void setting(const uint32_t* symbol, const char* text, int16_t v, bool sign) {
    if (symbol) puts32(symbol);
    label(text);
    value_line();
    put_int(v, sign);
}

static void labelled_symbol(const uint32_t* symbol, const char* text) {
    puts32(symbol);
    label(text);
}

const uint32_t* midi_legend(uint16_t keycode, uint32_t raw) {
    if (keycode < QK_MIDI || keycode > QK_MIDI_MAX) return NULL;
    const cfg_t c = unpack(raw);
    s_len         = 0;

    if (keycode >= QK_MIDI_NOTE_C_0 && keycode <= QK_MIDI_NOTE_B_5) {
        note_legend(note_of(keycode, c));
        return done();
    }
    if (keycode >= QK_MIDI_OCTAVE_N2 && keycode <= QK_MIDI_OCTAVE_7) {
        setting(NULL, "OCT", sci_octave((uint8_t)(keycode - QK_MIDI_OCTAVE_N2)), false);
        return done();
    }
    if (keycode >= QK_MIDI_TRANSPOSE_N6 && keycode <= QK_MIDI_TRANSPOSE_6) {
        setting(NULL, "TRNS", (int16_t)(keycode - QK_MIDI_TRANSPOSE_0), true);
        return done();
    }
    if (keycode >= QK_MIDI_VELOCITY_0 && keycode <= QK_MIDI_VELOCITY_10) {
        const uint8_t level = (uint8_t)(keycode - QK_MIDI_VELOCITY_0);
        // The levels musicians have a mark for, as the mark; the rest as a number.
        switch (level) {
            case 2:  puts32(ICON_MIDI_PP); return done();
            case 3:  puts32(ICON_MIDI_P);  return done();
            case 5:  puts32(ICON_MIDI_MP); return done();
            case 6:  puts32(ICON_MIDI_MF); return done();
            case 8:  puts32(ICON_MIDI_F);  return done();
            case 10: puts32(ICON_MIDI_FF); return done();
            default: break;
        }
        // compute_velocity() in process_midi.c: level * (128 / 10)
        setting(NULL, "VEL", (int16_t)(level * (128 / 10)), false);
        return done();
    }
    if (keycode >= QK_MIDI_CHANNEL_1 && keycode <= QK_MIDI_CHANNEL_16) {
        setting(NULL, "CHAN", (int16_t)(keycode - QK_MIDI_CHANNEL_1 + 1), false);
        return done();
    }

    switch (keycode) {
        case QK_MIDI_OCTAVE_DOWN:     setting(ICON_MIDI_8VB, "OCT", sci_octave(c.octave), false); break;
        case QK_MIDI_OCTAVE_UP:       setting(ICON_MIDI_8VA, "OCT", sci_octave(c.octave), false); break;
        case QK_MIDI_TRANSPOSE_DOWN:  setting(ICON_MIDI_FLAT, "TRNS", c.transpose, true); break;
        case QK_MIDI_TRANSPOSE_UP:    setting(ICON_MIDI_SHARP, "TRNS", c.transpose, true); break;
        case QK_MIDI_VELOCITY_DOWN:   setting(ICON_MIDI_DIM, "VEL", c.velocity, false); break;
        case QK_MIDI_VELOCITY_UP:     setting(ICON_MIDI_CRESC, "VEL", c.velocity, false); break;
        case QK_MIDI_CHANNEL_DOWN:
        case QK_MIDI_CHANNEL_UP:
            label("CHAN");
            value_line();
            put(keycode == QK_MIDI_CHANNEL_UP ? '+' : '-');
            put(' ');
            put_int((int16_t)(c.channel + 1), false);
            break;
        case QK_MIDI_SUSTAIN:         puts32(ICON_MIDI_PED); break;
        case QK_MIDI_SOSTENUTO:       puts32(ICON_MIDI_SOST); break;
        case QK_MIDI_SOFT:            puts32(ICON_MIDI_UC); break;
        case QK_MIDI_LEGATO:          labelled_symbol(ICON_MIDI_LEGATO, "LEG"); break;
        case QK_MIDI_PORTAMENTO:      labelled_symbol(ICON_MIDI_PORTA, "PORT"); break;
        case QK_MIDI_MODULATION:      labelled_symbol(ICON_MIDI_WAVE, "MOD"); break;
        case QK_MIDI_PITCH_BEND_DOWN: labelled_symbol(ICON_MIDI_BEND_DOWN, "BEND"); break;
        case QK_MIDI_PITCH_BEND_UP:   labelled_symbol(ICON_MIDI_BEND_UP, "BEND"); break;
        case QK_MIDI_MODULATION_SPEED_DOWN:
        case QK_MIDI_MODULATION_SPEED_UP:
            label("MOD");
            value_line();
            put_str(keycode == QK_MIDI_MODULATION_SPEED_UP ? "spd +" : "spd -");
            break;
        case QK_MIDI_ALL_NOTES_OFF:
            // MID_TWO_WORD("All", "Off")
            put(OP_MID);
            putn(OP_UP, 4);
            put_str("All");
            put('\r');
            put('\v');
            putn(OP_DOWN, 4);
            put_str("Off");
            break;
        case QK_MIDI_ON:     label("MIDI"); value_line(); put_str("on");  break;
        case QK_MIDI_OFF:    label("MIDI"); value_line(); put_str("off"); break;
        case QK_MIDI_TOGGLE: label("MIDI"); value_line(); put_str("tog"); break;
        default:             return NULL;
    }
    return done();
}
