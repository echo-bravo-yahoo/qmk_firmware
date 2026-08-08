/*
 * route_anim — on-device journey state machine for the left-OLED transit map.
 *
 * The master half owns a journey driven through a player-controlled three-state
 * loop (no auto-advance):
 *
 *   MISSION CONTROL → (LAUNCH) → MID-MISSION → (arrival) → MISSION COMPLETE
 *        ▲                                                        │
 *        └──────────────────── (BACK: new mission) ──────────────┘
 *
 *   • MISSION CONTROL (RA_CONTROL) — a route is plotted and baked, the ship held at
 *     departure (t=0). Re-roll for a new random system, trim the ETA, or type a
 *     freeform designation. Nothing moves until LAUNCH.
 *   • MID-MISSION (RA_MISSION) — the ship crawls the whole route as a real-time
 *     progress bar (hours), a per-frame crosshair pulse keeping the 20 Hz panel
 *     alive. On arrival the journey HOLDS in MISSION COMPLETE — it does NOT regen.
 *   • MISSION COMPLETE (RA_COMPLETE) — the ship sits on the destination, which reads
 *     DOCKED (a colony) or LANDED (unpopulated). BACK plots a fresh mission.
 *
 * Each edge fires a named lifecycle FX event (ra_fire_fx) with a timestamp — the
 * single attach point the (out-of-scope) transition animations will read.
 *
 * Telemetry is data-driven from the active route and pushed to the slave half over a
 * split RPC (the slave's OLED can't see the master's RAM), carrying the journey
 * state + colony status so the right OLED matches the live loop.
 */
#pragma once
#include "oled_gfx.h"

/* Journey state — the three-state mission loop. RA_CONTROL is the boot/default. */
enum {
    RA_CONTROL  = 0,   /* Mission Control — plotted, held at departure */
    RA_MISSION  = 1,   /* Mid-mission — the ship is flying             */
    RA_COMPLETE = 2,   /* Mission Complete — docked/landed at the dest */
};

/* Lifecycle FX hook events — the single attach point future transition animations
 * read. ra_fire_fx stamps fx_event + fx_start_ms; nothing consumes them yet. */
enum {
    RA_FX_NONE   = 0,
    RA_FX_PLOT   = 1,   /* a new route was plotted (held in control)   */
    RA_FX_LAUNCH = 2,   /* the mission launched (control → mission)    */
    RA_FX_ARRIVE = 3,   /* the ship arrived (mission → complete)       */
    RA_FX_REROLL = 4,   /* re-plotted to a fresh random system         */
};

/* Compact snapshot synced master→slave each housekeeping tick. */
typedef struct {
    char     system_name[8];   /* not NUL-guaranteed; render exactly 8 cols */
    char     designation[8];
    uint16_t eta_remaining_min;
    uint8_t  phase;            /* gfx_phase_t — drives the STATUS line */
    uint8_t  burn;             /* 1 while a powered burn overlays the panel */
    uint8_t  gaming;           /* highest layer is a gaming layer */
    uint8_t  state;            /* RA_CONTROL / RA_MISSION / RA_COMPLETE */
    uint8_t  is_colony;        /* destination colony status (DOCKED vs LANDED) */
} route_telemetry_t;

typedef struct {
    gfx_route_t route;
    uint8_t     bg_cache[512];
    uint32_t    start_ms;
    uint32_t    duration_ms;
    uint32_t    dest_rng;
    float       cur_t;         /* journey fraction at the last render */
    bool        active;        /* 1 while the ship is flying (state == RA_MISSION) */
    /* Three-state mission loop (append-only — keeps the ctypes mirror stable). */
    uint8_t     state;         /* RA_CONTROL / RA_MISSION / RA_COMPLETE */
    uint8_t     fx_event;      /* last RA_FX_* fired (animation hook)    */
    uint32_t    fx_start_ms;   /* timestamp the last fx fired           */
    char        tok[8];        /* freeform token-entry buffer ("KG-9999" + NUL) */
    uint8_t     tok_len;       /* chars buffered in tok                  */
    bool        tok_entry;     /* token-entry sub-mode active            */
} route_journey_t;

/* Master: seed the destination sequence and plot the opening route — boots into
 * MISSION CONTROL, held (no auto-run). */
void route_anim_init(route_journey_t *j);

/* Master, per frame: blit the cached background, draw the pulsing ship, and overlay
 * the per-state text. In MID-MISSION it advances the clock and, on arrival, holds in
 * MISSION COMPLETE (no regen). */
void route_anim_render_map(route_journey_t *j);

/* Master: snapshot the route-derived telemetry incl. state + colony (caller sets
 * .gaming). */
void route_anim_fill_telemetry(const route_journey_t *j, route_telemetry_t *out);

/* Master: re-plot a fresh random system, held in MISSION CONTROL (the re-roll key).
 * No-op outside MISSION CONTROL. */
void route_anim_reroll(route_journey_t *j);

/* Master: launch the held route — MISSION CONTROL → MID-MISSION. No-op otherwise. */
void route_anim_launch(route_journey_t *j);

/* Master: from MISSION COMPLETE, plot a fresh mission (→ MISSION CONTROL). No-op
 * otherwise. */
void route_anim_back(route_journey_t *j);

/* Master: trim the journey duration by `delta_min` minutes, clamped to [30, 599].
 * Burns rescale automatically (sized off eta_minutes). While flying, the start clock
 * is re-anchored so the current progress fraction does not jump. */
void route_anim_adjust_eta(route_journey_t *j, int delta_min);

/* Master: plot the route for an explicit designation token, held in MISSION CONTROL
 * (the freeform token-entry confirm). */
void route_anim_set_token(route_journey_t *j, const char *tok);

/* Master: feed one keystroke to the token-entry buffer editor. A–Z/0–9 append
 * (uppercase, a dash auto-inserted after the 2-char prefix, capped at 7 chars);
 * KC_BSPC deletes; KC_ENT confirms (→ set_token, leaves entry); KC_ESC cancels. */
void route_anim_token_key(route_journey_t *j, uint16_t kc);
