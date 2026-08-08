"""
Journey state-machine tests — drive the real route_anim.c (the firmware's master
loop) on the host with a simulated clock + eeconfig counter, confirming the
player-controlled three-state loop that is otherwise only observable on-device over
1.5–10 hours:

  MISSION CONTROL → (LAUNCH) → MID-MISSION → (arrival) → MISSION COMPLETE
       ▲                                                        │
       └──────────────────── (BACK: new mission) ──────────────┘

Nothing auto-advances: boot holds in Mission Control, the ship only flies after
LAUNCH, arrival holds in Mission Complete (no regen), and BACK plots a fresh
mission. Re-roll, ETA trim, and freeform token entry edit the held plan.

    uv run --with pytest --with pillow pytest test_anim.py -q
"""
import oled_gfx_lib as g


def _fresh(boot_n, now=5):
    """Boot a journey at a fixed clock with boot counter = boot_n."""
    g.set_boot_counter(boot_n)
    g.set_now_ms(now)
    return g.anim_init()


def _ident(j):
    """(system, designation, dest_rng) — the route identity, for change checks."""
    r = g.journey_route(j)
    return (r['system_name'], r['designation'], j.dest_rng)


# ── Boot lands in Mission Control, held, fresh each power cycle ────────────────

def test_boot_lands_in_control_held():
    """Boot plots a route but holds it in Mission Control — the ship does not fly
    until LAUNCH, so advancing the clock leaves cur_t pinned at 0."""
    j = _fresh(0, now=5)
    r = g.journey_route(j)
    assert g.journey_state(j) == g.RA_CONTROL
    assert not j.active
    assert len(r['legs']) >= 1 and r['system_name'] and r['designation']
    assert j.duration_ms > 0
    # Held: the clock advancing past a whole journey must not move the ship.
    g.set_now_ms(5 + j.duration_ms * 2)
    g.anim_render_map(j)
    assert g.journey_state(j) == g.RA_CONTROL
    assert j.cur_t == 0.0


def test_boot_counter_makes_each_boot_fresh():
    """Even with an identical boot clock, the persisted counter must yield a
    different opening route nearly every power cycle (the entropy the boot-time
    timer alone can't provide)."""
    seen = []
    for n in range(30):
        j = _fresh(n, now=5)            # SAME clock every "boot" — counter is the only variable
        seen.append(_ident(j))
    distinct = len(set(seen))
    assert distinct >= 28, f"only {distinct}/30 distinct opening routes across boots"


def test_boot_counter_persists_and_increments():
    g.set_boot_counter(41)
    g.set_now_ms(5)
    g.anim_init()
    assert g._get_lib().host_get_boot_counter() == 42   # bumped + written back


# ── LAUNCH flies the ship; arrival holds in Mission Complete (no regen) ────────

def test_launch_flies_then_holds_complete():
    j = _fresh(7, now=1000)
    assert g.journey_state(j) == g.RA_CONTROL
    g.set_now_ms(1000)
    g.anim_launch(j)
    assert g.journey_state(j) == g.RA_MISSION and j.active
    dur = j.duration_ms
    # Mid-flight: the ship tracks the clock.
    g.set_now_ms(1000 + int(dur * 0.5))
    g.anim_render_map(j)
    assert g.journey_state(j) == g.RA_MISSION
    assert abs(j.cur_t - 0.5) < 0.02
    # Cross the finish line: lands in Mission Complete, pinned at the destination.
    g.set_now_ms(1000 + dur + 1)
    g.anim_render_map(j)
    assert g.journey_state(j) == g.RA_COMPLETE
    assert j.cur_t == 1.0 and not j.active
    # And STAYS — no auto-regen even after the clock runs far past arrival.
    before = _ident(j)
    g.set_now_ms(1000 + dur * 4)
    g.anim_render_map(j)
    assert g.journey_state(j) == g.RA_COMPLETE
    assert _ident(j) == before, "Mission Complete regenerated the route (should hold)"


def test_ship_moves_from_departure_to_destination():
    j = _fresh(7, now=0)
    g.anim_launch(j)
    dep = (j.route.legs[0].p0x, j.route.legs[0].p0y)
    dst = (j.route.legs[j.route.leg_count - 1].p1x, j.route.legs[j.route.leg_count - 1].p1y)
    r = g.journey_route(j)
    assert g.ship_pos(r, 0.0) == dep
    assert g.ship_pos(r, 1.0) == dst
    assert dep != dst


# ── BACK plots a fresh mission; re-roll re-plots, held ────────────────────────

def test_back_from_complete_returns_to_control():
    j = _fresh(11, now=0)
    g.anim_launch(j)
    g.set_now_ms(j.duration_ms + 1)
    g.anim_render_map(j)
    assert g.journey_state(j) == g.RA_COMPLETE
    before = _ident(j)
    g.anim_back(j)
    assert g.journey_state(j) == g.RA_CONTROL
    assert not j.active and j.cur_t == 0.0
    assert _ident(j) != before, "BACK did not plot a fresh mission"
    # BACK is a no-op away from Mission Complete.
    held = _ident(j)
    g.anim_back(j)
    assert _ident(j) == held and g.journey_state(j) == g.RA_CONTROL


def test_reroll_replots_held_in_control():
    """The re-roll key swaps to a fresh random system but stays held in Mission
    Control (it does not launch)."""
    j = _fresh(11, now=1000)
    before = _ident(j)
    g.set_now_ms(7777)
    g.anim_reroll(j)
    after = _ident(j)
    assert after[2] != before[2], "reroll did not advance the seed sequence"
    assert after != before, "reroll did not change the route"
    assert g.journey_state(j) == g.RA_CONTROL
    assert not j.active and j.cur_t == 0.0


def test_reroll_is_control_only():
    """Re-roll is inert once a mission has launched — it only re-plots from Mission
    Control (where its key lives)."""
    j = _fresh(11, now=0)
    g.anim_launch(j)
    g.set_now_ms(int(j.duration_ms * 0.4))
    g.anim_render_map(j)
    before = _ident(j)
    g.anim_reroll(j)
    assert g.journey_state(j) == g.RA_MISSION
    assert _ident(j) == before, "reroll mutated an in-flight mission"


# ── ETA trim: clamps [30, 599], preserves progress while flying ───────────────

def test_adjust_eta_clamps_range():
    j = _fresh(3, now=0)
    for _ in range(40):
        g.anim_adjust_eta(j, -30)
    assert g.journey_route(j)['eta_minutes'] == 30, "ETA under-ran the 30-min floor"
    for _ in range(60):
        g.anim_adjust_eta(j, +30)
    assert g.journey_route(j)['eta_minutes'] == 599, "ETA over-ran the 599-min cap"
    assert j.duration_ms == 599 * 60000


def test_adjust_eta_preserves_progress_while_flying():
    """Trimming the duration mid-flight re-anchors the clock so the ship doesn't
    jump — the progress fraction is preserved across the change."""
    j = _fresh(3, now=0)
    g.anim_launch(j)
    g.set_now_ms(int(j.duration_ms * 0.4))
    g.anim_render_map(j)
    t_before = j.cur_t
    g.anim_adjust_eta(j, +60)
    g.anim_render_map(j)
    assert abs(j.cur_t - t_before) < 0.01, f"progress jumped {t_before:.3f} → {j.cur_t:.3f}"


# ── Freeform token entry plots that exact system ──────────────────────────────

def test_set_token_plots_exact_system():
    j = _fresh(1, now=0)
    g.anim_set_token(j, "LV-426")
    assert g.journey_state(j) == g.RA_CONTROL
    r = g.journey_route(j)
    assert r['designation'] == "LV-426"
    # Same identity as a direct build of that token.
    direct = g.generate_route("LV-426")
    assert r['system_name'] == direct['system_name']
    assert r['designation'] == direct['designation']


# ── Telemetry mirrors the live journey + its state ────────────────────────────

def test_telemetry_carries_state_and_colony():
    j = _fresh(5, now=0)
    t = g.anim_fill_telemetry(j)
    assert t['state'] == g.RA_CONTROL
    assert t['burn'] is False                     # the t=0 departure burn is suppressed off-mission
    assert isinstance(t['is_colony'], bool)
    assert t['is_colony'] == g.journey_route(j)['is_colony']
    # Launch → mission telemetry counts down and fires the departure burn.
    g.set_now_ms(0); g.anim_launch(j)
    dur = j.duration_ms
    g.set_now_ms(0);              g.anim_render_map(j)
    t_full = g.anim_fill_telemetry(j)
    g.set_now_ms(int(dur * 0.5)); g.anim_render_map(j)
    t_half = g.anim_fill_telemetry(j)
    assert t_full['state'] == g.RA_MISSION
    assert t_half['eta_remaining_min'] < t_full['eta_remaining_min']
    assert t_full['phase'] == g.GFX_PHASE_DEPART
    assert t_full['burn'] is True
    # Arrival holds in Mission Complete; the arrival burn must NOT trip the slave.
    g.set_now_ms(dur + 1); g.anim_render_map(j)
    arr = g.anim_fill_telemetry(j)
    assert arr['state'] == g.RA_COMPLETE
    assert arr['burn'] is False, "Mission Complete reported a burn (would trigger the slave takeover)"


# ── Per-frame telemetry strip on the master's live panel ──────────────────────

def _lit(buf):
    """Lit (x, y) of a 512-byte SSD1306 page buffer (the live panel capture)."""
    pts = set()
    for page in range(4):
        for x in range(128):
            b = buf[page * 128 + x]
            for bit in range(8):
                if b & (1 << bit):
                    pts.add((x, page * 8 + bit))
    return pts


def _value_line_pixels(r, field_id):
    """Live-panel pixels of field `field_id`'s value line, isolated to its 5-px band on
    the long axis. The value line is empty in the baked background (it's drawn per frame),
    so whatever lands here came from route_anim's overlay."""
    bx, bw = r['banner_x'], r['banner_w']
    slot = g.tel_field_slot(g.tel_nfields(bw), field_id)
    assert slot >= 0, f"field {field_id} absent from a {g.tel_nfields(bw)}-field strip"
    lx = g.tel_line_x(bx, bw, 2 * slot + 1)
    return {(x, y) for x, y in _lit(g.panel_capture()) if lx <= x <= lx + g.GFX_TT_ROWS - 1}


def _find_journey_with_fields(boot0, want_n):
    """Boot, then re-roll until the (held) route carries exactly want_n strip fields."""
    j = _fresh(boot0, now=0)
    for _ in range(1000):
        if g.tel_nfields(g.journey_route(j)['banner_w']) == want_n:
            return j
        g.anim_reroll(j)
    raise AssertionError(f"no {want_n}-field strip route found across re-rolls")


def test_strip_eta_and_status_render_live():
    """The strip's ETA and STATUS values can't bake into bg_cache — the ETA counts down
    and the STATUS tracks the phase — so route_anim_render_map redraws them onto the live
    panel each frame. On a launched 5-field route (which carries both an ETA and a STATUS
    slot), confirm both value lines render, the ETA pixels change between an early and a
    later frame (counting down), and the STATUS word changes as the phase advances toward
    ARRIVE."""
    j = _find_journey_with_fields(5, want_n=g.GFX_TEL_NFIELDS)
    g.set_now_ms(0); g.anim_launch(j)
    dur = j.duration_ms
    g.set_now_ms(int(dur * 0.05));  g.anim_render_map(j)
    eta_early  = _value_line_pixels(g.journey_route(j), 2)   # ETA value
    sta_early  = _value_line_pixels(g.journey_route(j), 3)   # STATUS value
    g.set_now_ms(int(dur * 0.5));   g.anim_render_map(j)
    eta_mid    = _value_line_pixels(g.journey_route(j), 2)
    g.set_now_ms(int(dur * 0.999)); g.anim_render_map(j)
    sta_arrive = _value_line_pixels(g.journey_route(j), 3)
    assert eta_early and eta_mid, "ETA value line never rendered on the live panel"
    assert eta_early != eta_mid, "ETA value did not change — not counting down live"
    assert sta_early and sta_arrive, "STATUS value line never rendered on the live panel"
    assert sta_early != sta_arrive, "STATUS word did not change as the phase advanced"


# ── Phase STATUS + burn warning (gfx_route_phase, the master-side model) ───────
#
# Burns are physically motivated and not free: departure injection + arrival
# capture (4 min), one MCC trim per transfer leg (90 s), coast insert/eject trims
# (90 s) — but a gravity-assist flyby is unpowered. So a flight runs 3 burns
# (direct), 4 (flyby), or 6 (coast), and STATUS cycles DEPART→TRANSIT→(FLYBY|
# COAST)→…→ARRIVE.

GFX_LEG_TRANSFER, GFX_LEG_COAST = 0, 1


def _topology(route):
    types = [l['type'] for l in route['legs']]
    if len(types) == 1:
        return 'direct'
    if types == [GFX_LEG_TRANSFER, GFX_LEG_TRANSFER]:
        return 'flyby'
    if GFX_LEG_COAST in types:
        return 'coast'
    return 'other'


def _find_topologies():
    """One designation of each topology, picked from a sweep (as the plan asks)."""
    found = {}
    for n in range(100, 1400):
        r = g.generate_route(f"LV-{n}")
        tp = _topology(r)
        if tp in ('direct', 'flyby', 'coast') and tp not in found:
            found[tp] = (f"LV-{n}", r)
        if len(found) == 3:
            break
    return found


_TOPOS = _find_topologies()


def test_sweep_covers_all_three_topologies():
    assert set(_TOPOS) == {'direct', 'flyby', 'coast'}, f"missing topology: {set(_TOPOS)}"


def _phase_sequence(route, n=3000):
    seq = []
    for i in range(n + 1):
        nm = g.PHASE_NAMES[g.route_phase(route, i / n)[0]]
        if not seq or seq[-1] != nm:
            seq.append(nm)
    return seq


_EXPECTED_SEQ = {
    'direct': ['DEPART', 'TRANSIT', 'ARRIVE'],
    'flyby':  ['DEPART', 'TRANSIT', 'FLYBY', 'TRANSIT', 'ARRIVE'],
    'coast':  ['DEPART', 'TRANSIT', 'COAST', 'TRANSIT', 'ARRIVE'],
}


def test_phase_sequence_by_topology():
    for tp, (d, r) in _TOPOS.items():
        assert _phase_sequence(r) == _EXPECTED_SEQ[tp], f"{tp} {d}: {_phase_sequence(r)}"


def _burn_intervals(route, n=20000):
    """[(start_t, end_t), …] for each contiguous burn window along the journey."""
    res, start, prev = [], None, False
    for i in range(n + 1):
        bn = g.route_phase(route, i / n)[1]
        if bn and not prev:
            start = i / n
        if not bn and prev:
            res.append((start, i / n))
        prev = bn
    if prev:
        res.append((start, 1.0))
    return res


_EXPECTED_BURNS = {'direct': 3, 'flyby': 4, 'coast': 6}


def test_burn_count_by_topology():
    for tp, (d, r) in _TOPOS.items():
        ivs = _burn_intervals(r)
        assert len(ivs) == _EXPECTED_BURNS[tp], f"{tp} {d}: {len(ivs)} burns {ivs}"


def test_burn_window_lengths():
    """Departure/arrival burn ~4 min (one-sided); interior trims ~90 s."""
    for tp, (d, r) in _TOPOS.items():
        eta = r['eta_minutes']
        ivs = _burn_intervals(r)
        for (a, b) in (ivs[0], ivs[-1]):
            assert abs((b - a) * eta - 4.0) < 0.5, f"{d}: major window {(b-a)*eta:.2f} min"
        for (a, b) in ivs[1:-1]:
            assert abs((b - a) * eta - 1.5) < 0.3, f"{d}: trim window {(b-a)*eta:.2f} min"


def test_burn_true_at_each_node_direct():
    d, r = _TOPOS['direct']
    assert g.route_phase(r, 0.0)[1] is True   # departure injection
    assert g.route_phase(r, 0.5)[1] is True   # MCC at the single leg's midpoint
    assert g.route_phase(r, 1.0)[1] is True   # arrival capture


def test_mcc_overlays_transit_without_changing_phase():
    """The MCC fires mid-transfer: burn true, but STATUS stays TRANSIT."""
    d, r = _TOPOS['direct']
    phase, burn = g.route_phase(r, 0.5)
    assert burn is True
    assert g.PHASE_NAMES[phase] == 'TRANSIT'


def test_flyby_junction_is_unpowered():
    """At the transfer↔transfer junction the phase reads FLYBY but no burn fires."""
    d, r = _TOPOS['flyby']
    total = sum(l['len'] for l in r['legs'])
    f1 = r['legs'][0]['len'] / total          # the junction t-value
    phase, burn = g.route_phase(r, f1)
    assert g.PHASE_NAMES[phase] == 'FLYBY'
    assert burn is False


def test_plain_cruise_has_no_burn():
    """A quarter into a direct transfer (clear of departure window and the 0.5
    MCC) is plain cruise: TRANSIT, no burn."""
    d, r = _TOPOS['direct']
    phase, burn = g.route_phase(r, 0.2)
    assert g.PHASE_NAMES[phase] == 'TRANSIT'
    assert burn is False


def test_burn_warning_buffer_is_dark_on_light():
    """The takeover is mostly lit (dark-on-light) and non-trivial (glyphs cut in)."""
    buf = g.burn_warning(0)
    assert len(buf) == 512
    lit = sum(bin(b).count('1') for b in buf)
    total = 512 * 8
    assert lit > total * 0.5, "burn warning should be a lit field (dark-on-light)"
    assert lit < total, "burn warning must cut dark glyphs into the field"


def test_burn_warning_stripes_march():
    """now_ms drives the hazard stripes, so frames differ over time."""
    assert g.burn_warning(0) != g.burn_warning(300)
