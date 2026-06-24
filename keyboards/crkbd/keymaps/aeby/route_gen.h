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
#endif
