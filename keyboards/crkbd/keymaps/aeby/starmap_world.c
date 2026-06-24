#include "starmap_world.h"
#include <math.h>
#include <string.h>

#define SW_PI     3.14159265358979323846f
#define SW_TWO_PI 6.28318530717958647692f

/* ── World-generation constants (world units, "wu") ──────────────────────────
 * One wu is abstract; route_gen scales the whole system to the 128×32 panel.
 * The star sits at the world origin (0,0). */
#define SW_FIRST_PLANET_MIN   30.0f  /* innermost orbit lower bound        */
#define SW_FIRST_PLANET_SPAN  30.0f  /* …+ up to this much                 */
#define SW_PLANET_GAP_MIN     20.0f  /* spacing between successive orbits  */
#define SW_PLANET_GAP_SPAN    20.0f
#define SW_PLANET_R_MAX      120.0f  /* stop adding planets past this      */
#define SW_MOON_R_MIN          4.0f  /* moon orbit about its planet        */
#define SW_MOON_R_SPAN         6.0f
#define SW_VAGRANT_R_MIN_MUL   1.10f /* vagrant beyond outermost × this    */
#define SW_VAGRANT_R_SPAN_MUL  0.50f

/* Lagrange geometry (see header). L1/L2 sit this radial fraction inside /
 * outside the planet on the star–planet line; L4/L5 lead/trail by ±60°. */
#define SW_LAGRANGE_COLLINEAR_FRAC 0.12f
#define SW_LAGRANGE_TROJAN_ANGLE   (SW_PI / 3.0f)   /* 60° */

/* ── djb2 ────────────────────────────────────────────────────────────────── */
uint32_t starmap_seed(const char *designation) {
    uint32_t h = 5381;
    for (int i = 0; designation[i]; i++) {
        h = h * 33u ^ (uint8_t)designation[i];
    }
    return h;
}

/* ── Recursive world position ───────────────────────────────────────────────── */
void starmap_world_pos(const starmap_system_t *sys, int idx, float *x, float *y) {
    const starmap_body_t *b = &sys->bodies[idx];
    float px = 0.0f, py = 0.0f;
    if (b->parent_idx != 0xFF) {
        starmap_world_pos(sys, b->parent_idx, &px, &py);
    }
    *x = px + b->orbital_radius * cosf(b->angle);
    *y = py + b->orbital_radius * sinf(b->angle);
}

/* ── Lagrange point of a star-orbiting planet ───────────────────────────────── */
void starmap_lagrange_pos(const starmap_system_t *sys, int planet_body_idx,
                          starmap_lagrange_t which, float *x, float *y) {
    const starmap_body_t *p = &sys->bodies[planet_body_idx];
    float R  = p->orbital_radius;
    float th = p->angle;
    float a, rr;
    switch (which) {
        case STARMAP_L1: a = th;          rr = R * (1.0f - SW_LAGRANGE_COLLINEAR_FRAC); break;
        case STARMAP_L2: a = th;          rr = R * (1.0f + SW_LAGRANGE_COLLINEAR_FRAC); break;
        case STARMAP_L3: a = th + SW_PI;  rr = R;                                       break;
        case STARMAP_L4: a = th + SW_LAGRANGE_TROJAN_ANGLE; rr = R;                     break;
        case STARMAP_L5: default:
                         a = th - SW_LAGRANGE_TROJAN_ANGLE; rr = R;                     break;
    }
    *x = rr * cosf(a);
    *y = rr * sinf(a);
}

/* ── Procedural proper name (prefix + optional mid + suffix grammar) ──────────
 * Curated syllable lists composed prefix(+mid)+suffix, mirroring the canonical
 * aesthetic (Calpamos, Nimbulon, Sorvat, Karaxis). Deterministic from the RNG
 * so a designation always names the same system. The mid is dropped whenever it
 * would push past the 8-char OLED telemetry column, so names are always legible
 * and never truncated. */
static const char *SW_PREFIX[] = {
    "Cal","Zeph","Ach","Nim","Sor","Vel","Dax","Hel","Kar","Mir","Tan","Vor",
    "Pol","Lyr","Cer","Orb","Gor","Pel","Ten","Myx","Urb","Fel","Arc","Dra",
};
static const char *SW_MID[] = { "a","o","pa","bu","ra","mi","ve","no","ta","di","ga","lo" };
static const char *SW_SUFFIX[] = {
    "mos","ros","ron","lon","xis","rax","dor","nus","tis","vat","gon","lar",
    "sis","don","mar","kis","ron","vix","tor",
};

#define SW_ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

static int sw_strlen(const char *s) { int n = 0; while (s[n]) n++; return n; }

static void sw_append(char *dst, int *len, const char *s) {
    for (int i = 0; s[i] && *len < STARMAP_NAME_LEN - 1; i++) {
        dst[(*len)++] = s[i];
    }
    dst[*len] = '\0';
}

static void sw_make_name(starmap_rng_t *rng, char *out) {
    const int VISIBLE_MAX = 8;   /* the OLED telemetry column width */
    int len = 0;
    out[0] = '\0';

    const char *prefix = SW_PREFIX[starmap_rng_range(rng, 0, SW_ARRAY_LEN(SW_PREFIX) - 1)];
    const char *mid    = SW_MID[starmap_rng_range(rng, 0, SW_ARRAY_LEN(SW_MID) - 1)];
    const char *suffix = SW_SUFFIX[starmap_rng_range(rng, 0, SW_ARRAY_LEN(SW_SUFFIX) - 1)];

    bool use_mid = (starmap_rng_range(rng, 0, 1) == 0) &&
                   (sw_strlen(prefix) + sw_strlen(mid) + sw_strlen(suffix) <= VISIBLE_MAX);

    sw_append(out, &len, prefix);
    if (use_mid) sw_append(out, &len, mid);
    sw_append(out, &len, suffix);

    /* Uppercase: the OLED font reads best in caps, and it keeps the telemetry
     * column visually consistent with the class designation. */
    for (int i = 0; out[i]; i++) {
        if (out[i] >= 'a' && out[i] <= 'z') out[i] = (char)(out[i] - 'a' + 'A');
    }
}

/* ── Survey designation (derived from the destination body's class) ───────────
 * The displayed designation is no longer the seed token; it is a class label of
 * the chosen destination, grounded in canon (LV = Life Viable moons; KG = Jovian
 * gas giants, per KG-348; BG = colony worlds, per BG-386) plus an invented
 * minor-body catalog (RF) for trojans/vagrants. */

/* Finalmix hash of (seed, idx) → a well-distributed 32-bit value. */
static uint32_t sw_hash2(uint32_t seed, int idx) {
    uint32_t h = seed ^ ((uint32_t)idx * 2654435761u);
    h ^= h >> 16; h *= 0x7feb352du;
    h ^= h >> 15; h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

/* Jovian classification for a planet's designation (KG gas giant vs BG colony
 * world). Convention: outer, wider orbits skew Jovian. A planet beyond
 * SW_GAS_GIANT_R_HI is always a gas giant; inside SW_GAS_GIANT_R_LO always rocky;
 * between, a deterministic (seed,idx) hash bit decides so inner systems still
 * yield both classes. Derived (not stored) — keeps the ctypes mirror stable. */
#define SW_GAS_GIANT_R_LO  45.0f
#define SW_GAS_GIANT_R_HI  85.0f
static bool sw_is_gas_giant(uint32_t seed, const starmap_body_t *body, int idx) {
    if (body->orbital_radius >= SW_GAS_GIANT_R_HI) return true;
    if (body->orbital_radius <  SW_GAS_GIANT_R_LO) return false;
    return (sw_hash2(seed ^ 0x5BD1E995u, idx) & 1u) != 0;
}

/* "PP-N…" — two-letter prefix, dash, serial with no leading zeros. The longest
 * form ("RF-9999") is 7 chars, fitting designation[8] and the 8-col OLED. */
static void sw_format_designation(char *out, const char *prefix, uint32_t serial) {
    int n = 0;
    out[n++] = prefix[0];
    out[n++] = prefix[1];
    out[n++] = '-';
    char digits[6];
    int dn = 0;
    if (serial == 0) digits[dn++] = '0';
    while (serial > 0 && dn < (int)sizeof(digits)) {
        digits[dn++] = (char)('0' + serial % 10u);
        serial /= 10u;
    }
    while (dn > 0) out[n++] = digits[--dn];
    out[n] = '\0';
}

void starmap_designation(uint32_t seed, const starmap_body_t *body, int idx,
                         char out[STARMAP_DESIG_LEN]) {
    uint32_t h = sw_hash2(seed, idx);
    switch (body->type) {
        case STARMAP_MOON:
            sw_format_designation(out, "LV", 100u + (h % 1200u));   /* 100–1299 */
            break;
        case STARMAP_PLANET:
            sw_format_designation(out, sw_is_gas_giant(seed, body, idx) ? "KG" : "BG",
                                  100u + (h % 900u));                /* 100–999  */
            break;
        case STARMAP_TROJAN:
        case STARMAP_VAGRANT:
        default:
            sw_format_designation(out, "RF", 1000u + (h % 9000u));  /* 1000–9999 */
            break;
    }
}

/* ── Destination spectral class (reflectance taxonomy) ────────────────────────
 * Stars are classified by emission (OBAFGKM); these destinations are sub-stellar
 * bodies, classified by *reflectance* spectra instead. The pools below are the
 * real schemes that apply to each body type — the asteroid/KBO taxonomy (Tholen
 * 1984; Bus-DeMeo 2009: C/S/X complexes + D/P/V/M/K end-members), the Sudarsky
 * (2000) gas-giant classes I–V, and a lore-plausible silicate/metal extension for
 * terrestrials (which have no canonical reflectance class, like the invented RF
 * catalog). Trojan/vagrant pools are weighted D-heavy after the real Jupiter-
 * trojan fractions (~80% D-type). Selection is a pure (seed,idx) hash — no RNG-
 * stream draws — so it mirrors starmap_designation and stays host==device. */
#define SW_SALT_CLASS 0x5C1A55E5u  /* decorrelate from designation / gas-giant salts */

static const char *const SW_SUDARSKY[5] = { "I", "II", "III", "IV", "V" };

void starmap_spectral_class(uint32_t seed, const starmap_body_t *body, int idx,
                            char out[STARMAP_CLASS_LEN]) {
    uint32_t h = sw_hash2(seed ^ SW_SALT_CLASS, idx);

    /* Gas giants read the bare Sudarsky roman (I–V), ≤3 chars. */
    if (body->type == STARMAP_PLANET && sw_is_gas_giant(seed, body, idx)) {
        const char *roman = SW_SUDARSKY[h % 5u];
        int n = 0;
        while (roman[n] && n < STARMAP_CLASS_LEN - 1) { out[n] = roman[n]; n++; }
        out[n] = '\0';
        return;
    }

    /* Letter classes: a reflectance-taxonomy pool, weighted per body type, read as
     * "<L><digit>" (2 chars). */
    const char *pool;
    switch (body->type) {
        case STARMAP_MOON:    pool = "CDPS";        break; /* captured / icy small body  */
        case STARMAP_TROJAN:  pool = "DDDDPPXCSV";  break; /* trojans ~80% D-type         */
        case STARMAP_VAGRANT: pool = "DDDPPCXSV";   break; /* outer minor body, D-heavy   */
        case STARMAP_PLANET:                               /* rocky world (invented ext.) */
        default:              pool = "SQVMK";       break;
    }
    int len = sw_strlen(pool);
    out[0] = pool[h % (uint32_t)len];
    out[1] = (char)('0' + (h >> 8) % 10u);
    out[2] = '\0';
}

/* ── System build (single source of truth) ───────────────────────────────────── */
void starmap_build(const char *designation, starmap_system_t *out) {
    memset(out, 0, sizeof(*out));

    starmap_rng_t rng;
    rng.state = starmap_seed(designation);
    out->seed = rng.state;

    /* Proper name first so it doesn't depend on how many bodies spawn. */
    sw_make_name(&rng, out->name);

    /* ── Planets: 3–4, ascending orbital radius (occasionally realized as 2 when
     * the next orbit would exceed the planetary field, SW_PLANET_R_MAX). Biased to
     * ≥3 so most systems can host a flyby/coast route — a 2-planet system can only
     * ever be a single-leg direct hop, the least interesting case. ─────────────── */
    int n_planets = starmap_rng_range(&rng, 3, 4);
    float r = SW_FIRST_PLANET_MIN + starmap_rng_float(&rng) * SW_FIRST_PLANET_SPAN;
    for (int i = 0; i < n_planets && r <= SW_PLANET_R_MAX
                    && out->body_count < STARMAP_MAX_BODIES; i++) {
        starmap_body_t *b = &out->bodies[out->body_count];
        b->type           = STARMAP_PLANET;
        b->parent_idx     = 0xFF;
        b->orbital_radius = r;
        b->angle          = starmap_rng_float(&rng) * SW_TWO_PI;
        out->planet_idx[out->planet_count++] = out->body_count++;
        r += SW_PLANET_GAP_MIN + starmap_rng_float(&rng) * SW_PLANET_GAP_SPAN;
    }
    if (out->planet_count == 0) {
        /* Degenerate fallback: a single planet at r=50. */
        out->bodies[0].type = STARMAP_PLANET; out->bodies[0].parent_idx = 0xFF;
        out->bodies[0].orbital_radius = 50.0f; out->bodies[0].angle = 0.0f;
        out->planet_idx[0] = 0; out->planet_count = 1; out->body_count = 1;
    }

    int outermost = out->planet_idx[out->planet_count - 1];

    /* ── Moons: up to one per planet ─────────────────────────────────────── */
    for (int i = 0; i < out->planet_count && out->body_count < STARMAP_MAX_BODIES; i++) {
        if (starmap_rng_range(&rng, 0, 1) == 0) continue;
        starmap_body_t *b = &out->bodies[out->body_count];
        b->type           = STARMAP_MOON;
        b->parent_idx     = out->planet_idx[i];
        b->orbital_radius = SW_MOON_R_MIN + starmap_rng_float(&rng) * SW_MOON_R_SPAN;
        b->angle          = starmap_rng_float(&rng) * SW_TWO_PI;
        out->body_count++;
    }

    /* ── Trojans: occasionally park a body at a planet's L4 or L5 ─────────── */
    for (int i = 0; i < out->planet_count && out->body_count < STARMAP_MAX_BODIES; i++) {
        if (starmap_rng_range(&rng, 0, 2) != 0) continue;
        const starmap_body_t *host = &out->bodies[out->planet_idx[i]];
        float offset = (starmap_rng_range(&rng, 0, 1) == 0)
                       ? SW_LAGRANGE_TROJAN_ANGLE : -SW_LAGRANGE_TROJAN_ANGLE;
        starmap_body_t *b = &out->bodies[out->body_count];
        b->type           = STARMAP_TROJAN;
        b->parent_idx     = 0xFF;
        b->orbital_radius = host->orbital_radius;
        b->angle          = host->angle + offset;
        out->body_count++;
    }

    /* ── Vagrant: a lone body beyond the outermost orbit ─────────────────── */
    if (starmap_rng_range(&rng, 0, 3) == 0 && out->body_count < STARMAP_MAX_BODIES) {
        float outer_r = out->bodies[outermost].orbital_radius;
        starmap_body_t *b = &out->bodies[out->body_count];
        b->type           = STARMAP_VAGRANT;
        b->parent_idx     = 0xFF;
        b->orbital_radius = outer_r * (SW_VAGRANT_R_MIN_MUL
                            + starmap_rng_float(&rng) * SW_VAGRANT_R_SPAN_MUL);
        b->angle          = starmap_rng_float(&rng) * SW_TWO_PI;
        out->body_count++;
    }

    /* ── Departure / destination ──────────────────────────────────────────
     * Any two distinct non-star bodies. The star is never an endpoint (it isn't
     * in bodies[]); every planet, moon, trojan and vagrant is fair game, in
     * either direction. */
    out->depart_idx = (uint8_t)starmap_rng_range(&rng, 0, out->body_count - 1);
    int dst = starmap_rng_range(&rng, 0, out->body_count - 2);
    if (dst >= out->depart_idx) dst++;   /* keep it distinct from departure */
    out->dest_idx = (uint8_t)dst;

    /* Designation is a derived label of the destination body's class, not the
     * seed token — see starmap_designation. */
    starmap_designation(out->seed, &out->bodies[out->dest_idx], out->dest_idx,
                        out->designation);
}
