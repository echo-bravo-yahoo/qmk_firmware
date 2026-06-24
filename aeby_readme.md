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

## [Corne (blue, new)](./keyboards/crkbd) — daily driver

- Microcontroller: Elite-Pi × 2 (RP2040)
- Keymap: `aeby` (homerow mods A/S/D/F = GUI/Alt/Ctrl/Shift, mirrored; 6 layers)
- Left OLED: procedurally generated star-system transit map (Alien aesthetic) — the USCSS Patna
  crawls a route to an `LV-NNN` moon in real time, then a new system generates on arrival. Right OLED:
  data-driven mission telemetry (system name / LV destination / ETA), synced from the master half.
  Generator, world model, and preview/test tooling: `keyboards/crkbd/keymaps/aeby/preview/README.md`
- Bootload: **double-tap the reset button** — the `RPI-RP2` USB drive appears in Windows
- Flash: drag the matching UF2 onto the `RPI-RP2` drive, or use QMK Toolbox auto-flash

### Build workflow

Each half needs its own UF2 — EE_HANDS handedness (left vs. right USB host) is compiled
into the firmware. Flashing the wrong UF2 on a half swaps primary/secondary assignment.

```bash
# Left half
docker run --rm -v ~/workspace/qmk:/qmk_firmware qmkfm/qmk_cli \
  make -C /qmk_firmware crkbd/rev1:aeby:uf2-split-left CONVERT_TO=elite_pi
cp ~/workspace/qmk/crkbd_rev1_aeby_elite_pi.uf2 \
  /mnt/c/Users/heron/Downloads/crkbd_left.uf2

# Right half
docker run --rm -v ~/workspace/qmk:/qmk_firmware qmkfm/qmk_cli \
  make -C /qmk_firmware crkbd/rev1:aeby:uf2-split-right CONVERT_TO=elite_pi
cp ~/workspace/qmk/crkbd_rev1_aeby_elite_pi.uf2 \
  /mnt/c/Users/heron/Downloads/crkbd_right.uf2
```

The flash step inside Docker fails (no USB access) — that error is expected. Flash via Windows:

1. Double-tap reset on one half → `RPI-RP2` drive mounts in Explorer
2. Copy `crkbd_left.uf2` or `crkbd_right.uf2` onto the drive
3. Drive dismounts automatically; board reboots with new firmware
4. Repeat for the other half

### Keymap

Visual reference: `keyboards/crkbd/keymaps/aeby/keymap.svg` (regenerate with
`./scripts/draw-keymap.sh draw crkbd/rev1 aeby`).

| Layer | Name    | Activated by              | Description |
|-------|---------|---------------------------|-------------|
| 0     | Default | (base)                    | QWERTY with homerow mods: A=GUI S=Alt D=Ctrl F=Shift, mirrored on right |
| 1     | Symbols | hold SPC or BSPC          | Numbers 1–0, parens, brackets, `` ` = - ' \ `` |
| 2     | Nav     | hold ESC or ENT           | Arrow keys (both hands), PgUp/PgDn/Home/End |
| 3     | Media   | hold TAB or DEL           | F1–F12, one-shot mods, media controls, mouse buttons; right top-right key = `TG(4)` |
| 4     | Gaming  | `TG(4)` from layer 3      | No homerow mods, standard QWERTY + explicit Shift/Ctrl/Alt; right top-right = `TO(0)`, right bottom-right = `TG(5)` |
| 5     | G-Nav   | `TG(5)` from layer 4      | Arrow keys on WASD; overlays Gaming layer |

Gaming (layer 4) is a **persistent toggle** — not a hold layer. `TG(4)` to enter, `TO(0)` to exit.
On gaming layers the left OLED goes dark and the right swaps mission telemetry for a
`GAMING / HRM DISABLD` readout.

## Ploopy trackball nano
- Build: 
- Bootload: 
- Flash: 

