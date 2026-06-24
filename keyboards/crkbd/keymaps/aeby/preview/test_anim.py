"""
Journey state-machine tests — drive the real route_anim.c (the firmware's master
loop) on the host with a simulated clock + eeconfig counter, confirming the three
dynamic behaviors that are otherwise only observable on-device over 1.5–10 hours:

  1. boot picks a route, and it's a *fresh* one every power cycle;
  2. the ship progresses through the route in real (clock) time;
  3. on arrival the journey advances to a new route, endlessly.

    uv run --with pytest --with pillow pytest test_anim.py -q
"""
import oled_gfx_lib as g


def _fresh(boot_n, now=5):
    """Boot a journey at a fixed clock with boot counter = boot_n."""
    g.set_boot_counter(boot_n)
    g.set_now_ms(now)
    return g.anim_init()


# ── 1. Boot picks a route, fresh each power cycle ─────────────────────────────

def test_boot_picks_a_route():
    j = _fresh(0)
    r = g.journey_route(j)
    assert j.active
    assert len(r['legs']) >= 1
    assert r['system_name'] and r['designation']
    assert j.duration_ms > 0                 # eta_minutes → ms, non-degenerate


def test_boot_counter_makes_each_boot_fresh():
    """Even with an identical boot clock, the persisted counter must yield a
    different opening route nearly every power cycle (the entropy the boot-time
    timer alone can't provide)."""
    seen = []
    for n in range(30):
        j = _fresh(n, now=5)            # SAME clock every "boot" — counter is the only variable
        r = g.journey_route(j)
        seen.append((r['system_name'], r['designation'], j.dest_rng))
    distinct = len(set(seen))
    assert distinct >= 28, f"only {distinct}/30 distinct opening routes across boots"


def test_boot_counter_persists_and_increments():
    g.set_boot_counter(41)
    g.set_now_ms(5)
    g.anim_init()
    assert g._get_lib().host_get_boot_counter() == 42   # bumped + written back


# ── 2. The ship progresses through the route in clock time ────────────────────

def test_progress_tracks_clock():
    j = _fresh(7, now=1000)
    dur = j.duration_ms
    last = -1.0
    for frac in (0.0, 0.25, 0.5, 0.75, 0.99):
        g.set_now_ms(1000 + int(dur * frac))
        g.anim_render_map(j)
        assert j.cur_t >= last, "progress went backwards"
        assert abs(j.cur_t - frac) < 0.02, f"t={j.cur_t:.3f} expected ~{frac}"
        last = j.cur_t


def test_ship_moves_from_departure_to_destination():
    j = _fresh(7, now=0)
    dep = (j.route.legs[0].p0x, j.route.legs[0].p0y)
    dst = (j.route.legs[j.route.leg_count - 1].p1x, j.route.legs[j.route.leg_count - 1].p1y)
    g.set_now_ms(0);             g.anim_render_map(j)
    r = g.journey_route(j)
    assert g.ship_pos(r, 0.0) == dep
    assert g.ship_pos(r, 1.0) == dst
    assert dep != dst


# ── 3. Arrival advances to a new route ────────────────────────────────────────

def test_arrival_starts_new_route():
    j = _fresh(11, now=0)
    before = (g.journey_route(j)['system_name'], g.journey_route(j)['designation'], j.dest_rng)
    dur = j.duration_ms
    # Cross the finish line: elapsed >= duration triggers the next journey.
    g.set_now_ms(dur + 1)
    g.anim_render_map(j)
    after = (g.journey_route(j)['system_name'], g.journey_route(j)['designation'], j.dest_rng)
    assert after[2] != before[2], "dest_rng did not advance on arrival"
    assert after != before, "route did not change on arrival"
    assert j.start_ms == dur + 1, "new journey clock not reset to arrival time"
    assert j.cur_t < 0.01, "new journey did not restart progress at 0"


def test_reroll_jumps_to_a_new_route():
    """The re-roll key abandons the current journey mid-flight and starts the next
    system immediately — same transition as an arrival, but on demand."""
    j = _fresh(11, now=1000)
    before = (g.journey_route(j)['system_name'], g.journey_route(j)['designation'], j.dest_rng)
    g.set_now_ms(1000 + int(j.duration_ms * 0.4))    # partway through the flight
    g.anim_render_map(j)
    assert j.cur_t > 0.1                              # genuinely mid-journey
    g.set_now_ms(7777)
    g.anim_reroll(j)
    after = (g.journey_route(j)['system_name'], g.journey_route(j)['designation'], j.dest_rng)
    assert after[2] != before[2], "reroll did not advance the seed sequence"
    assert after != before, "reroll did not change the route"
    assert j.start_ms == 7777, "reroll did not reset the clock to now"
    assert j.cur_t < 0.01, "reroll did not restart progress at 0"
    assert j.active


def test_endless_chain_of_distinct_routes():
    j = _fresh(3, now=0)
    tokens, now = [], 0
    for _ in range(12):
        g.set_now_ms(now)
        g.anim_render_map(j)
        tokens.append(j.dest_rng)
        now += j.duration_ms + 1          # jump to just past this journey's arrival
    # Consecutive journeys differ (the LCG never repeats back-to-back).
    assert all(a != b for a, b in zip(tokens, tokens[1:]))
    assert len(set(tokens)) >= 11


# ── Telemetry mirrors the live journey ────────────────────────────────────────

def test_telemetry_counts_down_and_flips_phase():
    j = _fresh(5, now=0)
    dur = j.duration_ms
    g.set_now_ms(0);              g.anim_render_map(j)
    t_full = g.anim_fill_telemetry(j)
    g.set_now_ms(int(dur * 0.5)); g.anim_render_map(j)
    t_half = g.anim_fill_telemetry(j)
    assert t_half['eta_remaining_min'] < t_full['eta_remaining_min']
    assert t_full['phase'] == g.GFX_PHASE_DEPART   # t=0 is the departure injection
    assert t_full['burn'] is True
    # Just shy of arrival (no new journey yet): phase reads ARRIVE, burn fires.
    g.set_now_ms(int(dur * 0.999));  g.anim_render_map(j)
    arr = g.anim_fill_telemetry(j)
    assert arr['phase'] == g.GFX_PHASE_ARRIVE
    assert arr['burn'] is True
    assert t_full['designation'] == g.journey_route(j)['designation']  # telemetry == route


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
