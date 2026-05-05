#pragma once

// ─── Tap-hold tuning (matches Corne aeby) ────────────────────────────────────
#define TAPPING_TERM 175
#define PERMISSIVE_HOLD
#define QUICK_TAP_TERM 25

// ─── Auto-mouse layer ────────────────────────────────────────────────────────
// Override the board's AUTO_MOUSE_DEFAULT_LAYER (was 7 for the original
// 10-layer enum) to match this keymap's compacted MOUSE index. Keep in sync
// with the enum in keymap.c.
#undef AUTO_MOUSE_DEFAULT_LAYER
#define AUTO_MOUSE_DEFAULT_LAYER 4

// Tune to taste — see README.md for the procedure.
//   THRESHOLD: trackball motion accumulator before the layer activates
//   TIME:      ms the layer stays active after motion stops
//   DELAY:     ms after a manual keypress before auto-mouse can re-trigger
//   DEBOUNCE:  ms before the layer can re-activate after deactivation
// Defaults below are the board's defaults; copy/uncomment + adjust.
// #undef AUTO_MOUSE_THRESHOLD
// #define AUTO_MOUSE_THRESHOLD 80
// #undef AUTO_MOUSE_TIME
// #define AUTO_MOUSE_TIME      750
// #undef AUTO_MOUSE_DELAY
// #define AUTO_MOUSE_DELAY     750
// #undef AUTO_MOUSE_DEBOUNCE
// #define AUTO_MOUSE_DEBOUNCE  40

// ─── Trackball / pointing-device defaults ────────────────────────────────────
// Replaces the runtime BALL_SETTINGS layer. Adjust then rebuild + reflash.
//   SPD: 0..7 (CPI = 400 + spd * 200)
//   ANGLE: 0..29 (each step = 12°; total range 360°)
//   INVERT_*: true flips X-axis on that side
// Defaults below match the board's upstream defaults; copy/uncomment + tune.
// #undef SPD_DEFAULT_LEFT
// #define SPD_DEFAULT_LEFT  3
// #undef SPD_DEFAULT_RIGHT
// #define SPD_DEFAULT_RIGHT 3
// #undef ANGLE_DEFAULT_LEFT
// #define ANGLE_DEFAULT_LEFT  8
// #undef ANGLE_DEFAULT_RIGHT
// #define ANGLE_DEFAULT_RIGHT 7
// #undef INVERT_LEFT_DEFAULT
// #define INVERT_LEFT_DEFAULT  true
// #undef INVERT_RIGHT_DEFAULT
// #define INVERT_RIGHT_DEFAULT true
// #undef INVERT_SCROLL_DEFAULT
// #define INVERT_SCROLL_DEFAULT false

// ─── D-pad ───────────────────────────────────────────────────────────────────
// true = filter simultaneous D-pad presses (no diagonals); false = allow.
// #undef DPAD_EX_DEFAULT
// #define DPAD_EX_DEFAULT true
