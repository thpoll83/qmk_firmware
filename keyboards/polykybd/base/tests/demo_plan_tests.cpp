// Copyright 2026 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "gtest/gtest.h"

extern "C" {
#include "demo_plan.h"
}

#include <cstring>

namespace {

// config.h's FADE_TRANSITION_TIME. Not included (config.h is not pure); the idle
// segment has to outlast it or the demo wakes the board mid-fade and the idle style
// itself is never seen.
constexpr uint32_t kFadeMs = 10000;

const demo_seg_t &Seg(uint8_t i) { return demo_playlist[i]; }

// ---- the playlist as a whole ---------------------------------------------------

TEST(DemoPlaylist, OneLoopIsAboutFifteenMinutes) {
    const uint32_t cycle = demo_cycle_ms();
    EXPECT_GE(cycle, 14u * 60u * 1000u);
    EXPECT_LE(cycle, 16u * 60u * 1000u);
}

TEST(DemoPlaylist, SegmentsAreWellFormed) {
    ASSERT_GT(demo_playlist_len, 0u);
    for (uint8_t i = 0; i < demo_playlist_len; ++i) {
        const demo_seg_t &s = Seg(i);
        SCOPED_TRACE(int(i));
        EXPECT_LT(s.kind, DEMO_KIND_COUNT);
        EXPECT_LT(s.view, DEMO_VIEW_COUNT);
        EXPECT_LT(s.look, DEMO_LOOK_COUNT);
        if (s.kind == DEMO_TYPE) {
            ASSERT_NE(s.text, nullptr);
            EXPECT_GT(std::strlen(s.text), 0u);
            EXPECT_EQ(s.view, DEMO_VIEW_BASE);   // typing is shown on the base layout
            EXPECT_EQ(s.pages, 0u);
        } else {
            EXPECT_EQ(s.text, nullptr);
            EXPECT_GT(s.hold_ms, 0u);
        }
        if (s.kind == DEMO_SHOW) EXPECT_NE(s.view, DEMO_VIEW_BASE);   // a SHOW shows something
        if (s.kind == DEMO_IDLE) EXPECT_EQ(s.look, DEMO_LOOK_OWN);
        if (s.pages > 0) {
            EXPECT_TRUE(s.view == DEMO_VIEW_LANG_MENU || s.view == DEMO_VIEW_EMOJI);
            // Long enough to reach the last tab and dwell on it.
            EXPECT_GE(s.hold_ms, (uint32_t)(s.pages + 1u) * DEMO_PAGE_MS);
        }
    }
}

TEST(DemoPlaylist, EveryTypedCharacterHasAKey) {
    for (uint8_t i = 0; i < demo_playlist_len; ++i) {
        if (Seg(i).kind != DEMO_TYPE) continue;
        for (const char *p = Seg(i).text; *p; ++p) {
            bool shift = false;
            EXPECT_NE(demo_ascii_usage(*p, &shift), 0u) << "segment " << int(i) << " char '" << *p << "'";
        }
    }
}

// The request was "typing, menus, idle animation, then after a minute it types again":
// activity and idle alternate, every idle stretch is about a minute of the animation
// itself, and the loop ends idle so the next one opens with typing.
TEST(DemoPlaylist, ActivityAndIdleAlternate) {
    EXPECT_NE(Seg(0).kind, DEMO_IDLE);
    EXPECT_EQ(Seg(demo_playlist_len - 1).kind, DEMO_IDLE);
    uint32_t activity = 0;
    uint8_t  idles    = 0;
    for (uint8_t i = 0; i < demo_playlist_len; ++i) {
        const demo_seg_t &s = Seg(i);
        if (s.kind == DEMO_IDLE) {
            SCOPED_TRACE(int(i));
            EXPECT_GE(s.hold_ms, kFadeMs + 60000u);
            EXPECT_LE(s.hold_ms, kFadeMs + 90000u);
            EXPECT_GE(activity, 45000u);           // a real stretch of activity first
            EXPECT_LE(activity, 4u * 60u * 1000u); // ...but idle comes round regularly
            activity = 0;
            ++idles;
        } else {
            activity += demo_seg_ms(&s);
        }
    }
    EXPECT_GE(idles, 5u);
}

TEST(DemoPlaylist, CoversTheMenusAndTheTours) {
    bool view_seen[DEMO_VIEW_COUNT] = {};
    bool non_latin = false, script = false;
    for (uint8_t i = 0; i < demo_playlist_len; ++i) {
        view_seen[Seg(i).view] = true;
        if (Seg(i).look >= DEMO_LOOK_GREEK && Seg(i).look < DEMO_LOOK_TENGWAR) non_latin = true;
        if (Seg(i).look >= DEMO_LOOK_TENGWAR) script = true;
    }
    for (uint8_t v = 0; v < DEMO_VIEW_COUNT; ++v) EXPECT_TRUE(view_seen[v]) << "view " << int(v);
    EXPECT_TRUE(non_latin);
    EXPECT_TRUE(script);
}

// ---- ASCII -> key -----------------------------------------------------------------

TEST(DemoAscii, LettersDigitsAndShift) {
    bool sh = true;
    EXPECT_EQ(demo_ascii_usage('a', &sh), 0x04u); EXPECT_FALSE(sh);
    EXPECT_EQ(demo_ascii_usage('z', &sh), 0x1Du); EXPECT_FALSE(sh);
    EXPECT_EQ(demo_ascii_usage('Q', &sh), 0x14u); EXPECT_TRUE(sh);
    EXPECT_EQ(demo_ascii_usage('1', &sh), 0x1Eu); EXPECT_FALSE(sh);
    EXPECT_EQ(demo_ascii_usage('9', &sh), 0x26u); EXPECT_FALSE(sh);
    EXPECT_EQ(demo_ascii_usage('0', &sh), 0x27u); EXPECT_FALSE(sh);
    EXPECT_EQ(demo_ascii_usage(' ', &sh), 0x2Cu); EXPECT_FALSE(sh);
}

TEST(DemoAscii, SymbolsUseTheUsLayout) {
    bool sh = false;
    EXPECT_EQ(demo_ascii_usage('!', &sh), 0x1Eu); EXPECT_TRUE(sh);
    EXPECT_EQ(demo_ascii_usage('(', &sh), 0x26u); EXPECT_TRUE(sh);
    EXPECT_EQ(demo_ascii_usage(')', &sh), 0x27u); EXPECT_TRUE(sh);
    EXPECT_EQ(demo_ascii_usage('"', &sh), 0x34u); EXPECT_TRUE(sh);
    EXPECT_EQ(demo_ascii_usage('\'', &sh), 0x34u); EXPECT_FALSE(sh);
    EXPECT_EQ(demo_ascii_usage('{', &sh), 0x2Fu); EXPECT_TRUE(sh);
    EXPECT_EQ(demo_ascii_usage('\\', &sh), 0x31u); EXPECT_FALSE(sh);
    EXPECT_EQ(demo_ascii_usage('>', &sh), 0x37u); EXPECT_TRUE(sh);
    EXPECT_EQ(demo_ascii_usage('?', &sh), 0x38u); EXPECT_TRUE(sh);
}

TEST(DemoAscii, UntypeableIsZeroAndNeverShifted) {
    bool sh = true;
    EXPECT_EQ(demo_ascii_usage('\x01', &sh), 0u);
    EXPECT_FALSE(sh);
    EXPECT_EQ(demo_ascii_usage((char)0xE9, &sh), 0u);   // é is not a US key
    EXPECT_FALSE(sh);
    EXPECT_EQ(demo_ascii_usage('a', nullptr), 0x04u);   // the shift out-param is optional
}

// ---- typing timeline ----------------------------------------------------------------

TEST(DemoTyping, SlotsPauseAfterSpacesAndPunctuation) {
    // Same position, so the same jitter term is not what is being compared.
    for (uint16_t i = 0; i < 4; ++i) {
        const char letter[] = {'x', 'x', 'x', 'x', 0};
        char space[] = "xxxx", comma[] = "xxxx", stop[] = "xxxx";
        space[i] = ' '; comma[i] = ','; stop[i] = '.';
        const uint32_t l = demo_char_slot_ms(letter, i);
        EXPECT_GE(l, DEMO_KEY_MS);
        EXPECT_LT(l, DEMO_KEY_MS + DEMO_KEY_JITTER_MS);
        EXPECT_GT(demo_char_slot_ms(comma, i), demo_char_slot_ms(space, i) + 100u);
        EXPECT_GT(demo_char_slot_ms(stop, i), demo_char_slot_ms(comma, i) + 100u);
    }
}

TEST(DemoTyping, SlotIsLongEnoughForAShiftedPress) {
    // A shifted key goes down DEMO_SHIFT_LEAD_MS into its slot and stays DEMO_DOWN_MS:
    // the shortest slot must still contain the whole press, with a gap after it, or
    // two neighbouring keys would read as one long press.
    EXPECT_GT(DEMO_KEY_MS, DEMO_SHIFT_LEAD_MS + DEMO_DOWN_MS);
}

TEST(DemoTyping, TypeMsIsTheSumOfSlots) {
    const char *t = "Hi, you.";
    uint32_t sum = 0;
    for (uint16_t i = 0; t[i]; ++i) sum += demo_char_slot_ms(t, i);
    EXPECT_EQ(demo_type_ms(t), sum);
    EXPECT_EQ(demo_type_ms(""), 0u);
    EXPECT_EQ(demo_type_ms(nullptr), 0u);
}

TEST(DemoTyping, PlainKeyGoesDownAtOnceAndComesUp) {
    const char *t = "ab";
    demo_keys_t k = demo_type_keys(t, 0);
    EXPECT_EQ(k.ch, 'a');
    EXPECT_TRUE(k.key_down);
    EXPECT_FALSE(k.shift_down);
    k = demo_type_keys(t, DEMO_DOWN_MS - 1);
    EXPECT_TRUE(k.key_down);
    k = demo_type_keys(t, DEMO_DOWN_MS);
    EXPECT_EQ(k.ch, 'a');
    EXPECT_FALSE(k.key_down);   // released, still in a's slot
    k = demo_type_keys(t, demo_char_slot_ms(t, 0));
    EXPECT_EQ(k.ch, 'b');
    EXPECT_TRUE(k.key_down);
}

TEST(DemoTyping, ShiftLeadsTheShiftedKey) {
    const char *t = "aB";
    const uint32_t b0 = demo_char_slot_ms(t, 0);
    demo_keys_t k = demo_type_keys(t, b0);
    EXPECT_EQ(k.ch, 'B');
    EXPECT_TRUE(k.shift_down);
    EXPECT_FALSE(k.key_down);   // Shift first...
    k = demo_type_keys(t, b0 + DEMO_SHIFT_LEAD_MS);
    EXPECT_TRUE(k.shift_down);
    EXPECT_TRUE(k.key_down);    // ...then the key
    k = demo_type_keys(t, b0 + DEMO_SHIFT_LEAD_MS + DEMO_DOWN_MS);
    EXPECT_TRUE(k.shift_down);  // Shift outlasts the key, to the end of the slot
    EXPECT_FALSE(k.key_down);
    // The unshifted slot before it never shows Shift.
    for (uint32_t ms = 0; ms < b0; ms += 7) EXPECT_FALSE(demo_type_keys(t, ms).shift_down) << ms;
}

TEST(DemoTyping, ARunOfCapitalsHoldsShiftThroughout) {
    const char *t = "OK";
    const uint32_t total = demo_type_ms(t);
    for (uint32_t ms = 0; ms < total; ms += 3) EXPECT_TRUE(demo_type_keys(t, ms).shift_down) << ms;
}

TEST(DemoTyping, NothingIsPressedAfterTheLastKey) {
    const char *t = "Go!";
    const uint32_t end = demo_type_ms(t);
    for (uint32_t ms = end; ms < end + 5000; ms += 250) {
        const demo_keys_t k = demo_type_keys(t, ms);
        EXPECT_EQ(k.ch, 0);
        EXPECT_FALSE(k.key_down);
        EXPECT_FALSE(k.shift_down);
    }
}

TEST(DemoTyping, EveryKeyOfAPlaylistTextIsShownPressed) {
    // Walk a real text at 1 ms resolution: every character's key must be seen down at
    // least once, in order — a slot shorter than its own press would drop a key.
    for (uint8_t i = 0; i < demo_playlist_len; ++i) {
        if (Seg(i).kind != DEMO_TYPE) continue;
        const char *t = Seg(i).text;
        size_t      next = 0;
        char        last_down = 0;
        bool        was_down  = false;
        for (uint32_t ms = 0; ms < demo_type_ms(t); ++ms) {
            const demo_keys_t k = demo_type_keys(t, ms);
            if (k.key_down && (!was_down || k.ch != last_down)) {
                ASSERT_LT(next, std::strlen(t));
                EXPECT_EQ(k.ch, t[next]) << "segment " << int(i);
                ++next;
            }
            was_down  = k.key_down;
            last_down = k.ch;
        }
        EXPECT_EQ(next, std::strlen(t)) << "segment " << int(i);
    }
}

// ---- where the cycle is ----------------------------------------------------------------

TEST(DemoLocate, SegmentBoundariesAndWrap) {
    demo_pos_t p = demo_locate(0);
    EXPECT_EQ(p.seg, 0u);
    EXPECT_EQ(p.into, 0u);

    const uint32_t first = demo_seg_ms(&demo_playlist[0]);
    p = demo_locate(first - 1);
    EXPECT_EQ(p.seg, 0u);
    EXPECT_EQ(p.into, first - 1);
    p = demo_locate(first);
    EXPECT_EQ(p.seg, 1u);
    EXPECT_EQ(p.into, 0u);

    const uint32_t cycle = demo_cycle_ms();
    p = demo_locate(cycle - 1);
    EXPECT_EQ(p.seg, demo_playlist_len - 1);
    p = demo_locate(cycle);   // the loop starts over
    EXPECT_EQ(p.seg, 0u);
    EXPECT_EQ(p.into, 0u);
    p = demo_locate(3 * cycle + first + 5);
    EXPECT_EQ(p.seg, 1u);
    EXPECT_EQ(p.into, 5u);
}

TEST(DemoLocate, IntoNeverReachesTheSegmentLength) {
    const uint32_t cycle = demo_cycle_ms();
    for (uint32_t t = 0; t < cycle; t += 997) {
        const demo_pos_t p = demo_locate(t);
        ASSERT_LT(p.seg, demo_playlist_len);
        EXPECT_LT(p.into, demo_seg_ms(&demo_playlist[p.seg]));
    }
}

// ---- menus -------------------------------------------------------------------------------

TEST(DemoShow, PagesStepAndStopOnTheLastTab) {
    const demo_seg_t s = {DEMO_SHOW, DEMO_VIEW_EMOJI, DEMO_LOOK_OWN, 3, 20000, nullptr};
    EXPECT_EQ(demo_show_page(&s, 0), 0u);
    EXPECT_EQ(demo_show_page(&s, DEMO_PAGE_MS - 1), 0u);
    EXPECT_EQ(demo_show_page(&s, DEMO_PAGE_MS), 1u);
    EXPECT_EQ(demo_show_page(&s, 3 * DEMO_PAGE_MS), 3u);
    EXPECT_EQ(demo_show_page(&s, 19999), 3u);   // clamped, never past `pages`
    const demo_seg_t flat = {DEMO_SHOW, DEMO_VIEW_FN, DEMO_LOOK_OWN, 0, 5000, nullptr};
    EXPECT_EQ(demo_show_page(&flat, 4999), 0u);
}

TEST(DemoShow, HeldViewsKeepTheirKeyDownTappedMenusTapIt) {
    const demo_seg_t fn   = {DEMO_SHOW, DEMO_VIEW_FN, DEMO_LOOK_OWN, 0, 5000, nullptr};
    const demo_seg_t lang = {DEMO_SHOW, DEMO_VIEW_LANG_MENU, DEMO_LOOK_OWN, 2, 9000, nullptr};
    const demo_seg_t type = {DEMO_TYPE, DEMO_VIEW_BASE, DEMO_LOOK_OWN, 0, 1000, "x"};
    EXPECT_TRUE(demo_show_trigger_down(&fn, 0));
    EXPECT_TRUE(demo_show_trigger_down(&fn, 4999));
    EXPECT_TRUE(demo_show_trigger_down(&lang, 0));
    EXPECT_TRUE(demo_show_trigger_down(&lang, DEMO_TAP_MS - 1));
    EXPECT_FALSE(demo_show_trigger_down(&lang, DEMO_TAP_MS));
    EXPECT_FALSE(demo_show_trigger_down(&type, 0));
    EXPECT_TRUE(demo_view_is_held(DEMO_VIEW_NUM));
    EXPECT_TRUE(demo_view_is_held(DEMO_VIEW_INTL));
    EXPECT_FALSE(demo_view_is_held(DEMO_VIEW_EMOJI));
    EXPECT_FALSE(demo_view_is_held(DEMO_VIEW_BASE));
}

TEST(DemoExit, HoldIsDeliberateButShort) {
    EXPECT_GE(DEMO_EXIT_HOLD_MS, 1000u);   // not a tap a visitor makes by accident
    EXPECT_LE(DEMO_EXIT_HOLD_MS, 5000u);   // not a wait the operator gives up on
}

// ---- key demo (KC_DEMO_KEYS) ----------------------------------------------------

// The request: "without any modifier, so it writes into a notepad and never switches
// away". Every stroke the playlist can send is a plain key — a letter, digit, space,
// punctuation or Enter — and never Tab (focus), Esc, a modifier or a layer key.
TEST(DemoKeys, EveryStrokeIsAPlainNonModifierKey) {
    for (uint8_t i = 0; i < demo_playlist_len; ++i) {
        if (Seg(i).kind != DEMO_TYPE) continue;
        const char    *t = Seg(i).text;
        const uint16_t n = demo_type_strokes(t);
        ASSERT_EQ(n, std::strlen(t) + 1u);
        for (uint16_t k = 0; k < n; ++k) {
            SCOPED_TRACE(testing::Message() << "segment " << int(i) << " stroke " << k);
            const uint8_t u = demo_stroke_usage(t, k);
            EXPECT_GE(u, 0x04u);              // KC_A
            EXPECT_LE(u, 0x38u);              // KC_SLASH: no Caps Lock, F-key or modifier
            EXPECT_NE(u, 0x29u);              // Escape
            EXPECT_NE(u, 0x2Bu);              // Tab
            EXPECT_NE(u, 0x2Au);              // Backspace
        }
        EXPECT_EQ(demo_stroke_usage(t, (uint16_t)(n - 1u)), DEMO_USAGE_ENTER);
        EXPECT_EQ(demo_stroke_usage(t, n), 0u);   // past the end: nothing
    }
}

TEST(DemoKeys, ShiftIsDroppedNotSent) {
    EXPECT_EQ(demo_host_usage('H'), demo_host_usage('h'));
    EXPECT_EQ(demo_host_usage('('), demo_host_usage('9'));
    EXPECT_EQ(demo_host_usage('"'), demo_host_usage('\''));
    EXPECT_EQ(demo_host_usage('\t'), 0u);
    EXPECT_EQ(demo_host_usage('\x01'), 0u);
}

// Each stroke goes down at the moment the board shows its key down, so the keycap and
// the editor agree; the Enter goes down when typing ends, inside the segment.
TEST(DemoKeys, StrokesLandWhenTheKeyIsShownDown) {
    for (uint8_t i = 0; i < demo_playlist_len; ++i) {
        const demo_seg_t &s = Seg(i);
        if (s.kind != DEMO_TYPE) continue;
        SCOPED_TRACE(int(i));
        const uint16_t n    = demo_type_strokes(s.text);
        uint32_t       prev = 0;
        for (uint16_t k = 0; k + 1u < n; ++k) {
            const uint32_t at = demo_stroke_press_ms(s.text, k);
            EXPECT_GE(at, prev);
            prev = at;
            const demo_keys_t d = demo_type_keys(s.text, at);
            EXPECT_EQ(d.ch, s.text[k]);
            EXPECT_TRUE(d.key_down);
            // Lifted before the next stroke goes down: a doubled letter arrives twice.
            EXPECT_LT(at + DEMO_DOWN_MS, demo_stroke_press_ms(s.text, (uint16_t)(k + 1u)));
        }
        const uint32_t enter = demo_stroke_press_ms(s.text, (uint16_t)(n - 1u));
        EXPECT_EQ(enter, demo_type_ms(s.text));
        EXPECT_LE(enter + DEMO_DOWN_MS, demo_seg_ms(&s));
    }
}

}  // namespace
