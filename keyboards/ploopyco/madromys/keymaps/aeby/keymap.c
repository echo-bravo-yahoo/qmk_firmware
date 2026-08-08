/* Copyright 2023 Colin Lam (Ploopy Corporation)
 * Copyright 2020 Christopher Courtney, aka Drashna Jael're  (@drashna) <drashna@live.com>
 * Copyright 2019 Sunjun Kim
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include QMK_KEYBOARD_H

enum layers { _BASE = 0, _FN, _GAME };
enum custom_keycodes { DPI_MIN = SAFE_RANGE, DPI_LOMID, DPI_HIMID, DPI_MAX };
enum tap_dances { TD_DRAG_SCROLL };

// Settle window: zero cursor motion this long after a left OR right press.
// Kept in EVERY mode — for aiming a brief freeze beats a cursor hop (gaming is
// what motivated this filter). Protecting both buttons covers fire and ADS.
#define LCLICK_SETTLE_MS 40
#define SETTLE_BTNS (MOUSE_BTN1 | MOUSE_BTN2)
// CPI while gaming; desktop CPI comes from PLOOPY_DPI_OPTIONS, restored on exit.
#define GAME_CPI 1600

static bool     settle_held  = false;
static uint32_t settle_timer = 0;

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    //                 top-outer-L   top-inner-L  top-inner-R          top-outer-R  bottom-L  bottom-R
    [_BASE] = LAYOUT(  LALT(KC_TAB), OSL(_FN),    TD(TD_DRAG_SCROLL),  MS_BTN2,     MS_BTN1,  MS_BTN3 ),
    [_FN]   = LAYOUT(  DPI_MIN,      DPI_LOMID,   DPI_HIMID,           DPI_MAX,     _______,  _______ ),
    [_GAME] = LAYOUT(  MS_BTN4,      MS_BTN5,     KC_INS,              _______,     _______,  _______ ),
};

// Same physical chord (top-outer-left + top-inner-right) toggles gaming both
// ways: ⌥Tab+Drag on Base, M4+Insert on Gaming.
enum combos { CMB_GAME_ON, CMB_GAME_OFF };
const uint16_t PROGMEM game_on_combo[]  = {LALT(KC_TAB), TD(TD_DRAG_SCROLL), COMBO_END};
const uint16_t PROGMEM game_off_combo[] = {MS_BTN4, KC_INS, COMBO_END};
combo_t key_combos[] = {
    [CMB_GAME_ON]  = COMBO(game_on_combo,  TG(_GAME)),
    [CMB_GAME_OFF] = COMBO(game_off_combo, TG(_GAME)),
};

// Drag-scroll axis mode: single tap enters scroll mode with H+V inverted (matches
// POINTING_DEVICE_INVERT_Y and PLOOPY_DRAGSCROLL_DIVISOR_H's sign in config.h).
// Double tap enters scroll mode with H+V normal (raw sensor direction). Repeating
// the gesture that matches the currently active mode turns scroll off; the other
// gesture switches the active mode in place rather than stacking a third state.
extern bool is_drag_scroll;
static bool drag_scroll_normal = false;

void dragscroll_dance_finished(tap_dance_state_t *state, void *user_data) {
    bool want_normal = state->count >= 2;
    if (is_drag_scroll && drag_scroll_normal == want_normal) {
        is_drag_scroll = false;
    } else {
        is_drag_scroll     = true;
        drag_scroll_normal = want_normal;
    }
}

tap_dance_action_t tap_dance_actions[] = {
    [TD_DRAG_SCROLL] = ACTION_TAP_DANCE_FN_ADVANCED(NULL, dragscroll_dance_finished, NULL),
};

// Discrete "set DPI to X": write the index, persist, apply. Reuses the board's
// EEPROM machinery so the choice survives replug and is restored on gaming-exit.
static void set_dpi_index(uint8_t idx) {
    keyboard_config.dpi_config = idx;
    eeconfig_update_kb(keyboard_config.raw);
    pointing_device_set_cpi(dpi_array[idx]);
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        switch (keycode) {
            case DPI_MIN:   set_dpi_index(0); return false;
            case DPI_LOMID: set_dpi_index(1); return false;
            case DPI_HIMID: set_dpi_index(2); return false;
            case DPI_MAX:   set_dpi_index(3); return false;
        }
    }
    return true;
}

// Suppress the cursor jump from pressing the left or right button (all modes).
report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    if (mouse_report.buttons & SETTLE_BTNS) {
        if (!settle_held) { settle_held = true; settle_timer = timer_read32(); }
    } else {
        settle_held = false;
    }
    if (settle_held && timer_elapsed32(settle_timer) < LCLICK_SETTLE_MS) {
        mouse_report.x = 0;
        mouse_report.y = 0;
    }
    // Normal-mode drag-scroll: negate both axes here, before ploopyco.c's kb-level
    // accumulation runs. That cancels PLOOPY_DRAGSCROLL_DIVISOR_H's negative sign and
    // POINTING_DEVICE_INVERT_Y's flip, so the scroll direction matches raw sensor
    // motion instead of the inverted default (see dragscroll_dance_finished above).
    if (is_drag_scroll && drag_scroll_normal) {
        mouse_report.x = -mouse_report.x;
        mouse_report.y = -mouse_report.y;
    }
    return mouse_report;
}

// Gaming CPI on entry; restore the user's chosen desktop DPI on exit.
layer_state_t layer_state_set_user(layer_state_t state) {
    pointing_device_set_cpi(layer_state_cmp(state, _GAME)
                            ? GAME_CPI
                            : dpi_array[keyboard_config.dpi_config]);
    return state;
}
