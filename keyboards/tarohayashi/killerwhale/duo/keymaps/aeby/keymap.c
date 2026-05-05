// Copyright 2021 Hayashi (@w_vwbw)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// aeby keymap: Corne-style typing comforts on KillerWhale duo.
// - Home-row mods (GUI/ALT/CTL/SFT mirrored on A/S/D/F and J/K/L/;).
// - 4 thumb-LTs per side: side-switches activate Numbers&Sym / Navigation;
//   add-unit buttons activate Function&Media / Mouse.
// - Gaming layer activated by holding the left hardware toggle (MO(GAMING));
//   right hardware toggle bound to KC_CAPS.
// - Outer pinky column reassigned: left = CAPTCHA / TD_CCP / SH_MON,
//   right = unbound.
// - All KW hardware (D-pad, joystick, scroll wheel, encoders, hardware
//   toggles, ball/light settings layers, auto-mouse) preserved.

#include QMK_KEYBOARD_H
#include "lib/add_keycodes.h"

enum layer_number {
    BASE = 0,
    NUMSYM, NAV, FUNC,                    // Corne-ported typing layers
    MOUSE,                                 // auto-mouse target (AUTO_MOUSE_DEFAULT_LAYER == 4 in keymap config.h)
    LIGHT_SETTINGS,                        // KW RGB config
    GAMING                                 // activated by MO(GAMING) on left hardware toggle
};

// Keymap-level custom keycodes. WIN_* dispatch OS-aware desktop/space switching.
enum custom_keycodes {
    WIN_PREV = SAFE_RANGE,                 // previous space / desktop
    WIN_NEXT,                              // next space / desktop
    WIN_OVRV,                              // mission control / task view / overview
};

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (!record->event.pressed) return true;
    bool mac = detected_host_os() == OS_MACOS || detected_host_os() == OS_IOS;
    bool win = detected_host_os() == OS_WINDOWS;
    switch (keycode) {
        case WIN_PREV:
            if (mac)      { register_code(KC_LCTL); tap_code(KC_LEFT);  unregister_code(KC_LCTL); }
            else if (win) { register_code(KC_LCTL); register_code(KC_LGUI); tap_code(KC_LEFT);
                            unregister_code(KC_LGUI); unregister_code(KC_LCTL); }
            else          { register_code(KC_LCTL); register_code(KC_LALT); tap_code(KC_LEFT);
                            unregister_code(KC_LALT); unregister_code(KC_LCTL); }
            return false;
        case WIN_NEXT:
            if (mac)      { register_code(KC_LCTL); tap_code(KC_RGHT);  unregister_code(KC_LCTL); }
            else if (win) { register_code(KC_LCTL); register_code(KC_LGUI); tap_code(KC_RGHT);
                            unregister_code(KC_LGUI); unregister_code(KC_LCTL); }
            else          { register_code(KC_LCTL); register_code(KC_LALT); tap_code(KC_RGHT);
                            unregister_code(KC_LALT); unregister_code(KC_LCTL); }
            return false;
        case WIN_OVRV:
            if (mac)      { register_code(KC_LCTL); tap_code(KC_UP);    unregister_code(KC_LCTL); }
            else if (win) { register_code(KC_LGUI); tap_code(KC_TAB);   unregister_code(KC_LGUI); }
            else          { tap_code(KC_LGUI); }  // GNOME: tap Super for Activities
            return false;
    }
    return true;
}

// Tap-dance: 1 tap = Copy, 2 taps = Paste, 3+ taps = Cut. OS-aware (Cmd on Mac/iOS, Ctrl elsewhere).
enum tap_dance_codes { TD_CCP = 0 };

static void td_ccp_finished(tap_dance_state_t *state, void *user_data) {
    bool mac = detected_host_os() == OS_MACOS || detected_host_os() == OS_IOS;
    uint16_t mod = mac ? KC_LGUI : KC_LCTL;
    uint16_t key;
    if      (state->count == 1) key = KC_C;
    else if (state->count == 2) key = KC_V;
    else                        key = KC_X;
    register_code(mod);
    tap_code(key);
    unregister_code(mod);
}

tap_dance_action_t tap_dance_actions[] = {
    [TD_CCP] = ACTION_TAP_DANCE_FN(td_ccp_finished),
};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
  [BASE] = LAYOUT(
        // 左手
        // 天面スイッチ
        CAPTCHA,     KC_1,         KC_2,            KC_3, KC_4,                    LT(LIGHT_SETTINGS, KC_5),
        TD(TD_CCP),  KC_Q,         KC_W,            KC_E, KC_R, KC_T,
        SH_MON,      LGUI_T(KC_A), LALT_T(KC_S),    LCTL_T(KC_D), LSFT_T(KC_F), KC_G,
                 KC_Z,         KC_X,            KC_C, KC_V, KC_B,
                               MOD_SCRL,
        // 側面スイッチ
        LT(NAV, KC_ESC), LT(NUMSYM, KC_SPC),
        // 十字キーorジョイスティック                // ジョイスティックスイッチ
        KC_UP, KC_DOWN, KC_LEFT, KC_RIGHT,         L_CHMOD,
        // 追加スイッチ                                          // トグルスイッチ
        LT(FUNC, KC_TAB),     KC_MS_BTN1,                        MO(GAMING),
        // 右手
        LT(LIGHT_SETTINGS, KC_6), KC_7,                    KC_8, KC_9, KC_0, XXXXXXX,
        KC_Y, KC_U,         KC_I,         KC_O,             KC_P,         XXXXXXX,
        KC_H, RSFT_T(KC_J), RCTL_T(KC_K), RALT_T(KC_L),     RGUI_T(KC_SCLN), XXXXXXX,
        KC_N, KC_M,         KC_COMM,      KC_DOT,           KC_SLSH,
                                          MOD_SCRL,
        LT(NUMSYM, KC_BSPC), LT(NAV, KC_ENT),
        KC_UP, KC_DOWN, KC_LEFT, KC_RIGHT,                  R_CHMOD,
        KC_MS_BTN1,            LT(FUNC, KC_DEL),            KC_CAPS
    ),
    [NUMSYM] = LAYOUT(
        // 左手 — Corne L1 port: numbers on Q-row, parens & symbols on A/Z-rows
        _______, _______,  _______,   _______,   _______,    _______,
        _______, KC_1,     KC_2,      KC_3,      KC_4,       KC_5,
        _______, _______,  _______,   _______,   _______,    KC_LPRN,
                 KC_GRAVE, KC_EQUAL,  KC_MINUS,  KC_QUOTE,   KC_LBRC,
                           _______,
        _______, _______,
        _______, _______, _______, _______,          _______,
        _______, _______,                            _______,
        // 右手
        _______, _______, _______, _______, _______, _______,
        KC_6,    KC_7,    KC_8,    KC_9,    KC_0,    _______,
        KC_RPRN, _______, _______, _______, _______, _______,
        KC_RBRC, KC_BSLS, _______, _______, _______,
                                   _______,
        _______, _______,
        _______, _______, _______, _______,          _______,
        _______, _______,                            _______
    ),
    [NAV] = LAYOUT(
        // 左手 — Corne L2 port: arrows at ESDF, Pg keys on V/B
        // Window-management customs: OVRV above UP, PREV left of LEFT,
        //   NEXT right of RIGHT (spatial mnemonics).
        // Add-unit closer-to-center: KC_MS_BTN2 (right click) — overrides
        //   the BASE KC_MS_BTN1 tap action while NAV is held.
        _______, _______, _______,  _______,  _______,  _______,
        _______, _______, WIN_OVRV, KC_UP,    _______,  _______,
        _______, WIN_PREV,KC_LEFT,  KC_DOWN,  KC_RGHT,  WIN_NEXT,
                 _______, _______,  _______,  KC_PGUP,  KC_PGDN,
                          _______,
        _______, _______,
        _______, _______, _______, _______,          _______,
        _______, KC_MS_BTN2,                         _______,
        // 右手 — vim-style HJKL arrows, Home/End, PgUp/PgDn
        _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,
        KC_LEFT, KC_DOWN, KC_UP,   KC_RGHT, _______, _______,
        KC_END,  KC_HOME, _______, KC_PGDN, KC_PGUP,
                                   _______,
        _______, _______,
        _______, _______, _______, _______,          _______,
        KC_MS_BTN2, _______,                         _______
    ),
    [FUNC] = LAYOUT(
        // 左手 — Corne L3 port: F-keys on Q-row, OSM mods on home row
        _______, _______,      _______,      _______,      _______,      _______,
        _______, KC_F1,        KC_F2,        KC_F3,        KC_F4,        KC_F5,
        _______, OSM(KC_LGUI), OSM(KC_LALT), OSM(KC_LCTL), OSM(KC_LSFT), KC_F11,
                 _______,      _______,      _______,      _______,      _______,
                               _______,
        _______, _______,
        _______, _______, _______, _______,          _______,
        _______, _______,                            _______,
        // 右手 — F6-F10 + TG(GAMING) at top-right; F12 + media on home row
        _______, _______, _______, _______, _______, _______,
        KC_F6,   KC_F7,   KC_F8,   KC_F9,   KC_F10,  _______,
        KC_F12,  KC_MPLY, KC_MPRV, KC_MNXT, _______, _______,
        _______, KC_MUTE, KC_VOLD, KC_VOLU, _______,
                                   _______,
        _______, _______,
        _______, _______, _______, _______,          _______,
        _______, _______,                            _______
    ),
    [MOUSE] = LAYOUT(
        // 左手
        _______, _______, _______, _______, _______,    _______,
        _______, _______, _______, _______, _______,    _______,
        _______, _______, _______, KC_MS_BTN2, KC_MS_BTN1, MOD_SCRL,
                 QK_USER_4, _______, _______, _______, _______,
                          MOD_SCRL,
        _______, _______,
        _______, _______, _______, _______,          _______,
        _______, _______,                            _______,
        // 右手
        _______, _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,
        MOD_SCRL, KC_MS_BTN1, KC_MS_BTN2, _______, _______, _______,
        _______, _______, _______, _______, QK_USER_4,
                                   MOD_SCRL,
        _______, _______,
        _______, _______, _______, _______,          _______,
        _______, _______,                            _______
    ),
    // BALL_SETTINGS layer dropped. All trackball/joystick/auto-mouse/D-pad
    // settings are now compile-time defaults in this keymap's config.h.
    [LIGHT_SETTINGS] = LAYOUT(
        // 左手
        XXXXXXX, XXXXXXX, XXXXXXX, UG_NEXT, UG_PREV, _______,
        XXXXXXX, UG_SPDU, UG_VALU, UG_SATU, UG_HUEU, UG_TOGG,
        OLED_MOD, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,
                 UG_SPDD, UG_VALD, UG_SATD, UG_HUED, XXXXXXX,
                          QK_USER_15,
        UG_NEXT, UG_PREV,
        XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,          XXXXXXX,
        XXXXXXX, XXXXXXX,                            XXXXXXX,
        // 右手
        _______, UG_NEXT, UG_PREV, XXXXXXX, XXXXXXX, XXXXXXX,
        UG_TOGG, UG_HUEU, UG_SATU, UG_VALU, UG_SPDU, XXXXXXX,
        XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, OLED_MOD,
        XXXXXXX, UG_HUED, UG_SATD, UG_VALD, UG_SPDD,
                                   QK_USER_15,
        UG_PREV, UG_NEXT,
        XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,          XXXXXXX,
        XXXXXXX, XXXXXXX,                            XXXXXXX
    ),
    [GAMING] = LAYOUT(
        // 左手 — plain alphas, static modifiers on outer column,
        //         no LT thumbs, KW hardware preserved.
        //         Activated by holding the left hardware toggle (MO(GAMING)
        //         on BASE); released by flipping the toggle back.
        //         KC_ESC at top-left explicit since BASE row 1 col 0 is
        //         now CAPTCHA, not Esc.
        //         Thumbs: SPC/TAB/ENT only — add-unit and right-top side
        //         switch are inert during gaming to prevent accidents.
        // KC_5 / KC_6 explicit (BASE has LT(LIGHT_SETTINGS, …) at those slots).
        // Joystick switches XXXXXXX (BASE has L/R_CHMOD which cycle the
        // pointing-device mode — disruptive during gaming).
        KC_ESC,  _______, _______, _______, _______, KC_5,
        KC_LSFT, _______, _______, _______, _______, _______,
        KC_LCTL, KC_A,    KC_S,    KC_D,    KC_F,    _______,
                 _______, _______, _______, _______, _______,
                          KC_R,    // scroll-wheel slot: reload
        KC_TAB,  KC_SPC,
        _______, _______, _______, _______,          XXXXXXX, // joystick: inert
        XXXXXXX, XXXXXXX,                             _______,
        // 右手 — plain alphas; right toggle still functions as KC_CAPS.
        KC_6,    _______, _______, _______, _______, _______,
        _______, _______, _______, _______, _______, _______,
        KC_H,    KC_J,    KC_K,    KC_L,    KC_SCLN, _______,
        _______, _______, _______, _______, _______,
                                   XXXXXXX, // scroll-wheel slot: inert
        XXXXXXX, KC_ENT,
        _______, _______, _______, _______,          XXXXXXX, // joystick: inert
        XXXXXXX, XXXXXXX,                             _______
    )
};

// Swap-hands map for the SH_MON keycode on the BASE layer.
// Default rule for each cell: matrix [r][c] swaps with [(r+7) % 14][c]
// (alpha block, outer-pinky column, scroll wheel, hardware toggle, side
// switches, joystick switch, encoders all swap correctly that way).
//
// Two exceptions where the matrix isn't a clean column-mirror:
//
// 1. D-pad (col 6): each direction lives at a different row on each half.
//    Mapping by direction so SWAP_HANDS preserves arrow semantics.
//      LEFT     DOWN [1,6]  LEFT [2,6]  UP [3,6]  RIGHT [4,6]
//      RIGHT    RIGHT [8,6] DOWN [9,6]  LEFT [10,6] UP [11,6]
//
// 2. ADD-unit (row 5 ↔ row 12, cols 3-4): BTN1/BTN2 are on swapped
//    columns between halves. Mapping by button so BTN1 ↔ BTN1, BTN2 ↔ BTN2.
const keypos_t PROGMEM hand_swap_config[MATRIX_ROWS][MATRIX_COLS] = {
    [0]  = {{0, 7}, {1, 7}, {2, 7}, {3, 7}, {4, 7}, {5, 7}, {6, 7}},
    [1]  = {{0, 8}, {1, 8}, {2, 8}, {3, 8}, {4, 8}, {5, 8}, {6, 9}},  // [1,6] DPADDOWN → [9,6]
    [2]  = {{0, 9}, {1, 9}, {2, 9}, {3, 9}, {4, 9}, {5, 9}, {6,10}},  // [2,6] DPADLEFT → [10,6]
    [3]  = {{0,10}, {1,10}, {2,10}, {3,10}, {4,10}, {5,10}, {6,11}},  // [3,6] DPADUP → [11,6]
    [4]  = {{0,11}, {1,11}, {2,11}, {3,11}, {4,11}, {5,11}, {6, 8}},  // [4,6] DPADRIGHT → [8,6]
    [5]  = {{0,12}, {1,12}, {2,12}, {4,12}, {3,12}, {5,12}, {6,12}},  // [5,3]/[5,4] ADD2/ADD1 swap cols
    [6]  = {{0,13}, {1,13}, {2,13}, {3,13}, {4,13}, {5,13}, {6,13}},
    [7]  = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}, {5, 0}, {6, 0}},
    [8]  = {{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}, {5, 1}, {6, 4}},  // [8,6] DPADRIGHT → [4,6]
    [9]  = {{0, 2}, {1, 2}, {2, 2}, {3, 2}, {4, 2}, {5, 2}, {6, 1}},  // [9,6] DPADDOWN → [1,6]
    [10] = {{0, 3}, {1, 3}, {2, 3}, {3, 3}, {4, 3}, {5, 3}, {6, 2}},  // [10,6] DPADLEFT → [2,6]
    [11] = {{0, 4}, {1, 4}, {2, 4}, {3, 4}, {4, 4}, {5, 4}, {6, 3}},  // [11,6] DPADUP → [3,6]
    [12] = {{0, 5}, {1, 5}, {2, 5}, {4, 5}, {3, 5}, {5, 5}, {6, 5}},  // [12,3]/[12,4] ADD swap cols
    [13] = {{0, 6}, {1, 6}, {2, 6}, {3, 6}, {4, 6}, {5, 6}, {6, 6}},
};

const uint16_t PROGMEM encoder_map[][NUM_ENCODERS][2] = {
    [BASE] =   {
        ENCODER_CCW_CW(KC_ESC, KC_TAB),
        ENCODER_CCW_CW(REDO, UNDO),
        ENCODER_CCW_CW(KC_WH_U, KC_WH_D),
        ENCODER_CCW_CW(KC_WH_U, KC_WH_D),
        ENCODER_CCW_CW(KC_DEL, KC_BSPC),
        ENCODER_CCW_CW(KC_UP, KC_DOWN),
        ENCODER_CCW_CW(KC_WH_U, KC_WH_D),
        ENCODER_CCW_CW(KC_WH_U, KC_WH_D),
    },
    [LIGHT_SETTINGS] =   {
        ENCODER_CCW_CW(UG_SPDU, UG_SPDD),
        ENCODER_CCW_CW(UG_VALU, UG_VALD),
        ENCODER_CCW_CW(UG_SATU, UG_SATD),
        ENCODER_CCW_CW(UG_HUEU, UG_HUED),
        ENCODER_CCW_CW(UG_SPDU, UG_SPDD),
        ENCODER_CCW_CW(UG_VALU, UG_VALD),
        ENCODER_CCW_CW(UG_SATU, UG_SATD),
        ENCODER_CCW_CW(UG_HUEU, UG_HUED),
    },
};
