/* Copyright 2019
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

// Pull in the active variant's main header (struct display_info, the BITMASK*
// macros and the POLY_DISP_ROW_* / POLY_SPLASH_* parameters) so any source that
// reaches us through QMK_KEYBOARD_H — in particular the shared poly_keymap.c —
// sees the variant definitions. QMK passes -DKEYBOARD_<filesafe-name>.
#if defined(KEYBOARD_handwired_polykybd_split72)
#    include "split72/split72.h"
#elif defined(KEYBOARD_handwired_polykybd_split42)
#    include "split42/split42.h"
#elif defined(KEYBOARD_handwired_polykybd_corne42)
#    include "corne42/corne42.h"
#endif

