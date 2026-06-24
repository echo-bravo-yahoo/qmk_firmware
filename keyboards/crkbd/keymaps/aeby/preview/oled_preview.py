"""
Single-journey preview: left OLED transit map (background + ship) beside the
right OLED telemetry, both driven entirely by the generated route.

    python3 oled_preview.py [--desig LV-NNN] [--t T] [--out FILE]

Telemetry (system name, LV destination, ETA, status) is read from the route, not
hardcoded — exactly what the firmware does on device.
"""
import os, re
from PIL import Image
from oled_gfx_lib import (generate_route, bake_bg, bake_ship, ship_pos, pulse_size,
                          buf_to_image, buf_to_image_portrait, route_phase, burn_warning,
                          PHASE_NAMES)

ON = (0, 210, 0)

# ── Tom Thumb 4×8 font (shared with the firmware) ─────────────────────────────

def _load_font():
    path = os.path.join(os.path.dirname(__file__), "..", "glcdfont_tomthumb.c")
    with open(path) as f:
        src = f.read()
    arr_hex = re.findall(r'0x[0-9A-Fa-f]+', src[src.index("font[] = {"):])
    return [int(v, 16) for v in arr_hex]

_FONT       = _load_font()
_FONT_START = 0x20
_FONT_WIDTH = 4

def _glyph(ch):
    code = ord(ch)
    if code < _FONT_START or code > 0x7E:
        return [0] * 4
    idx = (code - _FONT_START) * _FONT_WIDTH
    return _FONT[idx:idx + _FONT_WIDTH]

# ── Telemetry (data-driven from the route) ────────────────────────────────────

def telemetry_rows(route, t):
    """The right-OLED text rows for a route at journey fraction t (mirrors the
    firmware's route_anim telemetry)."""
    remaining = int(round(route['eta_minutes'] * (1.0 - max(0.0, min(1.0, t)))))
    eta = f"{remaining // 60:02d}H{remaining % 60:02d}"
    phase, _ = route_phase(route, t)
    status = PHASE_NAMES.get(phase, "TRANSIT")
    return [
        "--------",
        "MISSION ",
        f"{route['system_name']:<8}"[:8],
        "TRANSIT ",
        "--------",
        "DST     ",
        f"{route['designation']:<8}"[:8],
        "--------",
        "ETA     ",
        f"{eta:<8}"[:8],
        "--------",
        "STATUS  ",
        f"{status:<8}"[:8],
    ]

RIGHT_HEADER = ["USCSS   ", "PATNA   "]
HEADER_PAD   = 3

def render_right_burn(now_ms=0):
    """The right panel as the live BURN takeover — identical bytes to the slave's
    portrait page buffer (landscape word running the long 128 axis)."""
    return buf_to_image_portrait(burn_warning(now_ms))


def render_right(route, t):
    img = Image.new("RGB", (32, 128), (0, 0, 0))
    header_h = HEADER_PAD + 5 + HEADER_PAD + 5 + HEADER_PAD
    for xi in range(32):
        for yi in range(header_h):
            img.putpixel((xi, yi), ON)

    def draw_text(text, y_top, bits, invert):
        for ci, ch in enumerate(text[:8]):
            gl = _glyph(ch)
            for gi, cb in enumerate(gl[:4]):
                for bit in range(bits):
                    if (cb >> bit) & 1:
                        img.putpixel((ci * 4 + gi, y_top + bit),
                                     (0, 0, 0) if invert else ON)

    draw_text(RIGHT_HEADER[0], HEADER_PAD,             5, invert=True)
    draw_text(RIGHT_HEADER[1], HEADER_PAD + 5 + HEADER_PAD, 5, invert=True)
    for row_i, text in enumerate(telemetry_rows(route, t)):
        draw_text(text, header_h + row_i * 8, 8, invert=False)
    return img

# ── Composite both panels ─────────────────────────────────────────────────────

def render_left(route, t, now_ms=0):
    buf = bake_bg(route)
    bake_ship(route, t, pulse_size(now_ms), buf)
    return buf_to_image(buf)

def composite(route, t, now_ms=0, scale=4, gap=16, burn=None):
    """Left map + right telemetry. The right panel flips to the BURN takeover when
    a burn fires (auto-detected from the phase at t, or forced via burn=True)."""
    if burn is None:
        burn = route_phase(route, t)[1]
    left  = render_left(route, t, now_ms).resize((128 * scale, 32 * scale), Image.NEAREST)
    right_img = render_right_burn(now_ms) if burn else render_right(route, t)
    right = right_img.resize((32 * scale, 128 * scale), Image.NEAREST)
    out = Image.new("RGB", (128 * scale + gap + 32 * scale, 128 * scale), (20, 20, 20))
    out.paste(left,  (0, (128 * scale - 32 * scale) // 2))
    out.paste(right, (128 * scale + gap, 0))
    return out


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description="OLED transit map preview")
    ap.add_argument("--desig", default="LV-426", help="LV-NNN destination designation")
    ap.add_argument("--t", type=float, default=0.45, help="journey fraction 0–1")
    ap.add_argument("--out", default="preview.png", help="output PNG")
    ap.add_argument("--burn", action="store_true",
                    help="force the BURN takeover on the right panel and also save a "
                         "landscape-rotated legibility view (preview_burn.png)")
    args = ap.parse_args()

    route = generate_route(args.desig)
    phase, burn = route_phase(route, args.t)
    print(f"{args.desig} → {route['system_name']}  legs={len(route['legs'])}  "
          f"eta={route['eta_minutes']}min  ship@t={args.t}:{ship_pos(route, args.t)}  "
          f"phase={PHASE_NAMES.get(phase)}  burn={burn}")
    here = os.path.dirname(os.path.abspath(__file__))
    out = args.out if os.path.isabs(args.out) else os.path.join(here, args.out)
    show_burn = args.burn or burn
    composite(route, args.t, now_ms=int(args.t * 1000), burn=show_burn).save(out)
    print(f"Saved: {out}")

    if show_burn:
        # The panel shows the portrait bytes; rotate to landscape (CCW) so the big
        # BURN word reads upright for a human eyeballing legibility on the host.
        land = render_right_burn(now_ms=int(args.t * 1000)).rotate(90, expand=True)
        land_out = os.path.join(here, "preview_burn.png")
        land.resize((128 * 6, 32 * 6), Image.NEAREST).save(land_out)
        print(f"Saved BURN landscape view: {land_out}")
