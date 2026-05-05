// Normalize a parsed KLE output's x positions by shifting so min_x is 0.
// Useful when a Remap KLE places a single-half view at far-right (x>9 etc.)
// and you want a self-contained file starting at the origin.
//
// Usage: node normalize-x.mjs <parsed-input.json>
import fs from 'node:fs';

const path = process.argv[2];
const data = JSON.parse(fs.readFileSync(path, 'utf8'));
const minX = Math.min(...data.keys.map(k => k.x));
data.keys = data.keys.map(k => ({ ...k, x: +(k.x - minX).toFixed(4) }));
console.log(JSON.stringify(data, null, 2));
