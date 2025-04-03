i last flashed this on swift with `qmk flash -kb crkbd/rev1 -km aeby -bl dfu-splift-left` for the left-hand side and `qmk flash -kb crkbd/rev1 -km aeby -bl dfu-splift-right` for the right-hand side.

for the new corne, do `qmk flash -kb crkbd/rev1 -km aeby -bl dfu-util-split-left -e CONVERT_TO=elite_pi` and `qmk flash -kb crkbd/rev1 -km aeby -bl dfu-util-split-right -e CONVERT_TO=elite_pi`. but! you flash it by copying the .u2f file to the raspberry pi drive. to put the new keyboard into bootloader mode, double tap the reset button on the corne next to the TRRS jack.
