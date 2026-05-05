// Parse a Remap KLE-format `layouts.keymap` block and emit a flat list of
// {x, y, w, h, r, rx, ry, matrix: [row, col]} entries.
//
// Input: a Remap definition JSON (the "json" stringValue from the Firestore
// document at keyboards/v2/definitions/<id>). Fetch with:
//   curl -s 'https://firestore.googleapis.com/v1/projects/remap-b2d08/databases/(default)/documents/keyboards/v2/definitions/<id>' \
//     | jq -r '.fields.json.stringValue' > kle.json
//
// Some keyboards (e.g. KillerWhale solo) draw multiple views of the same
// hardware in one KLE — labels carry an orientation suffix like
// "0,3\n\n\n0,0" (matrix [0,3], variant 0,0) vs "0,3\n\n\n0,1" (variant 0,1).
// Pass --variant=0,0 (or any "row,col" pair) to filter to one view; default
// keeps all (first-occurrence-wins per matrix).
//
// Usage: node parse-remap-kle.mjs <kle-input.json> [--variant=0,0]
import fs from 'node:fs';

const inputPath = process.argv[2] || '/tmp/kw-kle.json';
const variantArg = process.argv.find(a => a.startsWith('--variant='));
const variantFilter = variantArg ? variantArg.slice('--variant='.length) : null;
const data = JSON.parse(fs.readFileSync(inputPath, 'utf8'));
const rows = data.layouts.keymap;

// KLE walker. State accumulates between rows.
let x = 0, y = 0;
let r = 0, rx = 0, ry = 0;

const keys = [];
for (const row of rows) {
  // After processing a row, x resets to rx and y advances by 1 (or whatever
  // the previous y delta dictated). KLE's behavior: at row start, set x=rx
  // and y+=1 (with y deltas from inline objects further adjusting).
  x = rx;
  y += 1;
  let pendingW = 1, pendingH = 1;
  for (const item of row) {
    if (typeof item === 'object') {
      // Position/size modifiers for the next key
      if (item.x !== undefined) x += item.x;
      if (item.y !== undefined) y += item.y;
      if (item.w !== undefined) pendingW = item.w;
      if (item.h !== undefined) pendingH = item.h;
      if (item.r !== undefined) r = item.r;
      if (item.rx !== undefined) { rx = item.rx; x = rx; }
      if (item.ry !== undefined) { ry = item.ry; y = ry; }
    } else if (typeof item === 'string') {
      // The string is the key's label/legend. Format: lines separated by \n,
      // with positional slots. The matrix label is at line index 3 in this
      // dataset based on inspection (e.g., "\n\n\n0,0\n\n\n\n\n\ne1" has
      // matrix at line 3: "0,0", or "0,3\n\n\n0,0" has matrix at line 0).
      // Strategy: find the first line that looks like "<int>,<int>".
      const lines = item.split('\n');
      // First "<int>,<int>" line is the matrix coord; second (if present) is
      // the variant marker for keyboards that draw multiple views.
      const coordLines = [];
      for (const ln of lines) {
        const m = ln.match(/^(\d+),(\d+)$/);
        if (m) coordLines.push([+m[1], +m[2]]);
      }
      const matrix = coordLines[0] || null;
      const variant = coordLines[1] ? `${coordLines[1][0]},${coordLines[1][1]}` : null;
      const encoderId = lines.find(l => /^e\d+$/.test(l)) || null;
      const skipForVariant = variantFilter && variant && variant !== variantFilter;
      if (!skipForVariant) keys.push({
        x: +x.toFixed(4),
        y: +y.toFixed(4),
        w: pendingW,
        h: pendingH,
        r: r || undefined,
        rx: r ? rx : undefined,
        ry: r ? ry : undefined,
        matrix,
        variant,
        encoderId,
        rawLabel: item,
      });
      x += pendingW;
      pendingW = 1;
      pendingH = 1;
    }
  }
}

// Filter to true matrix keys only (skip encoders — they carry a
// placeholder [0,0] matrix label but are not in keyboard.json's matrix
// layout). The KLE also includes a duplicate set with offset "0,1" for
// secondary views; first occurrence per (matrix) wins.
const matrixKeys = keys.filter(k => k.matrix && !k.encoderId);
const seen = new Set();
const primary = [];
for (const k of matrixKeys) {
  const key = `${k.matrix[0]},${k.matrix[1]}`;
  if (seen.has(key)) continue;
  seen.add(key);
  primary.push(k);
}

console.log(JSON.stringify({
  totalRows: rows.length,
  totalKeys: keys.length,
  matrixKeys: matrixKeys.length,
  primaryUnique: primary.length,
  encoders: keys.filter(k => k.encoderId).length,
  keys: primary,
}, null, 2));
