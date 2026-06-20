#!/usr/bin/env bash
# Flash crkbd/rev1 aeby firmware to one half using dfu-programmer.
# Mirrors the dfu-split-left / dfu-split-right make targets in
# platforms/avr/flash.mk, including the EE_HANDS EEPROM write.
#
# Usage: flash-crkbd.sh left|right
#
# Prerequisites:
#   - dfu-programmer installed (sudo apt-get install dfu-programmer)
#   - Board attached to WSL via usbipd (see below)
#
# USB passthrough (run in PowerShell before bootloading the board):
#   usbipd attach --wsl --busid <busid>
# Find the busid while the board is in DFU mode:
#   usbipd list   (look for "ATm32U4DFU" or "Caterina")
# Re-attach automatically on each bootload cycle:
#   usbipd attach --wsl --busid <busid> --auto-attach

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
HEX="$REPO_ROOT/crkbd_rev1_aeby.hex"
MCU="atmega32u4"

usage() {
    echo "Usage: $0 left|right"
    exit 1
}

[[ $# -eq 1 ]] || usage
[[ "$1" == "left" || "$1" == "right" ]] || usage
SIDE="$1"

EEP="$REPO_ROOT/quantum/split_common/eeprom-${SIDE}hand.eep"

if ! command -v dfu-programmer >/dev/null 2>&1; then
    echo "ERROR: dfu-programmer not found. Install with:" >&2
    echo "  sudo apt-get install dfu-programmer" >&2
    exit 1
fi

if [[ ! -f "$HEX" ]]; then
    echo "ERROR: firmware not found at $HEX" >&2
    echo "Run the Docker compile first:" >&2
    echo "  docker run --rm -v ~/workspace/qmk:/qmk_firmware qmkfm/qmk_cli qmk compile -kb crkbd/rev1 -km aeby" >&2
    exit 1
fi

echo "Flashing $SIDE half of crkbd (EE_HANDS)."
echo "Bootload the board now (plug in while holding Reset)."
echo "Waiting for DFU device..."

while ! dfu-programmer "$MCU" get bootloader-version >/dev/null 2>&1; do
    printf "."
    sleep 1
done
printf "\n"

dfu-programmer "$MCU" get bootloader-version

if dfu-programmer --version 2>&1 | grep -q "0\.7"; then
    dfu-programmer "$MCU" erase --force
    dfu-programmer "$MCU" flash --force --eeprom "$EEP"
    dfu-programmer "$MCU" flash --force "$HEX"
else
    dfu-programmer "$MCU" erase
    dfu-programmer "$MCU" flash-eeprom "$EEP"
    dfu-programmer "$MCU" flash "$HEX"
fi

dfu-programmer "$MCU" reset
echo "Done. Unplug and replug the $SIDE half."
