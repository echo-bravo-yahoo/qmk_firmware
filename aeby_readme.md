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
- Build left side: `qmk flash -kb crkbd/rev1 -km aeby -bl dfu-splift-left`
- Build right side: `qmk flash -kb crkbd/rev1 -km aeby -bl dfu-splift-right`
- Bootload: Plug in while holding reset
- Flash: Using `qmk flash ...` command above. Run `qmk flash ...` before plugging the bootload-mode keyboard in.

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

