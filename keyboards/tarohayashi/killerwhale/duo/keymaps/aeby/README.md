# aeby keymap — KillerWhale duo

Corne-style typing comforts on the KillerWhale duo: full home-row mods, layer-tap thumbs, fast tap-hold timing. KillerWhale hardware-specific functions (trackball auto-mouse, RGB lighting, OLED, encoders, hardware toggles) preserved.

## Layers at a glance

| # | Name | Activation |
|---|---|---|
| 0 | Base | default |
| 1 | Numbers & Symbols | hold left side-switch bottom OR right side-switch top |
| 2 | Navigation | hold left side-switch top OR right side-switch bottom |
| 3 | Function & Media | hold left or right add-unit closer-to-alphas |
| 4 | Mouse | trackball motion (auto-mouse) |
| 5 | Light Settings | hold KC_5 / KC_6 on the top row |
| 6 | Gaming | hold left hardware toggle |

Visualization: [`keymap.svg`](./keymap.svg).

## Tuning constants

Several behaviors are compile-time defaults that get baked into the firmware. To change them, edit [`config.h`](./config.h), then rebuild and reflash. Recommended workflow: flash with the defaults below, use the keyboard for a day or two, then adjust whatever feels off.

### Auto-mouse layer

The trackball is the *only* activator for the Mouse layer — moving it past `AUTO_MOUSE_THRESHOLD` units triggers it; no motion for `AUTO_MOUSE_TIME` ms deactivates it. Tune to taste.

| Constant | Default | Meaning |
|---|---|---|
| `AUTO_MOUSE_THRESHOLD` | `80` | Movement units before the layer activates. Higher = harder to trigger accidentally. |
| `AUTO_MOUSE_TIME` | `750` | ms the layer stays active after the last motion event. |
| `AUTO_MOUSE_DELAY` | `750` | ms after a regular keypress before auto-mouse can re-trigger. Prevents accidental click-during-typing. |
| `AUTO_MOUSE_DEBOUNCE` | `40` | ms before the layer can re-activate after deactivation. |

Confirm: cursor click feels natural, no spurious activations during typing, no annoying delays after typing pauses.

### Trackball / pointing-device

Replaces the runtime BALL_SETTINGS layer that the default keymap had. Set once, rebuild, done.

| Constant | Range / default | Meaning |
|---|---|---|
| `SPD_DEFAULT_LEFT` / `_RIGHT` | `0..7` (default `3`) | Speed step. CPI = 400 + spd × 200. Higher = faster cursor. |
| `ANGLE_DEFAULT_LEFT` / `_RIGHT` | `0..29` (defaults `8` / `7`) | Cursor-motion angle offset. Each step = 12°. Compensates for the trackball's physical mounting tilt — adjust until "up" on the ball means up on screen. |
| `INVERT_LEFT_DEFAULT` / `_RIGHT_DEFAULT` | `true` / `false` (defaults `true`) | Flip X-axis for that side's trackball. |
| `INVERT_SCROLL_DEFAULT` | `true` / `false` (default `false`) | Flip scroll direction (natural vs. line-style). |
| `SENSITIVITY_MULTIPLIER` | `1.1` | Temporary boost when motion is small. Higher = more sensitive to micro-moves. |
| `SENSITIVITY_DIVISOR` | `0.5` | Final sensitivity adjustment. Lower = faster. |
| `SMOOTHING_FACTOR` | `0.7` | How much previous motion influences the current frame. Higher = smoother but laggier. |
| `CPI_SLOW` | `300` | CPI while `Slow` (`QK_USER_4`) is held — for precise cursor work. |
| `AMP_SLOW` | `4.0` | Amplification while `Slow` is held. |

### D-pad

| Constant | Default | Meaning |
|---|---|---|
| `DPAD_EX_DEFAULT` | `true` | If `true`, simultaneous D-pad presses are filtered (no diagonals). Set `false` to allow diagonal input (e.g., for fighting games). |

### Joystick

| Constant | Default | Meaning |
|---|---|---|
| `JOYSTICK_OFFSET_MIN_DEFAULT` | `50` | Dead-zone for small joystick movements (max 200). |
| `JOYSTICK_OFFSET_MAX_DEFAULT` | `0` | Threshold beyond which joystick movement is ignored (max 200). |
| `SCROLL_DIVISOR` | `100.0` | Scroll-mode divisor. Lower = scroll faster per joystick deflection. |
| `JOYSTICK_DIVISOR` | `40.0` | Cursor-mode divisor. Lower = cursor moves faster per joystick deflection. |

### OLED

| Constant | Default | Meaning |
|---|---|---|
| `OLED_DEFAULT` | `true` | `true` shows current layer; `false` shows numeric stats. |
| `INTERRUPT_TIME` | `600` | ms an interrupt-display stays before reverting to default. |

### RGB lighting

| Constant | Default | Meaning |
|---|---|---|
| `RGB_LAYER_DEFAULT` | `true` | If `true`, RGB color tracks the active layer. Toggle at runtime via `RGB/Layer` (`QK_USER_15`) on Light Settings. |

### Mode-change tap window

| Constant | Default | Meaning |
|---|---|---|
| `TERM_TEMP` | `100` | Tap-window for keys like `Slow` / `Scroll` that have both a momentary mode and an alternate tap action. |

### Tap-hold (home-row mods, layer-tap)

| Constant | Default | Meaning |
|---|---|---|
| `TAPPING_TERM` | `175` | ms threshold to register a key as a hold. Lower = faster mods, more accidental holds. |
| `PERMISSIVE_HOLD` | enabled | If a second key is pressed and released while a tap-hold is held, the tap-hold registers as a hold. Helps with rolls. |
| `QUICK_TAP_TERM` | `25` | ms after a tap during which a re-press always registers as a tap (not a hold). Helps with double-tap. |

## Building the SVG

The visualization is a generated SVG showing every layer.

```bash
# One-time install (uses uv tool isolation; pipx works too)
uv tool install keymap-drawer
uv tool install qmk

# From the worktree root:
./scripts/draw-keymap.sh bootstrap tarohayashi/killerwhale/duo aeby   # parse keymap.c -> keymap.yaml
./scripts/draw-keymap.sh draw      tarohayashi/killerwhale/duo aeby   # render keymap.yaml -> keymap.svg
```

`bootstrap` is safe to re-run after editing `keymap.c` — label customizations live in `keymap_drawer.config.yaml`, not in the parsed YAML.

## Building the firmware

Requires the QMK toolchain (`avr-gcc` for AVR boards, `arm-none-eabi-gcc` for ARM/RP2040 boards). `qmk setup` once, then:

```bash
qmk compile -kb tarohayashi/killerwhale/duo -km aeby
```

Output goes to `<worktree-root>/.build/` and a top-level `tarohayashi_killerwhale_duo_aeby.uf2`.

The Mac in `~/workspace/qmk_firmware` does not have the toolchain installed by default. The user's documented dev box for QMK builds is `swift` (Windows). Run `qmk compile` there.

## Flashing

The KillerWhale duo runs RP2040 (Raspberry Pi Pico–based). Each half flashes independently.

1. Plug one half into USB while holding its bootloader button (or double-tap reset, depending on the controller). The half mounts as `RPI-RP2`.
2. Drag-drop the `.uf2` onto the mass-storage volume. It auto-resets and runs the new firmware.
3. Repeat for the other half.

Or, with `qmk flash`:

```bash
qmk flash -kb tarohayashi/killerwhale/duo -km aeby
```

Run this once per half, with that half in bootloader mode, while the other half is disconnected.

After both halves are flashed, reconnect the TRRS / TRS link between them and verify both report the new firmware version (the OLED defaults to showing the active layer; if it doesn't update on layer change, the firmware didn't take).
