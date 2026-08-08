#include "route_anim.h"
#include "route_gen.h"
#include <string.h>

/* Fallback journey length if a route reports no ETA (shouldn't happen). */
#define RA_DEFAULT_DURATION_MS (3u * 60u * 60u * 1000u)  /* 3 h */

/* ETA trim clamp: [30, 599] minutes. 599 is route_gen's existing ETA cap; 30 keeps
 * the one-sided 4-min major burns ≤ 13% of the journey (burns are sized off
 * eta_minutes in gfx_route_phase, so they rescale automatically). */
#define RA_ETA_MIN 30
#define RA_ETA_MAX 599

/* ── Seed-token sequence ──────────────────────────────────────────────────────
 * An LCG over dest_rng yields an endless, varied run of designation tokens of the
 * form "{PREFIX}-{serial}". The token now IS the displayed designation: it seeds
 * the system (djb2 → world), its prefix pins the destination body's type, and it
 * is echoed verbatim into route.designation (see starmap_build). The prefix is
 * weighted 16/32/32/20 (LV the rare jackpot, RF the minority) and the serial bands
 * match the catalog; high bits decorrelate the serial from the prefix so
 * successive journeys vary. Longest output "RF-9999" is 7 chars + NUL ≤ 12. */
static void ra_next_seed_token(route_journey_t *j, char out[12]) {
    j->dest_rng = j->dest_rng * 1664525u + 1013904223u;
    uint32_t s = j->dest_rng, hi = s >> 8;     /* hi decorrelates serial from prefix */
    uint32_t b = s % 25u;                      /* 16/32/32/20 split */
    const char *pfx; uint32_t serial;
    if      (b < 4u)  { pfx = "LV"; serial = 100u  + hi % 1200u; }   /* 100–1299 */
    else if (b < 12u) { pfx = "KG"; serial = 100u  + hi % 900u;  }   /* 100–999  */
    else if (b < 20u) { pfx = "BG"; serial = 100u  + hi % 900u;  }   /* 100–999  */
    else              { pfx = "RF"; serial = 1000u + hi % 9000u; }   /* 1000–9999 */

    int n = 0;
    out[n++] = pfx[0];
    out[n++] = pfx[1];
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

/* ── Journey lifecycle ────────────────────────────────────────────────────────── */

/* Stamp a lifecycle FX event — the single attach point future animations read. */
static void ra_fire_fx(route_journey_t *j, uint8_t ev) {
    j->fx_event    = ev;
    j->fx_start_ms = timer_read32();
}

/* Plot (and bake) a route, held in MISSION CONTROL at departure. token==NULL draws
 * the next seed-sequence token; otherwise the supplied token is used verbatim. */
static void ra_plot(route_journey_t *j, const char *token) {
    char buf[12];
    const char *tok = token;
    if (!tok) {
        ra_next_seed_token(j, buf);
        tok = buf;
    }
    route_gen_build(tok, &j->route);
    gfx_route_bake_bg(&j->route, j->bg_cache);
    j->duration_ms = (uint32_t)j->route.eta_minutes * 60000u;
    if (j->duration_ms == 0) j->duration_ms = RA_DEFAULT_DURATION_MS;
    j->cur_t  = 0.0f;
    j->active = false;
    j->state  = RA_CONTROL;
    ra_fire_fx(j, RA_FX_PLOT);
}

/* Launch the held route — MISSION CONTROL → MID-MISSION. */
static void ra_launch(route_journey_t *j) {
    j->start_ms = timer_read32();
    j->active   = true;
    j->state    = RA_MISSION;
    ra_fire_fx(j, RA_FX_LAUNCH);
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
    ra_plot(j, NULL);   /* boot into MISSION CONTROL, held — no auto-run */
}

/* ── Per-frame text overlays ──────────────────────────────────────────────────── */

/* Redraw the strip's per-frame ETA + STATUS value lines for journey fraction t. The
 * ETA counts down and STATUS tracks the live phase, so neither can bake into
 * bg_cache; positions derive from banner_x/banner_w with the same pitch the bake
 * used, so they land exactly on the value columns — only present when the strip
 * placed those fields. */
static void ra_draw_strip(const route_journey_t *j, float t) {
    uint8_t bx = j->route.banner_x, bw = j->route.banner_w;
    int n     = gfx_tel_nfields(bw);
    int s_eta = gfx_tel_field_slot(n, 2);          /* ETA slot in this count's order, or -1 */
    int s_sta = gfx_tel_field_slot(n, 3);          /* STATUS slot, or -1                    */
    if (s_eta >= 0) {
        uint16_t rem = (uint16_t)((float)j->route.eta_minutes * (1.0f - t) + 0.5f);
        char eta[8];                               /* "HHHMM" → e.g. "01H30" */
        eta[0] = (char)('0' + (rem / 600) % 10); eta[1] = (char)('0' + (rem / 60) % 10); eta[2] = 'H';
        eta[3] = (char)('0' + (rem % 60) / 10);  eta[4] = (char)('0' + (rem % 60) % 10); eta[5] = '\0';
        gfx_tel_draw_line(bx, bw, 2 * s_eta + 1, eta);
    }
    if (s_sta >= 0) {
        uint8_t ph = (uint8_t)gfx_route_phase(&j->route, t, NULL);
        gfx_tel_draw_line(bx, bw, 2 * s_sta + 1, gfx_phase_word(ph));
    }
}

/* "ETA 03H30" — the held duration, drawn alongside the MISSION CONTROL title so the
 * trim is readable even on routes with no telemetry strip. */
static void ra_draw_eta_label(const route_journey_t *j, int16_t x, int16_t y) {
    uint16_t m = j->route.eta_minutes;
    char line[12];
    int p = 0;
    line[p++] = 'E'; line[p++] = 'T'; line[p++] = 'A'; line[p++] = ' ';
    line[p++] = (char)('0' + (m / 600) % 10);
    line[p++] = (char)('0' + (m / 60) % 10);
    line[p++] = 'H';
    line[p++] = (char)('0' + (m % 60) / 10);
    line[p++] = (char)('0' + (m % 60) % 10);
    line[p]   = '\0';
    gfx_prim_text(x, y, line, 1);
}

/* The token-entry buffer with a ~1 Hz blinking cursor, e.g. "> LV-426_". */
static void ra_draw_token_buffer(const route_journey_t *j, int16_t x, int16_t y) {
    char line[12];
    int n = 0;
    line[n++] = '>';
    line[n++] = ' ';
    for (uint8_t i = 0; i < j->tok_len && n < 9; i++) line[n++] = j->tok[i];
    if ((timer_read32() / 500u) & 1u) line[n++] = '_';   /* blink */
    line[n] = '\0';
    gfx_prim_text(x, y, line, 1);
}

void route_anim_render_map(route_journey_t *j) {
    float t;
    if (j->state == RA_MISSION) {
        uint32_t elapsed = timer_elapsed32(j->start_ms);
        if (elapsed >= j->duration_ms) {
            /* Arrival: hold in MISSION COMPLETE — do NOT re-plot. */
            j->state  = RA_COMPLETE;
            j->active = false;
            j->cur_t  = 1.0f;
            t = 1.0f;
            ra_fire_fx(j, RA_FX_ARRIVE);
        } else {
            t = (float)elapsed / (float)j->duration_ms;
            if (t > 1.0f) t = 1.0f;
            j->cur_t = t;
        }
    } else if (j->state == RA_COMPLETE) {
        t = 1.0f;
        j->cur_t = 1.0f;
    } else {                          /* RA_CONTROL — held at departure */
        t = 0.0f;
        j->cur_t = 0.0f;
    }

    oled_set_cursor(0, 0);
    oled_write_raw((const char *)j->bg_cache, 512);

    /* The bg re-blit cleared the prior frame's dynamic strip values; redraw them. */
    ra_draw_strip(j, t);

    /* Per-state title overlay (the state-machine hook surface — the reveal/flourish
     * transition animations are out of scope and attach later via fx_event). */
    if (j->state == RA_CONTROL) {
        gfx_prim_text(1, 1, "MISSION CONTROL", 1);
        ra_draw_eta_label(j, 1, 8);
        if (j->tok_entry) ra_draw_token_buffer(j, 1, 16);
    } else if (j->state == RA_COMPLETE) {
        gfx_prim_text(1, 1, j->route.is_colony ? "DOCKED" : "LANDED", 1);
        gfx_prim_text(1, 8, j->route.designation, 1);
    }

    gfx_route_draw_ship(&j->route, t, gfx_pulse_size(timer_read32()));
}

void route_anim_reroll(route_journey_t *j) {
    /* New random system, held in MISSION CONTROL. Control-only — the re-roll key
     * lives on the Mission Control layer. */
    if (j->state != RA_CONTROL) return;
    ra_plot(j, NULL);
    ra_fire_fx(j, RA_FX_REROLL);
}

void route_anim_launch(route_journey_t *j) {
    if (j->state == RA_CONTROL) ra_launch(j);
}

void route_anim_back(route_journey_t *j) {
    if (j->state == RA_COMPLETE) ra_plot(j, NULL);   /* new mission → control */
}

void route_anim_adjust_eta(route_journey_t *j, int delta_min) {
    int eta = (int)j->route.eta_minutes + delta_min;
    if (eta < RA_ETA_MIN) eta = RA_ETA_MIN;
    if (eta > RA_ETA_MAX) eta = RA_ETA_MAX;
    j->route.eta_minutes = (uint16_t)eta;
    uint32_t new_dur = (uint32_t)eta * 60000u;
    /* While flying, re-anchor the start clock so the progress fraction is preserved
     * (no jump). gfx_route_phase reads eta_minutes live, so the burns rescale. */
    if (j->state == RA_MISSION) {
        j->start_ms = timer_read32() - (uint32_t)(j->cur_t * (float)new_dur);
    }
    j->duration_ms = new_dur;
}

void route_anim_set_token(route_journey_t *j, const char *tok) {
    ra_plot(j, tok);   /* freeform entry confirm → held in MISSION CONTROL */
}

void route_anim_token_key(route_journey_t *j, uint16_t kc) {
    char c = 0;
    if (kc >= KC_A && kc <= KC_Z) {
        c = (char)('A' + (kc - KC_A));            /* uppercase letter */
    } else if (kc >= KC_1 && kc <= KC_9) {
        c = (char)('1' + (kc - KC_1));            /* 1–9 */
    } else if (kc == KC_0) {
        c = '0';
    } else if (kc == KC_BSPC) {
        if (j->tok_len > 0) j->tok[--j->tok_len] = '\0';
        return;
    } else if (kc == KC_ENT) {
        j->tok[j->tok_len] = '\0';
        route_anim_set_token(j, j->tok);
        j->tok_entry = false;
        return;
    } else if (kc == KC_ESC) {
        j->tok_entry = false;
        j->tok_len   = 0;
        j->tok[0]    = '\0';
        return;
    } else {
        return;                                   /* ignore unmapped keys */
    }
    /* Append, auto-inserting the dash after the 2-char prefix. Cap at 7 chars
     * ("RF-9999"), leaving tok[7] for the NUL. */
    if (j->tok_len == 2 && j->tok_len < 7) j->tok[j->tok_len++] = '-';
    if (j->tok_len < 7) {
        j->tok[j->tok_len++] = c;
        j->tok[j->tok_len]   = '\0';
    }
}

void route_anim_fill_telemetry(const route_journey_t *j, route_telemetry_t *out) {
    memcpy(out->system_name, j->route.system_name, sizeof(out->system_name));
    memcpy(out->designation, j->route.designation, sizeof(out->designation));
    float remaining = (float)j->route.eta_minutes * (1.0f - j->cur_t);
    out->eta_remaining_min = (uint16_t)(remaining + 0.5f);
    bool burn = false;
    out->phase = (uint8_t)gfx_route_phase(&j->route, j->cur_t, &burn);
    /* Burns only fire mid-mission: at t=0 (control) and t=1 (complete) the phase
     * windows would otherwise read as a powered burn and trip the slave takeover. */
    out->burn      = (j->state == RA_MISSION && burn) ? 1u : 0u;
    out->gaming    = 0;
    out->state     = j->state;
    out->is_colony = j->route.is_colony;
}

/* ── Host-only struct-size probes (ctypes layout parity, like gfx_route_sizeof) ─ */
#ifdef RG_HOST
uint32_t route_journey_sizeof(void)   { return (uint32_t)sizeof(route_journey_t); }
uint32_t route_telemetry_sizeof(void) { return (uint32_t)sizeof(route_telemetry_t); }
#endif
