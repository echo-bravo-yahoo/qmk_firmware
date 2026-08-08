# Transit-map roadmap

Future improvements for the left-OLED star-system transit map + right-OLED telemetry.
The route already computes rich structure (typed legs, topology, flyby/coast planet, Lagrange
points, body classes) that the animation/narrative don't yet fully exploit — the cheapest wins
live there.

Constraint: the Corne OLEDs are ambiance, not a data dashboard (only gaming-vs-not matters).
Weight aesthetics / verisimilitude / narrative over utility readouts; ask before adding real-info.

## Status

- **Shipped** — phase STATUS line + big BURN warning; on-demand mission re-roll (details in the last sections).
- **Slated** — the curated set below; what we build next.
- **Backlog** — still-alive ideas, not yet slated.

**Cut** (rejected; don't revisit without new rationale): starfield void-fill, body-class glyphs, ship
wake/heading — too representational / sparse for a Weyland-Yutani terminal. The aesthetic anchor is a
MU-TH-UR-style CRT readout, not a literal space sim.

## Slated

### Aesthetics / render

- CRT scanline + phosphor flicker (S) — a faint rolling scanline + occasional brightness flicker on both
  OLEDs; reads "Nostromo terminal" with no structural change to the route engine.
- System scan-in reveal (M) — when a new system loads, rings + bodies draw on via a radial sweep instead
  of popping in. Reuses the baked background (`gfx_route_bake_bg`) plus a reveal pass.

### Verisimilitude

- Reaction-mass gauge (M) — a mass bar that drops at each burn node and resets per journey, giving burns
  consequence; seeds a future "low fuel" event. Flavor, not utility. Drives off the existing burn model.
- Hypersleep framing (S) — telemetry notes the crew in cryo + a time-compression readout, giving a
  diegetic reason journeys run hours. Cheap verisimilitude.

### Transition beats

- "WORKING…" course-plot beat (S) — at arrival, a brief `PLOTTING COURSE / WORKING…` spinner on the right
  OLED before the next mission appears, instead of an instant cut. Pairs with the arrival regen in
  `route_anim_render_map`.

### Keyboard interaction (new channel — the right-hand keys drive the sim)

The keymap already routes custom keycodes through `process_record_user`; a dedicated layer can host these
without disturbing typing. State lives on the master (`g_journey`); the slave only renders synced telemetry.

- Go to a named system — freeform designation entry (M–L) — **half-built.** The seed-is-designation half is
  done: typing `LV-426` now returns to that exact deterministic system _and_ lands on an `LV`-type world,
  every time. The old **design wrinkle** (the entered token only _seeds_ the world, so a class-derived label
  could disagree with what was typed) is resolved by inverting the contract — the designation _is_ the
  token (see "Seed-is-designation — as built" below). **Still unbuilt:** the on-OLED entry mode itself — a
  dedicated layer to type a token and lock it in, with a prompt + cursor affordance. The auto-cycler still
  drives the destination today.
- Set trip duration → journey scaled to it (S–M) — a key sets a target wall-clock journey length; the whole
  DEPART→ARRIVE timeline scales to it (ship progress, the ETA readout, and burn windows all derive from
  `duration_ms`). **Design wrinkle:** burn windows are currently _absolute_ real-time spans
  (`BURN_MAJOR_MS` = 4 min, `BURN_TRIM_MS` = 90 s) converted to a `t`-fraction via `duration_ms` — so a very
  short trip would make a 4-min burn longer than the whole journey. Scaling trip duration should switch
  burns to journey-_fraction_ sizing (or clamp). This is exactly the open question flagged when the burn
  model shipped.

## Backlog

- Live orbital drift (L) — advance planets/moons along their orbits over the multi-hour journey (periodic re-bake) so the system isn't frozen.
- Terminal approach (M) — at t→1 spiral the ship into the destination moon's drawn orbit (insertion) instead of landing on a point.
- Ship lore (S) — rotate USCSS Patna registry/cargo/crew flavor in telemetry.
- Spectral class + Hohmann-ish timing (S–M) — **destination** spectral class done (the banner's second
  line; see the last section). Remaining: give the **primary star** an emission spectral class (OBAFGKM);
  scale ETA more physically by orbit radii.
- Arrival flourish (S–M) — a short lock-on/flash beat at ARRIVED before the next journey.
- Rare anomalies (M) — occasional special systems (binary star, derelict, rogue planet) with unique glyphs/telemetry.
- Underglow tie-in (M) — RGBLIGHT is wired in config.h; hue by destination class, brightness pulse on burns.
- Also pitched, not slated — MU-TH-UR boot sequence, comms/status-log ticker, signal-degradation glitches, distress-beacon missions, occultation blink, fast-forward to next event, cycle telemetry pages (mission / manifest / log).

## Phase STATUS + big BURN warning — as built

The first roadmap item, implemented in this plan.

- **STATUS line** = the journey phase, derived from `cur_t` and the typed legs:
  `DEPART → TRANSIT → FLYBY / COAST → ARRIVE` (replaces the old `NOMINAL`/`ARRIVED`).
- **BURN warning** = a full-panel, dark-on-light, re-oriented (landscape) takeover of the right
  OLED while a burn is actually firing; lasts as long as the burn would, not a fixed few seconds.
- **Burns are physically motivated and not free:** departure injection + arrival capture (major,
  `BURN_MAJOR_MS` = 4 min), one mid-course correction (MCC) trim per transfer leg, and coast
  insertion/ejection trims at transfer↔coast boundaries (`BURN_TRIM_MS` = 90 s). A transfer↔transfer
  boundary is a gravity-assist flyby — unpowered, so **no burn** there. Per flight: **3 burns
  (direct), 4 (flyby), 6 (coast)**, avg ~4.2 with the 20/60/20 topology mix. An MCC fires
  mid-transfer, so STATUS stays `TRANSIT` while the warning overlays.
- **Windows** are real burn durations converted to a `t`-span via the route's eta; departure/arrival
  are one-sided (`[0,D]`, `[1−D,1]`), interior trims centred on their fraction. The durations are
  one-line `#define`s in `oled_gfx.h` (`BURN_MAJOR_MS` / `BURN_TRIM_MS` / `FLYBY_LABEL_MS`).
- **Code:** `gfx_route_phase()` (phase + burn from `t`, sharing the leg-walk of
  `gfx_route_ship_pos`) and `gfx_burn_warning()` (bespoke big glyphs, inverse fill, rotated 90° into
  the slave's `OLED_ROTATION_270` portrait buffer) in `oled_gfx.c`; telemetry `phase`/`burn` fields
  in `route_anim.{c,h}`; STATUS row + full-panel `oled_write_raw` takeover in `keymap.c`. Host
  mirror, burn preview (`oled_preview.py --burn`), and phase/burn tests in `preview/`.

## Re-roll mission — as built

A keypress abandons the current journey and jumps to a fresh system on demand.

- **Keycode** `RG_REROLL` (`keymap.c` custom-keycode enum), bound on **layer 3** (the media row,
  right hand). `process_record_user` runs split-key handling on the master, which owns `g_journey`.
- On press (master only) it calls `route_anim_reroll()` → `ra_start_journey()`: advance the LCG seed
  token, `route_gen_build()` the next system, `gfx_route_bake_bg()` the new background. The slave picks
  up the new route on the next telemetry sync.
- Semantically identical to the on-arrival auto-regen — re-roll is just "skip to the next mission now."

## Destination-class banner — as built

When best-fit framing + orbital rings don't fill the panel, a placard fills the wide x-gap on the
destination side: the destination **designation** + its **spectral class**. Because the framing and rings
usually fill the panel, it's an occasional accent on genuinely compact routes — suits ambiance, not every
frame (~38% of a seed sweep).

- **Principle.** After the route is framed, measure its rendered x-extent. The leftover beyond it is the
  banner budget. _Narrow leftover → a slim vertical (rotated) label that uses the tall axis; wider →
  a horizontal CRT-readout; widest → a framed 2× placard; below a floor → nothing (center as before)._
  The route shifts away from the strip (left-justified) so the journey still reads left→right with the
  placard ahead of the destination.
- **Measurement (gen-time, deterministic).** `rg_content_xspan` (`route_gen.c`) takes min/max display-x
  over every ring's reach (`center ± radius`, incl. moon `local_center` arcs), each leg's sampled path,
  and every marker / body with its pixel half-width — the leftmost→rightmost lit pixel without
  rasterizing. The extent is **true, not panel-clamped**: a ring running off the right edge is clipped
  today, but the left-justify shift would slide it back on-screen into the strip, so counting its
  off-panel reach inflates `w_img` and suppresses the banner in exactly those cases. Leftover
  `S = (128 − 2·RG_MARGIN_X) − w_img`; strip width `W_ban = S − gutter`. The shift is applied post-pack
  (`rg_shift_route_x`) — equivalent to injecting an x-offset into the two centering sites, but ordered
  after the measurement so there's no chicken-and-egg.
- **Tiers** (by strip width `W_ban`; tunable `#define`s in `route_gen.c` / `oled_gfx.c`):

    | `W_ban` (px) | Banner                                                                                 |
    | ------------ | -------------------------------------------------------------------------------------- |
    | `< 12`       | none — center the route (today's behavior)                                             |
    | `12–29`      | vertical rotated designation, 1× (reads bottom→top); + class as a 2nd column if `≥ 24` |
    | `30–55`      | horizontal 2-line readout: designation / class, in `[ ]` brackets (CRT-readout feel)   |
    | `≥ 56`       | big: designation 2× + class 1× subline, thin frame (BURN-style)                        |

- **Spectral class.** The destination is a sub-stellar body, classified by _reflectance_ spectra (not the
  stellar OBAFGKM _emission_ scheme). `starmap_spectral_class` (`starmap_world.c`) draws from the real
  taxonomy per body type — a pure `(seed, idx)` hash, **no** RNG-stream draws, so topology/ETA rolls and
  host==device stay untouched:

    | Body             | Scheme                            | Pool / format                                        |
    | ---------------- | --------------------------------- | ---------------------------------------------------- |
    | moon             | asteroid/KBO reflectance          | `{C,D,P,S}` + digit (captured / icy small bodies)    |
    | gas-giant planet | Sudarsky (2000) I–V               | bare roman, by temperature / cloud chemistry         |
    | rocky planet     | invented silicate/metal extension | `{S,Q,V,M,K}` + digit (terrestrials lack a real one) |
    | trojan           | asteroid reflectance, D-heavy     | `{D,P,X,C,S,V}` + digit, weighted ~80% D-type        |
    | vagrant          | asteroid reflectance, D-heavy     | `{D,P,C,X,S,V}` + digit, D-weighted                  |

    Format mimics the stellar `G2V` shape (≤3 chars): letter classes read `<L><digit>` (`D4`), gas giants
    the bare roman (`III`). The rocky pool is the one non-canonical extension — terrestrials have no real
    reflectance class — in the spirit of the invented `RF` minor-body catalog.
    Sources: [Asteroid spectral types](https://en.wikipedia.org/wiki/Asteroid_spectral_types),
    [Sudarsky's gas giant classification](https://en.wikipedia.org/wiki/Sudarsky's_gas_giant_classification),
    trojan D-type fractions per the Dark Energy Survey photometry ([arXiv:2211.10719](https://arxiv.org/pdf/2211.10719)).

- **Code:** `starmap_spectral_class` (`starmap_world.c`); `rg_content_xspan` + `rg_shift_route_x` + the
  `dest_class`/`banner_x`/`banner_w` pack (`route_gen.c`); `gfx_prim_text` / `gfx_prim_text_vertical` +
  `gfx_route_draw_banner`, baked at the end of `gfx_route_draw_bg` so `route_anim.c`'s bake site is
  unchanged (`oled_gfx.c`); the Tom Thumb glyph table shared via `gfx_tomthumb_font()`
  (`glcdfont_tomthumb.c`). Host struct mirror + banner/class tests in `preview/`.

## Life-viable designation — as built

The survey designation (`LV`/`KG`/`BG`/`RF`) is now a pure downstream label of the destination's
**intrinsic properties** — orbital role, composition, and a derived **life-viability** — rather than a
switch on body type. `LV` means **Life-Viable**, not "moon": it spans moons _and_ rocky planets
(canon-faithful — LV-426 is a moon, LV-178/895 are planets), so a habitable moon of a band gas giant
reads `LV` (the LV-426/Calpamos shape).

- **Predicate.** `sw_is_life_viable` (`starmap_world.c`): major rocky body (not a trojan/vagrant, not a
  gas giant) × orbital radius in the habitable band `[SW_HZ_MIN, SW_HZ_MAX]` = `[34, 82]` wu × a
  deterministic `(seed, idx)` viability roll. A moon inherits its parent planet's heliocentric distance.
  Derived, **not stored** — mirrors `sw_is_gas_giant`, so no `starmap_body_t` field and the ctypes mirror
  stays stable; pure `(seed, idx)` hash with no RNG-stream draws, so topology/ETA rolls and host==device
  are untouched.
- **Decision order** in `starmap_designation` (now takes the system, to look up a moon's parent): minor
  body → `RF`; gas giant → `KG`; life-viable → `LV`; else rocky-non-viable → `BG`. **Trojans and gas
  giants are excluded from `LV`** — a trojan must be ≪ its host for L4/L5 stability (so it's asteroidal,
  airless), and a gas giant has no surface. The viability roll leaves some in-band worlds barren (`BG`) so
  `LV` stays meaningful. Seed sweep: ~16% `LV`, ~32% `KG`, ~32% `BG`, ~20% `RF`.
- **Spectral class is unchanged** — still keyed on physical type (reflectance taxonomy), independent of
  the designation. A life-viable `LV` world still shows its surface reflectance class (e.g. `S3`), by
  decision.
- Full enumeration + rationale + canon/science sources:
  [`.claude/docs/world-classification.md`](world-classification.md).
- **Code:** `sw_is_life_viable` + `SW_HZ_*`/`SW_SALT_VIABLE` and the rewritten `starmap_designation`
  (`starmap_world.c` / `.h`); property-aware designation tests in `preview/test_route.py`.

## Frame-aware path-finding — as built

A trip from a planet to its **own moon** is now planned in that planet's **local frame** instead of the
heliocentric one, so the route stays at the moon's orbital scale (~a few wu around the host planet) and the
best-fit framing zooms into a clean local view. This fixes the old illegible loop where the ship flew out to
a planet's star-relative L4, coasted 120° around the star to L5, and returned to the moon (~336 wu to reach a
~6 wu destination — the BG-794 / LV-100 case). Interplanetary trips keep their heliocentric frame.

- **One classifier, two frames.** `rg_plan` (`route_gen.c`) checks whether the endpoints are a planet and
  its own moon — the **only** intra-subsystem pair, since moons are the only non-star body and there is ≤1
  per planet. If so it routes in the host planet's frame: the pivot whose Lagrange points the flyby/coast
  threads is the **moon**, the frame center the route bows around is the **host planet**, and the coast is
  centered there at the moon's orbital radius. Otherwise it routes heliocentrically (pivot = a mid-planet,
  center = the star). Full trip→frame taxonomy, and why planet↔own-moon is the only local case:
  [`route-frames.md`](route-frames.md).
- **Parent-relative Lagrange points.** `starmap_lagrange_pos` (`starmap_world.c`) now centers a body's
  L-points on its **parent** — a planet's parent is the star (origin → heliocentric, a no-op vs. before), a
  moon's parent is its planet (→ planet-local, moon-scale). One function serves both frames.
- **The coast carries its own center.** `rg_leg_t` gained a `center` field (`route_gen.c`); the coast arc is
  measured and packed about it instead of being hardcoded to the star, so it can express a small
  planet-local arc. A heliocentric coast has `center = (0,0)`, so its packed geometry is byte-identical to
  before. Transfers bow about the frame center too (`rg_transfer_control` takes it), so a local hop curves
  around the planet, not the distant star.
- **Sensible heliocentric intermediary.** The heliocentric mid-planet is picked from the interior planets
  **excluding** the depart/dest planets and biased to orbit **between** them (`rg_pick_mid_planet`), so an
  interplanetary coast/flyby never threads an endpoint's own orbit ring — the heliocentric twin of the
  planet↔moon loop.
- **Code:** parent-relative `starmap_lagrange_pos` (`starmap_world.c` / `.h`); frame classification, coast
  `center`, in-frame `rg_transfer_control`, and `rg_pick_mid_planet` (`route_gen.c`); local-route
  world-scale invariants + the LV-100 ETA regression in `preview/test_route.py`. No `gfx_route_t` /
  ctypes-mirror change (only internal `rg_leg_t` / `rg_plan_t` fields and a same-signature
  `starmap_lagrange_pos`).

## Seed-is-designation — as built

The seed token now **is** the destination. A conforming `{PREFIX}-{digits}` token (PREFIX ∈
`LV`/`BG`/`KG`/`RF`, case-insensitive) is echoed verbatim as the displayed designation, and its prefix pins
the destination body's **type**, so typing `LV-426` returns to the same deterministic system every time
_and_ lands on an `LV`-type world. This inverts the previous contract (token seeds the world; label derived
from the destination's class), resolving the "go to a named system" design wrinkle — the displayed
designation can't disagree with what was typed, because it _is_ what was typed.

- **Parser.** `sw_parse_token` (`starmap_world.c`) matches `{LV,BG,KG,RF}-{1..4 digits}`, normalizes to
  upper, and copies the serial verbatim (never interpreted numerically). The world is seeded from the
  canonical text, so `lv-426` and `LV-426` bookmark the same system. A malformed token falls back to the
  legacy derived-designation path, unchanged.
- **Generate-then-pin.** The tested world generator runs untouched; `sw_pin_destination`
  (`starmap_world.c`) then selects a destination body of the prefix's type — `RF` → trojan/vagrant, `KG` →
  gas-giant planet, `LV`/`BG` → moon or rocky planet — or constructs one if the world lacks it (append a
  vagrant or a moon, or promote the outermost planet to a guaranteed gas giant). The **departure** becomes
  the random-distinct endpoint (the destination is now fixed), consistent with the local-frame feature's
  "depart stays random." Rock-vs-moon variety for `LV`/`BG` falls out of the candidate pool.
- **`LV` vs `BG` is cosmetic** — both pin the same body pool and the type-keyed spectral class can't tell
  them apart, so the habitable-band / viability model (`sw_is_life_viable`) is retired to the
  malformed-token fallback only. Full contract: [`world-classification.md`](world-classification.md).
- **Auto-cycler emits valid tokens.** `ra_next_seed_token` (`route_anim.c`) now formats a weighted
  `{PREFIX}-{serial}` from the advanced LCG (16/32/32/20 split — `LV` the rare jackpot, `RF` the minority;
  serial bands match the catalog), so the endless run still produces well-formed designations.
- **Out of scope:** the on-OLED freeform _entry UI_ (typing a token live on the keyboard) — the other half
  of the "go to a named system" roadmap item.
- **Code:** `sw_parse_token` / `sw_pin_destination` / rewritten `starmap_build` and retired-to-fallback
  `starmap_designation` (`starmap_world.c` / `.h`); token-emitting `ra_next_seed_token` (`route_anim.c`);
  inverted designation invariants + multi-prefix sweep + discovered planet↔moon anchor + malformed-fallback
  / body-budget tests (`preview/test_route.py`). No `gfx_route_t` / `starmap_system_t` field added
  (construction appends into the already-mirrored `bodies[16]`), so the ctypes `sizeof` asserts are
  untouched.
