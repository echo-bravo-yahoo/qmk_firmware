/*
 * route_gen — plan a Patna journey for a seed token into a gfx_route_t the
 * renderer can bake. The token seeds the world; the destination's designation is
 * class-derived (see starmap_designation).
 *
 * Pipeline (single source of truth — starmap_build):
 *   1. starmap_build(token)                   → deterministic system + bodies
 *   2. plan a waypoint chain through real placements (Lagrange points, orbital
 *      nodes) with typed legs (transfer / coast), by topology
 *   3. rotate so departure→destination lies along +x, then bounding-box fit
 *   4. pack display-space geometry + telemetry into gfx_route_t
 *
 * route_gen_describe() dumps the same plan for eyeballing placement; it pulls
 * in printf/stdio and is compiled only for the host preview (RG_HOST), never
 * the firmware.
 */
#pragma once
#include "oled_gfx.h"

void route_gen_build(const char *designation, gfx_route_t *out);

#ifdef RG_HOST
void route_gen_describe(const char *designation);

/* Structured itinerary facts for route_explain.py — the same "re-run rg_plan"
 * shape as route_gen_describe, but returning data instead of printing. The host
 * CLI pairs this (topology, ETA, the named pivot + its type, the flyby L-selector)
 * with the identity it pulls from route_gen_build. */
typedef struct {
    uint8_t  topology;          /* RG_DIRECT / RG_FLYBY / RG_COAST */
    uint16_t eta_minutes;
    uint8_t  leg_count;
    uint8_t  depart_type, dest_type, pivot_type;  /* STARMAP_* ; pivot_type unused if no pivot */
    int8_t   flyby_lagrange;    /* 0=L1, 1=L2 for flyby; -1 otherwise */
    char     pivot[8];          /* pivot body designation, "" if none */
} route_explain_t;
void route_gen_explain(const char *token, route_explain_t *out);
#endif
