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

#pragma once

// DPI defines only take effect here (ploopyco.c reads PLOOPY_DPI_OPTIONS from a
// keymap config.h, never from keymap.c). Four discrete values for the Fn keys.
#define PLOOPY_DPI_OPTIONS { 400, 1200, 1600, 2400 }   // min, low-mid, up-mid, max
#define PLOOPY_DPI_DEFAULT 1                            // 1200 desktop

// 16-bit mouse reports: removes the ±127/axis fast-flick clamp (sensor-independent).
// Only tradeoff: no USB Boot Mouse (BIOS/UEFI/some KVMs).
#define MOUSE_EXTENDED_REPORT

#define COMBO_TERM 50          // gaming-toggle chord window (ms)

#define PLOOPY_DRAGSCROLL_DIVISOR_H -32.0   // negative inverts H to match the
                                             // already-inverted V axis (POINTING_DEVICE_INVERT_Y)
#define PLOOPY_DRAGSCROLL_DIVISOR_V 32.0
