/*
 * Host stub for QMK_KEYBOARD_H. The preview compiles the firmware code
 * (oled_gfx.c, route_gen.c, starmap_world.c, route_anim.c) into a shared library,
 * so it needs stand-ins for the few QMK symbols those files reference. When baking,
 * oled_gfx routes output through gfx_render_target; the live-panel path
 * (oled_write_pixel + oled_write_raw) is exercised by route_anim_render_map and is
 * captured into a host page buffer (host_qmk_shim.c) so tests can read back the live
 * frame — notably the per-frame ETA/STATUS overlay, which is never baked.
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

/* Live-panel pixel write — captured into the host page buffer (host_qmk_shim.c). */
void oled_write_pixel(uint8_t x, uint8_t y, bool on);

/* Simulated millisecond clock (host_qmk_shim.c). */
uint32_t timer_read32(void);
uint32_t timer_elapsed32(uint32_t last);

/* OLED output — oled_write_raw blits the background into the host page buffer; the
 * cursor is ignored (full-frame writes from (0,0)). */
void oled_set_cursor(uint8_t col, uint8_t line);
void oled_write_raw(const char *data, uint16_t size);

/* Host-only live-panel capture (host_qmk_shim.c): read back / reset the page buffer. */
void host_panel_clear(void);
void host_panel_get(uint8_t *out);

/* 4-byte user eeconfig slot, backing the per-boot route seed counter. */
uint32_t eeconfig_read_user(void);
void     eeconfig_update_user(uint32_t val);

/* QMK keycodes route_anim.c's token-entry editor compares against (HID usage IDs).
 * On device these come from the real QMK_KEYBOARD_H; the host build needs stand-ins
 * so route_anim_token_key compiles into the preview .so. */
#define KC_A    0x0004
#define KC_Z    0x001D
#define KC_1    0x001E
#define KC_9    0x0026
#define KC_0    0x0027
#define KC_ENT  0x0028
#define KC_ESC  0x0029
#define KC_BSPC 0x002A
