"""
pytest over the ctypes bridge — exercises the real firmware code, the single
source of truth. Run from this directory:

    uv run --with pytest --with pillow pytest -q
"""
import math
import re
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


# ── Seed-is-designation: the token IS the destination ─────────────────────────
#
# For a conforming "{PREFIX}-{digits}" token the displayed designation is the token
# verbatim, and the prefix pins the destination body's TYPE: LV/BG → moon or rocky
# planet, KG → gas giant, RF → trojan/vagrant. (LV vs BG is a label distinction
# only — both pin the same body pool; the type-keyed spectral class can't tell them
# apart.) The KG/rocky distinction is checked through the public spectral class, the
# surface proxy for the internal sw_is_gas_giant.

PREFIX_SWEEP = ([f"LV-{n}" for n in range(100, 400)] + [f"KG-{n}" for n in range(100, 400)] +
                [f"BG-{n}" for n in range(100, 400)] + [f"RF-{n}" for n in range(1000, 1300)])

_PREFIX_TYPES = {
    "LV": {g.STARMAP_MOON, g.STARMAP_PLANET},
    "BG": {g.STARMAP_MOON, g.STARMAP_PLANET},
    "KG": {g.STARMAP_PLANET},
    "RF": {g.STARMAP_TROJAN, g.STARMAP_VAGRANT},
}


def test_prefix_pins_destination_type():
    """Across all four prefixes: the designation is the token verbatim, and the
    destination body's type matches the prefix. A KG destination must really be a
    gas giant and an LV/BG rocky-planet destination must not be — checked via the
    type-keyed spectral class (gas giants read a Sudarsky roman; rocky planets a
    "<letter><digit>")."""
    seen = set()
    for tok in PREFIX_SWEEP:
        prefix = tok.split("-")[0]
        seen.add(prefix)
        s = g.build_system(tok)
        r = g.generate_route(tok)
        assert r['designation'] == tok, f"{tok}: designation {r['designation']!r} not the token"
        assert len(r['designation']) <= 7, f"{tok}: designation too long for OLED column"
        bt = s.bodies[s.dest_idx].type
        assert bt in _PREFIX_TYPES[prefix], f"{tok}: dest type {bt} not pinned for {prefix}"
        dest_class = r['dest_class']
        if prefix == "KG":
            assert dest_class in _ROMAN, \
                f"{tok}: KG dest_class {dest_class!r} not a gas-giant roman"
        if prefix in ("LV", "BG") and bt == g.STARMAP_PLANET:
            assert dest_class not in _ROMAN, \
                f"{tok}: {prefix} rocky-planet dest got gas-giant class {dest_class!r}"
    assert {"LV", "KG", "BG", "RF"} <= seen, f"sweep missed a prefix: {seen}"


def test_lv_dest_spans_moons_and_planets():
    """LV pins "moon or rocky planet", so the LV sweep must land on both across the
    range — the rock-vs-moon variety that falls out of the candidate pool."""
    lv_types = set()
    for n in range(100, 700):
        s = g.build_system(f"LV-{n}")
        lv_types.add(s.bodies[s.dest_idx].type)
    assert g.STARMAP_MOON in lv_types, "no LV moon across the sweep"
    assert g.STARMAP_PLANET in lv_types, "no LV rocky planet across the sweep"


def test_token_case_insensitive_bookmark():
    """"lv-426" and "LV-426" seed the same world and both display "LV-426" — the
    designation is a case-insensitive bookmark."""
    a = g.generate_route("lv-426")
    b = g.generate_route("LV-426")
    assert a['designation'] == "LV-426"
    assert a == b, "case variants produced different routes"
    sa, sb = g.build_system("lv-426"), g.build_system("LV-426")
    assert sa.seed == sb.seed and bytes(sa.bodies) == bytes(sb.bodies)


def test_malformed_token_fallback():
    """A non-conforming token takes the legacy derived path: a valid ??-NNN
    designation and no crash (no pinning, no token echo)."""
    import re
    for tok in ("garbage", "LV", "LV-", "LV-12345", "ZZ-100", "LV-42X", "3917459122", ""):
        d = g.generate_route(tok)['designation']
        assert re.fullmatch(r"(LV|BG|KG|RF)-\d+", d), f"{tok!r}: fallback designation {d!r} invalid"


def test_body_budget_across_sweep():
    """Selection/construction never overflows the fixed body / planet arrays."""
    for tok in PREFIX_SWEEP:
        s = g.build_system(tok)
        assert s.body_count <= 16, f"{tok}: body_count {s.body_count} over budget"
        assert s.planet_count <= 6, f"{tok}: planet_count {s.planet_count} over budget"


def test_designation_deterministic():
    for d in DESIGS:
        assert g.generate_route(d)['designation'] == g.generate_route(d)['designation']


# ── Destination spectral class + telemetry strip ──────────────────────────────

# Telemetry-strip geometry mirror — single source in oled_gfx_lib (mirrors oled_gfx.h).
GFX_TT_ROWS     = g.GFX_TT_ROWS
GFX_TEL_GAP     = g.GFX_TEL_GAP
GFX_TEL_PITCH   = g.GFX_TEL_PITCH
GFX_TEL_NFIELDS = g.GFX_TEL_NFIELDS
GFX_TEL_LABEL   = g.GFX_TEL_LABEL
GFX_TEL_ORDER   = g.GFX_TEL_ORDER
tel_nfields     = g.tel_nfields
tel_line_x      = g.tel_line_x


# Mixed-prefix sweep so strip and no-strip routes both appear and every body type —
# including a KG gas giant (Sudarsky roman) — is exercised.
BANNER_DESIGS = PREFIX_SWEEP

# Allowed dest_class per destination body type. Sub-stellar bodies use reflectance
# taxonomies: gas giants → Sudarsky roman I–V; everything else → "<letter><digit>"
# from a per-type pool (see starmap_spectral_class).
_ROMAN = {"I", "II", "III", "IV", "V"}

def _class_ok(body_type, c):
    if body_type == g.STARMAP_MOON:
        return len(c) == 2 and c[0] in "CDPS" and c[1].isdigit()
    if body_type == g.STARMAP_TROJAN:
        return len(c) == 2 and c[0] in "DPXCSV" and c[1].isdigit()
    if body_type == g.STARMAP_VAGRANT:
        return len(c) == 2 and c[0] in "DPCXSV" and c[1].isdigit()
    if body_type == g.STARMAP_PLANET:           # gas giant (roman) or rocky (<L><d>)
        return c in _ROMAN or (len(c) == 2 and c[0] in "SQVMK" and c[1].isdigit())
    return False


def test_dest_class_valid_and_deterministic():
    seen = set()
    for d in BANNER_DESIGS:
        s = g.build_system(d)
        c = g.generate_route(d)['dest_class']
        seen.add(c)
        assert 0 < len(c) <= 3, f"{d}: dest_class {c!r} wrong length"
        bt = s.bodies[s.dest_idx].type
        assert _class_ok(bt, c), f"{d}: dest_class {c!r} invalid for body type {bt}"
        assert g.generate_route(d)['dest_class'] == c, f"{d}: dest_class not deterministic"
    # Classes vary across the sweep, and gas-giant romans do occur.
    assert len(seen) > 5, f"dest_class barely varies: {seen}"
    assert seen & _ROMAN, "no gas-giant roman class appeared across the sweep"


# ── Destination colony status (DOCKED / LANDED · colony / unpopulated) ─────────
#
# A gas giant always hosts a colony; every other body is settled 70% of the time,
# decided by a salted (seed,idx) hash so the same token always reports the same
# status and the host matches the device (see starmap_is_colony).

def test_is_colony_present_and_deterministic():
    """generate_route carries is_colony, and it's stable per token."""
    for d in BANNER_DESIGS:
        r = g.generate_route(d)
        assert 'is_colony' in r and isinstance(r['is_colony'], bool)
        assert g.generate_route(d)['is_colony'] == r['is_colony'], f"{d}: is_colony not deterministic"


def test_kg_destinations_always_colony():
    """Every KG destination is a gas giant, which always reads colony."""
    for n in range(100, 400):
        tok = f"KG-{n}"
        assert g.generate_route(tok)['is_colony'] is True, f"{tok}: gas-giant dest not a colony"


def test_colony_split_has_both():
    """Across the non-KG sweep both colony and unpopulated worlds occur (the 70/30
    split), so the DOCKED/LANDED label genuinely varies."""
    seen = {g.generate_route(f"LV-{n}")['is_colony'] for n in range(100, 600)}
    assert seen == {True, False}, f"colony split degenerate across the sweep: {seen}"


def test_cli_annotates_colony_status():
    """The itinerary CLI tags the destination colony/unpopulated, agreeing with
    generate_route's is_colony for the same token."""
    import route_explain as rx
    colony_tok = "KG-348"                       # gas giant → always a colony
    assert g.generate_route(colony_tok)['is_colony'] is True
    assert "colony" in rx.render(colony_tok), f"{colony_tok}: CLI missing colony tag"
    unpop = next(f"LV-{n}" for n in range(100, 600)
                 if not g.generate_route(f"LV-{n}")['is_colony'])
    assert "unpopulated" in rx.render(unpop), f"{unpop}: CLI missing unpopulated tag"


# ── Telemetry-strip rendering helpers ─────────────────────────────────────────

_DESIG_RE = re.compile(r"^(LV|KG|BG|RF)-\d+$")


def _strip_pixels(r):
    """Lit pixels of the telemetry strip alone (route geometry suppressed)."""
    bare = {**r, 'arcs': [], 'legs': [], 'markers': [], 'bodies': []}
    return lit_pixels(g.bake_bg(bare))


def _glyph_at_line(pix, bx, bw, line):
    """Pixel block of strip line `line`, translated to its own origin so the same
    string at different positions compares equal. Each line is exactly GFX_TT_ROWS px
    wide on the long axis and lines are GFX_TEL_PITCH apart, so the 5-px band is clean."""
    lx = tel_line_x(bx, bw, line)
    band = [(x, y) for x, y in pix if lx <= x <= lx + GFX_TT_ROWS - 1]
    if not band:
        return frozenset()
    mnx = min(x for x, _ in band); mny = min(y for _, y in band)
    return frozenset((x - mnx, y - mny) for x, y in band)


def _canonical_labels():
    """Reference glyph per label, baked by the firmware itself: one n=5 strip renders
    every label (SYS/ORG/DST/ETA/STATUS) at a known slot, so each is a ground-truth
    template to match the real strips against — no font reimplementation needed."""
    bw = GFX_TEL_NFIELDS * 2 * GFX_TEL_PITCH - GFX_TEL_GAP
    bx = 128 - bw - g.GFX_TEL_CHIP_PAD
    tpl = {'arc_cx': 0, 'arc_cy': 16, 'arcs': [], 'dash_px': 3, 'gap_px': 3,
           'legs': [], 'markers': [], 'bodies': [], 'ship_offset_x': 0, 'eta_minutes': 0,
           'system_name': 'CALPAMOS', 'designation': 'LV-426', 'dest_class': 'D4',
           'origin': 'BG-203', 'banner_x': bx, 'banner_w': bw}
    pix = _strip_pixels(tpl)
    return {GFX_TEL_LABEL[fid]: _glyph_at_line(pix, bx, bw, 2 * slot)
            for slot, fid in enumerate(GFX_TEL_ORDER[GFX_TEL_NFIELDS])}


def _ring_spills_short_axis(r):
    """True if any orbital ring's vertical reach leaves the 32-px short axis."""
    for a in r['arcs']:
        cy = a['cy'] if a['local_center'] else r['arc_cy']
        if cy - a['radius'] < 0 or cy + a['radius'] > 31:
            return True
    return False


# ── Telemetry-strip tests ─────────────────────────────────────────────────────

def test_origin_valid_and_deterministic():
    seen = set()
    for d in BANNER_DESIGS:
        o = g.generate_route(d)['origin']
        assert _DESIG_RE.match(o), f"{d}: origin {o!r} is not a ??-NNN designation"
        assert g.generate_route(d)['origin'] == o, f"{d}: origin not deterministic"
        seen.add(o[:2])
    assert len(seen) >= 2, f"origin prefixes barely vary: {seen}"


def test_strip_layout_breather_gap():
    """Each strip-bearing route parks the strip flush to the high-x edge at [128-banner_w,
    128) and guarantees a fixed GFX_TEL_MAP_GAP breather between the (lit) map and the strip,
    overriding the equal-gap split so the text isn't jammed against the graphics. When there
    is slack to spare the map still centers (the two gaps balance), but the map→strip gap is
    never below the breather, and the map stays on-panel."""
    GAP = g.GFX_TEL_MAP_GAP
    n_strip = 0
    for d in BANNER_DESIGS:
        r = g.generate_route(d)
        n = tel_nfields(r['banner_w'])
        if n == 0:
            assert r['banner_x'] == 0, f"{d}: no strip but banner_x set"
            continue
        n_strip += 1
        bx, bw = r['banner_x'], r['banner_w']
        # Strip is flush to the high-x edge save GFX_TEL_CHIP_PAD reserved for the top chip border.
        assert bx == 128 - bw - g.GFX_TEL_CHIP_PAD, f"{d}: strip not at the high-x edge"
        lx0, lx1 = g.lit_xspan(r)                     # visible map extent the layout uses
        left, right = lx0, bx - lx1                   # leftGap and the map→strip breather
        assert right >= GAP - 1, f"{d}: map→strip gap {right} below breather {GAP}"
        assert lx0 >= 0 and lx1 < bx, f"{d}: map off-panel / into strip (lit [{lx0},{lx1}], bx {bx})"
        if right > GAP + 1:                          # slack beyond the breather → balanced
            assert abs(left - right) <= 1, f"{d}: slack not balanced ({left} vs {right})"
        # Strip pixels stay within the text region plus the top chip border (PAD above banner_w).
        for x, _ in _strip_pixels(r):
            assert bx <= x < bx + bw + g.GFX_TEL_CHIP_PAD, \
                f"{d}: strip pixel x={x} outside [{bx},{bx + bw + g.GFX_TEL_CHIP_PAD})"
    assert n_strip > 0, "no strip appeared across the sweep — feature never exercised"


def test_strip_region_clear_of_graphics():
    """The lit-based layout measures the visible map footprint, so the strip must still clear
    the graphics: no map pixel may fall in the strip's text region [banner_x, 128). The
    centered map + reserved breather guarantee it — asserting it across the sweep is what lets
    route_gen drop the old conservative geometric ring measure without rings hitting text."""
    n_strip = 0
    for d in BANNER_DESIGS:
        r = g.generate_route(d)
        if not r['banner_w']:
            continue
        n_strip += 1
        lx1 = g.lit_xspan(r)[1]
        assert lx1 < r['banner_x'], \
            f"{d}: map graphics reach x={lx1}, into the strip at {r['banner_x']}"
    assert n_strip > 0, "no strip appeared across the sweep — invariant never exercised"


def test_strip_field_count_and_order():
    """For an n-field strip the labels read top→bottom exactly as GFX_TEL_ORDER[n]
    (e.g. 2 fields → ORG, DST; 5 fields → SYS, ORG, DST, ETA, STATUS). Each rendered
    label column is matched against the firmware-baked canonical glyph; tokens are found
    at every field count across the sweep."""
    canon = _canonical_labels()
    assert len(set(canon.values())) == len(canon), "canonical label glyphs not distinct"
    found = {}
    for d in BANNER_DESIGS:
        r = g.generate_route(d)
        n = tel_nfields(r['banner_w'])
        if 1 <= n <= GFX_TEL_NFIELDS:
            found.setdefault(n, (d, r))
    assert set(found) == set(range(1, GFX_TEL_NFIELDS + 1)), \
        f"sweep is missing field counts {sorted(set(range(1, 6)) - set(found))}"
    for n, (d, r) in sorted(found.items()):
        bx, bw = r['banner_x'], r['banner_w']
        pix = _strip_pixels(r)
        for slot, fid in enumerate(GFX_TEL_ORDER[n]):
            label = GFX_TEL_LABEL[fid]
            glyph = _glyph_at_line(pix, bx, bw, 2 * slot)
            assert glyph == canon[label], \
                f"{d} (n={n}): slot {slot} label glyph != canonical {label}"


def test_some_routes_have_no_strip_and_rings_spill():
    """The framing crop is intended: best-fit zoom fits the journey on the long axis and
    lets orbital rings run off the short-axis edges. Many routes leave no long-axis room
    and so carry no strip; confirm both that no-strip routes occur and that nothing forces
    their spilled rings back on-panel."""
    no_strip = [r for r in (g.generate_route(d) for d in BANNER_DESIGS)
                if tel_nfields(r['banner_w']) == 0]
    assert no_strip, "every route got a strip — the no-strip crop is never exercised"
    assert any(_ring_spills_short_axis(r) for r in no_strip), \
        "no no-strip route lets a ring spill off the short axis — framing may be over-constrained"


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


# ── Frame-aware routing: planet↔its-own-moon stays in the planet-local frame ──
#
# A planet↔its-own-moon trip is planned in the host planet's local frame, so the
# whole route sits at the moon's orbital scale (~a few wu) around the host planet,
# never looping out to the star's neighborhood (the old BG-794/LV-100 bug). The
# route's geometry is exposed only in display px, so recover the world→display
# similarity from the two endpoint markers — whose *world* positions are known via
# the bridge — and invert the leg/ship geometry back to world to bound it.

# Tokens spanning all four prefixes; the local ones (planet ↔ its own moon) are
# selected inside the test, the rest skipped. LV/BG dominate because only they pin
# moon destinations, the raw material for the planet↔own-moon pair.
LOCAL_SCAN = ([f"LV-{n}" for n in range(100, 700)] + [f"BG-{n}" for n in range(100, 700)] +
              [f"KG-{n}" for n in range(100, 400)] + [f"RF-{n}" for n in range(1000, 1300)])


def _is_planet_own_moon(s):
    """(host_planet_idx, moon_idx) if depart/dest are a planet and its own moon,
    else None — mirrors the C frame classification in rg_plan."""
    dep, dst = s.depart_idx, s.dest_idx
    db, eb = s.bodies[dep], s.bodies[dst]
    if eb.type == g.STARMAP_MOON and eb.parent_idx == dep:
        return dep, dst
    if db.type == g.STARMAP_MOON and db.parent_idx == dst:
        return dst, dep
    return None


def _recover_world_to_disp(s, r):
    """Recover the (a, b, e, f) of the route's world→display map from its two
    endpoint markers. rg_to_disp is a reflection-similarity (rotation + uniform
    scale + y-flip): disp.x = a·wx + b·wy + e, disp.y = b·wx − a·wy + f. Two
    world↔display correspondences (depart, dest) pin all four coefficients."""
    w0 = g.world_pos(s, s.depart_idx)
    w1 = g.world_pos(s, s.dest_idx)
    d0 = (r['markers'][0]['x'], r['markers'][0]['y'])
    d1 = (r['markers'][1]['x'], r['markers'][1]['y'])
    dwx, dwy = w1[0] - w0[0], w1[1] - w0[1]
    ddx, ddy = d1[0] - d0[0], d1[1] - d0[1]
    det = dwx * dwx + dwy * dwy
    assert det > 1e-6, "degenerate endpoints"
    a = (dwx * ddx - dwy * ddy) / det
    b = (dwy * ddx + dwx * ddy) / det
    e = d0[0] - a * w0[0] - b * w0[1]
    f = d0[1] - (b * w0[0] - a * w0[1])
    return a, b, e, f


def _disp_to_world(coef, dx, dy):
    """Invert _recover_world_to_disp for one display point."""
    a, b, e, f = coef
    den = a * a + b * b
    px, py = dx - e, dy - f
    wx = (a * px + b * py) / den
    wy = (b * px - a * py) / den
    return wx, wy


def _route_world_points(s, r):
    """The route's flown geometry mapped back to world: every leg endpoint plus the
    ship sampled densely along the whole path (so the coast arc and bowed transfers
    are included)."""
    coef = _recover_world_to_disp(s, r)
    pts = []
    for leg in r['legs']:
        pts.append(_disp_to_world(coef, leg['p0x'], leg['p0y']))
        pts.append(_disp_to_world(coef, leg['p1x'], leg['p1y']))
    for k in range(41):
        dx, dy = g.ship_pos(r, k / 40.0)
        pts.append(_disp_to_world(coef, dx, dy))
    return pts


def test_planet_moon_routes_are_local():
    """Every planet↔its-own-moon route stays at moon-orbit scale around the host
    planet and never wanders out toward the star — the BG-794/LV-100 regression."""
    n_local = 0
    for d in LOCAL_SCAN:
        s = g.build_system(d)
        hm = _is_planet_own_moon(s)
        if hm is None:
            continue
        n_local += 1
        host, moon = hm
        hx, hy = g.world_pos(s, host)
        moon_r = s.bodies[moon].orbital_radius
        host_dist = math.hypot(hx, hy)            # host planet ↔ star
        pts = _route_world_points(s, r := g.generate_route(d))
        far_from_host = max(math.hypot(px - hx, py - hy) for px, py in pts)
        near_origin   = min(math.hypot(px, py) for px, py in pts)
        # Moon-orbit scale: the whole route sits within a few moon-radii of the host.
        assert far_from_host <= 4.0 * moon_r, \
            f"{d}: route reaches {far_from_host:.1f} wu from host (moon_r={moon_r:.1f})"
        # Strictly inside the host's own neighborhood — the heliocentric bug put the
        # coast ~2× host_dist away from the host, so this cleanly fails for it.
        assert far_from_host < host_dist, \
            f"{d}: route extent {far_from_host:.1f} not local to host (host_dist={host_dist:.1f})"
        # Never reaches the star (origin).
        assert near_origin > moon_r, \
            f"{d}: route passes within {near_origin:.1f} wu of the star"
    assert n_local > 50, f"only {n_local} planet↔moon seeds in the scan — too few to trust"


def test_planet_own_moon_local_eta_discovered():
    """The seed-is-designation generalization of the old hard-coded LV-100 anchor:
    LV-100 no longer pins to that topology, so discover the first planet↔own-moon
    system in the scan and assert its ETA is a local hop, far under the old
    heliocentric 384 minutes."""
    anchor = next((tok for tok in LOCAL_SCAN if _is_planet_own_moon(g.build_system(tok))), None)
    assert anchor is not None, "no planet↔own-moon system discovered in the scan"
    eta = g.generate_route(anchor)['eta_minutes']
    assert eta < 300, f"{anchor}: planet↔own-moon ETA {eta} not in the local-hop range"


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


# ── Explain hook + itinerary CLI ──────────────────────────────────────────────
#
# route_gen_explain mirrors route_gen_describe's "re-run rg_plan" shape but returns
# the structured facts the route_explain.py itinerary needs: topology, the named
# pivot, the flyby L-selector. Cross-check it against the leg geometry the public
# route already exposes, then assert the CLI renders the journey's identity.

def _topology_from_legs(r):
    """Topology implied by the route's leg types (mirrors test_anim._topology)."""
    types = [l['type'] for l in r['legs']]
    if len(types) == 1:
        return g.RG_DIRECT
    if types == [g.GFX_LEG_TRANSFER, g.GFX_LEG_TRANSFER]:
        return g.RG_FLYBY
    if g.GFX_LEG_COAST in types:
        return g.RG_COAST
    return None


def _topology_tokens():
    """One LV token of each topology, discovered from a sweep."""
    found = {}
    for n in range(100, 1400):
        tok = f"LV-{n}"
        tp = _topology_from_legs(g.generate_route(tok))
        if tp is not None and tp not in found:
            found[tp] = tok
        if len(found) == 3:
            break
    return found


_TOPO_TOKENS = _topology_tokens()


def test_explain_topology_matches_legs():
    """route_gen_explain agrees with the leg geometry route_gen_build packs: the
    sweep finds all three topologies, and for each the explain topology + leg count
    match what the public route's legs say."""
    assert set(_TOPO_TOKENS) == {g.RG_DIRECT, g.RG_FLYBY, g.RG_COAST}, \
        f"sweep missed a topology: {_TOPO_TOKENS}"
    for tp, tok in _TOPO_TOKENS.items():
        e = g.route_explain(tok)
        r = g.generate_route(tok)
        assert e['topology'] == tp, f"{tok}: explain topology {e['topology']} != legs {tp}"
        assert e['leg_count'] == len(r['legs']), \
            f"{tok}: explain leg_count {e['leg_count']} != {len(r['legs'])}"


def test_explain_pivot_and_flyby_selector():
    """The pivot is a valid ??-NNN designation for a flyby/coast and empty for a
    direct hop; the flyby L-selector is L1/L2 (0/1) exactly when the route is a
    flyby, and -1 otherwise."""
    for tp, tok in _TOPO_TOKENS.items():
        e = g.route_explain(tok)
        if tp == g.RG_DIRECT:
            assert e['pivot'] == "", f"{tok}: direct route named a pivot {e['pivot']!r}"
        else:
            assert _DESIG_RE.match(e['pivot']), \
                f"{tok}: pivot {e['pivot']!r} is not a ??-NNN designation"
        if tp == g.RG_FLYBY:
            assert e['flyby_lagrange'] in (0, 1), \
                f"{tok}: flyby L-selector {e['flyby_lagrange']} not in {{0,1}}"
        else:
            assert e['flyby_lagrange'] == -1, \
                f"{tok}: non-flyby L-selector {e['flyby_lagrange']} should be -1"


def test_cli_render_has_identity():
    """The itinerary CLI renders each topology and embeds the journey's identity —
    ORG, DST, system name — and, for a flyby, the named pivot and the topology
    label. render() is the module's testable formatter."""
    import route_explain as rx
    for tp, tok in _TOPO_TOKENS.items():
        r = g.generate_route(tok)
        out = rx.render(tok)
        for needle in (r['origin'], r['designation'], r['system_name']):
            assert needle in out, f"{tok}: render missing {needle!r}\n{out}"
    # The flyby case additionally names the pivot and labels the topology.
    fly = _TOPO_TOKENS[g.RG_FLYBY]
    e = g.route_explain(fly)
    out = rx.render(fly)
    for needle in (e['pivot'], "gravity-assist flyby"):
        assert needle in out, f"{fly}: flyby render missing {needle!r}\n{out}"
