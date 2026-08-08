# Route frames — which gravitational frame each trip is planned in

The path-finder plans a route between two bodies. A trip's **frame** is the gravitational system it stays
within, which decides whether its Lagrange waypoints are taken relative to the **star** (heliocentric) or
to a **planet** (planet-local). The frame is fixed entirely by the orbit hierarchy (`parent_idx`).

## Orbit hierarchy

- **Planet** — orbits the star (`parent_idx = 0xFF`).
- **Moon** — orbits a planet (`parent_idx` = that planet). **The only body that does not orbit the star.** ≤1 per planet, so no sibling moons.
- **Trojan** — orbits the **star** (`parent_idx = 0xFF`) at a host planet's radius, ±60° (it sits at the host's L4/L5 but is itself star-orbiting).
- **Vagrant** — orbits the star (`parent_idx = 0xFF`), beyond the outermost planet.

Only a **moon** lives in a planet's local frame; planets, trojans, and vagrants are all heliocentric.

## Trip → frame

A trip is **planet-local** iff both endpoints share one planet's subsystem. Because moons are the only
non-star body and there is ≤1 per planet, that is exactly **a planet and its own moon**. Everything else
is **heliocentric**.

| Trip                                                 | Frame            | Why                                                                                     |
| ---------------------------------------------------- | ---------------- | --------------------------------------------------------------------------------------- |
| planet ↔ **its own** moon                            | **planet-local** | moon orbits this planet; waypoints = moon's planet-relative L-points (moon-orbit scale) |
| planet ↔ a **different** planet's moon               | heliocentric     | different parents; route to the moon's world position (~6 wu from its planet)           |
| planet ↔ planet                                      | heliocentric     | both orbit the star                                                                     |
| planet ↔ trojan                                      | heliocentric     | the trojan orbits the star (co-orbital at L4/L5)                                        |
| planet ↔ vagrant                                     | heliocentric     | the vagrant orbits the star                                                             |
| moon ↔ moon (different planets)                      | heliocentric     | different parents                                                                       |
| moon ↔ trojan, moon ↔ vagrant                        | heliocentric     | different parents; the moon is ~at its planet                                           |
| trojan ↔ trojan, trojan ↔ vagrant, vagrant ↔ vagrant | heliocentric     | all orbit the star                                                                      |

## Consequences for path-finding

- **Heliocentric** trips use a mid-planet's **star-relative** Lagrange points for flyby/coast (or a direct
  transfer); the coast is centered on the star.
- **Planet-local** trips use the **moon's planet-relative** Lagrange points; the coast is centered on the
  host planet, radius = the moon's orbital radius. The route stays at moon scale and the framing zooms in.
- `starmap_lagrange_pos` is parent-relative, so one function serves both: a planet's parent is the star
  (origin → heliocentric); a moon's parent is its planet (→ planet-local).

See `transit-map-roadmap.md` (as-built path-finding) and `world-classification.md` (designation/spectral taxonomy).
