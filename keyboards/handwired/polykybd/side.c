// Copyright 2025 thpoll83
// SPDX-License-Identifier: GPL-2.0-or-later

#include "side.h"

static enum side_state side = UNDECIDED;

bool side_is_undecided(void) {
    return side == UNDECIDED;
}

void set_side(enum side_state s) {
    side = s;
}

bool is_left_side(void) {
    return side == LEFT_SIDE;
}

bool is_right_side(void) {
    return side == RIGHT_SIDE;
}

const char* pos_to_str(enum key_split_pos pos) {
    switch (pos)
    {
    case POS_LEFT: return "L";
    case POS_RIGHT: return "R";
    case POS_ON_BOTH: return "B";
    default: return "?";
    }
}

bool is_on_current_side(enum key_split_pos pos) {
    return pos==POS_ON_BOTH || (is_left_side() && pos==POS_LEFT) || (is_right_side() && pos ==POS_RIGHT);
}

bool is_on_other_side(enum key_split_pos pos) {
    return pos==POS_ON_BOTH || (is_left_side() && pos==POS_RIGHT) || (is_right_side() && pos ==POS_LEFT);
}
