# qmk_firmware (personal fork)

Personal QMK fork. Custom keymaps live under `keyboards/<kb>/keymaps/aeby/`.

## Build & flash

Per-board recipes live in `aeby_readme.md` at the repo root. Do not duplicate them here. `qmk` CLI is installed via `uv tool install qmk` and is on `$PATH`.

## Keymap visualization

`scripts/draw-keymap.sh` is the entry point. Two subcommands:

- `./scripts/draw-keymap.sh bootstrap <kb> <km>` — runs `qmk c2json | keymap parse` and writes `keymap.yaml` next to the source `keymap.c`. Safe to re-run after editing `keymap.c`; all label customization lives in `keymap_drawer.config.yaml` and is reapplied automatically.
- `./scripts/draw-keymap.sh draw <kb> <km>` — renders the YAML to `keymap.svg`.

Currently visualized:
- `crkbd/rev1 aeby` — `keyboards/crkbd/keymaps/aeby/keymap.svg`
- `ploopyco/madromys aeby` — `keyboards/ploopyco/madromys/keymaps/aeby/keymap.svg`

Adding a new keymap: extend the `layer_names_for` lookup in `scripts/draw-keymap.sh`, then bootstrap + draw. Custom hold/tap legends, modifier glyphs, and keycode display strings go in `keymap_drawer.config.yaml` (never in the parsed `keymap.yaml` — those edits are wiped on re-bootstrap).

Detailed reference: `~/notes/20-29 digital/22 esoteric-input-devices/22.01 qmk/keymap-drawer.md`.

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
