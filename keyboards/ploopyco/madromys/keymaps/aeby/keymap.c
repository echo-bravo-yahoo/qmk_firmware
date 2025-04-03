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

#define PLOOPY_DPI_OPTIONS { 1200 }
#define PLOOPY_DPI_DEFAULT 0

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT(
        LALT(KC_TAB), // top outer left
        OSL(1), // top inner left
        DRAG_SCROLL, // top inner right
        KC_BTN2, // top outer right
        KC_BTN1, // bottom left
        KC_BTN3 // bottom right
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
