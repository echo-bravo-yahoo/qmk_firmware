/*
 * Host stand-ins for the QMK runtime symbols route_anim.c needs. Only built into
 * the preview .so (never the firmware). The clock and eeconfig counter are
 * settable from Python so tests can drive route_anim's journey state machine —
 * boot pick, real-time progress, arrival → next route — deterministically and
 * fast, instead of the on-device 1.5–10 h per journey.
 */
#include <stdint.h>

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

/* ── OLED sinks (host renders via the page buffer, not the live panel) ───────── */
void oled_set_cursor(uint8_t col, uint8_t line) { (void)col; (void)line; }
void oled_write_raw(const char *data, uint16_t size) { (void)data; (void)size; }
