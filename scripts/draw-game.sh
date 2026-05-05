#!/usr/bin/env bash
# Render a hand-authored game-binding YAML to an SVG cheat sheet.
#
# Usage: scripts/draw-game.sh <path/to/game.yaml>
# Output: <path/to/game.svg> (alongside the input).
#
# The YAML's `layout:` block can point at any QMK keyboard via
# `qmk_keyboard: <kb>` (fetches geometry from config.qmk.fm) or a local
# `qmk_info_json: <path>` for vendored boards.
#
# See games/marathon.crkbd.yaml for an example.

set -euo pipefail

repo_root() { git -C "${BASH_SOURCE[0]%/*}" rev-parse --show-toplevel; }

[[ $# -eq 1 ]] || { echo "usage: $0 <path/to/game.yaml>" >&2; exit 64; }

root=$(repo_root)
config=$root/keymap_drawer.config.yaml
yaml=$1
out=${yaml%.yaml}.svg

[[ -f $config ]] || { echo "missing config: $config" >&2; exit 1; }
[[ -f $yaml   ]] || { echo "missing input: $yaml"    >&2; exit 1; }

keymap -c "$config" draw -o "$out" -- "$yaml"

# Strip the layer-name header (game SVGs are typically embedded in an HTML
# wrapper that has its own heading; the keymap-drawer header is redundant).
# Uses a portable sed -i form (works on both macOS and GNU sed by writing
# to a tempfile).
tmp=$(mktemp -t draw-game-XXXXXX.svg)
trap 'rm -f "$tmp"' EXIT
grep -v '<text [^>]*class="label"' "$out" > "$tmp"
mv "$tmp" "$out"

echo "wrote $out"
