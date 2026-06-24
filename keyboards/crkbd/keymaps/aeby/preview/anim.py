"""
Animate a full journey: the Patna traverses every leg as a progress bar while
the crosshair pulses, then a new system is generated on arrival.

    python3 anim.py [--desigs LV-426 LV-223 ...] [--frames N] [--out FILE]

Outputs a looping GIF (anim.gif) and a still frame-strip (anim_strip.png) for
quick eyeballing without a GIF viewer.
"""
import os, sys
from PIL import Image
from oled_gfx_lib import generate_route, ship_pos, pulse_size, route_phase
from oled_preview import composite

FRAME_MS = 50  # 20 Hz, matching OLED_UPDATE_INTERVAL


def burn_centers(route, N=4000):
    """t at the middle of each burn window — so even the 90 s trims (a fraction of
    a percent of a multi-hour journey) get a frame and surface in the strip/GIF."""
    centers, start, prev = [], None, False
    for i in range(N + 1):
        t = i / N
        _, bn = route_phase(route, t)
        if bn and not prev:
            start = t
        if not bn and prev:
            centers.append((start + t) / 2)
        prev = bn
    if prev:
        centers.append((start + 1.0) / 2)
    return centers


def journey_frames(designations, frames_per):
    frames = []
    now = 0
    for desig in designations:
        route = generate_route(desig)
        # Evenly-spaced frames + one snapped to each burn-window centre.
        ts = sorted(set([i / (frames_per - 1) for i in range(frames_per)] +
                        burn_centers(route)))
        for t in ts:
            frames.append(composite(route, t, now_ms=now, scale=4))
            now += FRAME_MS
    return frames


if __name__ == "__main__":
    args = sys.argv[1:]
    desigs = ["LV-426", "LV-223"]
    frames_per = 48
    out = "anim.gif"
    if "--desigs" in args:
        i = args.index("--desigs") + 1
        desigs = []
        while i < len(args) and not args[i].startswith("--"):
            desigs.append(args[i]); i += 1
    if "--frames" in args:
        frames_per = int(args[args.index("--frames") + 1])
    if "--out" in args:
        out = args[args.index("--out") + 1]

    here = os.path.dirname(os.path.abspath(__file__))
    frames = journey_frames(desigs, frames_per)

    gif_path = out if os.path.isabs(out) else os.path.join(here, out)
    frames[0].save(gif_path, save_all=True, append_images=frames[1:],
                   duration=FRAME_MS, loop=0, optimize=True)
    print(f"Saved GIF: {gif_path}  ({len(frames)} frames)")

    # Still strip: 8 evenly spaced frames stacked, to eyeball traversal + pulse.
    picks = [frames[round(k * (len(frames) - 1) / 7)] for k in range(8)]
    fw, fh = picks[0].size
    strip = Image.new("RGB", (fw, fh * len(picks)), (10, 10, 10))
    for k, fr in enumerate(picks):
        strip.paste(fr, (0, k * fh))
    strip_path = os.path.join(here, "anim_strip.png")
    strip.save(strip_path)
    print(f"Saved strip: {strip_path}")
