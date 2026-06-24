#include "route_anim.h"
#include "route_gen.h"
#include <string.h>

/* Fallback journey length if a route reports no ETA (shouldn't happen). */
#define RA_DEFAULT_DURATION_MS (3u * 60u * 60u * 1000u)  /* 3 h */

/* ── Seed-token sequence ──────────────────────────────────────────────────────
 * An LCG over dest_rng yields an endless, varied run of *seed tokens*. The token
 * only seeds the system (djb2 → world); it is no longer the displayed label —
 * starmap_build derives the designation from the destination body's class, and it
 * reaches telemetry via route.designation. The token is the decimal of the LCG
 * state, so successive journeys decorrelate. */
static void ra_next_seed_token(route_journey_t *j, char out[12]) {
    j->dest_rng = j->dest_rng * 1664525u + 1013904223u;
    uint32_t v = j->dest_rng;
    char digits[10];
    int dn = 0;
    if (v == 0) digits[dn++] = '0';
    while (v > 0 && dn < (int)sizeof(digits)) {
        digits[dn++] = (char)('0' + v % 10u);
        v /= 10u;
    }
    int n = 0;
    while (dn > 0) out[n++] = digits[--dn];
    out[n] = '\0';
}

/* ── Journey lifecycle ────────────────────────────────────────────────────────── */
static void ra_start_journey(route_journey_t *j) {
    char token[12];
    ra_next_seed_token(j, token);
    route_gen_build(token, &j->route);
    gfx_route_bake_bg(&j->route, j->bg_cache);
    j->start_ms    = timer_read32();
    j->duration_ms = (uint32_t)j->route.eta_minutes * 60000u;
    if (j->duration_ms == 0) j->duration_ms = RA_DEFAULT_DURATION_MS;
    j->cur_t  = 0.0f;
    j->active = true;
}

void route_anim_init(route_journey_t *j) {
    memset(j, 0, sizeof(*j));
    /* Fresh opening route every power cycle. The boot-time clock alone barely
     * varies at post_init (it runs early and deterministically), so a persisted
     * boot counter advances the seed monotonically and guarantees a different
     * route each boot; the clock is XOR-mixed for a little extra entropy. The
     * counter is bumped and written back for next boot. (eeconfig_*_user is the
     * 4-byte user slot — unused elsewhere in this keymap; one write per boot.) */
    uint32_t boot_n = eeconfig_read_user();
    eeconfig_update_user(boot_n + 1u);
    j->dest_rng = 0x00C0FFEEu ^ boot_n ^ timer_read32();
    ra_start_journey(j);
}

void route_anim_render_map(route_journey_t *j) {
    if (!j->active) ra_start_journey(j);

    uint32_t elapsed = timer_elapsed32(j->start_ms);
    if (elapsed >= j->duration_ms) {
        ra_start_journey(j);          /* arrival → next destination */
        elapsed = 0;
    }
    float t = (float)elapsed / (float)j->duration_ms;
    if (t > 1.0f) t = 1.0f;
    j->cur_t = t;

    oled_set_cursor(0, 0);
    oled_write_raw((const char *)j->bg_cache, 512);
    gfx_route_draw_ship(&j->route, t, gfx_pulse_size(timer_read32()));
}

void route_anim_reroll(route_journey_t *j) {
    /* Advance the seed sequence and start fresh — identical to the arrival path,
     * so a re-roll is just "skip to the next mission now." */
    ra_start_journey(j);
}

void route_anim_fill_telemetry(const route_journey_t *j, route_telemetry_t *out) {
    memcpy(out->system_name, j->route.system_name, sizeof(out->system_name));
    memcpy(out->designation, j->route.designation, sizeof(out->designation));
    float remaining = (float)j->route.eta_minutes * (1.0f - j->cur_t);
    out->eta_remaining_min = (uint16_t)(remaining + 0.5f);
    bool burn = false;
    out->phase = (uint8_t)gfx_route_phase(&j->route, j->cur_t, &burn);
    out->burn  = burn ? 1u : 0u;
    out->gaming = 0;
}

/* ── Host-only struct-size probes (ctypes layout parity, like gfx_route_sizeof) ─ */
#ifdef RG_HOST
uint32_t route_journey_sizeof(void)   { return (uint32_t)sizeof(route_journey_t); }
uint32_t route_telemetry_sizeof(void) { return (uint32_t)sizeof(route_telemetry_t); }
#endif
