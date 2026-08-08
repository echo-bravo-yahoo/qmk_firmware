/*
 * Host stand-ins for the QMK runtime symbols route_anim.c needs. Only built into
 * the preview .so (never the firmware). The clock and eeconfig counter are
 * settable from Python so tests can drive route_anim's journey state machine —
 * boot pick, real-time progress, arrival → next route — deterministically and
 * fast, instead of the on-device 1.5–10 h per journey.
 */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ── Simulated clock ─────────────────────────────────────────────────────────── */
static uint32_t g_now_ms = 0;

uint32_t timer_read32(void)            { return g_now_ms; }
uint32_t timer_elapsed32(uint32_t last) { return g_now_ms - last; }  /* wraps like QMK */

void     host_set_now_ms(uint32_t ms)  { g_now_ms = ms; }
uint32_t host_get_now_ms(void)         { return g_now_ms; }

/* ── eeconfig user slot (per-boot seed counter) ──────────────────────────────── */
static uint32_t g_eeconfig_user = 0;

uint32_t eeconfig_read_user(void)        { return g_eeconfig_user; }
void     eeconfig_update_user(uint32_t v) { g_eeconfig_user = v; }

void     host_set_boot_counter(uint32_t v) { g_eeconfig_user = v; }
uint32_t host_get_boot_counter(void)       { return g_eeconfig_user; }

/* ── Live panel capture ──────────────────────────────────────────────────────
 * The firmware's live-panel writes land in this 512-byte SSD1306 page buffer (same
 * format as a baked background) instead of a real OLED: oled_write_raw blits the
 * cached background, oled_write_pixel ORs in the per-frame ship + telemetry overlay.
 * route_anim_render_map always writes the full 512 bytes from (0,0), so the cursor is
 * ignored. host_panel_get lets tests read back the exact frame — in particular the
 * dynamic ETA/STATUS value lines, which are drawn here, never baked. */
static uint8_t g_panel[512];

void oled_set_cursor(uint8_t col, uint8_t line) { (void)col; (void)line; }

void oled_write_raw(const char *data, uint16_t size) {
    if (size > sizeof(g_panel)) size = sizeof(g_panel);
    memcpy(g_panel, data, size);
}

void oled_write_pixel(uint8_t x, uint8_t y, bool on) {
    if (x >= 128u || y >= 32u) return;
    uint16_t idx = (uint16_t)(y / 8u) * 128u + x;
    if (on) g_panel[idx] |=  (uint8_t)(1u << (y % 8u));
    else    g_panel[idx] &= (uint8_t)~(1u << (y % 8u));
}

void host_panel_clear(void)       { memset(g_panel, 0, sizeof(g_panel)); }
void host_panel_get(uint8_t *out) { memcpy(out, g_panel, sizeof(g_panel)); }
