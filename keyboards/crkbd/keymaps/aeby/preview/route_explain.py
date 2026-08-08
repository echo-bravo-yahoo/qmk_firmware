"""
Route itinerary: print a human-readable journey for one designation token.

    python3 route_explain.py <TOKEN>

Where route_dump.py prints raw world/display coordinates and route_samples.py
renders the OLED map, this prints the *itinerary* — the chain of bodies and
Lagrange waypoints the route threads, top→bottom, each with its icon, designation,
a type description, its role, and any burn. The flyby/coast pivot body is named
(e.g. "L2 of BG-340"), not called a generic "inner planet".

Identity (system name, ORG, DST, dest class, ETA) comes from route_gen_build; the
structure (topology, the named pivot, the flyby L-selector) from the host-only
route_gen_explain hook; body types from starmap_build. The burn set is fixed per
topology — direct 3, flyby 4, coast 6 — matching the firmware (oled_gfx.h burn
block / test_anim.py::test_burn_count_by_topology); no timestamps are shown.

Icon legend:
  bodies                       lagrange / path              maneuvers
    ☉ star (header)              ◇ L1 / L2  (collinear)       ✷ burn (powered)
    ◉ gas giant   (KG)           △ L4 / L5  (triangular)      · unpowered
    ● rocky planet (LV/BG)       ⌒ coast arc
    ☾ moon                       ╎ transfer leg (connector)
    ▲ trojan       ✦ vagrant
"""
import sys
import oled_gfx_lib as g

# ── Icons ─────────────────────────────────────────────────────────────────────
STAR_ICON  = "☉"
LEG_ICON   = "╎"
COAST_ICON = "⌒"
BURN       = "✷"
UNPOWERED  = "·"

# Burns per topology — the fixed schedule encoded in the firmware (oled_gfx.h burn
# block; mirrored by test_anim.py _EXPECTED_BURNS). Not re-derived here.
BURNS_BY_TOPO = {g.RG_DIRECT: 3, g.RG_FLYBY: 4, g.RG_COAST: 6}

# Column layout (code points): "  <icon> " then label | desc | role | maneuver.
# The maneuver column lands at the same offset on node and leg lines so the ✷
# markers stack. Long descriptions (the flyby junction) overflow harmlessly.
LABEL_W, DESC_W, ROLE_W = 14, 18, 7
PREFIX_W = LABEL_W + DESC_W + ROLE_W


# ── Vocabulary helpers ────────────────────────────────────────────────────────
def body_icon(body_type: int, designation: str) -> str:
    """Body glyph from (type, designation prefix): moon/trojan/vagrant by type, and
    a planet splits gas giant (KG prefix) vs rocky (LV/BG) by its designation."""
    if body_type == g.STARMAP_MOON:
        return "☾"
    if body_type == g.STARMAP_TROJAN:
        return "▲"
    if body_type == g.STARMAP_VAGRANT:
        return "✦"
    return "◉" if _prefix(designation) == "KG" else "●"


def lagrange_icon(which: int) -> str:
    """◇ for the collinear L1/L2, △ for the triangular L4/L5."""
    return "◇" if which in (g.L1, g.L2) else "△"


def type_desc(body_type: int, designation: str) -> str:
    """Plain-language body type, matching the icon's gas-giant/rocky split."""
    if body_type == g.STARMAP_MOON:
        return "moon"
    if body_type == g.STARMAP_TROJAN:
        return "trojan"
    if body_type == g.STARMAP_VAGRANT:
        return "vagrant"
    return "gas giant" if _prefix(designation) == "KG" else "rocky planet"


def topo_name(topology: int) -> str:
    """Topology label — the same vocabulary route_gen.c rg_topology_name uses."""
    return {g.RG_DIRECT: "direct transfer",
            g.RG_FLYBY:  "gravity-assist flyby",
            g.RG_COAST:  "Lagrange coast"}.get(topology, "?")


def fmt_eta(minutes: int) -> str:
    """Whole-minute ETA as "Hh MMm" — e.g. 326 → "5h26m"."""
    return f"{minutes // 60}h{minutes % 60:02d}m"


def _prefix(designation: str) -> str:
    return designation.split("-")[0].upper() if designation else ""


# ── Line builders ─────────────────────────────────────────────────────────────
# A space separates the desc and role columns so a desc that overflows DESC_W (the
# ARRIVE line, now carrying the colony tag) still reads "… colony  ARRIVE" rather
# than running together; _leg adds the matching space so the maneuver ✷ markers
# still stack between node and leg lines on the tidy (non-overflow) rows.
def _node(icon: str, label: str, desc: str, role: str, maneuver: str) -> str:
    return f"  {icon} {label:<{LABEL_W}}{desc:<{DESC_W}} {role:<{ROLE_W}}{maneuver}".rstrip()


def _leg(maneuver: str) -> str:
    return f"  {LEG_ICON} {'transfer':<{PREFIX_W}} {maneuver}".rstrip()


# ── Itinerary ─────────────────────────────────────────────────────────────────
def render(token: str) -> str:
    """The full itinerary string for a token (header + waypoint chain)."""
    r = g.generate_route(token)
    e = g.route_explain(token)
    sysm = g.build_system(token)

    org, dst = r['origin'], r['designation']
    topo, pivot = e['topology'], e['pivot']
    depart_type = sysm.bodies[sysm.depart_idx].type
    dest_type   = sysm.bodies[sysm.dest_idx].type

    header = (f"{STAR_ICON} {r['system_name']} · {topo_name(topo)} · "
              f"{fmt_eta(r['eta_minutes'])} · {BURNS_BY_TOPO[topo]} burns")
    lines = [header, ""]

    # Departure: injection burn.
    lines.append(_node(body_icon(depart_type, org), org,
                       type_desc(depart_type, org), "DEPART", f"{BURN} injection"))
    lines.append(_leg(f"{BURN} mid-course trim"))

    if topo == g.RG_FLYBY:
        which = e['flyby_lagrange']                    # 0 → L1, 1 → L2 (collinear)
        # The junction is unpowered, so its annotation floats free of the burn column
        # (nothing to stack with) — set tight after the label, not in the role slot.
        label = f"L{which + 1} of {pivot}"
        lines.append(f"  {lagrange_icon(which)} {label:<{LABEL_W}}"
                     f"flyby (gravity assist)  {UNPOWERED} unpowered")
        lines.append(_leg(f"{BURN} mid-course trim"))
    elif topo == g.RG_COAST:
        lines.append(_node(lagrange_icon(g.L4), f"L4 of {pivot}",
                           "coast insert", "", BURN))
        lines.append(f"  {COAST_ICON} coast arc · around {pivot}")
        lines.append(_node(lagrange_icon(g.L5), f"L5 of {pivot}",
                           "coast eject", "", BURN))
        lines.append(_leg(f"{BURN} mid-course trim"))

    # Arrival: capture burn; the destination carries its spectral class and colony
    # status (DOCKED vs LANDED on the device), so the CLI agrees with the panel.
    colony = "colony" if r['is_colony'] else "unpopulated"
    dest_desc = f"{type_desc(dest_type, dst)} · {r['dest_class']} · {colony}"
    lines.append(_node(body_icon(dest_type, dst), dst, dest_desc, "ARRIVE", f"{BURN} capture"))

    return "\n".join(lines)


if __name__ == "__main__":
    args = sys.argv[1:]
    if len(args) != 1:
        print("usage: route_explain.py <TOKEN>   (one token, one itinerary)", file=sys.stderr)
        raise SystemExit(2)
    print(render(args[0]))
