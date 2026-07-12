#!/usr/bin/env python3
"""Offline preview of the proposed split72 boot / idle demoscene animation.

This renders the *proposed* effect (sparkle dust -> converge -> letters
dither-dissolve in, over a faint plasma) across the real per-keycap OLED layout,
so the look can be signed off BEFORE any firmware is written.

Fidelity choices (kept honest):
  * Key positions are the real g_led_config physical coordinates copied from
    doom/tools/keycap_dispmap.py (POS_RAW). Left = LED 0..35, right = 36..71.
    So the stagger, the stacked thumbs and the split-gap are the true geometry.
  * Every panel is the native 72x40 px, 1-bit. The smooth effect fields are
    thresholded to on/off through the SAME 4x4 ordered Bayer matrix the firmware
    blitter uses (doom_blit.c BAYER4) -- that's what makes it read on mono and
    not "crawl" between frames.
  * The effect is evaluated per-pixel in ONE global board space, so a sparkle or
    plasma wave is continuous across panels (minus the real physical gaps) --
    exactly the "render locally from a shared clock" model the firmware would use.

This is a design mock, NOT the firmware renderer. It uses floats/numpy for
convenience; the firmware port would be fixed-point (sin8/cos8) + the same dither.
"""
import argparse
import numpy as np
from PIL import Image, ImageDraw, ImageFont

# ---- real physical key positions (from doom/tools/keycap_dispmap.py POS_RAW) ----
# (x_raw, y); left half = indices 0..35, right half = 36..71.
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
NKEYS = len(POS_RAW)          # 72
PANEL_W, PANEL_H = 72, 40     # native per-keycap OLED resolution

# matrix[row][col] -> LED index (from keycap_dispmap.py). Used to map the logical
# display grid (disp_row, disp_col) to a physical key, exactly like the firmware's
# show_splash_screen places the splash words.
_NO = None
MATRIX = [
    [6, 5, 4, 3, 2, 1, 0, _NO], [13, 12, 11, 10, 9, 8, 7, _NO],
    [20, 19, 18, 17, 16, 15, 14, _NO], [27, 26, 25, 24, 23, 22, 21, _NO],
    [35, 34, 33, 32, 31, 30, 29, 28],
    [_NO, 42, 41, 40, 39, 38, 37, 36], [_NO, 49, 48, 47, 46, 45, 44, 43],
    [_NO, 56, 55, 54, 53, 52, 51, 50], [_NO, 63, 62, 61, 60, 59, 58, 57],
    [71, 70, 69, 68, 67, 66, 65, 64],
]


def disp_led(left, dr, dc):
    """Logical display (dr,dc) -> LED/key index (0..71), or None (phantom)."""
    if left:
        mr, mc = dr, dc
    else:
        mr = dr + 5
        mc = dc + 1 if dr < 4 else dc   # undo the right-half c-- fold
    if not (0 <= mc <= 7):
        return None
    return MATRIX[mr][mc]

# 4x4 ordered Bayer matrix, normalized to (0,1) thresholds -- same one the
# firmware blitter (doom_blit.c) uses.
BAYER4 = np.array([[0, 8, 2, 10],
                   [12, 4, 14, 6],
                   [3, 11, 1, 9],
                   [15, 7, 13, 5]], dtype=np.float32) / 16.0

OLED_RGB = np.array([206, 224, 255], dtype=np.float32)   # cool-white lit pixel


def smooth(a, b, x):
    """smoothstep(a,b,x) -> 0..1"""
    t = np.clip((x - a) / (b - a + 1e-9), 0.0, 1.0)
    return t * t * (3 - 2 * t)


class Layout:
    """Maps the real key positions into a preview canvas and groups them into
    physical rows per half so the splash words can be placed faithfully."""

    def __init__(self, scale=3.2, margin=46):
        self.scale = scale
        self.margin = margin
        xs = [p[0] for p in POS_RAW]
        ys = [p[1] for p in POS_RAW]
        self.minx, self.miny = min(xs), min(ys)
        # panel top-left in preview px
        self.origin = []
        for (x, y) in POS_RAW:
            ox = margin + (x - self.minx) * scale
            oy = margin + (y - self.miny) * scale
            self.origin.append((ox, oy))
        maxox = max(o[0] for o in self.origin) + PANEL_W
        maxoy = max(o[1] for o in self.origin) + PANEL_H
        self.W = int(maxox + margin)
        self.H = int(maxoy + margin)
        self.center = [(ox + PANEL_W / 2, oy + PANEL_H / 2) for (ox, oy) in self.origin]

def assign_letters(layout):
    """Assign the real splash to keys the way show_splash_screen does:
    left  POLY @ disp_row1 col1, KYBD @ disp_row2 col1;
    right SPLIT @ disp_row1 col1, ' 7 2' @ disp_row3 col1."""
    targets = {}   # key index -> character
    plan = [
        (True, 1, 1, "POLY"),
        (True, 2, 1, "KYBD"),
        (False, 1, 1, "SPLIT"),
        (False, 3, 1, " 7 2"),
    ]
    for left, dr, col, msg in plan:
        for k, ch in enumerate(msg):
            if ch == " ":
                continue
            led = disp_led(left, dr, col + k)
            if led is not None:
                targets[led] = ch
    return targets


def render_glyph_masks(targets, font_path):
    """Rasterize each assigned letter into a 40x72 boolean mask (centered)."""
    font = ImageFont.truetype(font_path, 34)
    masks = {}
    for idx, ch in targets.items():
        img = Image.new("L", (PANEL_W, PANEL_H), 0)
        d = ImageDraw.Draw(img)
        bb = d.textbbox((0, 0), ch, font=font)
        w, h = bb[2] - bb[0], bb[3] - bb[1]
        d.text(((PANEL_W - w) / 2 - bb[0], (PANEL_H - h) / 2 - bb[1]), ch,
               fill=255, font=font)
        masks[idx] = (np.asarray(img) > 127)
    return masks


class Effect:
    """The procedural effect. All fields are functions of (global x, global y, t)
    so they're continuous across panels."""

    def __init__(self, layout, targets, seed=1234, mode="boot"):
        self.L = layout
        self.mode = mode
        rng = np.random.default_rng(seed)
        n = 240 if mode == "boot" else 90
        self.n = n
        # each particle homes to a random letter-key center (boot) or roams (idle)
        if targets and mode == "boot":
            homes = list(targets.keys())
            self.home = np.array([layout.center[rng.choice(homes)] for _ in range(n)],
                                 dtype=np.float32)
        else:
            self.home = np.array([(rng.uniform(0, layout.W), rng.uniform(0, layout.H))
                                  for _ in range(n)], dtype=np.float32)
        self.phase = rng.uniform(0, 2 * np.pi, n).astype(np.float32)
        self.spin = rng.uniform(0.6, 1.6, n).astype(np.float32) * rng.choice([-1, 1], n)
        self.rad = rng.uniform(0.35, 1.0, n).astype(np.float32)
        self.tw = rng.uniform(2.0, 5.0, n).astype(np.float32)      # twinkle rate
        self.drx = rng.uniform(0.3, 1.1, n).astype(np.float32) * rng.choice([-1, 1], n)
        self.dry = rng.uniform(0.3, 1.1, n).astype(np.float32) * rng.choice([-1, 1], n)

    def converge(self, tt):
        if self.mode != "boot":
            return np.float32(0.0)
        # dust (0..0.32) -> converge (0.32..0.62) -> settled
        return smooth(0.30, 0.62, tt)

    def particles(self, tt):
        """Return (pos Nx2, brightness N) at normalized time tt in [0,1)."""
        L = self.L
        c = self.converge(tt)
        R0 = 0.16 * min(L.W, L.H)          # dust cloud radius
        radius = self.rad * R0 * (1.0 - c)
        # wandering angle + secondary wobble
        ang = self.phase + tt * 6.283 * self.spin
        drift_amp = (1.0 - c) * 0.10 * min(L.W, L.H)
        dx = np.cos(ang) * radius + np.sin(tt * 6.283 * self.drx + self.phase) * drift_amp
        dy = np.sin(ang) * radius + np.cos(tt * 6.283 * self.dry + self.phase) * drift_amp
        pos = self.home + np.stack([dx, dy], axis=1)
        # brightness: twinkle; dust bright, settle fades (letters take over)
        tw = 0.5 + 0.5 * np.sin(tt * 6.283 * self.tw + self.phase)
        if self.mode == "boot":
            env = (0.6 + 0.4 * tw)
            env *= (1.0 - smooth(0.72, 1.0, tt) * 0.8)      # fade after arrival
            # arrival burst
            env += smooth(0.55, 0.66, tt) * (1 - smooth(0.66, 0.8, tt)) * 0.8 * tw
        else:
            env = 0.35 + 0.35 * tw
        return pos, np.clip(env, 0, 1.4).astype(np.float32)

    def plasma(self, gx, gy, tt):
        """Faint background plasma intensity 0..1 over global coords."""
        t = tt * 6.283
        cx, cy = self.L.W * 0.5, self.L.H * 0.5
        r = np.sqrt((gx - cx) ** 2 + (gy - cy) ** 2)
        v = (np.sin(gx * 0.020 + t) +
             np.sin(gy * 0.028 - t * 0.8) +
             np.sin((gx + gy) * 0.017 + t * 0.6) +
             np.sin(r * 0.024 - t * 1.3))
        return 0.5 + 0.125 * v


def render_frame(layout, eff, targets, masks, tt):
    L = layout
    board = np.zeros((L.H, L.W, 3), dtype=np.float32)
    # background board tint
    board[:] = (10, 11, 14)

    pos, pb = eff.particles(tt)

    # plasma strength ramps in then eases for boot; steady for idle
    if eff.mode == "boot":
        pgain = 0.10 + 0.16 * smooth(0.0, 0.5, tt)
        letter_on = smooth(0.50, 0.62, tt)     # when letters start dissolving in
    else:
        pgain = 0.22
        letter_on = 0.0

    for idx in range(NKEYS):
        ox, oy = L.origin[idx]
        ox_i, oy_i = int(round(ox)), int(round(oy))
        # global pixel coords for this panel
        gx = ox + np.arange(PANEL_W)[None, :]
        gy = oy + np.arange(PANEL_H)[:, None]
        bay = np.tile(BAYER4, (PANEL_H // 4 + 1, PANEL_W // 4 + 1))[:PANEL_H, :PANEL_W]

        # --- background plasma, dithered sparse ---
        pv = eff.plasma(gx, gy, tt) * pgain
        lit = (pv > bay).astype(np.float32) * 0.5     # dim texture

        # --- sparkles: gaussian cores of nearby particles ---
        sel = ((pos[:, 0] > ox - 4) & (pos[:, 0] < ox + PANEL_W + 4) &
               (pos[:, 1] > oy - 4) & (pos[:, 1] < oy + PANEL_H + 4))
        if np.any(sel):
            px = pos[sel, 0] - ox
            py = pos[sel, 1] - oy
            pbr = pb[sel]
            d2 = ((np.arange(PANEL_W)[None, None, :] - px[:, None, None]) ** 2 +
                  (np.arange(PANEL_H)[None, :, None] - py[:, None, None]) ** 2)
            spark = (np.exp(-d2 / (2 * 1.1 ** 2)) * pbr[:, None, None]).sum(axis=0)
            lit = np.maximum(lit, np.clip(spark, 0, 1))

        # --- letter, dither-dissolve reveal ---
        if idx in masks and letter_on > 0:
            # left->right wipe: stagger reveal by panel x
            xnorm = ox / L.W
            rf = np.clip((letter_on - 0.0) * 1.6 - xnorm * 0.5, 0, 1)
            reveal = (bay < rf)
            lit = np.maximum(lit, (masks[idx] & reveal).astype(np.float32))

        # compose panel -> board with a soft bloom
        panel = np.clip(lit, 0, 1)
        # bloom: cheap 3x3 spread
        bloom = panel.copy()
        bloom[1:, :] = np.maximum(bloom[1:, :], panel[:-1, :] * 0.35)
        bloom[:-1, :] = np.maximum(bloom[:-1, :], panel[1:, :] * 0.35)
        bloom[:, 1:] = np.maximum(bloom[:, 1:], panel[:, :-1] * 0.35)
        bloom[:, :-1] = np.maximum(bloom[:, :-1], panel[:, 1:] * 0.35)
        val = np.clip(panel + bloom * 0.5, 0, 1)

        cell = board[oy_i:oy_i + PANEL_H, ox_i:ox_i + PANEL_W]
        rgb = val[:, :, None] * OLED_RGB[None, None, :]
        # black OLED background inside the panel rectangle
        panelbg = np.array([2, 3, 5], dtype=np.float32)
        cell[:] = np.maximum(panelbg[None, None, :], rgb)
        # thin bezel
        board[oy_i - 1, ox_i - 1:ox_i + PANEL_W + 1] = (26, 28, 34)
        board[oy_i + PANEL_H, ox_i - 1:ox_i + PANEL_W + 1] = (26, 28, 34)
        board[oy_i - 1:oy_i + PANEL_H + 1, ox_i - 1] = (26, 28, 34)
        board[oy_i - 1:oy_i + PANEL_H + 1, ox_i + PANEL_W] = (26, 28, 34)

    return Image.fromarray(np.clip(board, 0, 255).astype(np.uint8), "RGB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mode", choices=["boot", "idle"], default="boot")
    ap.add_argument("--out", default="boot_preview.gif")
    ap.add_argument("--frames", type=int, default=96)
    ap.add_argument("--fps", type=int, default=24)
    ap.add_argument("--scale", type=float, default=3.2)
    ap.add_argument("--downscale", type=float, default=0.62)
    ap.add_argument("--font", default="/usr/share/fonts/truetype/freefont/FreeSansBold.ttf")
    ap.add_argument("--hold", type=int, default=14, help="hold frames at the end (boot)")
    args = ap.parse_args()

    layout = Layout(scale=args.scale)
    targets = assign_letters(layout) if args.mode == "boot" else {}
    masks = render_glyph_masks(targets, args.font) if targets else {}
    eff = Effect(layout, targets, mode=args.mode)
    print(f"canvas {layout.W}x{layout.H}px  keys={NKEYS}  letters={len(targets)}  mode={args.mode}")

    frames = []
    N = args.frames
    for f in range(N):
        tt = f / N
        img = render_frame(layout, eff, targets, masks, tt)
        if args.downscale != 1.0:
            img = img.resize((int(layout.W * args.downscale), int(layout.H * args.downscale)),
                             Image.LANCZOS)
        frames.append(img)
        if (f + 1) % 16 == 0:
            print(f"  frame {f+1}/{N}")
    if args.mode == "boot":
        frames += [frames[-1]] * args.hold      # hold the finished splash

    dur = int(1000 / args.fps)
    durations = [dur] * len(frames)
    if args.mode == "boot":
        durations[-1] = 900
    frames[0].save(args.out, save_all=True, append_images=frames[1:],
                   duration=durations, loop=0, optimize=True, disposal=2)
    print("wrote", args.out)


if __name__ == "__main__":
    main()
