/*
 * route_anim — on-device journey state machine for the left-OLED transit map.
 *
 * The master half owns a journey: it picks a seed token, builds and bakes the
 * route background once (the destination's class-derived designation comes back in
 * the route), then animates the Patna along the *whole*
 * route as a real-time progress bar (hours), with a per-frame crosshair pulse so
 * the 20 Hz panel stays alive while the ship crawls. On arrival it generates the
 * next destination — endless.
 *
 * Telemetry is data-driven from the active route and pushed to the slave half
 * over a split RPC (the slave's OLED can't see the master's RAM), so the right
 * OLED always matches the live journey.
 */
#pragma once
#include "oled_gfx.h"

/* Compact snapshot synced master→slave each housekeeping tick. */
typedef struct {
    char     system_name[8];   /* not NUL-guaranteed; render exactly 8 cols */
    char     designation[8];
    uint16_t eta_remaining_min;
    uint8_t  phase;            /* gfx_phase_t — drives the STATUS line */
    uint8_t  burn;             /* 1 while a powered burn overlays the panel */
    uint8_t  gaming;           /* highest layer ≥ gaming threshold */
} route_telemetry_t;

typedef struct {
    gfx_route_t route;
    uint8_t     bg_cache[512];
    uint32_t    start_ms;
    uint32_t    duration_ms;
    uint32_t    dest_rng;
    float       cur_t;         /* journey fraction at the last render */
    bool        active;
} route_journey_t;

/* Master: seed the destination sequence and start the first journey. */
void route_anim_init(route_journey_t *j);

/* Master, per frame: advance the clock, blit the cached background, draw the
 * pulsing ship; regenerate the journey on arrival. */
void route_anim_render_map(route_journey_t *j);

/* Master: snapshot the route-derived telemetry (caller sets .gaming). */
void route_anim_fill_telemetry(const route_journey_t *j, route_telemetry_t *out);

/* Master: abandon the current journey and jump to the next system immediately —
 * the same transition as an on-arrival regen, but on demand (e.g. a re-roll key). */
void route_anim_reroll(route_journey_t *j);
