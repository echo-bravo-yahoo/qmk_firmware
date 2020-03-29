/* Copyright 2018 MechMerlin
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

// Defines the keycodes used by our macros in process_record_user
enum custom_keycodes {
  QMKBEST = SAFE_RANGE,
  QMKURL
};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
 [0] = LAYOUT_ortho_5x15( \
    KC_ESC,   KC_1,    KC_2,    KC_3,    KC_4,    KC_5,    KC_NO,   KC_NO,   KC_NO,   KC_6,     KC_7,    KC_8,    KC_9,    KC_0,    KC_BSPC,  \
    KC_GRAVE, KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,    KC_NO,   KC_NO,   KC_NO,   KC_Y,     KC_U,    KC_I,    KC_O,    KC_P,    KC_MINUS, \
    KC_TAB,   KC_A,    KC_S,    KC_D,    KC_F,    KC_G,    KC_NO,   KC_NO,   KC_NO,   KC_H,     KC_J,    KC_K,    KC_L,    KC_SCLN, KC_QUOT,  \
    KC_LSFT,  KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,    KC_NO,   KC_NO,   RESET,   KC_N,     KC_M,    KC_LBRC, KC_RBRC, KC_UP,   KC_EQUAL, \
    KC_LCTL,  KC_LGUI, KC_LALT, KC_ENT,  KC_SPC,  SH_MON,  KC_NO,   KC_LEAD, KC_NO,   KC_COMM,  KC_DOT,  KC_SLSH, KC_LEFT, KC_DOWN, KC_RIGHT  \
  ),
};

const keypos_t hand_swap_config[MATRIX_ROWS][MATRIX_COLS] = {
  {{14, 0}, {13, 0}, {12, 0}, {11, 0}, {10, 0}, {9, 0}, {8, 0}, {7, 0}, {6, 0}, {5, 0}, {4, 0}, {3, 0}, {2, 0}, {1, 0}, {0, 0}},
  {{14, 1}, {13, 1}, {12, 1}, {11, 1}, {10, 1}, {9, 1}, {8, 1}, {7, 1}, {6, 1}, {5, 1}, {4, 1}, {3, 1}, {2, 1}, {1, 1}, {0, 1}},
  {{14, 2}, {13, 2}, {12, 2}, {11, 2}, {10, 2}, {9, 2}, {8, 2}, {7, 2}, {6, 2}, {5, 2}, {4, 2}, {3, 2}, {2, 2}, {1, 2}, {0, 2}},
  {{14, 3}, {13, 3}, {12, 3}, {11, 3}, {10, 3}, {9, 3}, {8, 3}, {7, 3}, {6, 3}, {5, 3}, {4, 3}, {3, 3}, {2, 3}, {1, 3}, {0, 3}},
  {{14, 4}, {13, 4}, {12, 4}, {11, 4}, {10, 4}, {9, 4}, {8, 4}, {7, 4}, {6, 4}, {5, 4}, {4, 4}, {3, 4}, {2, 4}, {1, 4}, {0, 4}},
};

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
  switch (keycode) {
    case QMKBEST:
      if (record->event.pressed) {
        // when keycode QMKBEST is pressed
        SEND_STRING("QMK is the best thing ever!");
      } else {
        // when keycode QMKBEST is released
      }
      break;
    case QMKURL:
      if (record->event.pressed) {
        // when keycode QMKURL is pressed
        SEND_STRING("https://qmk.fm/" SS_TAP(X_ENTER));
      } else {
        // when keycode QMKURL is released
      }
      break;
  }
  return true;
}

void matrix_init_user(void) {

}

LEADER_EXTERNS();

void matrix_scan_user(void) {
  LEADER_DICTIONARY() {
    leading = false;
    leader_end();

    SEQ_ONE_KEY(KC_W) {
      // Anything you can do in a macro.
      SEND_STRING("QMK is awesome.");
    }
  }
}

void led_set_user(uint8_t usb_led) {

}
