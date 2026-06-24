"""
Contact sheet: render a grid of generated systems for eyeballing placement.

    python3 route_samples.py [--out FILE] [TOKEN ...]

Each argument is a *seed token* (the old "LV-NNN" strings still work — they're
just seeds now); the label shows the derived class designation (LV/KG/BG/RF) and
system name. With no arguments, renders a fixed spread. Output defaults to
route_samples.png next to this script.
"""
import os, sys
from PIL import Image, ImageDraw
from oled_gfx_lib import generate_route, render_route_image

DEFAULT_DESIGS = [
    "LV-100", "LV-111", "LV-223", "LV-317", "LV-404",
    "LV-426", "LV-500", "LV-512", "LV-617", "LV-734",
    "LV-810", "LV-901", "LV-142", "LV-256", "LV-388",
]

COLS, SCALE, GUTTER, LABEL_H = 3, 4, 8, 16
CELL_W, CELL_H = 128 * SCALE, 32 * SCALE


def build_sheet(designations):
    rows = (len(designations) + COLS - 1) // COLS
    col_w = CELL_W + GUTTER
    row_h = CELL_H + LABEL_H + GUTTER
    sheet = Image.new("RGB", (COLS * col_w - GUTTER, rows * row_h - GUTTER), (30, 30, 30))
    draw = ImageDraw.Draw(sheet)
    for i, desig in enumerate(designations):
        x0 = (i % COLS) * col_w
        y0 = (i // COLS) * row_h
        route = generate_route(desig)
        cell = render_route_image(route).resize((CELL_W, CELL_H), Image.NEAREST)
        sheet.paste(cell, (x0, y0))
        # token → derived designation, then system name + leg count.
        label = f"{desig}>{route['designation']}  {route['system_name']}  {len(route['legs'])}leg"
        draw.text((x0 + 4, y0 + CELL_H + 2), label, fill=(160, 160, 160))
    return sheet


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    out = "route_samples.png"
    if "--out" in sys.argv:
        out = sys.argv[sys.argv.index("--out") + 1]
    designations = args or DEFAULT_DESIGS
    sheet = build_sheet(designations)
    out_path = out if os.path.isabs(out) else os.path.join(os.path.dirname(os.path.abspath(__file__)), out)
    sheet.save(out_path)
    print(f"Saved: {out_path}")
