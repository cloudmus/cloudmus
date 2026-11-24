#!/usr/bin/env python3
"""Make the About pages' waving AI agents: one strip of frames each.

clawd-waving.png  Claude Code's Clawd, from the block glyphs Claude Code
                  draws it with in the terminal: 9x3 glyphs, each a 2x2 block
                  of pixels twice as tall as wide (as a terminal's cell is), so
                  18x12 pixels a frame (20x14 with its clear margin). The right arm goes up ("one-up") and
                  down.
codex-waving.png  Codex's pet, the "waving" row of its spritesheet
                  (~/.codex/cache/tui-pets/v1/assets/codex-spritesheet-v4.webp,
                  fetched by Codex's /pets): cells of 192x208, row 3, frames
                  0-3, cropped to what they show and halved.

Run from anywhere, after changing either: python3 make_sprites.py [sheet.webp]
Needs Pillow.
"""
import sys
from pathlib import Path

from PIL import Image

here = Path(__file__).resolve().parent

# --- Clawd

# Each block glyph's 2x2 pixels: upper-left, upper-right, lower-left, lower-right.
QUADS = {
    " ": "0000", "█": "1111", "▀": "1100", "▄": "0011", "▌": "1010", "▐": "0101",
    "▘": "1000", "▝": "0100", "▖": "0010", "▗": "0001",
    "▛": "1110", "▜": "1101", "▙": "1011", "▟": "0111", "▚": "1001", "▞": "0110",
}
ARMS = {
    "down": {"r1L": " ▐", "r1R": "", "r2L": "▝▜", "r2R": "█▀"},
    "one-up": {"r1L": " ▐", "r1R": "▄", "r2L": "▝▜", "r2R": "█▘"},
}
EYES = "▛███▛█"  # open: the gaps in it are the eyes
FEET = " ▝▝   ▝▝ "
BODY = (0xD7, 0x77, 0x57, 255)  # Claude Code's clawd_body
EYE = (0x00, 0x00, 0x00, 255)  # clawd_background, through the eyes


def clawd(arms):
    a = ARMS[arms]
    # (glyphs, first column, whether its gaps show the eyes' color)
    rows = [
        [(a["r1L"], 0, False), (EYES, 2, True), (a["r1R"], 8, False)],
        [(a["r2L"], 0, False), ("█████", 2, True), (a["r2R"], 7, False)],
        [(FEET, 0, False)],
    ]
    image = Image.new("RGBA", (18, 6), (0, 0, 0, 0))
    for row, spans in enumerate(rows):
        for glyphs, column, eyes in spans:
            for i, glyph in enumerate(glyphs):
                for q, bit in enumerate(QUADS[glyph]):
                    x = (column + i) * 2 + q % 2
                    y = row * 2 + q // 2
                    if bit == "1":
                        image.putpixel((x, y), BODY)
                    elif eyes:
                        image.putpixel((x, y), EYE)
    # A terminal's cell is about twice as tall as wide: so are its quarters.
    return image.resize((18, 12), Image.NEAREST)


# A pause, then two waves: frames of equal length (the views play them evenly).
CLAWD_FRAMES = ["down"] * 4 + ["one-up", "down", "one-up", "down", "one-up"]


def strip(frames, margin):
    """The frames side by side, each with `margin` clear pixels all round: a
    frame scaled up samples a little of its neighbours at its edges, and Clawd's
    raised arm, at the frame's very edge, showed as a dot beside the next one."""
    width, height = frames[0].size
    cell = (width + 2 * margin, height + 2 * margin)
    out = Image.new("RGBA", (cell[0] * len(frames), cell[1]), (0, 0, 0, 0))
    for i, frame in enumerate(frames):
        out.paste(frame, (i * cell[0] + margin, margin))
    return out


clawd_strip = strip([clawd(arms) for arms in CLAWD_FRAMES], 1)
clawd_strip.save(here / "clawd-waving.png", optimize=True)
print(f"clawd frame: {clawd_strip.width // len(CLAWD_FRAMES)}x{clawd_strip.height}")

# --- Codex

sheet_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path.home() / ".codex/cache/tui-pets/v1/assets/codex-spritesheet-v4.webp"
if sheet_path.exists():
    sheet = Image.open(sheet_path).convert("RGBA")
    cell_w, cell_h, row = 192, 208, 3
    cells = [sheet.crop((c * cell_w, row * cell_h, (c + 1) * cell_w, (row + 1) * cell_h)) for c in range(4)]
    # Its timing (140 ms each, the last 280) in 140 ms frames, and a pause
    # after, as Clawd has.
    # Without the empty room around it in every frame: the cells leave space
    # for other rows' jumps and runs.
    boxes = [cell.getbbox() for cell in cells]
    box = (min(b[0] for b in boxes), min(b[1] for b in boxes), max(b[2] for b in boxes), max(b[3] for b in boxes))
    cells = [cell.crop(box) for cell in cells]
    frames = [cells[0]] * 4 + [cells[1], cells[2], cells[1], cells[2], cells[3], cells[3]]
    width, height = cells[0].size
    half = [f.resize((width // 2, height // 2), Image.LANCZOS) for f in frames]
    codex_strip = strip(half, 2)
    codex_strip.save(here / "codex-waving.png", optimize=True)
    print(f"codex frame: {codex_strip.width // len(half)}x{codex_strip.height}")
else:
    print(f"no {sheet_path}: codex-waving.png left as it is", file=sys.stderr)
