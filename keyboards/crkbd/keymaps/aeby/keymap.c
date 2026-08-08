/*
Copyright 2019 @foostan
Copyright 2020 Drashna Jaelre <@drashna>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include QMK_KEYBOARD_H

// The transit-map sim (left-OLED star map + right-OLED telemetry) is gated on
// STARMAP_ENABLE (set in rules.mk; auto-off on the ATmega32U4, where it would
// overflow flash). Its sources and these includes only come in when it is on.
#if defined(OLED_ENABLE) && defined(STARMAP_ENABLE)
#  include "transactions.h"   // split RPC (transaction_register_rpc / _rpc_send, RPC_ID_USER_*)
#  include "oled_gfx.h"
#  include "route_anim.h"
#  include <string.h>
#endif

// Temporary diagnostic OLED mode (KEYLOG_ENABLE; see rules.mk) -- shows readable
// keycode names instead of the transit-map sim.
#if defined(OLED_ENABLE) && defined(KEYLOG_ENABLE)
#  include <stdio.h>          // snprintf for the keylog OLED line
#endif

#define TAP_A LGUI_T(KC_A)
#define TAP_S LALT_T(KC_S)
#define TAP_D LCTL_T(KC_D)
#define TAP_F LSFT_T(KC_F)
#define TAP_J RSFT_T(KC_J)
#define TAP_K RCTL_T(KC_K)
#define TAP_L RALT_T(KC_L)
#define TAP_SEMI RGUI_T(KC_SCLN)
#define TOG_ESC LT(2, KC_ESC)
#define TOG_SPC LT(1, KC_SPC)
#define TOG_TAB LT(3, KC_TAB)
#define TOG_DEL LT(3, KC_DEL)
#define TOG_BSPC LT(1, KC_BSPC)
#define TOG_ENT LT(2, KC_ENT)

// Custom keycodes for the transit-map sim (see process_record_user). These drive the
// player-controlled three-state mission loop (Mission Control → Mid-mission →
// Mission Complete) on the master's left OLED.
enum custom_keycodes {
    RG_REROLL = SAFE_RANGE,   // re-plot a fresh random system (held in Mission Control)
    RG_LAUNCH,                // launch the held route (Mission Control → Mid-mission)
    RG_BACK,                  // from Mission Complete, plot a new mission
    RG_DUR_UP,                // trim the journey duration +30 min
    RG_DUR_DN,                // trim the journey duration -30 min
    RG_TOKEN,                 // begin freeform designation entry (layer 7)
};

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT_split_3x6_3(
  //,--------------------------------------------+--------.                    ,-----------------------------------------------------.
      XXXXXXX,    KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,                         KC_Y,    KC_U,    KC_I,    KC_O,    KC_P, XXXXXXX,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      XXXXXXX,   TAP_A,   TAP_S,   TAP_D,   TAP_F,    KC_G,                         KC_H,    TAP_J,  TAP_K,   TAP_L,TAP_SEMI, XXXXXXX,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      XXXXXXX,    KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,                         KC_N,    KC_M, KC_COMM,  KC_DOT, KC_SLSH, XXXXXXX,
  //|--------+--------+--------+--------+--------+--------+--------|  |--------+--------+--------+--------+--------+--------+--------|
                                          TOG_ESC, TOG_SPC, TOG_TAB,    TOG_DEL,TOG_BSPC,  TOG_ENT
                                      //`--------------------------'  `--------------------------'

  ),

    [1] = LAYOUT_split_3x6_3(
  //,-----------------------------------------------------.                    ,-----------------------------------------------------.
      XXXXXXX,    KC_1,    KC_2,    KC_3,    KC_4,    KC_5,                         KC_6,    KC_7,    KC_8,    KC_9,    KC_0, XXXXXXX,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      XXXXXXX, _______, _______, _______, _______, KC_LPRN,                      KC_RPRN, _______, _______, _______, _______, XXXXXXX,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      XXXXXXX,KC_GRAVE,KC_EQUAL,KC_MINUS,KC_QUOTE, KC_LBRC,                      KC_RBRC, KC_BSLS, _______, _______, _______, XXXXXXX,
  //|--------+--------+--------+--------+--------+--------+--------|  |--------+--------+--------+--------+--------+--------+--------|
                                          _______, _______, _______,    _______, _______, _______
                                      //`--------------------------'  `--------------------------'
  ),

    [2] = LAYOUT_split_3x6_3(
  //,-----------------------------------------------------.                    ,-----------------------------------------------------.
      _______, _______, _______,   KC_UP, _______, _______,                      _______, _______, _______, _______, _______, _______,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      _______, _______, KC_LEFT, KC_DOWN, KC_RGHT, _______,                      KC_LEFT, KC_DOWN,   KC_UP, KC_RGHT, _______, _______,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      _______, _______, _______, _______, KC_PGUP, KC_PGDN,                       KC_END, KC_HOME, _______, KC_PGDN, KC_PGUP, _______,
  //|--------+--------+--------+--------+--------+--------+--------|  |--------+--------+--------+--------+--------+--------+--------|
                                          _______, _______, _______,    _______, _______, _______
                                      //`--------------------------'  `--------------------------'
  ),

    [3] = LAYOUT_split_3x6_3(
  //,-----------------------------------------------------.                    ,-----------------------------------------------------.
      _______,   KC_F1,   KC_F2,   KC_F3,   KC_F4,   KC_F5,                        KC_F6,   KC_F7,   KC_F8,   KC_F9,  KC_F10,   TG(4),
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      _______,OSM(KC_LGUI),OSM(KC_LALT),OSM(KC_LCTL),OSM(KC_LSFT),KC_F11,         KC_F12, KC_MPLY, KC_MPRV, KC_MNXT,   TG(6), _______,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      _______, _______, _______, MS_BTN2, MS_BTN1, _______,                      _______, KC_MUTE, KC_VOLD, KC_VOLU, _______, _______,
  //|--------+--------+--------+--------+--------+--------+--------|  |--------+--------+--------+--------+--------+--------+--------|
                                          _______, _______, _______,    _______, _______, _______
                                      //`--------------------------'  `--------------------------'
  ),

  // gaming layer
    [4] = LAYOUT_split_3x6_3(
  //,-----------------------------------------------------.                    ,-----------------------------------------------------.
       KC_ESC,    KC_Q,    KC_W,    KC_E,    KC_R,    KC_T,                         KC_Y,    KC_U,    KC_I,    KC_O,   KC_P,    TO(0),
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      KC_LSFT,    KC_A,    KC_S,    KC_D,    KC_F,    KC_G,                         KC_H,    KC_J,    KC_K,    KC_L, KC_SCLN, KC_BSLS,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      KC_LCTL,    KC_Z,    KC_X,    KC_C,    KC_V,    KC_B,                         KC_N,    KC_M, KC_COMM,  KC_DOT, KC_SLSH,   TG(5),
  //|--------+--------+--------+--------+--------+--------+--------|  |--------+--------+--------+--------+--------+--------+--------|
                                          KC_LALT,  KC_SPC,  KC_TAB,     KC_DEL, KC_BSPC,   KC_ENT
                                      //`--------------------------'  `--------------------------'
  ),

  // arrow key gaming layer
    [5] = LAYOUT_split_3x6_3(
  //,-----------------------------------------------------.                    ,-----------------------------------------------------.
      _______, _______,   KC_UP, _______, _______, _______,                      _______, _______, _______, _______, _______, _______,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      _______, KC_LEFT, KC_DOWN, KC_RGHT, _______, _______,                      _______, _______, _______, _______, _______, _______,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      _______, _______, _______, _______, _______, _______,                      _______, _______, _______, _______, _______, _______,
  //|--------+--------+--------+--------+--------+--------+--------|  |--------+--------+--------+--------+--------+--------+--------|
                                          _______, _______, _______,    _______, _______, _______
                                      //`--------------------------'  `--------------------------'
  ),

  // Mission Control — plot a trip. RG_BACK (top-right outer) starts a new mission /
  // exits; the left-hand cluster launches, re-rolls, trims the ETA, or enters a token.
    [6] = LAYOUT_split_3x6_3(
  //,-----------------------------------------------------.                    ,-----------------------------------------------------.
      XXXXXXX, XXXXXXX,   XXXXXXX,   XXXXXXX,   XXXXXXX,   XXXXXXX,              XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, RG_BACK,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      XXXXXXX, RG_LAUNCH, RG_REROLL, RG_DUR_UP, RG_DUR_DN, _______,            _______, _______, _______, _______, _______, XXXXXXX,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      XXXXXXX, RG_TOKEN,  _______,   _______,   _______,   _______,            _______, _______, _______, _______, _______, XXXXXXX,
  //|--------+--------+--------+--------+--------+--------+--------|  |--------+--------+--------+--------+--------+--------+--------|
                                          _______, _______, _______,    _______, _______, _______
                                      //`--------------------------'  `--------------------------'
  ),

  // Token Entry — type a designation. Only the valid-prefix letters (L V B G K R F)
  // plus digits and ESC/BSPC/ENT; every key is captured by the token-entry intercept.
    [7] = LAYOUT_split_3x6_3(
  //,-----------------------------------------------------.                    ,-----------------------------------------------------.
       KC_ESC,    KC_1,    KC_2,    KC_3,    KC_4,    KC_5,                         KC_6,    KC_7,    KC_8,    KC_9,    KC_0, KC_BSPC,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      XXXXXXX,    KC_L,    KC_V,    KC_B,    KC_G,    KC_K,                         KC_R,    KC_F, XXXXXXX, XXXXXXX,  KC_ENT, XXXXXXX,
  //|--------+--------+--------+--------+--------+--------|                    |--------+--------+--------+--------+--------+--------|
      XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,                      XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX, XXXXXXX,
  //|--------+--------+--------+--------+--------+--------+--------|  |--------+--------+--------+--------+--------+--------+--------|
                                          XXXXXXX, XXXXXXX, XXXXXXX,    XXXXXXX, XXXXXXX, XXXXXXX
                                      //`--------------------------'  `--------------------------'
  )
};

#if defined(OLED_ENABLE) && defined(STARMAP_ENABLE)

// Token-entry layer (mirrors keymaps[7]); raised by RG_TOKEN, dropped on confirm/cancel.
#define TOKEN_LAYER 7

// Gaming layers blank the left OLED and disable HRM telemetry. Only layers 4 (gaming)
// and 5 (arrow gaming) qualify — the Mission Control / Token Entry layers (6/7) are
// also >= 4, but the transit map must stay visible there, so test the two explicitly.
static bool is_gaming_layer(void) {
    uint8_t hl = get_highest_layer(layer_state);
    return hl == 4 || hl == 5;
}

// Master owns the live journey; the slave renders telemetry from a synced copy.
static route_journey_t   g_journey;
static route_telemetry_t g_telemetry;

// Slave-side RPC sink: cache the telemetry the master pushes each tick.
static void telemetry_slave_handler(uint8_t in_len, const void *in_data,
                                    uint8_t out_len, void *out_data) {
    (void)out_len; (void)out_data;
    if (in_len == sizeof(route_telemetry_t)) {
        memcpy(&g_telemetry, in_data, sizeof(route_telemetry_t));
    }
}

void keyboard_post_init_user(void) {
    transaction_register_rpc(RPC_ID_USER_TELEMETRY, telemetry_slave_handler);
    if (is_keyboard_master()) {
        route_anim_init(&g_journey);
    }
}

// Push the active route's telemetry to the slave a few times a second.
void housekeeping_task_user(void) {
    if (!is_keyboard_master()) return;
    static uint32_t last_sync = 0;
    if (timer_elapsed32(last_sync) < 250) return;
    last_sync = timer_read32();

    route_telemetry_t pkt;
    route_anim_fill_telemetry(&g_journey, &pkt);
    pkt.gaming = is_gaming_layer() ? 1 : 0;
    transaction_rpc_send(RPC_ID_USER_TELEMETRY, sizeof(pkt), &pkt);
}

// Drive the three-state mission loop. Split key processing runs on the master, which
// owns g_journey; the slave picks up the new state on the next telemetry sync.
bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (!is_keyboard_master()) return true;

    // While entering a token, every keystroke feeds the buffer editor (and is
    // swallowed). ENTER/ESC end entry, so drop the token layer when they do.
    if (g_journey.tok_entry) {
        if (record->event.pressed) {
            route_anim_token_key(&g_journey, keycode);
            if (!g_journey.tok_entry) layer_off(TOKEN_LAYER);
        }
        return false;
    }

    switch (keycode) {
        case RG_LAUNCH:
            if (record->event.pressed) route_anim_launch(&g_journey);
            return false;
        case RG_BACK:
            if (record->event.pressed) route_anim_back(&g_journey);
            return false;
        case RG_REROLL:
            if (record->event.pressed) route_anim_reroll(&g_journey);
            return false;
        case RG_DUR_UP:
            if (record->event.pressed) route_anim_adjust_eta(&g_journey, +30);
            return false;
        case RG_DUR_DN:
            if (record->event.pressed) route_anim_adjust_eta(&g_journey, -30);
            return false;
        case RG_TOKEN:
            if (record->event.pressed) {
                g_journey.tok_entry = true;
                g_journey.tok_len   = 0;
                g_journey.tok[0]    = '\0';
                layer_on(TOKEN_LAYER);
            }
            return false;
    }
    return true;
}

// Portrait right OLED: OLED_ROTATION_270 gives 8 chars/row × 16 rows with Tom Thumb 4×8.
oled_rotation_t oled_init_user(oled_rotation_t rotation) {
    if (!is_keyboard_master()) {
        return OLED_ROTATION_270;
    }
    return rotation;
}

// Write a space-padded 8-column row from a possibly-unterminated source.
static void oled_row(uint8_t row, const char *s, bool invert) {
    char line[9];
    bool ended = false;
    for (uint8_t i = 0; i < 8; i++) {
        if (!ended && s[i] == '\0') ended = true;
        line[i] = ended ? ' ' : s[i];
    }
    line[8] = '\0';
    oled_set_cursor(0, row);
    oled_write(line, invert);
}

// Phase word for the STATUS line. The table now lives in oled_gfx.c (gfx_phase_word)
// so the master's transit-map strip and this slave panel share one source.
static const char *phase_word(uint8_t phase) {
    return gfx_phase_word(phase);
}

// Big dark-on-light landscape BURN warning takes over the whole right panel while
// a burn is firing — built into the slave's portrait page buffer, blitted raw so
// the driver's OLED_ROTATION_270 lands it upright-landscape.
static void render_burn(void) {
    static uint8_t burn_buf[512];
    gfx_burn_warning(burn_buf, timer_read32());
    oled_set_cursor(0, 0);
    oled_write_raw((const char *)burn_buf, 512);
}

static void render_telemetry(void) {
    oled_row(0, "USCSS",  true);
    oled_row(1, "PATNA",  true);
    oled_row(2, "--------", false);
    if (g_telemetry.gaming) {
        oled_row(3,  "MODE",    false);
        oled_row(4,  "GAMING",  false);
        oled_row(5,  "",        false);
        oled_row(6,  "HRM",     false);
        oled_row(7,  "DISABLD", false);
        for (uint8_t r = 8; r < 16; r++) oled_row(r, "", false);
        return;
    }

    char eta[9];
    uint16_t hh = g_telemetry.eta_remaining_min / 60;
    uint16_t mm = g_telemetry.eta_remaining_min % 60;
    eta[0] = (char)('0' + (hh / 10) % 10); eta[1] = (char)('0' + hh % 10);
    eta[2] = 'H';
    eta[3] = (char)('0' + (mm / 10) % 10); eta[4] = (char)('0' + mm % 10);
    eta[5] = eta[6] = eta[7] = ' '; eta[8] = '\0';

    // Mission Control: a plotted route held at departure — preview DST + ETA.
    if (g_telemetry.state == RA_CONTROL) {
        oled_row(3,  "MISSION", false);
        oled_row(4,  "CONTROL", false);
        oled_row(5,  "--------", false);
        oled_row(6,  "DST", false);
        oled_row(7,  g_telemetry.designation, false);
        oled_row(8,  "--------", false);
        oled_row(9,  "ETA", false);
        oled_row(10, eta, false);
        oled_row(11, "--------", false);
        oled_row(12, "STATUS", false);
        oled_row(13, "HELD", false);
        for (uint8_t r = 14; r < 16; r++) oled_row(r, "", false);
        return;
    }

    // Mission Complete: arrived — DOCKED (a colony) or LANDED (unpopulated).
    if (g_telemetry.state == RA_COMPLETE) {
        oled_row(3,  "MISSION", false);
        oled_row(4,  "COMPLETE", false);
        oled_row(5,  "--------", false);
        oled_row(6,  "DST", false);
        oled_row(7,  g_telemetry.designation, false);
        oled_row(8,  "--------", false);
        oled_row(9,  "STATUS", false);
        oled_row(10, g_telemetry.is_colony ? "DOCKED" : "LANDED", false);
        for (uint8_t r = 11; r < 16; r++) oled_row(r, "", false);
        return;
    }

    // Mid-mission: the live running panel.
    oled_row(3,  "MISSION", false);
    oled_row(4,  g_telemetry.system_name, false);
    oled_row(5,  "TRANSIT", false);
    oled_row(6,  "--------", false);
    oled_row(7,  "DST", false);
    oled_row(8,  g_telemetry.designation, false);
    oled_row(9,  "--------", false);
    oled_row(10, "ETA", false);
    oled_row(11, eta, false);
    oled_row(12, "--------", false);
    oled_row(13, "STATUS", false);
    oled_row(14, phase_word(g_telemetry.phase), false);
    oled_row(15, "", false);
}

bool oled_task_user(void) {
    if (is_keyboard_master()) {
        // Left OLED goes dark on the gaming layers to signal the mode; the transit
        // map (incl. Mission Control / Token Entry) stays visible otherwise.
        if (is_gaming_layer()) {
            oled_clear();
        } else {
            route_anim_render_map(&g_journey);
        }
    } else {
        // A live burn takes over the right panel; gaming mode still wins.
        if (g_telemetry.burn && !g_telemetry.gaming) {
            render_burn();
        } else {
            render_telemetry();
        }
    }
    return false;
}

#endif // OLED_ENABLE && STARMAP_ENABLE

#if defined(OLED_ENABLE) && defined(KEYLOG_ENABLE)

// Temporary diagnostic OLED mode (KEYLOG_ENABLE=yes on the make command line, mutually
// exclusive with STARMAP_ENABLE -- see rules.mk). Shows each pressed key as a
// get_keycode_string() name, the same "keylogger" idiom several stock split-OLED
// keymaps ship (e.g. keyboards/lily58/lib/keylogger.c), but backed by the real
// keycode-string decoder so homerow-mod and layer-tap keys (TAP_A, TOG_ESC, ...) show
// correctly instead of just their base letter.

oled_rotation_t oled_init_user(oled_rotation_t rotation) {
    if (!is_keyboard_master()) {
        return OLED_ROTATION_270;
    }
    return rotation;
}

static char keylog_line[9] = "";

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        snprintf(keylog_line, sizeof(keylog_line), "%-8s", get_keycode_string(keycode));
    }
    return true;
}

bool oled_task_user(void) {
    if (is_keyboard_master()) {
        oled_write_ln_P(PSTR("KEYLOG"), false);
        oled_write_ln(keylog_line, false);
    } else {
        oled_clear();
    }
    return false;
}

#endif // OLED_ENABLE && KEYLOG_ENABLE
