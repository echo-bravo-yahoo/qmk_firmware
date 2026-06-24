# Star-system transit map — generator & preview

The left OLED (128×32) renders a procedurally generated star system and animates the **USCSS Patna**
along a route to a destination body. On arrival a new destination is chosen and the cycle repeats,
endlessly. This directory hosts the host-side tooling that drives the same firmware C code through a
ctypes bridge — the single source of truth for both the device and the preview.

## World model — class designations

A journey is the Patna transiting **within a proper-named system** (a gas giant / star) to a
destination body. The destination's **survey designation is a label derived from that body's intrinsic
properties** — orbital role, composition, and a derived life-viability — not supplied as input. `LV`
means **Life-Viable**: a habitability class that spans moons _and_ rocky planets (canon: LV-426 is a
moon, LV-178/895 are planets), orthogonal to orbital role; minor bodies and gas giants are excluded:

| Prefix | Assigned when (properties)                                | Meaning              | Serial range | Canon anchor |
| ------ | --------------------------------------------------------- | -------------------- | ------------ | ------------ |
| `LV`   | life-viable world — moon **or** rocky planet, in the band | **L**ife-**V**iable  | 100–1299     | LV-426       |
| `KG`   | gas giant (by composition)                                | Jovian survey        | 100–999      | KG-348       |
| `BG`   | rocky world, **not** life-viable (out of band / barren)   | colony / barren rock | 100–999      | BG-386       |
| `RF`   | minor body (trojan / vagrant)                             | minor-body catalog   | 1000–9999    | none         |

Life-viability, gas-giant class, and serial are all **derived** (not stored): a `(seed, idx)` hash plus
the body's orbital radius (a moon inherits its parent planet's) decides the class, so no
`starmap_body_t` field is needed and the ctypes mirror stays stable. A deterministic viability roll
leaves some in-band worlds barren (`BG`) so `LV` stays meaningful. The serial is a hash of
`(seed, dest_idx)` into the per-prefix range; every form is ≤ 7 chars, fitting `designation[8]` and the
8-col OLED. `RF` is invented (canon only fixes LV/KG/BG), kept short and lore-plausible.

**Full rationale + sources:** [`.claude/docs/world-classification.md`](../.claude/docs/world-classification.md).

Each journey names a system via a syllable grammar (CALPAMOS, NIMBULON, SORVAT…). The **input token is
purely a seed** (djb2 → LCG): the same token always yields the same bodies, Lagrange geometry, name,
and class designation — it is no longer the displayed label. Telemetry (system name, designation, ETA,
journey phase) is data-driven from the active route, not hardcoded.

## Pipeline

One generate→plan→fit→render→animate path, one source of truth:

| Stage            | Module                        | Output                                                                              |
| ---------------- | ----------------------------- | ----------------------------------------------------------------------------------- |
| System           | `starmap_world.c`             | deterministic bodies + Lagrange points + name + class designation from a seed token |
| Route plan       | `route_gen.c`                 | waypoint chain through real placements, typed legs                                  |
| Viewport fit     | `route_gen.c`                 | rotate to best-fit the visited path into the wide panel (dest oriented rightward)   |
| Pack             | `route_gen.c`                 | display-space geometry + telemetry → `gfx_route_t`                                  |
| Render / animate | `oled_gfx.c` / `route_anim.c` | bake background once, animate the ship as a real-time progress bar                  |

### Placements, not tweens

Waypoints are **real orbital locations**, so the path visibly pivots instead of tweening between two
spots. Lagrange points are stylized but geometrically faithful (masses are fictional): L4/L5
lead/trail the planet by **±60°** on its orbit; L1/L2/L3 are **collinear** on the star–planet axis
(L1/L2 a fixed radial fraction inside/outside, L3 diametrically opposite).

Topology is chosen per system (when ≥3 planets):

- **Direct transfer** — departure orbit → destination orbit via one tangent transfer arc.
- **Gravity-assist flyby** — thread an intermediate planet's L1/L2.
- **Lagrange coast** — ride a planet's orbit between L4 and L5.

### Leg types

- **transfer** — a quadratic Bézier whose control point is the intersection of the orbital tangent
  lines at both endpoints (so it leaves/arrives tangent to the orbits), with the bow bounded so
  star-straddling endpoints don't blow up the frame.
- **coast** — a true circular-orbit arc segment between two angular positions on one orbit.

The ship traverses **all** legs by cumulative arc length. While it crawls (journeys are hours), the
crosshair pulses from a fast clock independent of the journey, keeping the 20 Hz panel alive.

### Framing

The bbox fit bounds **only the bodies the route visits** (departure, destination, any flyby/coast
planet) plus the connecting path — not the star, not unrelated planets. Fitting every planet let one
scattered at a steep angle add huge vertical extent and collapse the scale into a heavy zoom-out;
fitting just the journey frames it. Short hops therefore fill the panel (two close bodies, few rings
visible) and bodies the route doesn't touch clip or cull as decoration — that's the route the journey
actually takes. The fit margin is 3 px so the destination reticle (±3 px) always lands whole.

## `gfx_route_t` layout

The wire format between generation and rendering (display pixels, origin top-left). See `oled_gfx.h`;
the ctypes mirror is in `oled_gfx_lib.py` and the struct size is asserted against the C `sizeof` at
load. Key fields:

| Field                               | Meaning                                                         |
| ----------------------------------- | --------------------------------------------------------------- |
| `arc_cx, arc_cy`                    | shared heliocentric center (the star)                           |
| `arcs[8]`                           | orbital rings (radius, dashed/solid, optional local center)     |
| `legs[4]`                           | typed legs: transfer (Bézier) or coast (center/radius/a0/sweep) |
| `markers[4]`                        | departure body, destination reticle, star ring                  |
| `bodies[6]`                         | planets + moons/trojans/vagrant, off-panel ones culled          |
| `eta_minutes`                       | nominal journey duration (telemetry)                            |
| `system_name[12]`, `designation[8]` | data-driven telemetry strings                                   |

## Tooling

The bridge auto-rebuilds `oled_gfx.so` from `oled_gfx.c` + `route_gen.c` + `starmap_world.c` (with
`-DRG_HOST`) whenever a source is newer, so every tool exercises the real firmware code.

| Command                                          | Purpose                                                  |
| ------------------------------------------------ | -------------------------------------------------------- |
| `python3 route_samples.py [LV-NNN ...]`          | contact sheet of generated systems → `route_samples.png` |
| `python3 oled_preview.py --desig LV-426 --t 0.5` | one journey, both OLEDs (`--burn` → BURN takeover)       |
| `python3 anim.py --desigs LV-426 LV-223`         | looping journey GIF + frame strip                        |
| `python3 route_dump.py LV-426`                   | text dump: bodies, Lagrange points, leg types, framing   |

## Tests

```bash
uv run --with pytest --with pillow pytest -q
```

Covers determinism (token → identical struct), in-bounds geometry, route endpoints landing on the
departure/destination bodies, property-derived designations (`LV` only on life-viable moons/rocky
planets, never on minor bodies; gas-giant→`KG`; rocky-non-viable→`BG`; trojan/vagrant→`RF`; all four
prefixes appearing across a sweep), Lagrange geometry (±60° / collinear), ring connectivity (regression
for the old broken-ring rasterizer), and even dash coverage.

It also drives the journey state machine (boot picks a fresh route, clock-tracked progress, arrival →
next route) and the phase/burn model: the STATUS phase sequence (`DEPART → TRANSIT → FLYBY/COAST →
ARRIVE`), the burn count per topology (direct 3 / flyby 4 / coast 6, with the flyby junction
unpowered), and the burn-window lengths (~4 min departure/arrival, ~90 s trims).

## Host build note

`oled_gfx.h` includes `QMK_KEYBOARD_H`; the host build points it at `oled_gfx_stub.h` (a no-op
`oled_write_pixel`, since baking routes all output through the page buffer). `route_gen_describe` and
its `printf` are guarded behind `RG_HOST`, so they never link into the firmware.
