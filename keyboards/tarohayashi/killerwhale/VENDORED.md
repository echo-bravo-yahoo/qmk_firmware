# Vendored upstream

| Field | Value |
|-|-|
| Source | https://github.com/thoeyz/killerwhale |
| Branch | `tarohayashi` |
| Commit | `c43d5d377338ed0201f322c719960383010df67b` |
| Sync date | 2026-05-04 |
| Local patches | Both `duo/keyboard.json` and `solo/keyboard.json` `layouts.LAYOUT.layout` arrays were replaced with physical coordinates extracted from Remap's KLE definitions (the vendored upstream has all keys at `x:0, y:0`, which keymap-drawer can't render). Remap Firestore IDs: `OZfNLOgMBiX25qyo3ZXv` (duo), `0Dr7EqE8YIRub3OX02br` (solo). Re-apply after a resync via the helpers in `scripts/vendor/`. |

The KillerWhale board is not present in upstream QMK. `thoeyz/killerwhale@tarohayashi` is the maintainer-blessed fork (commit message "0.27.1") that produces the binaries [Taro-Hayashi releases](https://github.com/Taro-Hayashi/KillerWhale/releases). The version-pinned `tarohayashi_0.23.9` branch is the alternative if upgrading to current QMK breaks anything.

To resync:

```bash
git fetch https://github.com/thoeyz/killerwhale.git tarohayashi
git checkout FETCH_HEAD -- keyboards/tarohayashi/killerwhale
```

Then update Source/Commit/Sync date above.
