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

/* ── Seed-token parser (seed-is-designation) ─────────────────────────────────
 * A conforming token is "{PREFIX}-{1..4 digits}", case-insensitive, where PREFIX
 * is one of LV/BG/KG/RF. The token IS the destination: it seeds the world and is
 * echoed verbatim (uppercased) as the displayed designation, and its prefix pins
 * the destination body's type (see sw_pin_destination / starmap_build). The serial
 * is never interpreted numerically — only validated and copied. A non-conforming
 * token yields ok=false, and the legacy derived-designation path runs instead. */
typedef enum { SW_PFX_LV, SW_PFX_BG, SW_PFX_KG, SW_PFX_RF } sw_prefix_t;
static const char SW_PFX_STR[4][3] = { "LV", "BG", "KG", "RF" };

typedef struct {
    bool        ok;                        /* false → malformed: take legacy derived path   */
    sw_prefix_t prefix;
    char        text[STARMAP_DESIG_LEN];   /* canonical "PP-NNNN", uppercased, ≤7 + NUL     */
} sw_token_t;

static char sw_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static sw_token_t sw_parse_token(const char *tok) {
    sw_token_t tk;
    tk.ok = false;
    tk.prefix = SW_PFX_LV;
    tk.text[0] = '\0';
    if (!tok || !tok[0] || !tok[1]) return tk;

    /* Match the two-letter prefix, case-insensitively. */
    char p0 = sw_upper(tok[0]), p1 = sw_upper(tok[1]);
    int pfx = -1;
    for (int i = 0; i < 4; i++) {
        if (SW_PFX_STR[i][0] == p0 && SW_PFX_STR[i][1] == p1) { pfx = i; break; }
    }
    if (pfx < 0 || tok[2] != '-') return tk;

    /* 1..4 digits, then end-of-string. */
    int ndig = 0;
    while (tok[3 + ndig] >= '0' && tok[3 + ndig] <= '9') ndig++;
    if (ndig < 1 || ndig > 4 || tok[3 + ndig] != '\0') return tk;

    /* Build the canonical "PP-DDDD" (≤7 chars, always fits STARMAP_DESIG_LEN). */
    int n = 0;
    tk.text[n++] = p0;
    tk.text[n++] = p1;
    tk.text[n++] = '-';
    for (int i = 0; i < ndig && n < STARMAP_DESIG_LEN - 1; i++) tk.text[n++] = tok[3 + i];
    tk.text[n] = '\0';
    tk.prefix = (sw_prefix_t)pfx;
    tk.ok = true;
    return tk;
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

/* ── Lagrange point of an orbiting body, about its parent ────────────────────── */
void starmap_lagrange_pos(const starmap_system_t *sys, int planet_body_idx,
                          starmap_lagrange_t which, float *x, float *y) {
    const starmap_body_t *p = &sys->bodies[planet_body_idx];
    /* Center on the body's parent. A planet's parent is the star (origin), so this
     * is a no-op for planets and matches the old heliocentric behavior; a moon's
     * parent is its planet, yielding planet-local L-points at the moon's scale (the
     * body's orbital_radius/angle are already parent-relative). */
    float ox = 0.0f, oy = 0.0f;
    if (p->parent_idx != 0xFF) starmap_world_pos(sys, p->parent_idx, &ox, &oy);
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
    *x = ox + rr * cosf(a);
    *y = oy + rr * sinf(a);
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

/* ── Survey designation (malformed-token fallback only) ────────────────────────
 * For a conforming "{PREFIX}-{digits}" token the designation IS the token and the
 * destination's type is pinned to the prefix (see starmap_build). This derived
 * label is now reached ONLY when the token is malformed: it classifies the
 * randomly-chosen destination from its intrinsic properties — orbital role,
 * composition and a derived life-viability — never stored on starmap_body_t (so
 * the ctypes mirror stays stable). Grounded in canon: LV = Life-Viable world (moon
 * OR rocky planet in the habitable band — LV-426 is a moon, LV-178/895 are
 * planets); KG = Jovian gas giant, per KG-348; BG = rocky world that is not
 * life-viable (barren / colony), per BG-386; plus an invented minor-body catalog
 * (RF) for trojans/vagrants. See world-classification.md for the full rationale. */

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

/* Life-viability for the LV designation. RETIRED FROM THE LIVE PATH: a conforming
 * token pins its destination by prefix, so this predicate now feeds only the
 * malformed-token fallback (starmap_designation). A major rocky body (moon or
 * planet) in the temperate habitable band. Derived (not stored) — mirrors
 * sw_is_gas_giant, so no starmap_body_t field is added and the ctypes mirror stays
 * stable. Pure (seed,idx) hash, no RNG-stream draws, so topology/ETA rolls and
 * host==device are untouched. A moon inherits its parent planet's heliocentric
 * distance, so a habitable moon of a band gas giant reads LV — the LV-426/Calpamos
 * shape.
 *
 * Habitable band (wu): the temperate annulus where a rocky world can be life-
 * viable. Tunable; first-pass excludes scorching-inner and frozen/gas-giant-outer
 * orbits. Planets generate at ~30–120 wu; gas giants skew ≥85; this band sits in
 * the middle. */
#define SW_HZ_MIN      34.0f
#define SW_HZ_MAX      82.0f
#define SW_SALT_VIABLE 0x1FE57AB1u   /* decorrelate from designation / gas-giant / class salts */

static bool sw_is_life_viable(uint32_t seed, const starmap_system_t *sys, int idx) {
    const starmap_body_t *b = &sys->bodies[idx];
    if (b->type == STARMAP_TROJAN || b->type == STARMAP_VAGRANT) return false; /* minor body */
    if (sw_is_gas_giant(seed, b, idx)) return false;                            /* no surface */
    float r = (b->type == STARMAP_MOON)
              ? sys->bodies[b->parent_idx].orbital_radius   /* moon ≈ its planet's heliocentric distance */
              : b->orbital_radius;
    if (r < SW_HZ_MIN || r > SW_HZ_MAX) return false;       /* must be in the temperate band */
    return (sw_hash2(seed ^ SW_SALT_VIABLE, idx) & 1u) != 0;/* roll so in-zone worlds vary → some BG */
}

/* Colony status for the destination's DOCKED/LANDED label (route_anim) and the host
 * CLI's colony/unpopulated annotation (route_explain). A gas giant always reads
 * colony — the orbital docks ride its rings — while every other body is settled 70%
 * of the time. The 70/30 split is a salted (seed,idx) hash, so the same token always
 * reports the same status and the host matches the device. Pure hash like
 * sw_is_gas_giant / starmap_designation (no RNG-stream draws), so topology/ETA rolls
 * are untouched and no starmap_body_t field is added (the ctypes mirror stays stable). */
#define SW_SALT_COLONY 0xC0107A11u  /* distinct from designation / gas-giant / viable / class salts */

bool starmap_is_colony(uint32_t seed, const starmap_system_t *sys, int idx) {
    if (sw_is_gas_giant(seed, &sys->bodies[idx], idx)) return true;
    return (sw_hash2(seed ^ SW_SALT_COLONY, idx) % 100u) < 70u;
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

void starmap_designation(uint32_t seed, const starmap_system_t *sys, int idx,
                         char out[STARMAP_DESIG_LEN]) {
    const starmap_body_t *b = &sys->bodies[idx];
    uint32_t h = sw_hash2(seed, idx);
    /* Decision order: minor body → gas giant → life-viable → else rocky-non-viable. */
    if (b->type == STARMAP_TROJAN || b->type == STARMAP_VAGRANT)
        sw_format_designation(out, "RF", 1000u + (h % 9000u));   /* minor body   1000–9999 */
    else if (sw_is_gas_giant(seed, b, idx))
        sw_format_designation(out, "KG", 100u + (h % 900u));     /* gas giant     100–999  */
    else if (sw_is_life_viable(seed, sys, idx))
        sw_format_designation(out, "LV", 100u + (h % 1200u));    /* life-viable   100–1299 */
    else
        sw_format_designation(out, "BG", 100u + (h % 900u));     /* rocky, barren/colony   */
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

/* ── Destination pinning (seed-is-designation) ───────────────────────────────
 * After the ambient world is generated, select — or, if absent, construct — a
 * destination body whose type matches the token's prefix, and return its index.
 * The prefix→type contract:
 *   RF → trojan or vagrant          KG → gas-giant planet
 *   LV → moon or rocky planet       BG → moon or rocky planet  (LV/BG share a pool)
 * Rock-vs-moon variety for LV/BG falls out of the candidate selection; when no
 * candidate exists, construction defaults to the always-possible moon (LV/BG), a
 * vagrant (RF), or a promotion of the outermost planet to a guaranteed gas giant
 * (KG). The selection/construction draws from `rng`, so the choice is deterministic
 * for a given token. */
static bool sw_is_dest_candidate(uint32_t seed, const starmap_body_t *b, int idx,
                                 sw_prefix_t p) {
    switch (p) {
        case SW_PFX_RF:
            return b->type == STARMAP_TROJAN || b->type == STARMAP_VAGRANT;
        case SW_PFX_KG:
            return b->type == STARMAP_PLANET && sw_is_gas_giant(seed, b, idx);
        case SW_PFX_LV:
        case SW_PFX_BG:
        default:
            return b->type == STARMAP_MOON ||
                   (b->type == STARMAP_PLANET && !sw_is_gas_giant(seed, b, idx));
    }
}

static int sw_pin_destination(starmap_rng_t *rng, starmap_system_t *out, sw_prefix_t p) {
    /* Prefer an existing matching body (deterministic pick from the pool). */
    int cand[STARMAP_MAX_BODIES];
    int n = 0;
    for (int i = 0; i < out->body_count; i++) {
        if (sw_is_dest_candidate(out->seed, &out->bodies[i], i, p)) cand[n++] = i;
    }
    if (n > 0) return cand[starmap_rng_range(rng, 0, n - 1)];

    int outermost = out->planet_idx[out->planet_count - 1];

    switch (p) {
        case SW_PFX_KG: {
            /* No gas giant exists: promote the outermost planet to a guaranteed one
             * (r ≥ HI). It stays outermost, so planet_idx ascending order holds and
             * no new slot is needed. */
            if (out->bodies[outermost].orbital_radius < SW_GAS_GIANT_R_HI)
                out->bodies[outermost].orbital_radius = SW_GAS_GIANT_R_HI;
            return outermost;
        }
        case SW_PFX_RF: {
            /* Append a vagrant beyond the outermost orbit (the vagrant block). */
            if (out->body_count < STARMAP_MAX_BODIES) {
                float outer_r = out->bodies[outermost].orbital_radius;
                starmap_body_t *b = &out->bodies[out->body_count];
                b->type           = STARMAP_VAGRANT;
                b->parent_idx     = 0xFF;
                b->orbital_radius = outer_r * (SW_VAGRANT_R_MIN_MUL
                                    + starmap_rng_float(rng) * SW_VAGRANT_R_SPAN_MUL);
                b->angle          = starmap_rng_float(rng) * SW_TWO_PI;
                return out->body_count++;
            }
            break;
        }
        case SW_PFX_LV:
        case SW_PFX_BG:
        default: {
            /* Append a moon to a planet that has none yet (preserving the ≤1-moon-
             * per-planet invariant the local-frame router relies on); fall back to
             * any planet if all already have a moon. Reachable only when no moon and
             * no rocky planet exist, so the fallback never actually fires. */
            if (out->body_count < STARMAP_MAX_BODIES) {
                int host = -1;
                for (int i = 0; i < out->planet_count; i++) {
                    int pidx = out->planet_idx[i];
                    bool has_moon = false;
                    for (int j = 0; j < out->body_count; j++) {
                        if (out->bodies[j].type == STARMAP_MOON &&
                            out->bodies[j].parent_idx == pidx) { has_moon = true; break; }
                    }
                    if (!has_moon) { host = pidx; break; }
                }
                if (host < 0) host = out->planet_idx[0];
                starmap_body_t *b = &out->bodies[out->body_count];
                b->type           = STARMAP_MOON;
                b->parent_idx     = (uint8_t)host;
                b->orbital_radius = SW_MOON_R_MIN + starmap_rng_float(rng) * SW_MOON_R_SPAN;
                b->angle          = starmap_rng_float(rng) * SW_TWO_PI;
                return out->body_count++;
            }
            break;
        }
    }

    /* Pathological overflow (≥16 bodies already): return any existing body so the
     * route still renders. */
    return 0;
}

/* ── System build (single source of truth) ───────────────────────────────────── */
void starmap_build(const char *designation, starmap_system_t *out) {
    memset(out, 0, sizeof(*out));
    sw_token_t tk = sw_parse_token(designation);

    /* Seed from the CANONICAL text when conforming, so "lv-426" and "LV-426" hash
     * to the same world — the designation is a case-insensitive bookmark. A
     * malformed token seeds from its raw string (legacy). */
    starmap_rng_t rng;
    rng.state = starmap_seed(tk.ok ? tk.text : designation);
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
    if (tk.ok) {
        /* Pinned path: the destination is fixed by the prefix; the DEPARTURE is the
         * random-distinct one (consistent with the local-frame feature's "depart
         * stays random"). The designation IS the token, echoed verbatim. */
        out->dest_idx = (uint8_t)sw_pin_destination(&rng, out, tk.prefix);
        int dep = starmap_rng_range(&rng, 0, out->body_count - 2);
        if (dep >= out->dest_idx) dep++;   /* keep it distinct from the destination */
        out->depart_idx = (uint8_t)dep;
        int i = 0;
        for (; tk.text[i] && i < STARMAP_DESIG_LEN - 1; i++) out->designation[i] = tk.text[i];
        out->designation[i] = '\0';
    } else {
        /* Malformed token: legacy path — random endpoints, then a designation
         * derived from the destination body's properties (see starmap_designation;
         * pass the system so a moon's life-viability can read its parent's distance). */
        out->depart_idx = (uint8_t)starmap_rng_range(&rng, 0, out->body_count - 1);
        int dst = starmap_rng_range(&rng, 0, out->body_count - 2);
        if (dst >= out->depart_idx) dst++;   /* keep it distinct from departure */
        out->dest_idx = (uint8_t)dst;
        starmap_designation(out->seed, out, out->dest_idx, out->designation);
    }
}
