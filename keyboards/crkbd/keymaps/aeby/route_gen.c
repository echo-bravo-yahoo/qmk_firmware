#include "route_gen.h"
#include "starmap_world.h"
#include <math.h>
#include <string.h>

#ifdef RG_HOST
#include <stdio.h>
#endif

#define RG_PI     3.14159265358979323846f
#define RG_TWO_PI 6.28318530717958647692f

/* Viewport-fit margins (px) kept clear of each edge. Asymmetric: the panel is far
 * wider than tall (128×32), and the framing sweep (Stage 3) deliberately lays the
 * route's long extent along x — dest oriented rightward — so the binding axis is
 * usually x. Pad x generously to keep that long extent off the side edges; keep y
 * small (vertical space is precious) — just enough to inset the path. Reticle
 * clearance needs no special case: every visited body sits inside the fit bbox and
 * is thus inset by the margins, so the destination reticle's ±3 px brackets clear
 * the side within MARGIN_X (12) and its ±2 px body clears top/bottom within
 * MARGIN_Y (5) wherever the dest lands. */
#define RG_MARGIN_X  12
#define RG_MARGIN_Y  5

/* Telemetry strip: after best-fit framing, the long-axis space the rendered content
 * (rings included) leaves over is split — the map is centered in it and the high-x end
 * holds a stacked label/value strip, flush to the edge. The strip's field/line geometry
 * (GFX_TEL_*) lives in oled_gfx.h so the bake and the per-frame overlay size it the same
 * way. No static end padding: the content uses the full long axis, with only the
 * centering gaps between the map and the strip / opposite edge. (RG_MARGIN_X still
 * insets the best-fit framing — a separate knob from the strip layout.) */

/* Framing sweep: choose the rotation that best fits the visited path into the wide
 * panel rather than always aligning dep→dest with +x. */
#define RG_FRAME_STEPS 120     /* candidate angles over [0,π); 1.5° resolution */
#define RG_FRAME_EPS   1e-4f   /* a later candidate must beat the incumbent by this to win →
                                  lowest-index wins near-ties identically on host + firmware */

/* Topologies — how the route threads the system. */
enum { RG_DIRECT = 0, RG_FLYBY = 1, RG_COAST = 2 };

typedef struct { float x, y; } rg_vec2_t;

typedef struct {
    uint8_t   type;          /* GFX_LEG_TRANSFER / GFX_LEG_COAST */
    rg_vec2_t p0, p1;        /* world endpoints */
    rg_vec2_t ctrl;          /* TRANSFER: Bézier control (world) */
    rg_vec2_t through;       /* COAST: the pivot body position the arc bows through */
    rg_vec2_t center;        /* COAST: arc center (star for heliocentric, planet for local) */
    float     coast_r;       /* COAST: orbital radius (world) */
} rg_leg_t;

typedef struct {
    starmap_system_t sys;
    rg_leg_t   legs[4];
    int        leg_count;
    uint8_t    topology;
    int        mid_planet_idx;   /* body index, or -1 */
    rg_vec2_t  frame_center;     /* gravitational frame: (0,0) star, or host planet for a local route */

    /* framing */
    float   cos_phi, sin_phi;    /* rotate dep→dest onto +x */
    float   bbox_cx, bbox_cy;    /* rotated-world bbox center */
    float   scale;               /* px per world unit */
    int16_t arc_cx, arc_cy;      /* star in display space */
    uint16_t eta_minutes;

    /* Explain hook (host-only readers): the named pivot the flyby/coast threads.
     * Plain ints set during planning, never read on-device — firmware-safe. */
    int     pivot_idx;           /* lagr_idx the flyby/coast threads, or -1 (direct / no pivot) */
    int     flyby_lagrange;      /* STARMAP_L1 / STARMAP_L2 for a flyby, else -1               */
} rg_plan_t;

/* ── Geometry helpers ───────────────────────────────────────────────────────── */
static rg_vec2_t rg_rotate(rg_vec2_t v, float c, float s) {
    rg_vec2_t r = { v.x * c + v.y * s, -v.x * s + v.y * c };
    return r;
}

/* How far a transfer's Bézier control may sit off the chord, as a fraction of
 * the chord length. Keeps the arc tangent-shaped but bounded, so endpoints that
 * straddle the star don't produce a control point far off-screen (which the
 * bbox fit would then squash the whole route into a needle to accommodate). */
#define RG_BOW_PERP_MAX  0.60f
#define RG_BOW_ALONG_MAX 0.50f

/* Transfer control point: intersection of the orbital tangent lines at both
 * endpoints (tangent ⟂ the radial from the frame center) so the Bézier leaves and
 * arrives tangent to the orbits — a real transfer arc, not a tween. The control
 * is then decomposed onto the chord and clamped, preserving tangent *direction*
 * while bounding how far it bows. Near-parallel tangents fall back to a
 * center-ward bow. `center` is the gravitational frame's center: the star (0,0)
 * for a heliocentric route, the host planet for a planet-local one — so a local
 * hop bows around its planet, not the distant star. */
static rg_vec2_t rg_transfer_control(rg_vec2_t p0, rg_vec2_t p1, rg_vec2_t center) {
    rg_vec2_t mid = { (p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f };
    rg_vec2_t D   = { p1.x - p0.x, p1.y - p0.y };
    float dlen    = sqrtf(D.x * D.x + D.y * D.y);
    if (dlen < 1e-3f) return mid;

    rg_vec2_t u   = { D.x / dlen, D.y / dlen };       /* along chord */
    rg_vec2_t nrm = { -u.y, u.x };                    /* chord normal */

    /* Radials measured from the frame center (origin → unchanged from the old
     * star-relative form). */
    rg_vec2_t r0  = { p0.x - center.x, p0.y - center.y };
    rg_vec2_t r1  = { p1.x - center.x, p1.y - center.y };
    rg_vec2_t cand;
    rg_vec2_t d0  = { -r0.y, r0.x };                  /* tangent at p0 (⟂ radial) */
    rg_vec2_t d1  = { -r1.y, r1.x };
    float det     = d1.x * d0.y - d0.x * d1.y;
    if (fabsf(det) > 1e-4f) {
        float a = (d1.x * D.y - d1.y * D.x) / det;
        cand.x = p0.x + a * d0.x;
        cand.y = p0.y + a * d0.y;
    } else {
        /* Tangents parallel: bow toward the frame center. */
        rg_vec2_t ts = { center.x - mid.x, center.y - mid.y };
        float perp_sign = (ts.x * nrm.x + ts.y * nrm.y) < 0.0f ? -1.0f : 1.0f;
        cand.x = mid.x + perp_sign * 0.4f * dlen * nrm.x;
        cand.y = mid.y + perp_sign * 0.4f * dlen * nrm.y;
    }

    /* Decompose (cand − mid) onto the chord and clamp both components. */
    rg_vec2_t rel = { cand.x - mid.x, cand.y - mid.y };
    float along = rel.x * u.x + rel.y * u.y;
    float perp  = rel.x * nrm.x + rel.y * nrm.y;
    float along_max = RG_BOW_ALONG_MAX * dlen;
    float perp_max  = RG_BOW_PERP_MAX  * dlen;
    if (along >  along_max) along =  along_max;
    if (along < -along_max) along = -along_max;
    if (perp  >  perp_max)  perp  =  perp_max;
    if (perp  < -perp_max)  perp  = -perp_max;

    rg_vec2_t c = {
        mid.x + along * u.x + perp * nrm.x,
        mid.y + along * u.y + perp * nrm.y,
    };
    return c;
}

static float rg_norm_angle(float a) {           /* → [0, 2π) */
    while (a < 0.0f)       a += RG_TWO_PI;
    while (a >= RG_TWO_PI) a -= RG_TWO_PI;
    return a;
}

/* ── Leg builders ───────────────────────────────────────────────────────────── */
static void rg_add_transfer(rg_plan_t *pl, rg_vec2_t p0, rg_vec2_t p1) {
    rg_leg_t *l = &pl->legs[pl->leg_count++];
    l->type = GFX_LEG_TRANSFER;
    l->p0 = p0; l->p1 = p1;
    l->ctrl = rg_transfer_control(p0, p1, pl->frame_center);
}

static void rg_add_coast(rg_plan_t *pl, rg_vec2_t p0, rg_vec2_t p1,
                         rg_vec2_t through, rg_vec2_t center, float radius) {
    rg_leg_t *l = &pl->legs[pl->leg_count++];
    l->type = GFX_LEG_COAST;
    l->p0 = p0; l->p1 = p1; l->through = through; l->center = center; l->coast_r = radius;
}

/* World-space point along a leg at s∈[0,1] — used by the bbox fit so framing
 * bounds the *whole* drawn path, not just its endpoints (multi-leg curves bow
 * past their endpoints). Coast sweeps the short way through the planet, matching
 * the pack-time display arc. */
static rg_vec2_t rg_leg_world_point(const rg_leg_t *l, float s) {
    if (l->type == GFX_LEG_COAST) {
        float a0 = atan2f(l->p0.y - l->center.y, l->p0.x - l->center.x);
        float a1 = atan2f(l->p1.y - l->center.y, l->p1.x - l->center.x);
        float ap = atan2f(l->through.y - l->center.y, l->through.x - l->center.x);
        float sweep_pos = rg_norm_angle(a1 - a0);
        float dp        = rg_norm_angle(ap - a0);
        float sweep     = (dp < sweep_pos) ? sweep_pos : (sweep_pos - RG_TWO_PI);
        float a = a0 + sweep * s;
        rg_vec2_t p = { l->center.x + l->coast_r * cosf(a),
                        l->center.y + l->coast_r * sinf(a) };
        return p;
    }
    float mt = 1.0f - s;
    rg_vec2_t p = {
        mt * mt * l->p0.x + 2.0f * mt * s * l->ctrl.x + s * s * l->p1.x,
        mt * mt * l->p0.y + 2.0f * mt * s * l->ctrl.y + s * s * l->p1.y,
    };
    return p;
}

/* ── Stage 3 helper: score one candidate framing rotation ───────────────────────
 * Bound the visited bodies + sampled path under the rotation (c,s), then fit that
 * bbox into the usable panel rectangle. Returns the rotated-world bbox center plus
 * the uniform px/world scale the fit allows — larger scale = better fill. The
 * sample set is identical to the old fixed-rotation fit: 9 points per leg (k=0..8)
 * via rg_leg_world_point, plus the flyby/coast planet center when one exists. */
typedef struct { float cx, cy, scale; } rg_fit_t;

static rg_fit_t rg_fit_candidate(const rg_plan_t *pl, float c, float s) {
    float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
    for (int i = 0; i < pl->leg_count; i++) {
        for (int k = 0; k <= 8; k++) {
            rg_vec2_t r = rg_rotate(rg_leg_world_point(&pl->legs[i], (float)k / 8.0f), c, s);
            if (r.x < minx) minx = r.x;
            if (r.x > maxx) maxx = r.x;
            if (r.y < miny) miny = r.y;
            if (r.y > maxy) maxy = r.y;
        }
    }
    if (pl->mid_planet_idx >= 0) {
        float bx, by;
        starmap_world_pos(&pl->sys, pl->mid_planet_idx, &bx, &by);
        rg_vec2_t r = rg_rotate((rg_vec2_t){ bx, by }, c, s);
        if (r.x < minx) minx = r.x;
        if (r.x > maxx) maxx = r.x;
        if (r.y < miny) miny = r.y;
        if (r.y > maxy) maxy = r.y;
    }
    float bw = maxx - minx, bh = maxy - miny;
    if (bw < 1.0f) bw = 1.0f;          /* collinear-path clamp (as today) */
    if (bh < 1.0f) bh = 1.0f;
    float sx = (127.0f - 2.0f * RG_MARGIN_X) / bw;
    float sy = (31.0f  - 2.0f * RG_MARGIN_Y) / bh;
    rg_fit_t f = { (minx + maxx) * 0.5f, (miny + maxy) * 0.5f, (sx < sy) ? sx : sy };
    return f;
}

/* Heliocentric mid-planet for a flyby/coast: an interior planet (never the
 * innermost or outermost — those frame poorly) that is NOT an endpoint's own
 * planet and, where possible, orbits between the two endpoints. Excluding the
 * endpoints' planets stops a coast/flyby from threading an endpoint's own orbit
 * ring — the heliocentric twin of the planet↔moon loop; the between-orbits
 * preference keeps the assist on the way rather than a detour. An endpoint's
 * "planet" is itself if it is a planet, its parent if it is a moon; trojans and
 * vagrants have no host planet to exclude. Sets pl->mid_planet_idx, leaving it −1
 * when no usable intermediary exists (forcing a direct hop). */
static void rg_pick_mid_planet(const starmap_system_t *sys, starmap_rng_t *rng,
                               rg_vec2_t depart, rg_vec2_t dest, rg_plan_t *pl) {
    pl->mid_planet_idx = -1;
    if (sys->planet_count < 3) return;            /* no interior band to thread */

    const starmap_body_t *d = &sys->bodies[sys->depart_idx];
    const starmap_body_t *e = &sys->bodies[sys->dest_idx];
    int excl0 = (d->type == STARMAP_PLANET) ? sys->depart_idx
              : (d->type == STARMAP_MOON)   ? d->parent_idx : -1;
    int excl1 = (e->type == STARMAP_PLANET) ? sys->dest_idx
              : (e->type == STARMAP_MOON)   ? e->parent_idx : -1;

    /* Endpoint heliocentric radii bound the "between" band. */
    float r_dep = sqrtf(depart.x * depart.x + depart.y * depart.y);
    float r_dst = sqrtf(dest.x * dest.x + dest.y * dest.y);
    float r_lo  = (r_dep < r_dst) ? r_dep : r_dst;
    float r_hi  = (r_dep < r_dst) ? r_dst : r_dep;

    /* Candidate interior planets (planet_idx[1 .. count-2]) minus the endpoints'
     * planets, split into those orbiting between the endpoints and the rest. */
    int between[STARMAP_MAX_PLANETS], other[STARMAP_MAX_PLANETS];
    int nb = 0, no = 0;
    for (int i = 1; i <= sys->planet_count - 2; i++) {
        int idx = sys->planet_idx[i];
        if (idx == excl0 || idx == excl1) continue;
        float r = sys->bodies[idx].orbital_radius;
        if (r >= r_lo && r <= r_hi) between[nb++] = idx;
        else                        other[no++]  = idx;
    }

    const int *pool = nb ? between : other;
    int n = nb ? nb : no;
    if (n == 0) return;                           /* nothing usable → direct hop */
    pl->mid_planet_idx = pool[starmap_rng_range(rng, 0, n - 1)];
}

/* ── Stage 1+2: build the system and plan the route ─────────────────────────── */
static void rg_plan(const char *designation, rg_plan_t *pl) {
    memset(pl, 0, sizeof(*pl));
    starmap_build(designation, &pl->sys);
    const starmap_system_t *sys = &pl->sys;

    /* Decorrelated-but-deterministic RNG for topology / ETA choices. */
    starmap_rng_t rng;
    rng.state = sys->seed ^ 0x9E3779B9u;

    float dx, dy;
    starmap_world_pos(sys, sys->depart_idx, &dx, &dy);
    rg_vec2_t depart = { dx, dy };
    float ex, ey;
    starmap_world_pos(sys, sys->dest_idx, &ex, &ey);
    rg_vec2_t dest = { ex, ey };

    /* Frame classification: a trip from a planet to its *own* moon is planned in
     * that planet's local frame — waypoints are the moon's planet-relative Lagrange
     * points (~moon-orbit scale) and the coast/transfers center on the host planet,
     * so the route stays local instead of looping out to the star's neighborhood.
     * Moons are the only non-star body and there is ≤1 per planet, so planet↔its-own-
     * moon is the only intra-subsystem pair; every other trip is heliocentric (see
     * .claude/docs/route-frames.md for the full trip→frame taxonomy). */
    const starmap_body_t *d = &sys->bodies[sys->depart_idx];
    const starmap_body_t *e = &sys->bodies[sys->dest_idx];
    int local_pivot = -1, local_host = -1;   /* the moon, and its host planet */
    if (e->type == STARMAP_MOON && e->parent_idx == sys->depart_idx) {
        local_pivot = sys->dest_idx;   local_host = sys->depart_idx;
    } else if (d->type == STARMAP_MOON && d->parent_idx == sys->dest_idx) {
        local_pivot = sys->depart_idx; local_host = sys->dest_idx;
    }
    bool local = local_pivot >= 0;

    /* Unify both frames behind one pivot body (whose Lagrange points the flyby/coast
     * threads), one frame center (what the route bows around), and a can_thread gate.
     * Local: pivot = the moon, center = the host planet, always threadable. Else:
     * pivot = a heliocentric mid-planet, center = the star (origin), threadable only
     * when such a planet exists. A local route never draws a mid-planet, so
     * mid_planet_idx keeps its −1 init and the framing fit bounds only the path. */
    int  lagr_idx;
    bool can_thread;
    pl->mid_planet_idx = -1;
    pl->pivot_idx = -1; pl->flyby_lagrange = -1;
    if (local) {
        lagr_idx = local_pivot;
        starmap_world_pos(sys, local_host, &pl->frame_center.x, &pl->frame_center.y);
        can_thread = true;
    } else {
        rg_pick_mid_planet(sys, &rng, depart, dest, pl);   /* sets mid_planet_idx */
        lagr_idx = pl->mid_planet_idx;
        pl->frame_center = (rg_vec2_t){ 0.0f, 0.0f };      /* star at the origin */
        can_thread = pl->mid_planet_idx >= 0;
    }

    /* Topology: with no pivot to thread, only a direct hop. Otherwise pick weighted
     * direct:flyby:coast = 4:27:9 (/40 ≈ 10/68/22). Combined with the ~89% of systems
     * that host ≥3 planets (the other ~11% are forced direct), this lands the overall
     * leg split near 20/60/20 single/double/triple — the gravity-assist flyby is the
     * common, visually interesting case. */
    if (!can_thread) {
        pl->topology = RG_DIRECT;
    } else {
        int roll = starmap_rng_range(&rng, 0, 39);         /* 0..39 */
        pl->topology = (roll < 4)  ? RG_DIRECT             /* 10%   */
                     : (roll < 31) ? RG_FLYBY              /* 67.5% */
                                   : RG_COAST;             /* 22.5% */
    }

    pl->leg_count = 0;
    switch (pl->topology) {
        case RG_FLYBY: {
            starmap_lagrange_t which = (starmap_rng_range(&rng, 0, 1) == 0)
                                       ? STARMAP_L1 : STARMAP_L2;
            pl->flyby_lagrange = (int)which;
            float lx, ly;
            starmap_lagrange_pos(sys, lagr_idx, which, &lx, &ly);
            rg_vec2_t lp = { lx, ly };
            rg_add_transfer(pl, depart, lp);
            rg_add_transfer(pl, lp, dest);
            break;
        }
        case RG_COAST: {
            float l4x, l4y, l5x, l5y, px, py;
            starmap_lagrange_pos(sys, lagr_idx, STARMAP_L4, &l4x, &l4y);
            starmap_lagrange_pos(sys, lagr_idx, STARMAP_L5, &l5x, &l5y);
            starmap_world_pos(sys, lagr_idx, &px, &py);   /* pivot sits between its L4/L5 */
            rg_vec2_t l4 = { l4x, l4y }, l5 = { l5x, l5y }, pp = { px, py };
            float coast_r = sys->bodies[lagr_idx].orbital_radius;
            rg_add_transfer(pl, depart, l4);
            rg_add_coast(pl, l4, l5, pp, pl->frame_center, coast_r);
            rg_add_transfer(pl, l5, dest);
            break;
        }
        case RG_DIRECT:
        default:
            rg_add_transfer(pl, depart, dest);
            break;
    }
    /* A non-direct topology threads the pivot whose Lagrange points its legs visit
     * (the moon for a local route, else the heliocentric mid-planet). */
    if (pl->topology != RG_DIRECT) pl->pivot_idx = lagr_idx;

    /* ── Stage 3: fit-optimal framing rotation ──────────────────────────────
     * Sweep candidate rotations over the half-circle [0,π) and keep the one whose
     * bbox fits the visited bodies + path into the wide panel at the largest scale.
     * Only the route-visited set is fit (departure, destination, any flyby/coast
     * planet, plus the connecting path) — never the star or unrelated planets,
     * which scattered at all angles add huge extent and collapse the scale; their
     * off-panel rings/bodies clip or cull as decoration (see rg_add_body). Uniform
     * scale keeps circles round; centering on the bbox guarantees no clip.
     *
     * Pure best-fit: no chord seed and no horizontal bias — the strict `>` + EPS
     * gate makes the lowest-index candidate win any near-tie (a stable, arbitrary
     * tie-break) identically on host (x86 SSE) and firmware (M0+ soft-float), so a
     * last-bit cosf/sinf difference can't make the preview disagree with the
     * device. j=0 (θ=0, world-axes-aligned) is the default/incumbent. Half-circle
     * suffices: a π rotation maps the bbox to itself; the dest-rightward flip below
     * resolves the orientation. No RNG consumed → ETA/topology rolls unaffected. */
    float best_c = 1.0f, best_s = 0.0f;                   /* θ=0 → (cos,sin)=(1,0) */
    rg_fit_t best = rg_fit_candidate(pl, best_c, best_s);
    for (int j = 1; j < RG_FRAME_STEPS; j++) {
        float th = (float)j * (RG_PI / (float)RG_FRAME_STEPS);
        float c = cosf(th), s = sinf(th);
        rg_fit_t f = rg_fit_candidate(pl, c, s);
        if (f.scale > best.scale + RG_FRAME_EPS) { best = f; best_c = c; best_s = s; }
    }

    /* Free 180° flip so dep→dest points rightward — identical bbox/scale, just a
     * point reflection of the layout; keeps the journey reading left-to-right.
     * Independent of fit. */
    float dirx = dest.x - depart.x, diry = dest.y - depart.y;
    if (dirx * best_c + diry * best_s < 0.0f) { best_c = -best_c; best_s = -best_s; }

    pl->cos_phi = best_c; pl->sin_phi = best_s;
    rg_fit_t fin = rg_fit_candidate(pl, best_c, best_s);  /* center in final (flipped) frame */
    pl->bbox_cx = fin.cx; pl->bbox_cy = fin.cy; pl->scale = fin.scale;

    /* Star (world origin) in display space. */
    rg_vec2_t o = rg_rotate((rg_vec2_t){0.0f, 0.0f}, pl->cos_phi, pl->sin_phi);
    pl->arc_cx = (int16_t)lroundf(63.5f + (o.x - pl->bbox_cx) * pl->scale);
    pl->arc_cy = (int16_t)lroundf(15.5f - (o.y - pl->bbox_cy) * pl->scale);

    /* ETA: longer routes read as longer journeys; varied per system. */
    float route_len = 0.0f;
    for (int i = 0; i < pl->leg_count; i++) {
        const rg_leg_t *l = &pl->legs[i];
        float lx = l->p1.x - l->p0.x, ly = l->p1.y - l->p0.y;
        route_len += sqrtf(lx * lx + ly * ly);
    }
    int eta = 90 + starmap_rng_range(&rng, 0, 180) + (int)(route_len * 0.6f);
    if (eta > 599) eta = 599;
    pl->eta_minutes = (uint16_t)eta;
}

/* ── Display transform (rotated world → panel px) ───────────────────────────── */
static rg_vec2_t rg_to_disp(const rg_plan_t *pl, float wx, float wy) {
    rg_vec2_t r = rg_rotate((rg_vec2_t){wx, wy}, pl->cos_phi, pl->sin_phi);
    rg_vec2_t d = {
        63.5f + (r.x - pl->bbox_cx) * pl->scale,
        15.5f - (r.y - pl->bbox_cy) * pl->scale,
    };
    return d;
}
static int16_t rg_dx(const rg_plan_t *pl, float wx, float wy) {
    return (int16_t)lroundf(rg_to_disp(pl, wx, wy).x);
}
static int16_t rg_dy(const rg_plan_t *pl, float wx, float wy) {
    return (int16_t)lroundf(rg_to_disp(pl, wx, wy).y);
}
static uint8_t rg_radius_px(float world_r, float scale) {
    float r = world_r * scale;
    if (r < 1.0f)   return 1;
    if (r > 255.0f) return 255;
    return (uint8_t)lroundf(r);
}

/* Add a body at world (wx,wy) to the route, skipping it if it falls off-panel. */
static void rg_add_body(gfx_route_t *out, const rg_plan_t *pl, float wx, float wy) {
    if (out->body_count >= 6) return;
    int16_t dx = rg_dx(pl, wx, wy);
    int16_t dy = rg_dy(pl, wx, wy);
    if (dx < 0 || dx >= 128 || dy < 0 || dy >= 32) return;
    out->bodies[out->body_count].x = dx;
    out->bodies[out->body_count].y = dy;
    out->body_count++;
}

/* Approximate the display length of a leg (px) for cumulative ship traversal. */
static float rg_leg_len(const gfx_leg_t *l) {
    if (l->type == GFX_LEG_COAST) {
        return (float)l->arc_r * fabsf(l->a_sweep);
    }
    float len = 0.0f;
    int16_t px = l->p0x, py = l->p0y;
    for (int i = 1; i <= 16; i++) {
        int16_t bx, by;
        gfx_prim_bezier_point(l->p0x, l->p0y, l->cx, l->cy, l->p1x, l->p1y,
                              (float)i / 16.0f, &bx, &by);
        float dx = (float)(bx - px), dy = (float)(by - py);
        len += sqrtf(dx * dx + dy * dy);
        px = bx; py = by;
    }
    return len;
}

/* ── Banner: measure the *visible* content extent, then shift the map off the strip ─
 * Bake the content once (banner_w is still 0 here, so only the map's rings / legs /
 * markers / bodies draw) and scan for the leftmost→rightmost lit column. This is the
 * on-panel footprint that actually draws — unlike a geometric ring center±radius span,
 * an off-panel star's big rings only light a small on-panel cap, so the strip is laid
 * out against the pixels you see rather than phantom off-panel ring reach. The strip
 * stays out of the map because the layout centers the map and reserves the breather;
 * test_strip_region_clear_of_graphics asserts that invariant across the sweep. */
static void rg_lit_xspan(const gfx_route_t *out, int16_t *x0, int16_t *x1) {
    uint8_t buf[512];
    gfx_route_bake_bg(out, buf);
    int lo = 128, hi = -1;
    for (int x = 0; x < 128; x++) {
        for (int page = 0; page < 4; page++) {
            if (buf[page * 128 + x]) { if (x < lo) lo = x; if (x > hi) hi = x; break; }
        }
    }
    if (hi < 0) { lo = 0; hi = 0; }   /* empty content (shouldn't happen) */
    *x0 = (int16_t)lo;
    *x1 = (int16_t)hi;
}

/* Translate every packed x by dx — equivalent to injecting the offset into both
 * centering sites (rg_to_disp + arc_cx), but applied post-pack so the measurement
 * above can run first. Integer dx, so it commutes with the lroundf already done.
 * Coast angles (a0/a_sweep) are center-relative, so shifting center + endpoints
 * together leaves them valid. */
static void rg_shift_route_x(gfx_route_t *out, int16_t dx) {
    if (dx == 0) return;
    out->arc_cx = (int16_t)(out->arc_cx + dx);
    for (int i = 0; i < out->arc_count; i++)
        if (out->arcs[i].local_center) out->arcs[i].cx = (int16_t)(out->arcs[i].cx + dx);
    for (int i = 0; i < out->leg_count; i++) {
        out->legs[i].p0x = (int16_t)(out->legs[i].p0x + dx);
        out->legs[i].p1x = (int16_t)(out->legs[i].p1x + dx);
        out->legs[i].cx  = (int16_t)(out->legs[i].cx + dx);
    }
    for (int i = 0; i < out->marker_count; i++)
        out->markers[i].x = (int16_t)(out->markers[i].x + dx);
    for (int i = 0; i < out->body_count; i++)
        out->bodies[i].x = (int16_t)(out->bodies[i].x + dx);
}

/* ── Stage 4: pack gfx_route_t ──────────────────────────────────────────────── */
void route_gen_build(const char *designation, gfx_route_t *out) {
    memset(out, 0, sizeof(*out));
    rg_plan_t pl;
    rg_plan(designation, &pl);
    const starmap_system_t *sys = &pl.sys;

    out->arc_cx = pl.arc_cx;
    out->arc_cy = pl.arc_cy;
    out->dash_px = 3;
    out->gap_px  = 3;
    out->ship_offset_x = 0;
    out->eta_minutes   = pl.eta_minutes;
    memcpy(out->system_name, sys->name,        sizeof(out->system_name));
    memcpy(out->designation, sys->designation, sizeof(out->designation));
    out->system_name[sizeof(out->system_name) - 1] = '\0';
    out->designation[sizeof(out->designation) - 1] = '\0';

    /* Orbital rings: one per planet (heliocentric), then moon orbits. */
    out->arc_count = 0;
    for (int i = 0; i < sys->planet_count && out->arc_count < 8; i++) {
        out->arcs[out->arc_count].radius = rg_radius_px(
            sys->bodies[sys->planet_idx[i]].orbital_radius, pl.scale);
        out->arcs[out->arc_count].solid = false;
        out->arc_count++;
    }
    for (int i = 0; i < sys->body_count && out->arc_count < 8; i++) {
        if (sys->bodies[i].type != STARMAP_MOON) continue;
        float pxw, pyw;
        starmap_world_pos(sys, sys->bodies[i].parent_idx, &pxw, &pyw);
        gfx_arc_t *a = &out->arcs[out->arc_count++];
        a->radius       = rg_radius_px(sys->bodies[i].orbital_radius, pl.scale);
        a->solid        = false;
        a->local_center = true;
        a->cx           = rg_dx(&pl, pxw, pyw);
        a->cy           = rg_dy(&pl, pxw, pyw);
    }

    /* Legs → display space. */
    out->leg_count = (uint8_t)pl.leg_count;
    for (int i = 0; i < pl.leg_count; i++) {
        const rg_leg_t *src = &pl.legs[i];
        gfx_leg_t *dst = &out->legs[i];
        dst->type = src->type;
        dst->p0x = rg_dx(&pl, src->p0.x, src->p0.y);
        dst->p0y = rg_dy(&pl, src->p0.x, src->p0.y);
        dst->p1x = rg_dx(&pl, src->p1.x, src->p1.y);
        dst->p1y = rg_dy(&pl, src->p1.x, src->p1.y);
        if (src->type == GFX_LEG_TRANSFER) {
            dst->cx = rg_dx(&pl, src->ctrl.x, src->ctrl.y);
            dst->cy = rg_dy(&pl, src->ctrl.x, src->ctrl.y);
        } else {
            /* Coast arc: center = the leg's frame center (star for a heliocentric
             * route, the host planet for a planet-local one), radius scaled, sweep
             * the short way that passes through the pivot body. For an interplanetary
             * coast center == (0,0), so rg_dx/rg_dy collapse to arc_cx/arc_cy —
             * byte-identical to the old `dst->cx = pl.arc_cx` path. */
            dst->cx    = rg_dx(&pl, src->center.x, src->center.y);
            dst->cy    = rg_dy(&pl, src->center.x, src->center.y);
            dst->arc_r = rg_radius_px(src->coast_r, pl.scale);
            float a0 = atan2f((float)(dst->p0y - dst->cy), (float)(dst->p0x - dst->cx));
            float a1 = atan2f((float)(dst->p1y - dst->cy), (float)(dst->p1x - dst->cx));
            int16_t tx = rg_dx(&pl, src->through.x, src->through.y);
            int16_t ty = rg_dy(&pl, src->through.x, src->through.y);
            float ap = atan2f((float)(ty - dst->cy), (float)(tx - dst->cx));
            float sweep_pos = rg_norm_angle(a1 - a0);          /* CCW 0..2π */
            float dp        = rg_norm_angle(ap - a0);
            dst->a0 = a0;
            dst->a_sweep = (dp < sweep_pos) ? sweep_pos : (sweep_pos - RG_TWO_PI);
        }
        dst->len = rg_leg_len(dst);
    }

    /* Markers: departure body, destination reticle, star ring (if on-screen). */
    out->marker_count = 0;
    {
        float wx, wy;
        starmap_world_pos(sys, sys->depart_idx, &wx, &wy);
        out->markers[out->marker_count].x = rg_dx(&pl, wx, wy);
        out->markers[out->marker_count].y = rg_dy(&pl, wx, wy);
        out->markers[out->marker_count].type = GFX_MARKER_BODY;
        out->marker_count++;
        starmap_world_pos(sys, sys->dest_idx, &wx, &wy);
        out->markers[out->marker_count].x = rg_dx(&pl, wx, wy);
        out->markers[out->marker_count].y = rg_dy(&pl, wx, wy);
        out->markers[out->marker_count].type = GFX_MARKER_TARGET;
        out->marker_count++;
    }
    if (pl.arc_cx >= 0 && pl.arc_cx < 128 && pl.arc_cy >= 0 && pl.arc_cy < 32 &&
        out->marker_count < 4) {
        out->markers[out->marker_count].x = pl.arc_cx;
        out->markers[out->marker_count].y = pl.arc_cy;
        out->markers[out->marker_count].type = GFX_MARKER_RING;
        out->marker_count++;
    }

    /* Bodies: planets (except the departure planet, already a marker), then
     * moons / trojans / vagrant, capped at the struct's 6 slots. Only the
     * route-visited bodies are bbox-fit, so any body the journey doesn't touch
     * (a distant vagrant, a trailing trojan, an unrelated planet) is decoration —
     * culled by rg_add_body when it falls off-panel rather than clipped. */
    out->body_count = 0;
    for (int i = 0; i < sys->planet_count && out->body_count < 6; i++) {
        int idx = sys->planet_idx[i];
        if (idx == sys->depart_idx) continue;
        float bx, by;
        starmap_world_pos(sys, idx, &bx, &by);
        rg_add_body(out, &pl, bx, by);
    }
    for (int i = 0; i < sys->body_count && out->body_count < 6; i++) {
        if (sys->bodies[i].type == STARMAP_PLANET) continue;
        float bx, by;
        starmap_world_pos(sys, i, &bx, &by);
        rg_add_body(out, &pl, bx, by);
    }

    /* Destination spectral class (still computed for the struct/tests, no longer drawn)
     * and the origin designation (ORG strip value). The departure body isn't pinned by
     * the seed token the way the destination is, so its designation is derived. */
    starmap_spectral_class(sys->seed, &sys->bodies[sys->dest_idx], sys->dest_idx,
                           out->dest_class);
    starmap_designation(sys->seed, sys, sys->depart_idx, out->origin);
    /* Destination colony status — DOCKED/LANDED at arrival, colony/unpopulated in the
     * host CLI. Pinned to the destination body, deterministic per token. */
    out->is_colony = starmap_is_colony(sys->seed, sys, sys->dest_idx) ? 1u : 0u;

    /* Telemetry strip + long-axis layout. Measure the *visible* (lit) content extent M and
     * the leftover L over the full long axis. Reserve a fixed GFX_TEL_MAP_GAP breather
     * between the map and the strip *before* packing fields, so the text always clears the
     * graphics — this deliberately overrides the equal-gap split (text and map were reading
     * too close). As many fields as fit the remaining budget (cap GFX_TEL_NFIELDS) stack
     * flush to the high-x edge; the strip's slack beyond the breather balances the map on
     * the low-x side (it re-centers once there is room to spare). L too small for even one
     * breathing field ⇒ no strip, the visible map centered full-bleed with rings free to
     * spill — desired variety, not a fallback. The framing/zoom is unchanged. Because M is
     * the lit footprint (not the geometric ring span), a route whose star sits off-panel
     * reclaims the empty margin its big rings leave for a strip. */
    {
        int16_t x0, x1;
        rg_lit_xspan(out, &x0, &x1);
        int16_t M = (int16_t)(x1 - x0);
        int16_t L = (int16_t)(128 - M);
        /* A strip reserves the breather plus GFX_TEL_CHIP_PAD at the high-x edge — the top
         * label's inverse chip needs that 1 px border on-panel, not clipped at x=128. */
        int16_t budget = (int16_t)(L - GFX_TEL_MAP_GAP - GFX_TEL_CHIP_PAD);
        int n = (budget >= 2 * GFX_TT_ROWS + GFX_TEL_GAP) ? gfx_tel_nfields((uint8_t)budget) : 0;
        int16_t textSize = n ? (int16_t)(n * 2 * GFX_TEL_PITCH - GFX_TEL_GAP) : 0;
        bool placed = false;
        if (n) {
            int16_t slack    = (int16_t)(L - textSize - GFX_TEL_CHIP_PAD);  /* leftGap + map↔strip gap */
            int16_t rightGap = (int16_t)(slack / 2);
            if (rightGap < GFX_TEL_MAP_GAP) rightGap = GFX_TEL_MAP_GAP;  /* guarantee the breather */
            int16_t leftGap  = (int16_t)(slack - rightGap);        /* ≥ 0: budget reserved the gap */
            int16_t dx       = (int16_t)(leftGap - x0);
            int16_t banner_x = (int16_t)(128 - textSize - GFX_TEL_CHIP_PAD);
            rg_shift_route_x(out, dx);                             /* map left, breather to the strip */
            /* Centering the map by the *visible* extent can pull a ring whose cap was just
             * off-panel back on-screen toward the strip. Re-measure the shifted content; if
             * anything now eats into the breather (within GFX_TEL_MAP_GAP of the strip), this
             * route can't carry one without crowding the text — undo and fall back to
             * no-strip full-bleed (rare; ~the off-panel-edge ring case the old geometric
             * measure used to suppress wholesale). */
            int16_t sx0, sx1;
            rg_lit_xspan(out, &sx0, &sx1);
            if (sx1 <= (int16_t)(banner_x - GFX_TEL_MAP_GAP)) {
                out->banner_w = (uint8_t)textSize;
                out->banner_x = (uint8_t)banner_x;     /* PAD reserved above for the top chip border */
                placed = true;
            } else {
                rg_shift_route_x(out, (int16_t)(-dx));  /* undo: back to the unshifted measure */
            }
        }
        if (!placed) {
            /* No strip: center the visible content full-bleed (lit M ≤ 127 keeps it on-panel). */
            rg_shift_route_x(out, (int16_t)(L / 2 - x0));
            out->banner_w = 0;
            out->banner_x = 0;
        }
    }
}

/* ── Host-only route dump ───────────────────────────────────────────────────── */
#ifdef RG_HOST
static const char *rg_body_type_name(uint8_t t) {
    switch (t) {
        case STARMAP_PLANET:  return "planet";
        case STARMAP_MOON:    return "moon";
        case STARMAP_TROJAN:  return "trojan";
        case STARMAP_VAGRANT: return "vagrant";
        default:              return "?";
    }
}
static const char *rg_topology_name(uint8_t t) {
    switch (t) {
        case RG_DIRECT: return "direct transfer";
        case RG_FLYBY:  return "gravity-assist flyby";
        case RG_COAST:  return "Lagrange coast";
        default:        return "?";
    }
}
static const char *rg_leg_type_name(uint8_t t) {
    return (t == GFX_LEG_COAST) ? "coast" : "transfer";
}

void route_gen_describe(const char *designation) {
    rg_plan_t pl;
    rg_plan(designation, &pl);
    const starmap_system_t *sys = &pl.sys;

    printf("=== %s  →  system \"%s\"  (seed %u) ===\n",
           sys->designation, sys->name, sys->seed);
    printf("topology: %s   ETA %uh%02u\n\n",
           rg_topology_name(pl.topology), pl.eta_minutes / 60, pl.eta_minutes % 60);

    printf("Bodies (%d):\n", sys->body_count);
    for (int i = 0; i < sys->body_count; i++) {
        float wx, wy;
        starmap_world_pos(sys, i, &wx, &wy);
        const char *tag = (i == sys->depart_idx) ? " [DEPART]"
                        : (i == sys->dest_idx)   ? " [DEST]" : "";
        printf("  [%2d] %-7s r=%6.1f  ang=%6.1f°  world=(%7.1f,%7.1f)%s\n",
               i, rg_body_type_name(sys->bodies[i].type),
               sys->bodies[i].orbital_radius,
               sys->bodies[i].angle * 180.0f / RG_PI, wx, wy, tag);
    }

    printf("\nLagrange points:\n");
    for (int i = 0; i < sys->planet_count; i++) {
        int idx = sys->planet_idx[i];
        const char *names[5] = { "L1", "L2", "L3", "L4", "L5" };
        printf("  planet[%d] (body %d):\n", i, idx);
        for (int k = 0; k < 5; k++) {
            float lx, ly;
            starmap_lagrange_pos(sys, idx, (starmap_lagrange_t)k, &lx, &ly);
            printf("    %s world=(%7.1f,%7.1f)  disp=(%3d,%3d)\n",
                   names[k], lx, ly, rg_dx(&pl, lx, ly), rg_dy(&pl, lx, ly));
        }
    }

    printf("\nLegs (%d):\n", pl.leg_count);
    for (int i = 0; i < pl.leg_count; i++) {
        const rg_leg_t *l = &pl.legs[i];
        printf("  leg %d  %-8s  p0 disp=(%3d,%3d)  p1 disp=(%3d,%3d)\n",
               i, rg_leg_type_name(l->type),
               rg_dx(&pl, l->p0.x, l->p0.y), rg_dy(&pl, l->p0.x, l->p0.y),
               rg_dx(&pl, l->p1.x, l->p1.y), rg_dy(&pl, l->p1.x, l->p1.y));
    }

    printf("\nFraming: scale=%.3f px/wu  star disp=(%d,%d)  rotate %.1f°\n\n",
           pl.scale, pl.arc_cx, pl.arc_cy,
           atan2f(pl.sin_phi, pl.cos_phi) * 180.0f / RG_PI);
    fflush(stdout);
}

void route_gen_explain(const char *token, route_explain_t *out) {
    rg_plan_t pl;
    rg_plan(token, &pl);
    const starmap_system_t *sys = &pl.sys;

    memset(out, 0, sizeof(*out));
    out->topology       = pl.topology;
    out->eta_minutes    = pl.eta_minutes;
    out->leg_count      = (uint8_t)pl.leg_count;
    out->depart_type    = sys->bodies[sys->depart_idx].type;
    out->dest_type      = sys->bodies[sys->dest_idx].type;
    out->flyby_lagrange = (int8_t)pl.flyby_lagrange;
    if (pl.pivot_idx >= 0) {
        out->pivot_type = sys->bodies[pl.pivot_idx].type;
        /* A planet↔own-moon local route threads the *destination* moon's own Lagrange
         * points, so the pivot and the destination are one body. The destination shows
         * the token verbatim (sys->designation), while starmap_designation would derive
         * a different label for the same body — so reuse the destination's designation
         * to keep the itinerary self-consistent. (When the pivot is the departure body,
         * its derived designation already matches the derived ORG label, so the plain
         * starmap_designation path stays consistent there.) */
        if (pl.pivot_idx == sys->dest_idx)
            memcpy(out->pivot, sys->designation, sizeof(out->pivot));
        else
            starmap_designation(sys->seed, sys, pl.pivot_idx, out->pivot);
    } else {
        out->pivot[0] = '\0';
    }
}
#endif /* RG_HOST */
