#!/usr/bin/env python3
# Offline model of the split72 per-keycap DISPLAY geometry + the IDDQD attract
# screensaver's placement (doom_blit.c select_display_placed). Built because the
# OLED chip-select does NOT track the RGB g_led_config x-order, and reasoning from
# the RGB layout produced several wrong firmware revisions before this model was
# validated against hardware. Run it to (a) print disp_idx -> physical position,
# and (b) verify the screensaver covers every real display (both thumbs + the
# staggered in-between key) with the doom frame reading LEFT->RIGHT on both halves.
#
# The three composed mappings (all from the firmware / keyboard.json):
#   1. chip-select (split72.c BITMASK):  disp_idx = disp_row*8 + disp_col
#   2. invert_display:  matrix(r,c) -> disp_idx.  Right rows 5..8 apply `c--`;
#      row 9 and all left rows do not.  Inverted here to recover the matrix key.
#   3. g_led_config LED->physical position (x = LX first arg / 2, y).
#
# KEY FACTS this surfaces (the ones that cost the firmware revisions):
#   * disp_col 0 is the OUTER edge on the LEFT half but the INNER edge on the RIGHT
#     (the c-- fold reverses the right's upper rows) — so a naive contiguous sweep
#     runs backwards on the right, and the doom frame must be column-mirrored on the
#     right half to read left->right.
#   * only the bottom matrix row is a full 8-wide row; the upper rows have no OLED
#     at their inner phantom column.
#   * the bottom row is physically STAGGERED vs the rows above (a gap where an upper
#     key has no key below it) and the two inner thumb keys are STACKED (same x, two
#     heights) inboard of everything, both electrically on the bottom matrix row.

NO = None
# matrix[row][col] -> LED index (keyboard.json / g_led_config, key-matrix block)
MATRIX = [
    [6, 5, 4, 3, 2, 1, 0, NO], [13, 12, 11, 10, 9, 8, 7, NO],
    [20, 19, 18, 17, 16, 15, 14, NO], [27, 26, 25, 24, 23, 22, 21, NO],
    [35, 34, 33, 32, 31, 30, 29, 28],
    [NO, 42, 41, 40, 39, 38, 37, 36], [NO, 49, 48, 47, 46, 45, 44, 43],
    [NO, 56, 55, 54, 53, 52, 51, 50], [NO, 63, 62, 61, 60, 59, 58, 57],
    [71, 70, 69, 68, 67, 66, 65, 64],
]
# LED index -> (x_raw, y); physical x = x_raw / 2  (g_led_config position block)
POS_RAW = [
    (144, 9), (129, 9), (104, 5), (79, 1), (55, 5), (30, 9), (0, 9),
    (144, 33), (129, 33), (104, 19), (79, 25), (55, 29), (30, 33), (0, 33),
    (144, 58), (129, 58), (104, 54), (79, 50), (55, 54), (30, 58), (0, 58),
    (144, 83), (129, 83), (104, 79), (79, 75), (55, 79), (30, 83), (0, 83),
    (170, 99), (170, 127), (144, 118), (129, 113), (79, 99), (55, 103), (30, 107), (6, 107),
    (446, 9), (415, 9), (390, 5), (365, 1), (341, 5), (316, 9), (286, 9),
    (446, 33), (415, 33), (390, 19), (365, 25), (341, 29), (316, 33), (286, 33),
    (446, 58), (415, 58), (390, 54), (365, 50), (341, 54), (316, 58), (286, 58),
    (446, 83), (415, 83), (390, 79), (365, 75), (341, 79), (316, 83), (286, 83),
    (440, 107), (415, 107), (390, 103), (365, 99), (324, 113), (290, 118), (264, 127), (264, 99),
]

# ---- firmware geometry constants (doom_blit.c / doom_mode.c) ----
DOOM_VIEW_COLS = 5
DOOM_VIEW_ROWS = 5
MATRIX_COLS = 8
NUM_SHIFT_REGISTERS = 5
SAVER_INSETS = 4     # inset 0..3 (DOOM_SAVER_INSETS)


def disp_to_matrix(left, dr, dc):
    """Invert invert_display: display (dr,dc) -> matrix (r,c). None if off-grid."""
    if left:
        return (dr, dc)
    mr = dr + 5
    mc = dc + 1 if dr < 4 else dc   # undo the c-- applied to right rows 5..8
    return (mr, mc)


def disp_pos(left, dr, dc):
    """Physical (x, y) of display (dr, dc), or None for a routing phantom."""
    mr, mc = disp_to_matrix(left, dr, dc)
    if not (0 <= mc <= 7):
        return None
    led = MATRIX[mr][mc]
    if led is None:
        return None
    xr, y = POS_RAW[led]
    return (xr / 2.0, y)


def select_display_placed(left, view_row, view_col, row_off, inset):
    """Port of doom_blit.c select_display_placed (the inset>=0 screensaver path).
    Returns (disp_row, disp_col) that lights, or None."""
    disp_row = view_row + row_off
    # Content mirror on the RIGHT half so the frame reads left->right (v58).
    vc = view_col if left else (DOOM_VIEW_COLS - 1 - view_col)
    grid = vc + inset
    if grid == 7:                        # stacked thumb column
        if disp_row == 3:
            tcol = 7 if left else 0      # upper thumb
        elif disp_row == 4:
            tcol = 6 if left else 1      # lower thumb
        else:
            return None
        return (4, tcol)                 # both thumbs live on disp_row 4
    if grid > 7:
        return None
    if disp_row < 4:                     # upper rows: 7 real cols, left up / right down
        if grid > 6:
            return None
        disp_col = grid if left else (6 - grid)
    else:                                # bottom row: staggered, GAP where no key sits
        bottom_left = [0, 1, 2, 3, 0xFF, 4, 5]
        bottom_right = [7, 6, 5, 4, 0xFF, 3, 2]
        c = bottom_left[grid] if left else bottom_right[grid]
        if c == 0xFF:
            return None
        disp_col = c
    if not (0 <= disp_col < MATRIX_COLS):
        return None
    if disp_row * 8 + disp_col >= NUM_SHIFT_REGISTERS * 8:
        return None
    return (disp_row, disp_col)


def _real_displays(left):
    return {(dr, dc) for dr in range(5) for dc in range(8) if disp_pos(left, dr, dc)}


def verify():
    ok = True
    for left in (True, False):
        half = "LEFT" if left else "RIGHT"
        real = _real_displays(left)
        covered = set()
        # the screensaver renders 4 view rows (skip_bottom_row) over the cycle
        for row_off in (0, 1):
            for inset in range(SAVER_INSETS):
                for vr in range(DOOM_VIEW_ROWS - 1):
                    for vc in range(DOOM_VIEW_COLS):
                        r = select_display_placed(left, vr, vc, row_off, inset)
                        if r:
                            covered.add(r)
        missing = real - covered
        thumbs = {(4, 6), (4, 7)} if left else {(4, 0), (4, 1)}
        thumbs_ok = thumbs <= covered
        # content order: for a placement, lit keycaps' physical x must rise with view_col
        order_ok = True
        for row_off in (0, 1):
            for inset in range(SAVER_INSETS):
                for vr in range(DOOM_VIEW_ROWS - 1):
                    xs = []
                    for vc in range(DOOM_VIEW_COLS):
                        r = select_display_placed(left, vr, vc, row_off, inset)
                        if r:
                            xs.append(disp_pos(left, *r)[0])
                    if xs != sorted(xs):
                        order_ok = False
        print(f"{half}: {len(covered)}/{len(real)} real displays covered; "
              f"thumbs lit={thumbs_ok}; content left->right={order_ok}; "
              f"uncovered={sorted(missing) if missing else 'none'}")
        ok = ok and not missing and thumbs_ok and order_ok
    print("\nRESULT:", "ALL GOOD ✓" if ok else "MISMATCH ✗")
    return ok


def dump_positions():
    for left in (True, False):
        print("=" * 64)
        print(("LEFT" if left else "RIGHT") + " HALF — physical (x,y) per [disp_row][disp_col]  ('--' = phantom)")
        for dr in range(5):
            cells = []
            for dc in range(8):
                p = disp_pos(left, dr, dc)
                cells.append("   --   " if p is None else f"({p[0]:>4.0f},{p[1]:>3})")
            print(f"  row {dr}: " + " ".join(cells))


if __name__ == "__main__":
    dump_positions()
    print()
    raise SystemExit(0 if verify() else 1)
