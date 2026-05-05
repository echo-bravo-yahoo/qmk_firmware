// Merge parse-remap-kle.mjs output into a vendored keyboard.json's
// `layouts.LAYOUT.layout` array, pairing entries by matrix [row, col].
//
// Usage: node merge-remap-into-keyboard-json.mjs <kbd-json> [parsed-json]
//   kbd-json: path to the vendored keyboard.json (modified in place)
//   parsed-json: output of parse-remap-kle.mjs (default /tmp/kw-parsed.json)
import fs from 'node:fs';

const kbjsonPath = process.argv[2] || 'keyboards/tarohayashi/killerwhale/duo/keyboard.json';
const parsedPath = process.argv[3] || '/tmp/kw-parsed.json';
const parsed = JSON.parse(fs.readFileSync(parsedPath, 'utf8'));
const kb = JSON.parse(fs.readFileSync(kbjsonPath, 'utf8'));

// Index parsed keys by "row,col"
const byMatrix = new Map();
for (const k of parsed.keys) {
  byMatrix.set(`${k.matrix[0]},${k.matrix[1]}`, k);
}

// Walk the keyboard.json layout, keep label+matrix, replace x/y/w/h/r.
const merged = kb.layouts.LAYOUT.layout.map(entry => {
  const key = `${entry.matrix[0]},${entry.matrix[1]}`;
  const pos = byMatrix.get(key);
  if (!pos) {
    console.error(`no Remap position for matrix ${key} (label=${entry.label})`);
    return entry;
  }
  const out = {
    label: entry.label,
    matrix: entry.matrix,
    x: pos.x,
    y: pos.y,
  };
  if (pos.w !== 1) out.w = pos.w;
  if (pos.h !== 1) out.h = pos.h;
  if (pos.r) {
    out.r = pos.r;
    out.rx = pos.rx;
    out.ry = pos.ry;
  }
  return out;
});

// Emit a minimal info.json compatible with keymap-drawer's --qmk-info-json.
const info = {
  keyboard_name: kb.keyboard_name || 'tarohayashi/killerwhale/duo',
  layouts: { LAYOUT: { layout: merged } },
};

console.log(JSON.stringify(info, null, 2));
