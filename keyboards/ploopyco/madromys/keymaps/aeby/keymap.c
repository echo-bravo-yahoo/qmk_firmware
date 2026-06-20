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

#define LCLICK_SETTLE_MS 40

static bool     lclick_held  = false;
static uint32_t lclick_timer = 0;

report_mouse_t pointing_device_task_user(report_mouse_t mouse_report) {
    if (mouse_report.buttons & MOUSE_BTN1) {
        if (!lclick_held) {
            lclick_held  = true;
            lclick_timer = timer_read32();
        }
    } else {
        lclick_held = false;
    }

    if (lclick_held && timer_elapsed32(lclick_timer) < LCLICK_SETTLE_MS) {
        mouse_report.x = 0;
        mouse_report.y = 0;
    }

    return mouse_report;
}

#define PLOOPY_DPI_OPTIONS { 1200 }
#define PLOOPY_DPI_DEFAULT 0

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(
        LALT(KC_TAB), // top outer left
        OSL(1), // top inner left
        DRAG_SCROLL, // top inner right
        MS_BTN2, // top outer right
        MS_BTN1, // bottom left
        MS_BTN3 // bottom right
    )

    // put window movement on this one-shot-accessible layer
    /* [1] = LAYOUT( */
    /*     ------, // top outer left */
    /*     ------, // top inner left */
    /*     ------, // top inner right */
    /*     ------, // top outer right */
    /*     ------, // bottom left */
    /*     ------ // bottom right */
    /* ) */
};
