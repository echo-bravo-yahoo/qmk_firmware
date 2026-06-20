All boards compile via the `qmkfm/qmk_cli` Docker image — no local QMK toolchain needed.

## [Ploopy adept](./keyboards/ploopyco/madromys)

- Bootload: Plug in while holding the bottom left button down
- Flash: copy `ploopyco_madromys_rev1_001_aeby.uf2` to the RPI drive

### Build workflow

Compilation uses the `qmkfm/qmk_cli` Docker image with a volume bind. The repo
at `~/workspace/qmk` must have the required submodules initialized (one-time):

```bash
git submodule update --init lib/chibios lib/chibios-contrib lib/pico-sdk lib/lufa lib/printf
```

To build:

```bash
docker run --rm -v ~/workspace/qmk:/qmk_firmware qmkfm/qmk_cli \
  qmk compile -kb ploopyco/madromys -km aeby
```

UF2 lands at `~/workspace/qmk/ploopyco_madromys_rev1_001_aeby.uf2`. Copy to Windows Downloads:

```bash
cp ~/workspace/qmk/ploopyco_madromys_rev1_001_aeby.uf2 \
  /mnt/c/Users/$(cmd.exe /c 'echo %USERNAME%' 2>/dev/null | tr -d '\r')/Downloads/
```

Then bootload the board and copy the UF2 to the RPI drive.

## [Corne (white, busted)](./keyboards/crkbd)

- Microcontroller: Elite-C x 2
- Bootload: Plug in while holding reset

### Build workflow

Compile via Docker (same as Ploopy Adept — Docker has the AVR toolchain):

```bash
docker run --rm -v ~/workspace/qmk:/qmk_firmware qmkfm/qmk_cli \
  qmk compile -kb crkbd/rev1 -km aeby
```

HEX lands at `~/workspace/qmk/crkbd_rev1_aeby.hex`.

### Flash workflow

Docker can't see USB, so flashing uses a local script. One-time prereqs:

```bash
sudo apt-get install dfu-programmer
```

USB passthrough from Windows (run in PowerShell, keep open or use `--auto-attach`):

```powershell
usbipd list                               # find busid for "ATm32U4DFU" / "Caterina"
usbipd attach --wsl --busid <busid> --auto-attach
```

Then flash each half from WSL. Run the script first, then bootload:

```bash
~/workspace/qmk/scripts/flash-crkbd.sh left
# bootload left half — script polls until it detects the board, then flashes
~/workspace/qmk/scripts/flash-crkbd.sh right
# bootload right half
```

The script writes the EE_HANDS EEPROM byte (handedness) in addition to the firmware.

## [Corne (blue, new)](./keyboards/crkbd)

- Microcontroller: Elite-Pi x 2
- Build left side: `qmk flash -kb crkbd/rev1 -km default -bl dfu-util-split-left -e CONVERT_TO=elite_pi`
- Build right side: `qmk flash -kb crkbd/rev1 -km default -bl dfu-util-split-right -e CONVERT_TO=elite_pi`
- Bootload: Plug in while holding reset
- Flash: Using `qmk flash` command above. Plug the bootload-mode keyboard in first.

## Ploopy trackball nano
- Build: 
- Bootload: 
- Flash: 

