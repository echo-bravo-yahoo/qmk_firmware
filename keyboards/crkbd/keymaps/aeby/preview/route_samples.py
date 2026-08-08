"""
Contact sheet: render a grid of generated systems for eyeballing placement.

    python3 route_samples.py [--out FILE] [--regions] [TOKEN ...]

Each argument is a designation token of the form {LV,BG,KG,RF}-{digits}: the token
IS the destination — it returns to that exact system and lands on a body of the
prefix's type (LV/BG → moon or rocky planet, KG → gas giant, RF → trojan/vagrant).
The label reads "token>designation" — an identity for a conforming token — plus the
system name and leg count. With no arguments, renders a fixed spread across all four
classes. Output defaults to route_samples.png next to this script.

--regions draws a color-coded ruler above each cell marking the long-axis layout
bands (centering gap, map content, map→strip breather, strip text, chip-border
reserve) so it's clear what padding sits where, with a legend at the bottom. The map
band and the whole layout track the *lit* content (what actually draws). A white cap on
a ruler edge means the orbital rings extend off-panel there (their geometric center±radius
span), so only a small on-panel cap is lit on that side — context for why the map sits
where it does.
"""
import os, sys
from PIL import Image, ImageDraw
from oled_gfx_lib import generate_route, render_route_image, content_xspan, lit_xspan

DEFAULT_DESIGS = [
    "LV-426", "KG-348", "BG-386", "RF-2001",
    "LV-178", "KG-900", "BG-794", "RF-1138",
    "LV-895", "KG-501", "BG-223", "RF-3000",
]

COLS, SCALE, GUTTER, LABEL_H = 3, 4, 8, 16
CELL_W, CELL_H = 128 * SCALE, 32 * SCALE
RULER_H, LEGEND_H = 12, 20      # region ruler / legend heights (px), --regions only

# Long-axis layout regions: key → (ruler color, legend text). One band per stretch of
# the 128-px long axis route_gen carves out.
REGION_STYLE = {
    'gap':      ((60, 95, 215),  "gap (centering slack)"),
    'map':      ((120, 120, 120), "map (content + rings)"),
    'breather': ((215, 70, 70),  "breather (map->strip)"),
    'strip':    ((185, 80, 205), "strip text"),
    'pad':      ((225, 200, 55), "chip-border reserve"),
}
OVERFLOW_COLOR = (255, 255, 255)   # ruler-edge cap: route_gen's geometric measure off-panel


def region_bands(route):
    """Long-axis [start, end) bands of the layout, clamped to the panel, as (a, b, key).
    The map band spans the *lit* content; gaps are the real visible margins. Strip routes:
    gap | map | breather | strip | chip-pad. No-strip: gap | map | gap."""
    lx0, lx1 = lit_xspan(route)
    mx1 = lx1 + 1                       # exclusive end of the lit map
    bx, bw = route['banner_x'], route['banner_w']
    if bw:
        raw = [(0, lx0, 'gap'), (lx0, mx1, 'map'), (mx1, bx, 'breather'),
               (bx, bx + bw, 'strip'), (bx + bw, 128, 'pad')]
    else:
        raw = [(0, lx0, 'gap'), (lx0, mx1, 'map'), (mx1, 128, 'gap')]
    out = []
    for a, b, key in raw:
        a, b = max(0, min(128, a)), max(0, min(128, b))
        if b > a:
            out.append((a, b, key))
    return out


def draw_ruler(draw, ox, oy, route):
    """Region ruler (width CELL_W, height RULER_H) at cell origin (ox, oy). A white cap on an
    edge flags the orbital rings extending off-panel there (geometric span) — only a small
    on-panel cap is lit on that side, context for where the map sits."""
    for a, b, key in region_bands(route):
        draw.rectangle([ox + a * SCALE, oy, ox + b * SCALE - 1, oy + RULER_H - 1],
                       fill=REGION_STYLE[key][0])
    gx0, gx1 = content_xspan(route)
    if gx0 < 0:
        draw.rectangle([ox, oy, ox + 2, oy + RULER_H - 1], fill=OVERFLOW_COLOR)
    if gx1 > 128:
        draw.rectangle([ox + CELL_W - 3, oy, ox + CELL_W - 1, oy + RULER_H - 1], fill=OVERFLOW_COLOR)


def draw_legend(draw, y):
    x = 4
    for color, text in list(REGION_STYLE.values()) + [(OVERFLOW_COLOR, "rings off-panel")]:
        draw.rectangle([x, y, x + 12, y + 12], fill=color)
        draw.text((x + 16, y + 2), text, fill=(205, 205, 205))
        x += 16 + 7 * len(text) + 22


def build_sheet(designations, regions=False):
    ruler = RULER_H if regions else 0
    legend = LEGEND_H if regions else 0
    col_w = CELL_W + GUTTER
    row_h = ruler + CELL_H + LABEL_H + GUTTER
    rows = (len(designations) + COLS - 1) // COLS
    sheet = Image.new("RGB", (COLS * col_w - GUTTER, rows * row_h - GUTTER + legend), (30, 30, 30))
    draw = ImageDraw.Draw(sheet)
    for i, desig in enumerate(designations):
        ox = (i % COLS) * col_w
        oy = (i // COLS) * row_h
        route = generate_route(desig)
        if regions:
            draw_ruler(draw, ox, oy, route)
        cell = render_route_image(route).resize((CELL_W, CELL_H), Image.NEAREST)
        sheet.paste(cell, (ox, oy + ruler))
        # token → derived designation, then system name + leg count.
        label = f"{desig}>{route['designation']}  {route['system_name']}  {len(route['legs'])}leg"
        draw.text((ox + 4, oy + ruler + CELL_H + 2), label, fill=(160, 160, 160))
    if regions:
        draw_legend(draw, sheet.height - legend + 4)
    return sheet


if __name__ == "__main__":
    # Parse --out FILE / --out=FILE and --regions without swallowing tokens.
    argv = sys.argv[1:]
    out = "route_samples.png"
    regions = False
    args = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--out":
            out = argv[i + 1] if i + 1 < len(argv) else out
            i += 2
        elif a.startswith("--out="):
            out = a[len("--out="):]
            i += 1
        elif a == "--regions":
            regions = True
            i += 1
        else:
            args.append(a)
            i += 1
    designations = args or DEFAULT_DESIGS
    sheet = build_sheet(designations, regions=regions)
    out_path = out if os.path.isabs(out) else os.path.join(os.path.dirname(os.path.abspath(__file__)), out)
    sheet.save(out_path)
    print(f"Saved: {out_path}")
