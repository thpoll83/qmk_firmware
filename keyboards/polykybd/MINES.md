# Mines: the idle-screen mine sweeper

A mine sweeper on the keycaps, one key per cell. Code: `mines_mode.c` (start, keys,
timeouts, drawing) and `base/mines_plan.c` (the rules, pure, `make
test:polykybd_mines_plan`).

## How it plays

- **Start**: hold Hyper for 1 s (`MINES_START_HOLD_MS`) while an idle animation runs
  (`DISP_IDLE`: pulse, jitter, Eden). A shorter tap only wakes the board. The doom
  attract screensaver does not set `DISP_IDLE`, so it is not a start point.
  ⚠️ split42's default keymap has no Hyper key; map one in the layout editor.
- **Board**: every key above the thumb row, both halves side by side. split72 14×4,
  split42 12×3. The inner edges of the two halves are neighbours.
- **Tap** opens a cell (the first tap of a game always opens a 3×3 area). **Hold 300 ms**
  flags or unflags it at that moment, while the key is still down; the release then does
  nothing. A tap on an open number whose flags match opens its other neighbours.
- **Thumb row**: Hide (outermost left), the flags left, the level (`Lv N`, or `Boom`),
  New (outermost right). After a win New reads **Next** and starts the next level.
- **Levels**: level 1 has 9 mines (split72) or 6 (split42), and each level adds one,
  up to cells − 9 (47 / 27), the most that still leaves the first tap's 3×3 area clear.
  New, or a loss, goes back to level 1. Nothing is persisted: a reboot ends the game.
- **Leaving**: Hide, or 2 min without a key press (`MINES_INACTIVE_MS`), repaints the
  normal legends for 400 ms and then hands the keycaps to the idle animation. A key
  typed in those 400 ms keeps the board awake. Hold Hyper again to resume.
- Every key event is swallowed while the game runs; nothing reaches the host.

## How it works

- **The board is `poly_sync_t.mines`** (`mines_board_t`, 24 B: state, the hit mine,
  three 56-bit masks, the level). The master plays on its copy and the ordinary state
  sync carries it; each half draws its own keys from it in `update_displays()`
  (`mines_render_key()`), and `user_sync_poly_data_handler()` requests a refresh when
  it changes. Neighbour counts are derived from the mine mask, never stored.
- **The reveal animation** holds a move's newly opened cells back on the master
  (`s_pending`) and releases one per 35 ms, nearest the tapped key first
  (`mines_next_pending()`). The slave animates from the same sync with no extra state.
  The win is checked once the last cell is out (`mines_check_win()`).
- **Idle**: while the game runs, `mines_tick()` calls `update_performed()` every pass,
  so the board's own idle timeout cannot fade it; the game's 2 min timeout replaces
  it. Hiding backdates `last_update` past the fade so the idle style starts at once.
- **Cost** (vs the image before Mines): split72 +84 B RAM, +4.2 KB flash; split42
  +80 B RAM, +4.2 KB flash. No overlay-pool borrowing, so leaving the game needs no
  overlay re-send from the host.
