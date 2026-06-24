/*
 * Host stub for QMK_KEYBOARD_H. The preview compiles the firmware code
 * (oled_gfx.c, route_gen.c, starmap_world.c, route_anim.c) into a shared library,
 * so it needs stand-ins for the few QMK symbols those files reference. oled_gfx
 * routes all output through gfx_render_target when baking, so oled_write_pixel is
 * a no-op here — the live-panel path is never exercised on the host.
 *
 * The remaining symbols (timer, OLED sinks, eeconfig user slot) are used only by
 * route_anim.c's journey state machine; they're defined in host_qmk_shim.c with a
 * Python-settable clock and boot counter, so tests can drive a whole journey
 * (boot → crawl → arrival → next route) deterministically in microseconds rather
 * than the on-device hours. Declared here so route_anim.c sees real prototypes.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>

static inline void oled_write_pixel(uint8_t x, uint8_t y, bool on) {
    (void)x; (void)y; (void)on;
}

/* Simulated millisecond clock (host_qmk_shim.c). */
uint32_t timer_read32(void);
uint32_t timer_elapsed32(uint32_t last);

/* OLED output sinks — no-ops; the host renders via the page buffer. */
void oled_set_cursor(uint8_t col, uint8_t line);
void oled_write_raw(const char *data, uint16_t size);

/* 4-byte user eeconfig slot, backing the per-boot route seed counter. */
uint32_t eeconfig_read_user(void);
void     eeconfig_update_user(uint32_t val);
