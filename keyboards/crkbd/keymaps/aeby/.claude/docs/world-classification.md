# World classification — survey designations & spectral classes

Reference for how the transit-map generator labels a destination body: the **survey designation** (the
`LV-426`-style prefix shown in telemetry / the banner) and its **spectral class** (the banner's second
line).

**The seed token IS the survey designation.** A conforming `{PREFIX}-{digits}` token (PREFIX ∈
`LV`/`BG`/`KG`/`RF`, case-insensitive) is echoed verbatim as the designation, and its prefix **pins the
destination's body type** — the generator selects a matching body or, if the world lacks one, constructs
it. So typing `LV-426` returns to the same system every time and lands on an `LV`-type world. (A malformed
token falls back to the legacy _derived_ designation — see the fallback note below.) The **spectral class**
stays property-derived and is never stored on `starmap_body_t` (so the ctypes mirror stays stable); its
selection is pure `(seed, idx)` hashing — no RNG-stream draws, so topology/ETA rolls are untouched and
host == device.

## Survey designations

The franchise never published a systematic prefix taxonomy — Cameron has said `LV` is officially
_meaningless_, and the canonical prefixes each appear on only a handful of bodies (`KG`/`BG` once each).
This generator makes a deliberate, canon-compatible choice: **the prefix pins the destination's _type_**,
and the typed token is the designation (not a survey-catalog code as real astronomy uses — HD/NGC/Gliese
name the catalog, not the body). The serial column is the band the auto-cycler emits (a typed token may
carry any 1–4-digit serial).

| Prefix | Destination type pinned              | Meaning              | Serial    | Canon grounding                                                                                                                               |
| ------ | ------------------------------------ | -------------------- | --------- | --------------------------------------------------------------------------------------------------------------------------------------------- |
| `LV`   | moon **or** rocky planet             | Life-Viable world    | 100–1299  | LV-426 (moon, _Aliens_); LV-178 / LV-895 (planets, novel / game). "Life Viable" is the adopted fan reading — Cameron: officially meaningless. |
| `KG`   | gas giant                            | Jovian / gas giant   | 100–999   | KG-348, the gas giant Sevastopol orbits (_Alien: Isolation_).                                                                                 |
| `BG`   | moon **or** rocky planet (LV's pool) | colony / barren rock | 100–999   | BG-386, Freya's Prospect colony (_Alien: Isolation_).                                                                                         |
| `RF`   | trojan or vagrant                    | minor-body catalog   | 1000–9999 | none — invented; canon has no minor-body prefix.                                                                                              |

**Why each type:**

- **`LV`/`BG` → moon or rocky planet** — faithful to canon (LV-426 is a moon; LV-178/895 are planets) and
  to "Life Viable" being a _habitability_ property, orthogonal to orbital role. A `LV` moon of a gas giant
  is the LV-426/Calpamos shape. Rock-vs-moon variety falls out of the candidate pool for free.
- **`LV`/`BG` exclude minor bodies (those are `RF`).** Atmosphere retention needs ~Mars-mass+, but L4/L5
  stability requires a trojan be ≪ its host; real trojans are all small airless asteroids and a habitable
  trojan is speculative/unconfirmed. This model's trojans/vagrants are asteroidal minor bodies.
- **`KG` → gas giant**, matching KG-348. Gas-giant-ness is derived from orbital radius + a hash
  (`sw_is_gas_giant`); when the world has no gas giant, the outermost planet is promoted to one.

**`LV` vs `BG` is a label distinction only** — both pin the same body types, and the spectral class
(type-keyed) is identical, so the two are physically indistinguishable on the panel. The habitable-band /
viability model (`sw_is_life_viable`: _major rocky body × in the band `[SW_HZ_MIN, SW_HZ_MAX]` wu × a
deterministic viability roll_, a moon inheriting its parent planet's heliocentric distance) now applies
**solely to the malformed-token fallback** (`starmap_designation`), where it splits the randomly-chosen
destination into `LV` (viable) vs `BG` (barren) so a non-conforming seed still reads as a plausible class.

## Spectral classes

Stars are classified by _emission_ (OBAFGKM); these destinations are sub-stellar bodies, classified by
_reflectance_ spectra. The pool per body type is the real taxonomy that applies, keyed on physical
properties (orbital role + composition) — **independent of the `LV`/`BG`/`KG`/`RF` label**.

| Body (property)  | Scheme                            | Pool / format                     | Why                                                                                                    |
| ---------------- | --------------------------------- | --------------------------------- | ------------------------------------------------------------------------------------------------------ |
| moon             | asteroid / KBO reflectance        | `{C,D,P,S}` + digit               | captured / icy small bodies share the asteroid/KBO taxonomy                                            |
| gas-giant planet | Sudarsky (2000)                   | bare roman `I`–`V`                | albedo / cloud-chemistry classes by temperature                                                        |
| rocky planet     | invented silicate/metal extension | `{S,Q,V,M,K}` + digit             | terrestrials have no real reflectance class; extend the silicate/metal end (lore-plausible, like `RF`) |
| trojan           | asteroid reflectance, D-heavy     | `{D,P,X,C,S,V}` + digit, ~80% `D` | Jupiter trojans are ~80% D-type                                                                        |
| vagrant          | asteroid reflectance, D-heavy     | `{D,P,C,X,S,V}` + digit           | outer minor body, D-heavy                                                                              |

Format mimics the stellar `G2V` shape (≤3 chars): letter classes `<L><digit>` (`D4`); gas giants the bare
roman (`III`). The digit is a survey serial (flavor — real subtypes use letters like `Cg`/`Ch`, not
digits). Selection is a pure `(seed, idx)` hash (`SW_SALT_CLASS`).

**Coherence note (deliberate):** a life-viable `LV` world still shows its _surface_ reflectance class
(a rocky `LV` planet reads e.g. `S3`), not a habitability class — kept simple by decision. The whole set
is unified by being _reflectance_ (how the body reflects starlight) rather than emission — Sudarsky
included (his 2000 paper is literally "albedo and reflection spectra").

## Sources

- Asteroid spectral types (Tholen 1984; Bus-DeMeo 2009) — <https://en.wikipedia.org/wiki/Asteroid_spectral_types>
- Sudarsky's gas-giant classification — <https://en.wikipedia.org/wiki/Sudarsky's_gas_giant_classification>
- Trojan D-type fractions (Dark Energy Survey photometry) — <https://arxiv.org/pdf/2211.10719>
- `LV` meaning (Cameron "means nothing") — <https://forum.frialigan.se/viewtopic.php?t=7734>
- KG-348 / BG-386 (_Alien: Isolation_) — <https://avp.fandom.com/wiki/KG-348>, <https://avp.fandom.com/wiki/Colony>
- LV-178 (_Out of the Shadows_) — <https://en.wikipedia.org/wiki/Alien:_Out_of_the_Shadows>; LV-895 (_Fireteam Elite_) — <https://avp.fandom.com/wiki/LV-895>
