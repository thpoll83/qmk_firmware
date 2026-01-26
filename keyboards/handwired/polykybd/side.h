#pragma once

#include <stdbool.h>

enum side_state { UNDECIDED, LEFT_SIDE, RIGHT_SIDE };

bool side_is_undecided(void);

void set_side(enum side_state s);

bool is_left_side(void);

bool is_right_side(void);

enum key_split_pos { POS_NOT_FOUND, POS_LEFT, POS_RIGHT, POS_ON_BOTH };

const char* pos_to_str(enum key_split_pos pos);

bool is_on_current_side(enum key_split_pos pos);

bool is_on_other_side(enum key_split_pos pos);
