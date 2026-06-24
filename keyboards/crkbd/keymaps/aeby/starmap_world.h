/*
 * starmap_world — fictional star-system model for the Corne transit map.
 *
 * A "system" is a proper-named gas giant / star (e.g. Calpamos) with a handful
 * of planets, moons, trojans and the occasional vagrant. A journey transits the
 * USCSS Patna within that system to a destination body whose survey designation
 * is derived from its intrinsic properties — orbital role, composition and a
 * derived life-viability: a life-viable world (moon OR rocky planet in the
 * habitable band) reads "LV-NNN", a gas giant "KG-NNN", a rocky-but-not-viable
 * world "BG-NNN", a minor body (trojan/vagrant) "RF-NNNN" (see
 * starmap_designation, preview/README.md, and .claude/docs/world-classification.md).
 *
 * A seed token seeds everything deterministically (djb2): the same token always
 * yields the same bodies, the same Lagrange geometry, and the same proper name.
 * This is the single source of truth for the system — both route_gen_build() and
 * route_gen_describe() consume starmap_build().
 *
 * Masses are fictional, so Lagrange points are stylized but geometrically
 * faithful: L4/L5 lead/trail the planet by ±60° on its orbit; L1/L2/L3 are
 * collinear with the star–planet axis (L1/L2 a fixed radial fraction inside /
 * outside the planet, L3 diametrically opposite). All geometry is heliocentric
 * with the star at the world origin (0,0).
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

/* Build the full deterministic system for a seed token. The token only seeds the
 * world (djb2 → LCG); the displayed designation is derived from the destination
 * body's properties — see starmap_designation. */
void starmap_build(const char *designation, starmap_system_t *out);

/* Survey designation derived from the destination body's properties — orbital
 * role, composition, and a derived life-viability (canon-grounded):
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

/* World position of a planet's Lagrange point. planet_body_idx must be a
 * star-orbiting planet (parent_idx == 0xFF). */
void starmap_lagrange_pos(const starmap_system_t *sys, int planet_body_idx,
                          starmap_lagrange_t which, float *x, float *y);
