#!/usr/bin/env bash
# Bootstrap or redraw a keymap-drawer SVG for one of this repo's keymaps.
#
# Usage:
#   scripts/draw-keymap.sh bootstrap <kb> <km>
#       parse keymap.c -> keymap.yaml (safe to re-run)
#   scripts/draw-keymap.sh draw <kb> <km> [--split=left|right]
#       render keymap.yaml -> keymap.svg (or keymap.<split>-hand.svg)
#
# Layer names per <kb>/<km> are encoded below. Add new entries when visualizing
# a new keymap. Label customization (hold legends, modifier glyphs, etc.) lives
# in keymap_drawer.config.yaml at the repo root.
#
# --split is for hardware where one physical device represents both a left and
# a right hand (e.g., KillerWhale solo). The flag selects:
#   - which info JSON to use for positions
#       (left -> keyboard.json or info.left-hand.json; right -> info.right-hand.json)
#   - which subset of layers to render (left-only vs right-only, plus shared)
#   - the output filename suffix.

set -euo pipefail

repo_root() {
  git -C "${BASH_SOURCE[0]%/*}" rev-parse --show-toplevel
}

# Full layer set for the keymap (used by bootstrap to name parsed layers).
layer_names_for() {
  case "$1/$2" in
    crkbd/rev1/aeby)
      printf '%s\n' Base "Numbers & Symbols" Navigation "Function & Media" Gaming "Gaming Arrows"
      ;;
    ploopyco/madromys/aeby)
      printf '%s\n' Base
      ;;
    tarohayashi/killerwhale/duo/default)
      printf '%s\n' Base "On/Off" "Off/On" "On/On" Mouse "Ball Settings" "Light Settings"
      ;;
    tarohayashi/killerwhale/duo/aeby)
      printf '%s\n' Base "Numbers & Symbols" Navigation "Function & Media" Mouse "Light Settings" Gaming
      ;;
    tarohayashi/killerwhale/solo/default | tarohayashi/killerwhale/solo/aeby)
      printf '%s\n' "Left Base" "Right Base" Mouse "Ball Settings" "Light Settings"
      ;;
    *)
      return 1
      ;;
  esac
}

# Subset of layer names to render for a given --split (left|right). Echoes
# nothing and returns 1 if --split is unsupported for this <kb>/<km>.
split_layers_for() {
  case "$1/$2/$3" in
    tarohayashi/killerwhale/solo/*/left)
      printf '%s\n' "Left Base" Mouse "Ball Settings" "Light Settings"
      ;;
    tarohayashi/killerwhale/solo/*/right)
      printf '%s\n' "Right Base" Mouse "Ball Settings" "Light Settings"
      ;;
    *)
      return 1
      ;;
  esac
}

usage() {
  cat >&2 <<EOF
usage: $0 bootstrap <kb> <km>
       $0 draw <kb> <km> [--split=left|right]
EOF
  exit 64
}

cmd=${1:-}
kb=${2:-}
km=${3:-}
split=
for arg in "${@:4}"; do
  case $arg in
    --split=*) split=${arg#--split=} ;;
    *) echo "unknown arg: $arg" >&2; usage ;;
  esac
done
[[ -z $cmd || -z $kb || -z $km ]] && usage
[[ -n $split && $split != left && $split != right ]] && { echo "--split must be left or right" >&2; exit 64; }

root=$(repo_root)
config=$root/keymap_drawer.config.yaml

# Locate the keymap dir. QMK lets `keymaps/<km>/` live at any ancestor of
# `keyboards/<kb>/`, so walk up from kb until we find one.
keymap_dir=
candidate=$root/keyboards/$kb
while [[ $candidate != $root/keyboards && $candidate != / ]]; do
  if [[ -d $candidate/keymaps/$km ]]; then
    keymap_dir=$candidate/keymaps/$km
    break
  fi
  candidate=${candidate%/*}
done

src=$keymap_dir/keymap.c
yaml=$keymap_dir/keymap.yaml
if [[ -n $split ]]; then
  svg=$keymap_dir/keymap.${split}-hand.svg
else
  svg=$keymap_dir/keymap.svg
fi

[[ -f $config ]] || { echo "missing config: $config" >&2; exit 1; }
[[ -n $keymap_dir ]] || { echo "no keymap dir found for $kb/$km" >&2; exit 1; }

case "$cmd" in
  bootstrap)
    [[ -n $split ]] && { echo "--split is not valid for bootstrap; YAML is shared across splits" >&2; exit 64; }
    [[ -f $src ]] || { echo "missing source: $src" >&2; exit 1; }
    layer_names=()
    while IFS= read -r ln; do layer_names+=("$ln"); done < <(layer_names_for "$kb" "$km") || {
      echo "no layer-name table entry for $kb/$km — add one to ${BASH_SOURCE[0]##*/}" >&2
      exit 1
    }
    tmp_json=$(mktemp -t keymap-drawer-XXXXXX.json)
    trap 'rm -f "$tmp_json"' EXIT
    # qmk c2json runs the C preprocessor by default; some keymaps include
    # neighbor headers (e.g. "lib/foo.h") that don't resolve outside the QMK
    # build tree. Fall back to --no-cpp if preprocessing fails.
    qmk c2json -kb "$kb" -km "$km" -o "$tmp_json" "$src" >/dev/null 2>&1 \
      || qmk c2json -kb "$kb" -km "$km" --no-cpp -o "$tmp_json" "$src" >/dev/null
    keymap -c "$config" parse -q "$tmp_json" --layer-names "${layer_names[@]}" > "$yaml"
    echo "wrote $yaml"
    ;;
  draw)
    [[ -f $yaml ]] || { echo "missing $yaml — run bootstrap first" >&2; exit 1; }

    # Resolve the info JSON for positions.
    # - With --split=left: prefer info.left-hand.json then keyboard.json/info.json
    # - With --split=right: require info.right-hand.json
    # - Without --split: walk up from <kb> for any keyboard.json/info.json
    #   that has a non-empty `layouts` field.
    info_json=
    if [[ $split == right ]]; then
      cand=$root/keyboards/$kb/info.right-hand.json
      [[ -f $cand ]] || { echo "missing $cand for --split=right" >&2; exit 1; }
      info_json=$cand
    elif [[ $split == left ]]; then
      for cand in "$root/keyboards/$kb/info.left-hand.json" "$root/keyboards/$kb/keyboard.json" "$root/keyboards/$kb/info.json"; do
        if [[ -f $cand ]] && jq -e '.layouts // empty | length > 0' "$cand" >/dev/null 2>&1; then
          info_json=$cand; break
        fi
      done
      [[ -n $info_json ]] || { echo "no left-hand info JSON found under $root/keyboards/$kb/" >&2; exit 1; }
    else
      walk=$root/keyboards/$kb
      while [[ $walk != $root/keyboards && $walk != / ]]; do
        for cand in "$walk/keyboard.json" "$walk/info.json"; do
          if [[ -f $cand ]] && jq -e '.layouts // empty | length > 0' "$cand" >/dev/null 2>&1; then
            info_json=$cand; break 2
          fi
        done
        walk=${walk%/*}
      done
    fi

    # Resolve layer subset for split renders.
    select_layers=()
    if [[ -n $split ]]; then
      while IFS= read -r ln; do select_layers+=("$ln"); done < <(split_layers_for "$kb" "$km" "$split") || {
        echo "no split-layer table entry for $kb/$km/$split — add one to ${BASH_SOURCE[0]##*/}" >&2
        exit 1
      }
    fi

    args=(-c "$config" draw)
    [[ -n $info_json ]] && args+=(-j "$info_json")
    if (( ${#select_layers[@]} )); then
      args+=(-s "${select_layers[@]}")
    fi
    args+=(-o "$svg" -- "$yaml")
    keymap "${args[@]}"
    echo "wrote $svg"
    ;;
  *)
    usage
    ;;
esac
