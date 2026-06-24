"""
pytest over the ctypes bridge — exercises the real firmware code, the single
source of truth. Run from this directory:

    uv run --with pytest --with pillow pytest -q
"""
import math
import oled_gfx_lib as g

# A spread of designations covering 1/2/3-leg topologies and varied systems.
DESIGS = [f"LV-{n}" for n in range(100, 940, 13)]

LAGRANGE_FRAC = 0.12          # must match SW_LAGRANGE_COLLINEAR_FRAC
SIXTY = math.pi / 3.0


def lit_pixels(buf):
    """Decode a 512-byte SSD1306 page buffer to a set of lit (x, y)."""
    pts = set()
    for page in range(4):
        for x in range(128):
            byte = buf[page * 128 + x]
            for bit in range(8):
                if byte & (1 << bit):
                    pts.add((x, page * 8 + bit))
    return pts


def synthetic_arc(radius, solid, dash=3, gap=3, cx=64, cy=16):
    """A minimal route carrying a single centered ring, for rasterizer tests."""
    return {
        'arc_cx': cx, 'arc_cy': cy,
        'arcs': [{'radius': radius, 'solid': solid}],
        'dash_px': dash, 'gap_px': gap,
        'legs': [], 'markers': [], 'bodies': [],
        'ship_offset_x': 0, 'eta_minutes': 0,
        'system_name': '', 'designation': '',
    }


# ── Determinism ───────────────────────────────────────────────────────────────

def test_route_determinism():
    for d in DESIGS:
        a = g.generate_route(d)
        b = g.generate_route(d)
        assert a == b, f"route for {d} is not deterministic"


def test_system_determinism():
    for d in DESIGS:
        s1 = g.build_system(d)
        s2 = g.build_system(d)
        assert s1.seed == s2.seed
        assert s1.name == s2.name
        assert s1.body_count == s2.body_count
        assert bytes(s1.bodies) == bytes(s2.bodies)


# ── Naming fits the OLED column ──────────────────────────────────────────────

def test_name_fits_and_printable():
    for d in DESIGS:
        name = g.generate_route(d)['system_name']
        assert 0 < len(name) <= 8, f"{d}: name {name!r} wrong length"
        assert all(32 <= ord(c) <= 126 for c in name), f"{d}: non-printable name {name!r}"


# ── Designation reflects the destination body's class ─────────────────────────

# Prefix → which body classes may legitimately carry it.
_CLASS_PREFIX = {
    g.STARMAP_MOON:    {"LV"},
    g.STARMAP_PLANET:  {"KG", "BG"},   # gas giant vs colony world (derived in C)
    g.STARMAP_TROJAN:  {"RF"},
    g.STARMAP_VAGRANT: {"RF"},
}


def test_designation_matches_body_class():
    seen = set()
    for d in DESIGS:
        s = g.build_system(d)
        desig = g.generate_route(d)['designation']
        prefix = desig.split("-")[0]
        seen.add(prefix)
        body_type = s.bodies[s.dest_idx].type
        assert prefix in _CLASS_PREFIX[body_type], \
            f"{d}: dest class {body_type} got designation {desig!r}"
        assert len(desig) <= 7, f"{d}: designation {desig!r} too long for OLED column"
    # The class designation actually varies across a sweep (not all one prefix).
    assert {"LV", "KG", "BG"} <= seen, f"prefixes don't vary as expected: {seen}"


def test_designation_deterministic():
    for d in DESIGS:
        assert g.generate_route(d)['designation'] == g.generate_route(d)['designation']


# ── Everything drawn lands on the panel ───────────────────────────────────────

def test_geometry_in_bounds():
    for d in DESIGS:
        r = g.generate_route(d)
        pts = ([(m['x'], m['y']) for m in r['markers']] +
               [(b['x'], b['y']) for b in r['bodies']])
        for leg in r['legs']:
            pts += [(leg['p0x'], leg['p0y']), (leg['p1x'], leg['p1y'])]
        for t in [i / 20 for i in range(21)]:
            pts.append(g.ship_pos(r, t))
        for x, y in pts:
            assert 0 <= x < 128 and 0 <= y < 32, f"{d}: ({x},{y}) out of bounds"


def test_baked_pixels_in_bounds():
    # bake clips, so a non-empty buffer with the right dimensions confirms no crash.
    for d in DESIGS[:8]:
        pts = lit_pixels(g.bake_bg(g.generate_route(d)))
        assert pts, f"{d}: empty background"
        assert all(0 <= x < 128 and 0 <= y < 32 for x, y in pts)


# ── Framing quality: best-fit rotation lays the journey on the wide axis ──────

USABLE_W = 127 - 2 * 12        # 103 px between the x-margins (RG_MARGIN_X = 12)


def _journey_spans(r):
    """(x_span, y_span) of the rendered journey in display px: departure +
    destination markers, every leg endpoint, and the ship sampled along the whole
    path. Excludes the star ring marker and the decoration `bodies[]` — neither is
    part of the journey — so the spans reflect how the best-fit rotation framed the
    path the ship actually flies."""
    pts = [(m['x'], m['y']) for m in r['markers'] if m['type'] != g.GFX_MARKER_RING]
    for leg in r['legs']:
        pts += [(leg['p0x'], leg['p0y']), (leg['p1x'], leg['p1y'])]
    for t in [i / 20 for i in range(21)]:
        pts.append(g.ship_pos(r, t))
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return max(xs) - min(xs), max(ys) - min(ys)


def test_long_axis_utilization():
    """Best-fit framing aligns each journey's long extent with the wide (x) display
    axis, so the journey's x-span should meet or exceed its y-span for the large
    majority of routes. (A route whose fit is dominated by an off-path body — e.g. a
    direct hop that still has a chosen-but-unused mid planet on the far side — can
    land tilted; those are the minority.)"""
    wins = 0
    for d in DESIGS:
        xspan, yspan = _journey_spans(g.generate_route(d))
        if xspan >= yspan:
            wins += 1
    frac = wins / len(DESIGS)
    assert frac >= 0.80, f"long extent on x for only {frac:.0%} of routes (want >=80%)"


def test_framing_not_zoomed_out():
    """Guard against a regression to tiny / zoomed-out framing: the journey's larger
    pixel span should fill at least half the usable width for most routes. The tail
    below the floor is routes whose fit reserves room for an off-path body (preserved
    behavior) — hence 'most', not 'all'."""
    floor = 0.50 * USABLE_W
    ok = sum(1 for d in DESIGS if max(_journey_spans(g.generate_route(d))) >= floor)
    frac = ok / len(DESIGS)
    assert frac >= 0.70, f"only {frac:.0%} of routes fill >=50% of usable width (want >=70%)"


# ── Route endpoints land on the departure / destination bodies ────────────────

def test_endpoints_on_bodies():
    for d in DESIGS:
        r = g.generate_route(d)
        dep = (r['legs'][0]['p0x'], r['legs'][0]['p0y'])
        dst = (r['legs'][-1]['p1x'], r['legs'][-1]['p1y'])
        assert g.ship_pos(r, 0.0) == dep, f"{d}: ship@0 not at departure"
        assert g.ship_pos(r, 1.0) == dst, f"{d}: ship@1 not at destination"
        # marker[0] is the departure body, marker[1] the destination reticle.
        assert (r['markers'][0]['x'], r['markers'][0]['y']) == dep
        assert (r['markers'][1]['x'], r['markers'][1]['y']) == dst


# ── Lagrange geometry (±60° / collinear) ──────────────────────────────────────

def test_lagrange_geometry():
    for d in DESIGS:
        s = g.build_system(d)
        for k in range(s.planet_count):
            idx = s.planet_idx[k]
            R = s.bodies[idx].orbital_radius
            th = s.bodies[idx].angle
            checks = {
                g.L4: (R, th + SIXTY),
                g.L5: (R, th - SIXTY),
                g.L1: (R * (1 - LAGRANGE_FRAC), th),
                g.L2: (R * (1 + LAGRANGE_FRAC), th),
                g.L3: (R, th + math.pi),
            }
            for which, (exp_r, exp_a) in checks.items():
                x, y = g.lagrange_pos(s, idx, which)
                assert math.isclose(math.hypot(x, y), exp_r, rel_tol=1e-4, abs_tol=1e-3)
                ex, ey = exp_r * math.cos(exp_a), exp_r * math.sin(exp_a)
                assert math.isclose(x, ex, abs_tol=1e-2) and math.isclose(y, ey, abs_tol=1e-2)


# ── Arc rasterizer: connected rings, even dashing ─────────────────────────────

def test_solid_ring_is_connected():
    """Every lit pixel of a solid ring has a lit 8-neighbour — the broken
    scanline rasterizer left isolated pixels near the top/bottom of circles."""
    pts = lit_pixels(g.bake_bg(synthetic_arc(13, solid=True)))
    for (x, y) in pts:
        neigh = any((x + dx, y + dy) in pts
                    for dx in (-1, 0, 1) for dy in (-1, 0, 1) if (dx or dy))
        assert neigh, f"isolated pixel at ({x},{y}) — ring not continuous"


def test_dashed_ring_covers_all_octants():
    cx, cy, r = 64, 16, 13
    pts = lit_pixels(g.bake_bg(synthetic_arc(r, solid=False, dash=3, gap=3, cx=cx, cy=cy)))
    octants = set()
    for (x, y) in pts:
        ang = math.atan2(y - cy, x - cx) % (2 * math.pi)
        octants.add(int(ang / (math.pi / 4)) % 8)
    assert octants == set(range(8)), f"dash gaps in octants {set(range(8)) - octants}"


def test_dash_fraction_reasonable():
    cx, cy, r = 64, 16, 13
    solid = lit_pixels(g.bake_bg(synthetic_arc(r, solid=True, cx=cx, cy=cy)))
    dashed = lit_pixels(g.bake_bg(synthetic_arc(r, solid=False, dash=3, gap=3, cx=cx, cy=cy)))
    frac = len(dashed) / len(solid)
    assert 0.35 < frac < 0.65, f"dash fraction {frac:.2f} off (expected ~0.5)"
