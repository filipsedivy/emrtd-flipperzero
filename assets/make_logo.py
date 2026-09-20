#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Filip Sedivy
"""Draw the eMRTD mark, everywhere it is needed, from one definition.

The mark is the contact plate of the chip module inside a travel document:
a plate divided into six pads. It is defined once, on a ten by ten grid,
because that is the hardest place it has to work - the Flipper launcher shows
the application icon at exactly that size, in one bit colour, with no
antialiasing to hide behind. Everything larger is the same grid scaled by a
whole number, so an edge is never half a pixel anywhere.

Run with:

    uv run --with pillow python assets/make_logo.py

Outputs:
    images/emrtd_10px.png      the application icon, 10x10, one bit
    images/EmrtdChip_24x24.png the same mark for the reading screen
    assets/logo.svg            the mark and the wordmark, as vector art
    assets/logo.png            a raster export of the same
    assets/logo.txt            the mark as text, for a terminal or a README
"""

from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent

# The mark on its ten by ten grid. The plate is an eight by eight square with
# a one pixel margin; four notches cut it into six pads. Nothing drawn is
# thinner than two pixels, which is what keeps it from turning into a dither
# pattern on the device.
GRID = 10
PLATE = (1, 1, 8, 8)  # left, top, right, bottom, inclusive
NOTCHES = [
    (3, 2, 3, 3),  # upper left gap
    (6, 2, 6, 3),  # upper right gap
    (3, 6, 3, 7),  # lower left gap
    (6, 6, 6, 7),  # lower right gap
]

INK = 0
PAPER = 1


def mark_pixels():
    """The mark as a set of the grid cells that are inked."""
    left, top, right, bottom = PLATE
    cells = {
        (x, y)
        for x in range(left, right + 1)
        for y in range(top, bottom + 1)
    }
    for nx0, ny0, nx1, ny1 in NOTCHES:
        for x in range(nx0, nx1 + 1):
            for y in range(ny0, ny1 + 1):
                cells.discard((x, y))
    return cells


def write_bitmap(path, scale, margin=0):
    """Write the mark as a one bit PNG, scaled by a whole number."""
    size = GRID * scale + 2 * margin
    image = Image.new("1", (size, size), PAPER)
    draw = ImageDraw.Draw(image)
    for x, y in mark_pixels():
        x0 = margin + x * scale
        y0 = margin + y * scale
        draw.rectangle([x0, y0, x0 + scale - 1, y0 + scale - 1], fill=INK)
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path)

    reopened = Image.open(path)
    assert reopened.mode == "1", f"{path}: mode is {reopened.mode}, not 1"
    assert reopened.size == (size, size), f"{path}: size is {reopened.size}"
    print(f"  {path.relative_to(ROOT)}  {reopened.size[0]}x{reopened.size[1]}, 1 bit")


def merged_rectangles():
    """The mark as a few rectangles rather than a hundred cells.

    Rows of adjacent cells are joined, and identical rows stacked, so the
    vector form is a handful of shapes instead of a bitmap traced in XML.
    """
    cells = mark_pixels()
    runs_by_row = {}
    for y in range(GRID):
        runs = []
        x = 0
        while x < GRID:
            if (x, y) in cells:
                start = x
                while x < GRID and (x, y) in cells:
                    x += 1
                runs.append((start, x - start))
            else:
                x += 1
        if runs:
            runs_by_row[y] = runs

    rects = []
    y = 0
    while y < GRID:
        runs = runs_by_row.get(y)
        if runs is None:
            y += 1
            continue
        height = 1
        while runs_by_row.get(y + height) == runs:
            height += 1
        for start, width in runs:
            rects.append((start, y, width, height))
        y += height
    return rects


def write_svg(path, unit=24, gap=18):
    """The mark beside the wordmark, in vector units."""
    rects = merged_rectangles()
    mark_size = GRID * unit
    text_x = mark_size + gap
    width = text_x + 360
    height = mark_size

    lines = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}" role="img" aria-label="eMRTD">',
        "  <title>eMRTD</title>",
        '  <g fill="currentColor">',
    ]
    for x, y, w, h in rects:
        lines.append(
            f'    <rect x="{x * unit}" y="{y * unit}" '
            f'width="{w * unit}" height="{h * unit}"/>'
        )
    lines.append("  </g>")
    lines.append(
        f'  <text x="{text_x}" y="{height * 0.62:.0f}" fill="currentColor" '
        f'font-family="Helvetica Neue, Helvetica, Arial, sans-serif" '
        f'font-size="{mark_size * 0.46:.0f}" font-weight="600" '
        f'letter-spacing="{mark_size * 0.03:.0f}">eMRTD</text>'
    )
    lines.append(
        f'  <text x="{text_x + 3}" y="{height * 0.87:.0f}" fill="currentColor" '
        f'font-family="Helvetica Neue, Helvetica, Arial, sans-serif" '
        f'font-size="{mark_size * 0.15:.0f}" opacity="0.72">'
        f"electronic passport reader for Flipper Zero</text>"
    )
    lines.append("</svg>")

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"  {path.relative_to(ROOT)}  {len(rects)} rectangles, {width}x{height}")


def write_raster_logo(path, scale=40):
    """A raster export of the mark, on a transparent background."""
    size = GRID * scale
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    for x, y, w, h in merged_rectangles():
        draw.rectangle(
            [x * scale, y * scale, (x + w) * scale - 1, (y + h) * scale - 1],
            fill=(0, 0, 0, 255),
        )
    image.save(path)
    print(f"  {path.relative_to(ROOT)}  {size}x{size}, RGBA")


def write_ascii(path):
    """The mark as text, in one block form and one plain form."""
    cells = mark_pixels()
    rows = range(PLATE[1], PLATE[3] + 1)
    cols = range(PLATE[0], PLATE[2] + 1)

    def render(on, off):
        return [
            "".join(on if (x, y) in cells else off for x in cols) for y in rows
        ]

    block = render("██", "  ")
    plain = render("##", "  ")

    wordmark = ["", "", "  e M R T D", "", "  electronic passport reader", "  for Flipper Zero", "", ""]

    text = [
        "eMRTD",
        "=====",
        "",
        "The mark is the contact plate of the chip module inside a travel",
        "document: a plate divided into six pads. It is drawn on a ten by ten",
        "grid so that it survives the Flipper's application icon, and every",
        "larger form is that same grid scaled by a whole number.",
        "",
        "Banner:",
        "",
    ]
    for line, word in zip(block, wordmark):
        text.append(f"    {line}{word}")
    text += [
        "",
        "Plain 7-bit form, for a terminal that has no block characters:",
        "",
    ]
    text += [f"    {line}" for line in plain]
    text += [
        "",
        "Everything here is drawn by assets/make_logo.py, which also writes",
        "the application icon and the image the reading screen uses.",
        "",
    ]
    path.write_text("\n".join(text), encoding="utf-8")
    print(f"  {path.relative_to(ROOT)}  {len(block)} rows")


def main():
    print("Drawing the eMRTD mark:")
    # The launcher icon. One grid cell to one pixel - the hardest case, and
    # the one the whole design is sized for.
    write_bitmap(ROOT / "images" / "emrtd_10px.png", scale=1)
    # The reading screen. Two pixels per cell inside a two pixel margin keeps
    # the edges whole and the mark centred in twenty-four.
    write_bitmap(ROOT / "images" / "EmrtdChip_24x24.png", scale=2, margin=2)
    write_svg(ROOT / "assets" / "logo.svg")
    write_raster_logo(ROOT / "assets" / "logo.png")
    write_ascii(ROOT / "assets" / "logo.txt")


if __name__ == "__main__":
    main()
