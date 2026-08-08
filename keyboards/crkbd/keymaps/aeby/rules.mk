# ── Transit-map sim (left OLED = star-system map, right OLED = telemetry) ──────
# Heavy by design: float trig, the world generator, and the OLED rasterizer. Fits
# the RP2040 (blue PCB / Elite-Pi) with room to spare, but overflows the
# ATmega32U4's 32K flash by ~15K — so it is auto-excluded on AVR (the white PCB /
# Elite-C). Everything else in the keymap (layers, homerow mods, RGB) is
# unaffected; the OLEDs simply stay blank on a build without the sim.
#
# Override on the make command line, e.g. `STARMAP_ENABLE=no` to force it off on
# RP2040, or `STARMAP_ENABLE=yes` to force it on (it will not link on AVR).
STARMAP_ENABLE ?= yes
# Auto-exclude only on a bare ATmega32U4 target (its 32K flash can't hold the sim).
# A CONVERT_TO converter (e.g. elite_pi → RP2040, the daily driver) swaps the MCU
# *after* this file is parsed, so MCU still reads atmega32u4 here — require an empty
# CONVERT_TO too, or the RP2040 daily-driver build would wrongly lose the sim.
ifeq ($(strip $(MCU)),atmega32u4)
    ifeq ($(strip $(CONVERT_TO)),)
        STARMAP_ENABLE = no
    endif
endif

# Temporary diagnostic OLED mode: shows get_keycode_string() output for each keypress
# instead of the transit-map sim, to visually verify homerow-mod / layer-tap keycodes
# decode the way they're meant to. Build with `KEYLOG_ENABLE=yes` on the command line;
# forces STARMAP_ENABLE off (must run before the ifeq below) so only one
# oled_task_user()/process_record_user() pair gets compiled in. Not part of the
# daily-driver build.
KEYLOG_ENABLE ?= no
ifeq ($(strip $(KEYLOG_ENABLE)),yes)
    STARMAP_ENABLE = no
    OPT_DEFS += -DKEYLOG_ENABLE
    KEYCODE_STRING_ENABLE = yes
endif

ifeq ($(strip $(STARMAP_ENABLE)),yes)
    OPT_DEFS += -DSTARMAP_ENABLE
    SRC += oled_gfx.c
    SRC += starmap_world.c
    SRC += route_gen.c
    SRC += route_anim.c
endif
