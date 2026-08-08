/*
 * oled_gfx — 128×32 monochrome primitives + the route render struct.
 *
 * gfx_route_t is the wire format between route_gen (which plans a journey) and
 * the renderer (which bakes a static background once, then animates the ship).
 * All coordinates are display pixels: x∈[0,128), y∈[0,32), origin top-left.
 *
 * Backgrounds are baked once per journey into a 512-byte SSD1306 page buffer;
 * only the ship moves per frame, so per-frame cost is one point eval + a small
 * crosshair. The arc rasterizer is integer midpoint-circle (no per-scanline
 * sqrtf) — see oled_gfx.c.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include QMK_KEYBOARD_H

typedef struct { int16_t x, y; } gfx_pt_t;

/* Orbital ring (full circle). Dashed unless solid. local_center overrides the
 * route's shared heliocentric center (arc_cx/arc_cy) — used for moon orbits. */
typedef struct {
    uint8_t radius;
    bool    solid;
    uint8_t gap_px;        /* 0 → use route gap_px */
    bool    local_center;
    int16_t cx, cy;
} gfx_arc_t;

/* A route leg pivots through real placements rather than tweening:
 *  - TRANSFER: quadratic Bézier p0→ctrl→p1, tangent to both orbits.
 *  - COAST:    a true circular-orbit arc segment, center (cx,cy) radius arc_r,
 *              starting at angle a0 and sweeping a_sweep radians (signed). */
typedef enum { GFX_LEG_TRANSFER = 0, GFX_LEG_COAST = 1 } gfx_leg_type_t;

typedef struct {
    uint8_t type;          /* gfx_leg_type_t */
    int16_t p0x, p0y;
    int16_t p1x, p1y;
    int16_t cx, cy;        /* TRANSFER: Bézier control; COAST: arc center */
    uint8_t arc_r;         /* COAST radius (px) */
    float   a0;            /* COAST start angle (rad, display space) */
    float   a_sweep;       /* COAST signed sweep (rad) */
    float   len;           /* leg length (px) — for cumulative ship traversal */
} gfx_leg_t;

/* TARGET appended last so existing values stay stable for the ctypes bridge. */
typedef enum {
    GFX_MARKER_RING, GFX_MARKER_CROSS, GFX_MARKER_BODY, GFX_MARKER_TARGET
} gfx_marker_type_t;
typedef struct { int16_t x, y; gfx_marker_type_t type; } gfx_marker_t;

typedef struct {
    int16_t      arc_cx, arc_cy;    /* shared heliocentric center (the star) */
    uint8_t      arc_count;
    gfx_arc_t    arcs[8];
    uint8_t      dash_px, gap_px;

    uint8_t      leg_count;
    gfx_leg_t    legs[4];

    uint8_t      marker_count;
    gfx_marker_t markers[4];

    uint8_t      body_count;
    gfx_pt_t     bodies[6];

    int8_t       ship_offset_x;     /* nudge so the crosshair leads cleanly */
    uint16_t     eta_minutes;       /* nominal journey duration (telemetry)  */

    char         system_name[12];   /* proper system name, e.g. "CALPAMOS"   */
    char         designation[8];    /* destination, e.g. "LV-426"            */

    /* Telemetry strip: when best-fit framing leaves long-axis room, route_gen centers
     * the map on the long axis and reserves [banner_x, banner_x+banner_w) at the high-x
     * end for a stacked label/value strip (DST/ORG/ETA/STATUS/SYS). banner_x/banner_w
     * name that strip; dest_class is still computed (tests/struct parity) but no longer
     * drawn. Fields appended last so the ctypes bridge stays stable (gfx_route_sizeof
     * asserts parity). */
    char         dest_class[4];     /* destination spectral class, e.g. "D4" / "III" (computed, not drawn) */
    uint8_t      banner_x;          /* strip left x (0 ⇒ no strip) */
    uint8_t      banner_w;          /* strip width  (0 ⇒ no strip) */
    char         origin[8];         /* ORG value: departure-body designation (derived) */
    uint8_t      is_colony;         /* destination colony status (1 ⇒ DOCKED, 0 ⇒ LANDED); see starmap_is_colony */
} gfx_route_t;

/* ── Primitives ─────────────────────────────────────────────────────────────── */
void gfx_prim_pixel(int16_t x, int16_t y, bool on);
void gfx_prim_arc(int16_t cx, int16_t cy, uint8_t r, bool solid,
                  uint8_t dash_px, uint8_t gap_px);
void gfx_prim_arc_segment(int16_t cx, int16_t cy, uint8_t r, float a0, float a_sweep);
void gfx_prim_bezier(int16_t p0x, int16_t p0y, int16_t cx, int16_t cy,
                     int16_t p1x, int16_t p1y);
void gfx_prim_bezier_point(int16_t p0x, int16_t p0y, int16_t cx, int16_t cy,
                           int16_t p1x, int16_t p1y, float t, int16_t *ox, int16_t *oy);
void gfx_prim_ring(int16_t x, int16_t y);
void gfx_prim_cross(int16_t x, int16_t y);
void gfx_prim_body(int16_t x, int16_t y);
/* Destination reticle: the 3×3 body icon framed by [ ] brackets. */
void gfx_prim_reticle(int16_t x, int16_t y);
/* size = outer arm offset (px); the crosshair pulses by varying it per frame. */
void gfx_prim_crosshair(int16_t x, int16_t y, uint8_t size);

/* Tom Thumb (3×5, 4px advance, 6px line) text into the current render target.
 * gfx_prim_text draws left→right from the top-left (x,y); gfx_prim_text_vertical
 * draws a 90°-rotated column reading bottom→top, anchored at bottom-left
 * (x, y_bottom). scale ≥ 1 multiplies every cell. Glyph bytes come from the shared
 * Tom Thumb table (gfx_tomthumb_font), not a duplicate. */
void gfx_prim_text(int16_t x, int16_t y, const char *s, uint8_t scale);
void gfx_prim_text_vertical(int16_t x, int16_t y_bottom, const char *s, uint8_t scale);

/* Tom Thumb cell geometry. Shared (not private to oled_gfx.c) so route_gen and
 * route_anim can size the telemetry strip against the same 3×5 font the renderer
 * draws — one rotated text column is GFX_TT_ROWS px wide on the long axis. */
#define GFX_TT_FIRST  0x20
#define GFX_TT_LAST   0x7E
#define GFX_TT_COLS   3
#define GFX_TT_ROWS   5
#define GFX_TT_ADV    4   /* per-char step (3 cols + 1 spacing) */

/* ── Route rendering ────────────────────────────────────────────────────────── */
void gfx_route_draw_bg(const gfx_route_t *route);
void gfx_route_bake_bg(const gfx_route_t *route, uint8_t buf[512]);
/* t∈[0,1] over the *whole* route; pulse = crosshair arm size (px). */
void gfx_route_draw_ship(const gfx_route_t *route, float t, uint8_t pulse);
void gfx_route_bake_ship(const gfx_route_t *route, float t, uint8_t pulse, uint8_t buf[512]);
/* Evaluate the ship position (x,y, after ship_offset_x) at journey t. */
void gfx_route_ship_pos(const gfx_route_t *route, float t, int16_t *ox, int16_t *oy);
/* Crosshair pulse size from a free-running millisecond clock (~1 Hz triangle). */
uint8_t gfx_pulse_size(uint32_t now_ms);

/* sizeof(gfx_route_t) — lets the host ctypes bridge assert struct-layout parity. */
uint32_t gfx_route_sizeof(void);

/* ── Telemetry strip ──────────────────────────────────────────────────────────
 * route_gen reserves a strip [banner_x, banner_x+banner_w) flush to the long axis's
 * high-x end, guaranteeing a GFX_TEL_MAP_GAP breather between the map and the strip.
 * The strip stacks up to GFX_TEL_NFIELDS fields (DST, ORG, ETA, STATUS, SYS), each two
 * rotated text lines — a label above a value. A "line" is one font column, GFX_TT_ROWS
 * px wide on the long axis; lines are GFX_TEL_GAP apart, GFX_TEL_PITCH per line. Text is
 * left-aligned on the short axis; labels render inverse (filled chip, text punched out).
 * The bake draws every label plus the static (DST/ORG/SYS) values; route_anim redraws
 * the dynamic ETA and STATUS values per frame. Both derive each line's long-axis
 * position from banner_x/banner_w with these helpers, so they agree without storing
 * coordinates. */
#define GFX_TEL_GAP      2                          /* px between lines on the long axis */
#define GFX_TEL_NFIELDS  5                          /* DST, ORG, ETA, STATUS, SYS        */
#define GFX_TEL_PITCH    (GFX_TT_ROWS + GFX_TEL_GAP)   /* 7 px per line                  */
#define GFX_TEL_MAP_GAP  (2 * GFX_TEL_PITCH)        /* guaranteed map↔strip breather (2 lines) */
#define GFX_TEL_CHIP_PAD 1                          /* green border around inverse label text; also
                                                       reserved at the strip's high-x end so the top
                                                       label's border isn't clipped off-panel */

/* Fields a strip of width bw holds (capped at GFX_TEL_NFIELDS); the set + top→bottom
 * order for that count come from the per-count table in oled_gfx.c. */
int  gfx_tel_nfields(uint8_t bw);
/* Draw text line `line` (0 = top = high-x) of strip [bx, bx+bw) into the render target. */
void gfx_tel_draw_line(uint8_t bx, uint8_t bw, int line, const char *s);
/* Top→bottom slot of field `id` in an n-field strip, or -1 if absent. Shared so the
 * per-frame overlay can locate the ETA/STATUS value lines without recomputing order. */
int  gfx_tel_field_slot(int n, int id);
/* STATUS phase word for gfx_phase_t (moved here from keymap.c so master + slave share). */
const char *gfx_phase_word(uint8_t phase);

/* ── Journey phase + burn warning ─────────────────────────────────────────────
 * The journey position t∈[0,1] (walked over the legs by cumulative arc length,
 * exactly as gfx_route_ship_pos does) derives two telemetry channels:
 *   • a continuous *phase* word for the STATUS line, and
 *   • a momentary *burn* flag for the powered maneuvers.
 *
 * Burns are physically motivated and *not* free: departure injection and arrival
 * capture (the big ones), a mid-course correction (MCC) trim at each transfer
 * leg's midpoint, and coast insertion / ejection trims at a transfer↔coast
 * boundary. A transfer↔transfer boundary is a gravity-assist flyby — unpowered,
 * so no burn there. Burns per flight: direct 3, flyby 4, coast 6.
 *
 * Each node's window is a real burn duration converted to a t-span via the
 * route's eta (duration_ms). Departure/arrival are one-sided ([0,D] / [1−D,1]);
 * interior trims are centred on their fraction. Tunable in one line below. */
#define BURN_MAJOR_MS    240000u   /* departure injection / arrival capture: 4 min */
#define BURN_TRIM_MS      90000u   /* coast insert/eject + MCC trims:        90 s  */
#define FLYBY_LABEL_MS    90000u   /* FLYBY phase label window (no burn):    90 s  */
#define BURN_FALLBACK_DURATION_MS 10800000u  /* if eta is 0 (matches route_anim) */

typedef enum {
    GFX_PHASE_DEPART  = 0,   /* leaving the departure body (injection burn)   */
    GFX_PHASE_TRANSIT = 1,   /* cruising a transfer leg                       */
    GFX_PHASE_FLYBY   = 2,   /* gravity-assist junction between transfer legs */
    GFX_PHASE_COAST   = 3,   /* riding a Lagrange coast leg                   */
    GFX_PHASE_ARRIVE  = 4,   /* capturing at the destination                  */
} gfx_phase_t;

/* Phase word + burn flag at journey t. Shares the leg-walk gfx_route_ship_pos
 * uses; reads windows from route->eta_minutes. burn_out may be NULL. */
gfx_phase_t gfx_route_phase(const gfx_route_t *route, float t, bool *burn_out);

/* Fill a 512-byte buffer with the full-panel BURN warning: dark-on-light, big
 * bespoke glyphs re-oriented to landscape on the slave's portrait OLED. The
 * buffer is in the slave's OLED_ROTATION_270 logical page format (32 wide × 128
 * tall, stride 32) so oled_write_raw + the driver's render-time rotate land it
 * upright-landscape. now_ms drives the marching hazard stripes. */
void gfx_burn_warning(uint8_t buf[512], uint32_t now_ms);
