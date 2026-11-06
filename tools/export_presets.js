// Writes the JavaScript version's built-in presets as `.fh-preset.json` files into presets/, so
// the C++ version has them too.
// Needs Node:      node tools/export_presets.js
// The files are exactly what the JavaScript "export" button writes, so they also load in the web
// version.
const fs = require('fs'),
  path = require('path');
const Core = require('../js/falsehorizon-core.js');
const dir = path.join(__dirname, '..', 'presets');
fs.mkdirSync(dir, { recursive: true });
for (const f of fs.readdirSync(dir))
  if (f.endsWith('.fh-preset.json')) fs.unlinkSync(path.join(dir, f));
let n = 0;
for (const p of Core.builtins()) {
  const file =
    p.name
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, '-')
      .replace(/^-|-$/g, '') + '.fh-preset.json';
  fs.writeFileSync(path.join(dir, file), Core.exportPreset(p) + '\n');
  n++;
}
console.log(`${n} presets written to presets/`);
