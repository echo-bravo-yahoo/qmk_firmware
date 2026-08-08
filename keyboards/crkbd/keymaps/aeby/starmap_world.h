/*
 * starmap_world — fictional star-system model for the Corne transit map.
 *
 * A "system" is a proper-named gas giant / star (e.g. Calpamos) with a handful
 * of planets, moons, trojans and the occasional vagrant. A journey transits the
 * USCSS Patna within that system to a destination body.
 *
 * The seed token IS the designation. A conforming "{PREFIX}-{digits}" token
 * (PREFIX ∈ LV/BG/KG/RF, case-insensitive) seeds the world AND is echoed verbatim
 * as the displayed designation, while its prefix pins the destination's body
 * type: LV/BG → moon or rocky planet, KG → gas giant, RF → trojan/vagrant. So
 * typing "LV-426" returns to the same system every time and lands on an LV-type
 * world. A malformed token falls back to the legacy path — random endpoints with
 * a class label derived from the destination's properties (see starmap_designation,
 * preview/README.md, and .claude/docs/world-classification.md).
 *
 * The token seeds everything deterministically (djb2): the same token always
 * yields the same bodies, the same Lagrange geometry, and the same proper name
 * ("lv-426" and "LV-426" bookmark the same world, since the canonical text is
 * hashed). This is the single source of truth for the system — both
 * route_gen_build() and route_gen_describe() consume starmap_build().
 *
 * Masses are fictional, so Lagrange points are stylized but geometrically
 * faithful: L4/L5 lead/trail the body by ±60° on its orbit; L1/L2/L3 are
 * collinear with the parent–body axis (L1/L2 a fixed radial fraction inside /
 * outside the body, L3 diametrically opposite). The star sits at the world origin
 * (0,0); Lagrange points are taken about each body's parent, so a planet's are
 * heliocentric and a moon's are planet-local (see starmap_lagrange_pos).
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define STARMAP_MAX_BODIES   16
#define STARMAP_MAX_PLANETS   6
#define STARMAP_NAME_LEN     12  /* proper name, NUL-terminated   */
#define STARMAP_DESIG_LEN     8  /* "LV-426" designation, NUL-terminated */
#define STARMAP_CLASS_LEN     4  /* spectral class, e.g. "D4" / "III", NUL-terminated */

/* Body kinds. */
enum {
    STARMAP_PLANET  = 0,
    STARMAP_MOON    = 1,
    STARMAP_TROJAN  = 2,
    STARMAP_VAGRANT = 3,
};

/* Lagrange point selector (see header comment for the convention). */
typedef enum {
    STARMAP_L1 = 0,  /* collinear, just inside the planet  */
    STARMAP_L2,      /* collinear, just outside the planet */
    STARMAP_L3,      /* collinear, opposite the planet     */
    STARMAP_L4,      /* +60° leading  on the orbit         */
    STARMAP_L5,      /* -60° trailing on the orbit         */
} starmap_lagrange_t;

typedef struct {
    uint8_t type;        /* STARMAP_PLANET / MOON / TROJAN / VAGRANT */
    uint8_t parent_idx;  /* index of body it orbits; 0xFF = orbits the star */
    float   orbital_radius;
    float   angle;       /* radians, measured at the parent */
} starmap_body_t;

typedef struct {
    starmap_body_t bodies[STARMAP_MAX_BODIES];
    uint8_t        body_count;
    uint8_t        planet_idx[STARMAP_MAX_PLANETS]; /* ascending orbital radius */
    uint8_t        planet_count;
    uint8_t        depart_idx;   /* body index the journey departs from */
    uint8_t        dest_idx;     /* body index of the destination */
    char           name[STARMAP_NAME_LEN];      /* proper system name, e.g. "CALPAMOS" */
    char           designation[STARMAP_DESIG_LEN]; /* class-derived, e.g. "LV-426" / "KG-348" */
    uint32_t       seed;
} starmap_system_t;

/* Shared deterministic PRNG (Numerical-Recipes LCG). Seeded from a djb2 hash so
 * the whole pipeline — system, naming, route topology — is reproducible. */
typedef struct { uint32_t state; } starmap_rng_t;

static inline uint32_t starmap_rng_next(starmap_rng_t *r) {
    r->state = r->state * 1664525u + 1013904223u;
    return r->state;
}
/* Uniform integer in [lo, hi]. */
static inline int32_t starmap_rng_range(starmap_rng_t *r, int32_t lo, int32_t hi) {
    if (hi <= lo) return lo;
    return lo + (int32_t)(starmap_rng_next(r) % (uint32_t)(hi - lo + 1));
}
/* Uniform float in [0, 1). */
static inline float starmap_rng_float(starmap_rng_t *r) {
    /* 1UL << 24: on AVR `int` is 16-bit, so a plain `1 << 24` is UB
     * (shift >= width); the unsigned-long literal makes 2^24 well-defined. */
    return (float)(starmap_rng_next(r) >> 8) / (float)(1UL << 24);
}

/* djb2 over the seed token — the deterministic seed. */
uint32_t starmap_seed(const char *designation);

/* Build the full deterministic system for a seed token. For a conforming
 * "{PREFIX}-{digits}" token (PREFIX ∈ LV/BG/KG/RF, case-insensitive) the displayed
 * designation IS the token, echoed verbatim (uppercased), and the prefix pins the
 * destination body's type — LV/BG → moon or rocky planet, KG → gas giant, RF →
 * trojan/vagrant — by selecting a matching body or constructing one. A malformed
 * token seeds from its raw string and derives the designation instead (see
 * starmap_designation). */
void starmap_build(const char *designation, starmap_system_t *out);

/* Survey designation for a MALFORMED token only — for a conforming token the
 * designation is the token verbatim (see starmap_build). Derives a class label
 * from the randomly-chosen destination's intrinsic properties — orbital role,
 * composition, and a derived life-viability (canon-grounded):
 *   life-viable world → "LV-NNN"  (moon OR rocky planet in the habitable band;
 *                                  serial 100–1299; LV-426 moon, LV-178/895 planets)
 *   gas-giant planet  → "KG-NNN"  (Jovian;            serial 100–999; KG-348)
 *   rocky, not viable → "BG-NNN"  (barren / colony;   serial 100–999; BG-386)
 *   trojan / vagrant  → "RF-NNNN" (minor body;        serial 1000–9999, invented)
 * LV spans moons and rocky planets (it is a habitability class, orthogonal to
 * orbital role); minor bodies and gas giants are excluded from LV. Decision order:
 * minor body → gas giant → life-viable → else rocky-non-viable. Needs the system
 * (not just the body) so a moon's life-viability can read its parent planet's
 * heliocentric distance. Serial is a deterministic hash of (seed, idx); gas-giant
 * and life-viable classes are derived (no starmap_body_t field, so the ctypes
 * mirror stays stable). See .claude/docs/world-classification.md for the full
 * rationale. out must hold STARMAP_DESIG_LEN bytes. */
void starmap_designation(uint32_t seed, const starmap_system_t *sys, int idx,
                         char out[STARMAP_DESIG_LEN]);

/* Colony status for the destination — true ⇒ DOCKED (a settled world / orbital
 * dock), false ⇒ LANDED (touched down on an unpopulated body). A gas giant is
 * always a colony; every other body is a colony 70% of the time, decided by a
 * salted (seed,idx) hash so the same token reports the same status on host and
 * device. Derived (not stored), like starmap_designation — keeps the ctypes mirror
 * stable. See route_anim (DOCKED/LANDED) and route_explain (colony/unpopulated). */
bool starmap_is_colony(uint32_t seed, const starmap_system_t *sys, int idx);

/* Destination spectral class — the banner's second line. Sub-stellar bodies are
 * classified by *reflectance* spectra (not stellar OBAFGKM emission), so the class
 * set per body type comes from the real reflectance taxonomies:
 *   moon            → {C,D,P,S}      asteroid/KBO reflectance pool (captured/icy)
 *   gas-giant plan. → Sudarsky I–V   (roman; temperature/cloud chemistry)
 *   rocky planet    → {S,Q,V,M,K}    silicate/metal end (lore-plausible extension)
 *   trojan          → D-heavy pool   (Jupiter trojans are ~80% D-type)
 *   vagrant         → D-heavy pool   (outer minor body)
 * Format mimics the stellar "G2V" shape, ≤3 chars: letter classes read "<L><digit>"
 * (e.g. "D4"); gas giants read the bare roman ("III"). Pure seed-hash like
 * starmap_designation — no RNG-stream draws, so topology/ETA rolls and host==device
 * are untouched. out must hold STARMAP_CLASS_LEN bytes. */
void starmap_spectral_class(uint32_t seed, const starmap_body_t *body, int idx,
                            char out[STARMAP_CLASS_LEN]);

/* World position of a body (recursively adds the parent's position). */
void starmap_world_pos(const starmap_system_t *sys, int idx, float *x, float *y);

/* World position of an orbiting body's Lagrange point, taken about its parent.
 * The body's orbital_radius/angle are parent-relative, so the L-points are
 * centered on the parent's world position: a planet's parent is the star
 * (origin → heliocentric L-points), a moon's parent is its planet (→ planet-local
 * L-points at the moon's orbital scale). planet_body_idx names the body whose
 * L-points are wanted (any orbiting body, not only a planet). */
void starmap_lagrange_pos(const starmap_system_t *sys, int planet_body_idx,
                          starmap_lagrange_t which, float *x, float *y);
