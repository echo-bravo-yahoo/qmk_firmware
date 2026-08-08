#include "oled_gfx.h"
#include <string.h>
#include <math.h>

#define GFX_PI       3.14159265358979323846f
#define GFX_HALF_PI  1.57079632679489661923f
#define GFX_SQRT2    1.41421356237309504880f

/* When non-NULL, primitives write into this 512-byte SSD1306 page buffer
 * instead of the live panel — used to bake a static background once. */
static uint8_t *gfx_render_target = NULL;

void gfx_prim_pixel(int16_t x, int16_t y, bool on) {
    if (x < 0 || x >= 128 || y < 0 || y >= 32) return;
    if (gfx_render_target) {
        uint16_t idx = (uint16_t)(y / 8) * 128 + (uint16_t)x;
        if (on) gfx_render_target[idx] |=  (1u << (y % 8));
        else    gfx_render_target[idx] &= ~(1u << (y % 8));
    } else {
        oled_write_pixel((uint8_t)x, (uint8_t)y, on);
    }
}

/* ── Arc: integer midpoint circle with even arc-length dashing ───────────────
 * The old rasterizer scanned by y and solved x = cx ± √(r²−dy²); near the top
 * and bottom of a circle x jumps many pixels per y-step, so rings rendered as
 * broken vertical dashes. This walks all eight octants by integer steps (one
 * pixel apart), so the circle is continuous, and accumulates true arc length so
 * dashes are evenly spaced around the whole ring. No per-pixel sqrtf — the only
 * chord lengths are 1 or √2. */
static void gfx_plot_dashed(int16_t x, int16_t y, float arclen,
                            bool solid, uint8_t period, uint8_t dash_px) {
    if (!solid) {
        uint32_t a = (uint32_t)(arclen + 0.5f);
        if ((a % period) >= dash_px) return;
    }
    gfx_prim_pixel(x, y, true);
}

void gfx_prim_arc(int16_t cx, int16_t cy, uint8_t r, bool solid,
                  uint8_t dash_px, uint8_t gap_px) {
    if (r == 0) { gfx_prim_pixel(cx, cy, true); return; }
    uint8_t period = (uint8_t)(dash_px + gap_px);
    if (period == 0) period = 1;

    const float Q = (float)r * GFX_HALF_PI;  /* arc length of one quadrant */
    int16_t x = (int16_t)r, y = 0;
    int16_t err = 1 - x;
    float   s  = 0.0f;                        /* arc length along octant 0 */
    int16_t px = x, py = y;
    bool    first = true;

    while (x >= y) {
        if (!first) s += (x != px) ? GFX_SQRT2 : 1.0f;
        first = false;

        /* Eight reflections, each tagged with its absolute arc length k·Q ± s. */
        gfx_plot_dashed((int16_t)(cx + x), (int16_t)(cy + y), s,             solid, period, dash_px);
        gfx_plot_dashed((int16_t)(cx + y), (int16_t)(cy + x), Q - s,         solid, period, dash_px);
        gfx_plot_dashed((int16_t)(cx - y), (int16_t)(cy + x), Q + s,         solid, period, dash_px);
        gfx_plot_dashed((int16_t)(cx - x), (int16_t)(cy + y), 2.0f * Q - s,  solid, period, dash_px);
        gfx_plot_dashed((int16_t)(cx - x), (int16_t)(cy - y), 2.0f * Q + s,  solid, period, dash_px);
        gfx_plot_dashed((int16_t)(cx - y), (int16_t)(cy - x), 3.0f * Q - s,  solid, period, dash_px);
        gfx_plot_dashed((int16_t)(cx + y), (int16_t)(cy - x), 3.0f * Q + s,  solid, period, dash_px);
        gfx_plot_dashed((int16_t)(cx + x), (int16_t)(cy - y), 4.0f * Q - s,  solid, period, dash_px);

        px = x; py = y; (void)py;
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

/* ── Arc segment: a solid circular-orbit arc for coast legs ───────────────────
 * Baked into the static background once per journey, so cosf/sinf per step is
 * fine. Steps ≈ one pixel of arc length. */
void gfx_prim_arc_segment(int16_t cx, int16_t cy, uint8_t r, float a0, float a_sweep) {
    if (r == 0) { gfx_prim_pixel(cx, cy, true); return; }
    int steps = (int)(fabsf(a_sweep) * (float)r + 0.5f);
    if (steps < 1) steps = 1;
    for (int i = 0; i <= steps; i++) {
        float a = a0 + a_sweep * ((float)i / (float)steps);
        int16_t x = (int16_t)(cx + (int16_t)roundf((float)r * cosf(a)));
        int16_t y = (int16_t)(cy + (int16_t)roundf((float)r * sinf(a)));
        gfx_prim_pixel(x, y, true);
    }
}

void gfx_prim_bezier_point(int16_t p0x, int16_t p0y, int16_t cx, int16_t cy,
                           int16_t p1x, int16_t p1y, float t, int16_t *ox, int16_t *oy) {
    float mt = 1.0f - t;
    *ox = (int16_t)(mt*mt*(float)p0x + 2.0f*mt*t*(float)cx + t*t*(float)p1x);
    *oy = (int16_t)(mt*mt*(float)p0y + 2.0f*mt*t*(float)cy + t*t*(float)p1y);
}

void gfx_prim_bezier(int16_t p0x, int16_t p0y, int16_t cx, int16_t cy, int16_t p1x, int16_t p1y) {
    int16_t prev_x, prev_y;
    gfx_prim_bezier_point(p0x, p0y, cx, cy, p1x, p1y, 0.0f, &prev_x, &prev_y);
    gfx_prim_pixel(prev_x, prev_y, true);

    for (int i = 1; i <= 200; i++) {
        float t = (float)i / 200.0f;
        int16_t bx, by;
        gfx_prim_bezier_point(p0x, p0y, cx, cy, p1x, p1y, t, &bx, &by);

        int16_t dy = by - prev_y;
        if (dy < 0) dy = -dy;
        if (dy > 1) {
            // fill y-gap with linear x interpolation
            int16_t sy = (by > prev_y) ? 1 : -1;
            for (int16_t fy = prev_y + sy; fy != by; fy += sy) {
                float frac = (float)(fy - prev_y) / (float)(by - prev_y);
                int16_t fx = prev_x + (int16_t)(frac * (float)(bx - prev_x));
                gfx_prim_pixel(fx, fy, true);
            }
        }
        gfx_prim_pixel(bx, by, true);
        prev_x = bx;
        prev_y = by;
    }
}

// Precomputed r=3 ring offsets at 45° intervals
static const int8_t ring_dx[8] = { 3,  2,  0, -2, -3, -2,  0,  2};
static const int8_t ring_dy[8] = { 0,  2,  3,  2,  0, -2, -3, -2};

void gfx_prim_ring(int16_t x, int16_t y) {
    for (uint8_t i = 0; i < 8; i++) {
        gfx_prim_pixel(x + ring_dx[i], y + ring_dy[i], true);
    }
}

void gfx_prim_cross(int16_t x, int16_t y) {
    for (int16_t d = -3; d <= 3; d++) {
        if (d != 0) {
            gfx_prim_pixel(x + d, y, true);
            gfx_prim_pixel(x, y + d, true);
        }
    }
    gfx_prim_pixel(x, y, true);
}

void gfx_prim_body(int16_t x, int16_t y) {
    // clear 3×3, then draw 8-pixel perimeter
    for (int16_t dy = -1; dy <= 1; dy++) {
        for (int16_t dx = -1; dx <= 1; dx++) {
            gfx_prim_pixel(x + dx, y + dy, false);
        }
    }
    gfx_prim_pixel(x - 1, y - 1, true);
    gfx_prim_pixel(x,     y - 1, true);
    gfx_prim_pixel(x + 1, y - 1, true);
    gfx_prim_pixel(x + 1, y,     true);
    gfx_prim_pixel(x + 1, y + 1, true);
    gfx_prim_pixel(x,     y + 1, true);
    gfx_prim_pixel(x - 1, y + 1, true);
    gfx_prim_pixel(x - 1, y,     true);
}

/* Destination reticle: the 3×3 body icon framed by literal [ ] brackets, two px
 * clear on each side. Each bracket is a 5-px vertical bar (y-2..y+2) with a one-px
 * inward tick at top and bottom, so the whole reads as [▫]. Distinct from the
 * star's dotted ring, a bare body square (departure / other bodies), and the
 * pulsing crosshair (ship). */
void gfx_prim_reticle(int16_t x, int16_t y) {
    gfx_prim_body(x, y);
    for (int16_t dy = -2; dy <= 2; dy++) {
        gfx_prim_pixel(x - 3, y + dy, true);   /* left  bracket bar */
        gfx_prim_pixel(x + 3, y + dy, true);   /* right bracket bar */
    }
    /* inward corner ticks, top and bottom of each bracket */
    gfx_prim_pixel(x - 2, y - 2, true);
    gfx_prim_pixel(x - 2, y + 2, true);
    gfx_prim_pixel(x + 2, y - 2, true);
    gfx_prim_pixel(x + 2, y + 2, true);
}

void gfx_prim_crosshair(int16_t x, int16_t y, uint8_t size) {
    if (size < 1) size = 1;
    /* Two diagonal arms at d = size-1 and d = size; varying size per frame makes
     * the marker expand/contract while the ship itself barely moves. */
    for (int d = (int)size - 1; d <= (int)size; d++) {
        if (d < 1) continue;
        gfx_prim_pixel((int16_t)(x - d), (int16_t)(y - d), true);
        gfx_prim_pixel((int16_t)(x + d), (int16_t)(y + d), true);
        gfx_prim_pixel((int16_t)(x + d), (int16_t)(y - d), true);
        gfx_prim_pixel((int16_t)(x - d), (int16_t)(y + d), true);
    }
    gfx_prim_pixel(x, y, true);
}

/* ── Tom Thumb text (shared glyph table; see glcdfont_tomthumb.c) ──────────────
 * 4 bytes/glyph from 0x20: [col0, col1, col2, pad]; bit b of a column = row b
 * (bit0 = top). 3 cols wide, 5 rows tall, 4px advance, 6px line. */
extern const uint8_t *gfx_tomthumb_font(void);

/* Tom Thumb cell geometry (GFX_TT_*) lives in oled_gfx.h so route_gen / route_anim
 * can size the telemetry strip against the same font. */

void gfx_prim_text(int16_t x, int16_t y, const char *s, uint8_t scale) {
    if (scale < 1) scale = 1;
    const uint8_t *font = gfx_tomthumb_font();
    for (int i = 0; s[i]; i++) {
        uint8_t c = (uint8_t)s[i];
        if (c < GFX_TT_FIRST || c > GFX_TT_LAST) continue;
        const uint8_t *g = &font[(uint16_t)(c - GFX_TT_FIRST) * 4u];
        int16_t cx = (int16_t)(x + i * GFX_TT_ADV * scale);
        for (int col = 0; col < GFX_TT_COLS; col++) {
            uint8_t bits = g[col];
            for (int row = 0; row < GFX_TT_ROWS; row++) {
                if (!((bits >> row) & 1u)) continue;
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++)
                        gfx_prim_pixel((int16_t)(cx + col * scale + sx),
                                       (int16_t)(y  + row * scale + sy), true);
            }
        }
    }
}

/* Vertical column for the left-OLED banner, anchored bottom-left at (x, y_bottom).
 * The left OLED renders at OLED_ROTATION_0 onto a panel whose buffer-left edge is
 * the physical bottom (a 90° turn), so a naive bottom→top column would land upside
 * down. Each pixel is therefore reflected 180° through the column's bounding-box
 * centre, so it reads top→bottom in the buffer and upright on the panel. The anchor
 * and bounding box are unchanged (x = left edge, y_bottom = bottom edge), so the
 * caller's centring still holds. */
/* Core rasterizer; `on` selects the draw color (false erases — used for black text on a
 * filled inverse chip). gfx_prim_text_vertical is the public on=true wrapper. */
static void gfx_prim_vtext(int16_t x, int16_t y_bottom, const char *s, uint8_t scale, bool on) {
    if (scale < 1) scale = 1;
    const uint8_t *font = gfx_tomthumb_font();
    int len = 0; while (s[len]) len++;
    /* Point-reflection sums (xmin+xmax, ymin+ymax) of the nominal column bbox: a lit
     * pixel (px,py) is plotted at (xsum-px, ysum-py) → a 180° turn in place. */
    int16_t xsum = (int16_t)(2 * x + GFX_TT_ROWS * scale - 1);
    int16_t ysum = (int16_t)(2 * y_bottom - (len - 1) * GFX_TT_ADV * scale
                             - (GFX_TT_COLS - 1) * scale - (scale - 1));
    for (int i = 0; s[i]; i++) {
        uint8_t c = (uint8_t)s[i];
        if (c < GFX_TT_FIRST || c > GFX_TT_LAST) continue;
        const uint8_t *g = &font[(uint16_t)(c - GFX_TT_FIRST) * 4u];
        int16_t cbase = (int16_t)(y_bottom - i * GFX_TT_ADV * scale);
        for (int col = 0; col < GFX_TT_COLS; col++) {
            uint8_t bits = g[col];
            for (int row = 0; row < GFX_TT_ROWS; row++) {
                if (!((bits >> row) & 1u)) continue;
                for (int sy = 0; sy < scale; sy++)
                    for (int sx = 0; sx < scale; sx++) {
                        int16_t px = (int16_t)(x     + row * scale + sx);
                        int16_t py = (int16_t)(cbase - col * scale - sy);
                        gfx_prim_pixel((int16_t)(xsum - px), (int16_t)(ysum - py), on);
                    }
            }
        }
    }
}

void gfx_prim_text_vertical(int16_t x, int16_t y_bottom, const char *s, uint8_t scale) {
    gfx_prim_vtext(x, y_bottom, s, scale, true);
}

uint8_t gfx_pulse_size(uint32_t now_ms) {
    /* ~1 Hz triangle between 2 and 4 px, independent of the (hours-long)
     * journey clock so the 20 Hz panel stays visibly alive. */
    uint32_t phase = now_ms % 1000u;
    uint32_t tri   = (phase < 500u) ? phase : (1000u - phase); /* 0..500 */
    return (uint8_t)(2u + (tri * 2u) / 500u);                  /* 2, 3, 4 */
}

/* ── Telemetry strip ──────────────────────────────────────────────────────────
 * route_gen centers the framed map on the long axis and reserves the freed high-x
 * end as a strip [banner_x, banner_x+banner_w). This bakes the strip's STATIC lines:
 * every field's label, plus the DST/ORG/SYS values. The DYNAMIC ETA/STATUS values
 * count down / change live, so route_anim redraws those per frame (route_anim.c) —
 * the bg re-blit clears the prior value each frame. Every line is one rotated font
 * column (GFX_TT_ROWS wide on the long axis); each field stacks two lines, label
 * above value, GFX_TEL_PITCH apart. */

/* Rendered length (px) of a Tom Thumb string at scale, trailing advance gap
 * dropped — the run of cells down a vertical column. */
static int16_t gfx_text_h(const char *s, uint8_t scale) {
    int n = 0; while (s[n]) n++;
    return n ? (int16_t)((n * GFX_TT_ADV - 1) * scale) : 0;
}

/* Long-axis left-x of line index `line` (0 = top = high-x), strip = [bx, bx+bw). */
static int16_t gfx_tel_line_x(uint8_t bx, uint8_t bw, int line) {
    return (int16_t)(bx + bw - GFX_TT_ROWS - line * GFX_TEL_PITCH);
}

int gfx_tel_nfields(uint8_t bw) {
    if (!bw) return 0;
    int lines = ((int)bw + GFX_TEL_GAP) / GFX_TEL_PITCH;
    int f = lines / 2;
    if (f > GFX_TEL_NFIELDS) f = GFX_TEL_NFIELDS;
    return f;
}

/* Short-axis (physical-left) inset for left-aligned strip text. */
#define GFX_TEL_TEXT_Y0  1

/* Left-aligned bottom anchor: the text run occupies short-axis [Y0, Y0+text_h], so every
 * line starts at the same physical-left edge instead of being centered on the 32-px axis. */
static int16_t gfx_tel_text_y(const char *s) {
    return (int16_t)(gfx_text_h(s, 1) + GFX_TEL_TEXT_Y0);
}

void gfx_tel_draw_line(uint8_t bx, uint8_t bw, int line, const char *s) {
    gfx_prim_text_vertical(gfx_tel_line_x(bx, bw, line), gfx_tel_text_y(s), s, 1);
}

/* Inverse label: fill the line's chip — the glyph box grown by GFX_TEL_CHIP_PAD on every
 * side — green, then punch the label out in black, so the field names read as framed tags
 * above their values. */
static void gfx_tel_draw_label(uint8_t bx, uint8_t bw, int line, const char *s) {
    int16_t lx = gfx_tel_line_x(bx, bw, line);
    int16_t h  = gfx_text_h(s, 1);
    int16_t x0 = (int16_t)(lx - GFX_TEL_CHIP_PAD);
    int16_t x1 = (int16_t)(lx + GFX_TT_ROWS - 1 + GFX_TEL_CHIP_PAD);
    int16_t y0 = (int16_t)(GFX_TEL_TEXT_Y0 - GFX_TEL_CHIP_PAD);
    int16_t y1 = (int16_t)(GFX_TEL_TEXT_Y0 + h + GFX_TEL_CHIP_PAD);
    for (int16_t x = x0; x <= x1; x++)
        for (int16_t y = y0; y <= y1; y++)
            gfx_prim_pixel(x, y, true);
    gfx_prim_vtext(lx, gfx_tel_text_y(s), s, 1, false);
}

/* Field ids: 0 DST  1 ORG  2 ETA  3 STATUS  4 SYS. A strip slot i (0 = top = high-x)
 * holds two lines — label at line 2i, value at line 2i+1. Which fields appear, and in
 * what top→bottom order, depends on the field count: a fixed lookup keeping DST the
 * centered anchor (context grows above it, live flight data below it toward the map). */
static const char *const GFX_TEL_LABEL[5] = { "DST", "ORG", "ETA", "STATUS", "SYS" };

/* GFX_TEL_ORDER[n][slot] = field id at top→bottom slot, for an n-field strip (-1 empty). */
static const int8_t GFX_TEL_ORDER[6][5] = {
    { -1, -1, -1, -1, -1 },   /* 0: (no strip)                 */
    {  0, -1, -1, -1, -1 },   /* 1: DST                        */
    {  1,  0, -1, -1, -1 },   /* 2: ORG  DST                   */
    {  1,  0,  2, -1, -1 },   /* 3: ORG  DST  ETA              */
    {  4,  1,  0,  2, -1 },   /* 4: SYS  ORG  DST  ETA         */
    {  4,  1,  0,  2,  3 },   /* 5: SYS  ORG  DST  ETA  STATUS */
};

int gfx_tel_field_slot(int n, int id) {
    if (n < 0 || n > GFX_TEL_NFIELDS) return -1;
    for (int i = 0; i < n; i++) if (GFX_TEL_ORDER[n][i] == id) return i;
    return -1;
}

/* Baked value for a field, or NULL when the value is drawn per-frame (ETA/STATUS). */
static const char *gfx_tel_static_value(const gfx_route_t *r, int id) {
    return id == 0 ? r->designation : id == 1 ? r->origin : id == 4 ? r->system_name : NULL;
}

static void gfx_route_draw_telemetry(const gfx_route_t *r) {
    int n = gfx_tel_nfields(r->banner_w);
    const int8_t *order = GFX_TEL_ORDER[n];
    for (int i = 0; i < n; i++) {
        int id = order[i];
        gfx_tel_draw_label(r->banner_x, r->banner_w, 2 * i, GFX_TEL_LABEL[id]);  /* inverse chip */
        const char *v = gfx_tel_static_value(r, id);
        if (v) gfx_tel_draw_line(r->banner_x, r->banner_w, 2 * i + 1, v);  /* dynamic ids drawn per-frame */
    }
}

/* STATUS phase word for gfx_phase_t — shared by the master strip overlay and the
 * slave telemetry panel (keymap.c's phase_word delegates here). */
const char *gfx_phase_word(uint8_t phase) {
    static const char *const words[] = {
        [GFX_PHASE_DEPART]  = "DEPART",
        [GFX_PHASE_TRANSIT] = "TRANSIT",
        [GFX_PHASE_FLYBY]   = "FLYBY",
        [GFX_PHASE_COAST]   = "COAST",
        [GFX_PHASE_ARRIVE]  = "ARRIVE",
    };
    return (phase <= GFX_PHASE_ARRIVE) ? words[phase] : "TRANSIT";
}

/* ── Route rendering ────────────────────────────────────────────────────────── */
void gfx_route_draw_bg(const gfx_route_t *route) {
    for (uint8_t i = 0; i < route->arc_count; i++) {
        int16_t cx = route->arcs[i].local_center ? route->arcs[i].cx : route->arc_cx;
        int16_t cy = route->arcs[i].local_center ? route->arcs[i].cy : route->arc_cy;
        gfx_prim_arc(cx, cy,
                     route->arcs[i].radius, route->arcs[i].solid,
                     route->dash_px,
                     route->arcs[i].gap_px ? route->arcs[i].gap_px : route->gap_px);
    }

    for (uint8_t i = 0; i < route->leg_count; i++) {
        const gfx_leg_t *l = &route->legs[i];
        if (l->type == GFX_LEG_COAST) {
            gfx_prim_arc_segment(l->cx, l->cy, l->arc_r, l->a0, l->a_sweep);
        } else {
            gfx_prim_bezier(l->p0x, l->p0y, l->cx, l->cy, l->p1x, l->p1y);
        }
    }

    for (uint8_t i = 0; i < route->marker_count; i++) {
        const gfx_marker_t *m = &route->markers[i];
        if (m->type == GFX_MARKER_RING) {
            gfx_prim_ring(m->x, m->y);
        } else if (m->type == GFX_MARKER_CROSS) {
            gfx_prim_cross(m->x, m->y);
        } else if (m->type == GFX_MARKER_TARGET) {
            gfx_prim_reticle(m->x, m->y);
        } else {
            gfx_prim_body(m->x, m->y);
        }
    }

    for (uint8_t i = 0; i < route->body_count; i++) {
        gfx_prim_body(route->bodies[i].x, route->bodies[i].y);
    }

    if (route->banner_w) gfx_route_draw_telemetry(route);
}

void gfx_route_bake_bg(const gfx_route_t *route, uint8_t buf[512]) {
    memset(buf, 0, 512);
    gfx_render_target = buf;
    gfx_route_draw_bg(route);
    gfx_render_target = NULL;
}

/* Point on a single leg at local parameter t∈[0,1]. Endpoints snap to the stored
 * integer p0/p1 so the ship lands exactly on the departure / destination body
 * (coast endpoints don't reconstruct exactly from the rounded radius/angles). */
static void gfx_leg_point(const gfx_leg_t *l, float t, int16_t *x, int16_t *y) {
    if (t <= 0.0f) { *x = l->p0x; *y = l->p0y; return; }
    if (t >= 1.0f) { *x = l->p1x; *y = l->p1y; return; }
    if (l->type == GFX_LEG_COAST) {
        float a = l->a0 + l->a_sweep * t;
        *x = (int16_t)(l->cx + (int16_t)roundf((float)l->arc_r * cosf(a)));
        *y = (int16_t)(l->cy + (int16_t)roundf((float)l->arc_r * sinf(a)));
    } else {
        gfx_prim_bezier_point(l->p0x, l->p0y, l->cx, l->cy, l->p1x, l->p1y, t, x, y);
    }
}

void gfx_route_ship_pos(const gfx_route_t *route, float t, int16_t *ox, int16_t *oy) {
    if (route->leg_count == 0) { *ox = 0; *oy = 0; return; }
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;

    /* Walk all legs by cumulative arc length so the ship traverses the whole
     * route, not just the final leg. */
    float total = 0.0f;
    for (uint8_t i = 0; i < route->leg_count; i++) total += route->legs[i].len;
    if (total <= 0.0f) total = 1.0f;

    float   target = t * total;
    float   acc    = 0.0f;
    uint8_t li     = (uint8_t)(route->leg_count - 1);
    float   lt     = 1.0f;
    for (uint8_t i = 0; i < route->leg_count; i++) {
        float len = route->legs[i].len;
        if (len <= 0.0f) len = 0.0001f;
        if (target <= acc + len || i == route->leg_count - 1) {
            li = i;
            lt = (target - acc) / len;
            if (lt < 0.0f) lt = 0.0f;
            if (lt > 1.0f) lt = 1.0f;
            break;
        }
        acc += len;
    }
    /* Snap the journey endpoints exactly onto the last leg's finish. */
    if (t >= 1.0f) { li = (uint8_t)(route->leg_count - 1); lt = 1.0f; }

    int16_t x, y;
    gfx_leg_point(&route->legs[li], lt, &x, &y);
    *ox = (int16_t)(x + route->ship_offset_x);
    *oy = y;
}

void gfx_route_draw_ship(const gfx_route_t *route, float t, uint8_t pulse) {
    int16_t x, y;
    gfx_route_ship_pos(route, t, &x, &y);
    gfx_prim_crosshair(x, y, pulse);
}

void gfx_route_bake_ship(const gfx_route_t *route, float t, uint8_t pulse, uint8_t buf[512]) {
    gfx_render_target = buf;
    gfx_route_draw_ship(route, t, pulse);
    gfx_render_target = NULL;
}

uint32_t gfx_route_sizeof(void) {
    return (uint32_t)sizeof(gfx_route_t);
}

/* ── Journey phase + burn ─────────────────────────────────────────────────────
 * Cumulative leg-boundary fractions f[0..leg_count] are the maneuver-node t
 * values (same arc-length walk as gfx_route_ship_pos). Departure/arrival sit at
 * t≈0/1; each transfer leg has an MCC at its midpoint; each transfer↔coast
 * boundary is a coast insert/eject. A transfer↔transfer boundary is a flyby —
 * free, so it carries the FLYBY label but no burn. */
gfx_phase_t gfx_route_phase(const gfx_route_t *route, float t, bool *burn_out) {
    uint8_t n = route->leg_count;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    if (n == 0) { if (burn_out) *burn_out = false; return GFX_PHASE_TRANSIT; }

    uint32_t dur_ms = (uint32_t)route->eta_minutes * 60000u;
    if (dur_ms == 0u) dur_ms = BURN_FALLBACK_DURATION_MS;
    float w_major = (float)BURN_MAJOR_MS         / (float)dur_ms;  /* one-sided */
    float h_trim  = 0.5f * (float)BURN_TRIM_MS   / (float)dur_ms;  /* half-width */
    float h_flyby = 0.5f * (float)FLYBY_LABEL_MS / (float)dur_ms;

    /* Cumulative boundary fractions f[0..n] (f[n] pinned to 1). */
    float total = 0.0f;
    for (uint8_t i = 0; i < n; i++) total += (route->legs[i].len > 0.0f) ? route->legs[i].len : 1e-4f;
    if (total <= 0.0f) total = 1.0f;
    float f[5];
    f[0] = 0.0f;
    float acc = 0.0f;
    for (uint8_t i = 0; i < n; i++) {
        acc += (route->legs[i].len > 0.0f) ? route->legs[i].len : 1e-4f;
        f[i + 1] = acc / total;
    }
    f[n] = 1.0f;

    /* ── Burn nodes ───────────────────────────────────────────────────────── */
    bool burn = false;
    if (t <= w_major)        burn = true;   /* departure injection (major) */
    if (t >= 1.0f - w_major) burn = true;   /* arrival capture    (major) */
    for (uint8_t i = 0; i < n; i++) {       /* one MCC per transfer leg, at its midpoint */
        if (route->legs[i].type != GFX_LEG_TRANSFER) continue;
        float mid = 0.5f * (f[i] + f[i + 1]);
        if (t >= mid - h_trim && t <= mid + h_trim) burn = true;
    }
    for (uint8_t b = 1; b < n; b++) {       /* coast insert/eject at transfer↔coast boundaries */
        bool coast_boundary = (route->legs[b - 1].type == GFX_LEG_COAST) ||
                              (route->legs[b].type     == GFX_LEG_COAST);
        if (!coast_boundary) continue;      /* transfer↔transfer = flyby = free */
        if (t >= f[b] - h_trim && t <= f[b] + h_trim) burn = true;
    }

    /* ── Phase (precedence: arrive > depart > flyby > coast > transit) ─────── */
    gfx_phase_t phase;
    if (t >= 1.0f - w_major) {
        phase = GFX_PHASE_ARRIVE;
    } else if (t <= w_major) {
        phase = GFX_PHASE_DEPART;
    } else {
        bool in_flyby = false;
        for (uint8_t b = 1; b < n; b++) {
            if (route->legs[b - 1].type == GFX_LEG_TRANSFER &&
                route->legs[b].type     == GFX_LEG_TRANSFER &&
                t >= f[b] - h_flyby && t <= f[b] + h_flyby) { in_flyby = true; break; }
        }
        if (in_flyby) {
            phase = GFX_PHASE_FLYBY;
        } else {
            uint8_t li = (uint8_t)(n - 1);   /* current leg by arc-length walk */
            for (uint8_t i = 0; i < n; i++) { if (t <= f[i + 1]) { li = i; break; } }
            phase = (route->legs[li].type == GFX_LEG_COAST) ? GFX_PHASE_COAST : GFX_PHASE_TRANSIT;
        }
    }

    if (burn_out) *burn_out = burn;
    return phase;
}

/* ── Big BURN warning (slave portrait OLED, dark-on-light, re-oriented) ───────
 * The slave runs OLED_ROTATION_270, so its logical buffer is 32 wide × 128 tall
 * (portrait) and the driver rotates it to the panel at render. The telemetry
 * text reads down that portrait surface; the warning is drawn rotated 90° from
 * it — a landscape word running the long (128) axis — so it's genuinely
 * re-oriented and as big as the panel allows. */

/* Portrait-buffer pixel: x∈[0,32), y∈[0,128); page stride 32. */
static void gfx_burn_px(uint8_t buf[512], int16_t x, int16_t y, bool on) {
    if (x < 0 || x >= 32 || y < 0 || y >= 128) return;
    uint16_t idx = (uint16_t)x + (uint16_t)(y >> 3) * 32u;
    if (on) buf[idx] |=  (uint8_t)(1u << (y & 7));
    else    buf[idx] &= ~(uint8_t)(1u << (y & 7));
}

/* Author upright in a 128(lx)×32(ly) landscape frame; rotate +90° into the
 * portrait buffer so the word runs the long axis and reads upright when the
 * panel is viewed in landscape. */
static void gfx_burn_land_px(uint8_t buf[512], int16_t lx, int16_t ly, bool on) {
    gfx_burn_px(buf, (int16_t)(31 - ly), lx, on);
}

/* 5×7 cells for B U R N (bit4 = leftmost column). */
static const uint8_t gfx_burn_glyphs[4][7] = {
    { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E },  /* B */
    { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },  /* U */
    { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 },  /* R */
    { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 },  /* N */
};

#define GFX_BURN_SCALE   4                                   /* px per glyph cell */
#define GFX_BURN_GW      (5 * GFX_BURN_SCALE)                /* glyph width  (lx) */
#define GFX_BURN_GH      (7 * GFX_BURN_SCALE)                /* glyph height (ly) */
#define GFX_BURN_GAP     4                                   /* inter-letter gap  */
#define GFX_BURN_WORD_W  (4 * GFX_BURN_GW + 3 * GFX_BURN_GAP)
#define GFX_BURN_LX0     ((128 - GFX_BURN_WORD_W) / 2)       /* word start (lx)   */
#define GFX_BURN_LY0     ((32 - GFX_BURN_GH) / 2)            /* word top   (ly)   */

void gfx_burn_warning(uint8_t buf[512], uint32_t now_ms) {
    memset(buf, 0xFF, 512);                     /* lit field — dark glyphs cut into it */

    /* Marching hazard stripes in the two end margins (before/after the word). */
    int16_t phase = (int16_t)((now_ms / 60u) % 8u);   /* ~16 px/s drift */
    for (int16_t lx = 0; lx < 128; lx++) {
        if (lx >= GFX_BURN_LX0 - 2 && lx < GFX_BURN_LX0 + GFX_BURN_WORD_W + 2) continue;
        for (int16_t ly = 0; ly < 32; ly++) {
            if (((lx + ly + phase) & 7) < 4) gfx_burn_land_px(buf, lx, ly, false);
        }
    }

    /* Big dark "BURN". */
    for (int16_t li = 0; li < 4; li++) {
        const uint8_t *g = gfx_burn_glyphs[li];
        int16_t ox = (int16_t)(GFX_BURN_LX0 + li * (GFX_BURN_GW + GFX_BURN_GAP));
        for (int16_t r = 0; r < 7; r++) {
            for (int16_t c = 0; c < 5; c++) {
                if (!((g[r] >> (4 - c)) & 1)) continue;
                for (int16_t sy = 0; sy < GFX_BURN_SCALE; sy++) {
                    for (int16_t sx = 0; sx < GFX_BURN_SCALE; sx++) {
                        gfx_burn_land_px(buf,
                            (int16_t)(ox + c * GFX_BURN_SCALE + sx),
                            (int16_t)(GFX_BURN_LY0 + r * GFX_BURN_SCALE + sy), false);
                    }
                }
            }
        }
    }

    /* Thin dark frame so the lit field reads as a deliberate alert panel. */
    for (int16_t lx = 0; lx < 128; lx++) {
        gfx_burn_land_px(buf, lx, 0,  false);
        gfx_burn_land_px(buf, lx, 31, false);
    }
    for (int16_t ly = 0; ly < 32; ly++) {
        gfx_burn_land_px(buf, 0,   ly, false);
        gfx_burn_land_px(buf, 127, ly, false);
    }
}
