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

- Go to a named system — freeform designation entry (M–L) — an entry mode on a dedicated layer to type a
  token (e.g. `LV-426`) and lock it in, seeding that exact deterministic system via `route_gen_build`.
  **Design wrinkle:** the entered token only _seeds_ the world (djb2 → LCG); the displayed designation is
  _class-derived_, so typing `LV-426` won't necessarily display `LV-426`. To make "go to LV-426" actually
  show LV-426, either (a) accept it as a seed and show whatever class-designation results (simple, honest),
  or (b) search for / table a seed whose derived designation matches the input (true "go-to", more work).
  Also needs an on-OLED prompt + cursor affordance (the heavier part vs. a curated cycle).
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
