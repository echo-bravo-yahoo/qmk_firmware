/*
 * starmap_world — fictional star-system model for the Corne transit map.
 *
 * A "system" is a proper-named gas giant / star (e.g. Calpamos) with a handful
 * of planets, moons, trojans and the occasional vagrant. A journey transits the
 * USCSS Patna within that system to a destination body whose survey designation
 * reflects its class — a moon reads "LV-NNN", a gas giant "KG-NNN", a colony
 * world "BG-NNN", a minor body "RF-NNNN" (see starmap_designation and
 * preview/README.md).
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
    return (float)(starmap_rng_next(r) >> 8) / (float)(1 << 24);
}

/* djb2 over the seed token — the deterministic seed. */
uint32_t starmap_seed(const char *designation);

/* Build the full deterministic system for a seed token. The token only seeds the
 * world (djb2 → LCG); the displayed designation is derived from the destination
 * body's class — see starmap_designation. */
void starmap_build(const char *designation, starmap_system_t *out);

/* Survey designation derived from the destination body's class (canon-grounded):
 *   moon             → "LV-NNN"  (Life Viable;  serial 100–1299)
 *   gas-giant planet → "KG-NNN"  (Jovian;       serial 100–999)
 *   rocky planet     → "BG-NNN"  (colony world; serial 100–999)
 *   trojan / vagrant → "RF-NNNN" (minor body;   serial 1000–9999, invented)
 * Serial is a deterministic hash of (seed, idx); gas-giant class is derived from
 * orbital radius + a (seed,idx) hash (no starmap_body_t field, so the ctypes
 * mirror stays stable). out must hold STARMAP_DESIG_LEN bytes. */
void starmap_designation(uint32_t seed, const starmap_body_t *body, int idx,
                         char out[STARMAP_DESIG_LEN]);

/* World position of a body (recursively adds the parent's position). */
void starmap_world_pos(const starmap_system_t *sys, int idx, float *x, float *y);

/* World position of a planet's Lagrange point. planet_body_idx must be a
 * star-orbiting planet (parent_idx == 0xFF). */
void starmap_lagrange_pos(const starmap_system_t *sys, int planet_body_idx,
                          starmap_lagrange_t which, float *x, float *y);
