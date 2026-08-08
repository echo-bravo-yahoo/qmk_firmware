# qmk_firmware (personal fork)

Personal QMK fork. Custom keymaps live under `keyboards/<kb>/keymaps/aeby/`.

## Build & flash

Per-board recipes live in `aeby_readme.md` at the repo root. Do not duplicate them here. `qmk` CLI is installed via `uv tool install qmk` and is on `$PATH`.

When adding a new `rules.mk` build flag meant to override another flag (e.g. forcing one feature off when another turns on), see `.claude/docs/rules-mk-flag-ordering.md` — Make evaluates `ifeq` blocks top-to-bottom against the variable's value at that line, so the overriding block must come first in the file, not just nearby.

## Keymap visualization

`scripts/draw-keymap.sh` is the entry point. Two subcommands:

- `./scripts/draw-keymap.sh bootstrap <kb> <km>` — runs `qmk c2json | keymap parse` and writes `keymap.yaml` next to the source `keymap.c`. Safe to re-run after editing `keymap.c`; all label customization lives in `keymap_drawer.config.yaml` and is reapplied automatically.
- `./scripts/draw-keymap.sh draw <kb> <km>` — renders the YAML to `keymap.svg`.

Currently visualized:

- `crkbd/rev1 aeby` — `keyboards/crkbd/keymaps/aeby/keymap.svg`
- `ploopyco/madromys aeby` — `keyboards/ploopyco/madromys/keymaps/aeby/keymap.svg`

All visualized devices are also linked in `keymap-previews.html` (Devices group). When adding a new device, append `{ label: '…', src: 'keyboards/<path>/keymap.svg' }` to the second group's `items` array in `keymap-previews.html`.

Adding a new keymap: extend the `layer_names_for` lookup in `scripts/draw-keymap.sh`, then bootstrap + draw. Custom hold/tap legends, modifier glyphs, and keycode display strings go in `keymap_drawer.config.yaml` (never in the parsed `keymap.yaml` — those edits are wiped on re-bootstrap).

Detailed reference: `~/notes/20-29 digital/22 esoteric-input-devices/22.01 qmk/keymap-drawer.md`.

## Game cheat sheets

Hand-authored `keymap-drawer` YAMLs annotate physical key positions with what a specific game does when each key is pressed. Output: a per-game directory with one SVG per input device plus an HTML wrapper that shows them together.

Files involved:

- `games/<game>.<kb>.yaml` — keyboard binding map (one layer named after the game).
- `games/<game>.mouse.yaml` — mouse binding map (uses the shared mouse layout).
- `games/<game>.<kb>.svg` / `games/<game>.mouse.svg` — generated.
- `games/<game>.html` — side-by-side wrapper.
- `layouts/mouse.json` — reusable stylized mouse "keyboard" (thumb buttons + L / scroll-wheel / R). Reference from any mouse YAML via `layout: { qmk_info_json: layouts/mouse.json }`.
- `scripts/draw-game.sh <yaml>` — renders one YAML to a sibling `.svg`.
- `keymap-previews.html` (repo root) — tab index; lists all game HTML wrappers and device keymap SVGs.

To add a new game (`<game>` placeholder for a slug like `marathon`, `<kb>` for the QMK keyboard slug like `crkbd`):

1. Copy an existing pair as a starting point:
    ```bash
    cp games/marathon.crkbd.yaml games/<game>.<kb>.yaml
    cp games/marathon.mouse.yaml games/<game>.mouse.yaml
    cp games/marathon.html       games/<game>.html
    ```
2. Edit `games/<game>.<kb>.yaml`:
    - Set the `layout:` block to the right keyboard. Upstream QMK boards use `qmk_keyboard: <kb>/<rev>` + `layout_name: LAYOUT_*`; vendored boards use `qmk_info_json: keyboards/<vendor>/<board>/keyboard.json`.
    - Rename the layer key under `layers:` to the game's name (becomes the SVG header).
    - Replace each cell's string with the game's action label, leaving `""` for unbound positions. Cell count must match the LAYOUT exactly (count from an existing parsed `keymap.yaml` or the keyboard.json layout array).
3. Edit `games/<game>.mouse.yaml`: same pattern, but the layout already points at `layouts/mouse.json` (7 cells: thumb-fwd, thumb-back, L, wheel-up, wheel-click, wheel-down, R).
4. Edit `games/<game>.html`: change the `<title>`, the `<h1>`, and the two `<img src="...">` paths to match the new SVG filenames. Keep both `<figure>` blocks; figcaptions and the introductory `<p>` were intentionally trimmed for the Marathon page — match that minimal style or restore them per preference.
5. Render and view:
    ```bash
    ./scripts/draw-game.sh games/<game>.<kb>.yaml
    ./scripts/draw-game.sh games/<game>.mouse.yaml
    open -a Firefox keymap-previews.html
    ```
    Add `{ label: '<Game Name>', src: 'games/<game>.html' }` to the first group's `items` array in `keymap-previews.html`.
6. Iterate. Both YAMLs are hand-authored — changing a label only requires editing the YAML and re-running `draw-game.sh` on it.

The label-customization in `keymap_drawer.config.yaml` (modifier glyphs, custom keycode names) doesn't apply to game YAMLs since those entries are plain strings, not QMK keycodes — labels render verbatim.

## Vendored upstream sources

Boards not present in upstream QMK get vendored under `keyboards/<vendor>/<board>/`. Each vendored tree carries a `VENDORED.md` marker recording the source URL, branch, commit SHA, sync date, and any local patches. The marker doubles as the resync recipe.

**Currently vendored:**

- `keyboards/tarohayashi/killerwhale/{duo,solo}/` — from [thoeyz/killerwhale@tarohayashi](https://github.com/thoeyz/killerwhale/tree/tarohayashi). Marker: `keyboards/tarohayashi/killerwhale/VENDORED.md`.

**Discovery procedure** (run before assuming a board needs vendoring):

1. `gh search prs --repo qmk/qmk_firmware "<board>"` — upstream PR.
2. `find keyboards -iname '*<board>*'` — already vendored.
3. `gh search repos "<board>"` and `gh search code "<board>"` — community fork. For boards with a known maintainer, walk their fork's branches: `gh api repos/<owner>/<repo>/branches --paginate | jq -r '.[].name'`.
4. Product pages / vendor docs.

**Vendor procedure** when source is found:

1. `git fetch <fork-url> <branch>` then `git checkout FETCH_HEAD -- keyboards/<path>`.
2. Drop `keyboards/<vendor>/<board>/VENDORED.md` recording source URL, branch, commit SHA, sync date, local patches.
3. Verify build: `qmk compile -kb <vendor>/<board> -km default` (defer to `swift` if no local toolchain).
4. Add a `keymaps/aeby/` skeleton (copy from `default`).
5. Add layer-name entry to `scripts/draw-keymap.sh` `layer_names_for` once layer names are settled.

**Known unavailable** (as of 2026-05-04): Binepad BNK16 source not public — only flashable binaries at [`binepad-global/firmware`](https://github.com/binepad-global/firmware). Hardware ships Aug 2026; recheck upstream + community forks when source surfaces.

## Worktree workflow

This repo uses git worktrees under `.claude/worktrees/`. Run all commands from the worktree, never the main checkout. Land changes via `worktree-land` per the user's global rules.

## Permissions

Project-level allowlist lives in `.config/cc-allow.toml`. Currently allows `qmk`, `keymap`, and `scripts/**` so the visualization workflow runs without prompts.
