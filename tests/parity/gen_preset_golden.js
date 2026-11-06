// Records what the JavaScript preset loader (FalseHorizons/js/falsehorizon-
// core.js::parsePresetFile, the reference) does with a list of files, so the C++ port can be held
// to the same answers. Writes preset.golden next to this file. Needs Node; the result is committed,
// so only run it when the
// reference or the list of inputs changes:      node tests/parity/gen_preset_golden.js
//
// One case is two lines: T <the file's text, as a JSON string> R <what parsePresetFile returned, as
// JSON>: {"ok":false,"error":"..."} or {"ok":true,"preset":{...}} The inputs: hand-written good and
// bad files, every built-in preset, and a few hundred built-ins with fields damaged at random
// (wrong types, out-of-range numbers, broken formulas). ASCII except a few BMP-only names (the C++
// counts UTF-16 units like JavaScript, but a cut through the middle of a surrogate pair is not
// reproduced).
const fs = require('fs'),
  path = require('path');
const Core = require('../../js/falsehorizon-core.js');
const Raw = require('../../js/falsehorizon-presets.js');

let seed = 20261009;
const rnd = () => {
  seed |= 0;
  seed = (seed + 0x6d2b79f5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
};
const pick = list => list[Math.floor(rnd() * list.length)];

const inputs = [];
const add = (...t) => inputs.push(...t);
const file = (obj, extra) => JSON.stringify({ 'fh-preset': 1, ...obj, ...(extra || {}) });
const pad = n => 'a = 1;' + ' '.repeat(n - 6);

// ---------- not a preset at all ----------
add(
  '',
  ' ',
  '{',
  '}',
  '[]',
  '{}',
  'null',
  'true',
  '1',
  '"x"',
  '[1,2]',
  '{"fh-preset":1}',
  '{"fh-preset":2,"name":"x"}',
  '{"fh-preset":"1","name":"x"}',
  '{"fh-preset":true,"name":"x"}',
  '{"fh-preset":1.0,"name":"x"}',
  '{"fh-preset":1e0,"name":"x"}',
  '{"fh-preset":[1],"name":"x"}',
  '{"fh-preset":null,"name":"x"}',
  '{"other":1,"name":"x"}',
  '{"fh-preset":1,"name":"x",}',
  '{"fh-preset":1,"name":"x"} x',
  '{"fh-preset":1 "name":"x"}',
  "{'fh-preset':1,'name':'x'}",
  '{"fh-preset":1,"name":"x"}//c',
  '{"fh-preset":1,"name":"x","zoom":01}',
  '{"fh-preset":1,"name":"x","zoom":.5}',
  '{"fh-preset":1,"name":"x","zoom":+1}',
  '{"fh-preset":1,"name":"x","zoom":NaN}',
  '{"fh-preset":1,"name":"x","zoom":Infinity}',
  '{"fh-preset":1,"name":"x","zoom":1e}',
  '{"fh-preset":1,"name":"x","zoom":-}',
  '{"fh-preset":1,"name":"x","zoom":1.}',
  '﻿{"fh-preset":1,"name":"x"}',
  '{"fh-preset":1,"name":"x\ty"}',
  '{"fh-preset":1,"name":"x\\q"}',
  '{"fh-preset":1,"name":"x\\u12"}',
  '{"fh-preset":1,"name":"unterminated}',
  '{"fh-preset":1,"name":"x"}\n\t\r ',
  ' \n{"fh-preset":1,"name":"x"}',
  '{"fh-preset":1,"name":"x","name":"second"}',
  '{"fh-preset":1,"fh-preset":2,"name":"x"}',
  '{"fh-preset":2,"fh-preset":1,"name":"x"}',
  '['.repeat(100) + ']'.repeat(100),
  '{"a":'.repeat(100) + '1' + '}'.repeat(100),
  'x'.repeat(65537),
  ' '.repeat(65536) + '{',
  ' '.repeat(65500) + '{"fh-preset":1,"name":"x"}',
  '{"fh-preset":1,"name":"x"}' + ' '.repeat(65536 - 27),
  '{"fh-preset":1,"name":"x"}' + ' '.repeat(65536 - 26),
);

// ---------- names and text ----------
for (const name of [
  '',
  ' ',
  '   x   ',
  'a'.repeat(40),
  'a'.repeat(41),
  'a'.repeat(100),
  'tab\there',
  'line\nbreak',
  '\u0001\u0002\u007f',
  '\u0001a\u007f',
  ' \u0001 ',
  'été',
  ' x ',
  '　x ',
  '﻿x',
  ' x ',
  'a '.repeat(30),
  'é'.repeat(45),
  '中文',
  'x​z',
  'x\u0085y',
  '\u0085x',
  5,
  true,
  null,
  ['a'],
  { a: 1 },
  undefined,
])
  add(file({ name }), file({ name: 'ok', author: name }));
add(
  file({ name: 'x', author: 5 }),
  file({ name: 'x', author: '  me  ' }),
  file({ name: 'x', dialect: 'milk' }),
  file({ name: 'x', dialect: 'MILK' }),
  file({ name: 'x', dialect: 'fh' }),
  file({ name: 'x', dialect: 5 }),
  file({ name: 'x', dialect: null }),
);

// ---------- numbers ----------
const numberPool = [
  0, 1, -1, 0.5, 2, 3, 1.5, 2.5, -2.5, 0.001, 100, -100, 1e6, -1e6, 1e308, -1e308, 5e-324, 0.99,
  0.9900001, 1.3, 1.3000001, 0.8, 0.79999, -0.5, 1.5, 4, 16, 15.5, 16.5, 256, 255.5, 256.5, 3.5,
  100.5, 99.5,
];
for (const k of ['hear', 'zoom', 'rot', 'cx', 'cy', 'dx', 'dy', 'sx', 'sy', 'warp', 'decay'])
  for (const v of numberPool) add(file({ name: 'n' }, { [k]: v }));
for (const k of ['mode', 'r', 'g', 'b', 'a', 'scale', 'x', 'y', 'thick'])
  for (const v of numberPool) add(file({ name: 'n', wave: { [k]: v } }));
for (const k of [
  'gamma',
  'hue',
  'bright',
  'solarize',
  'echoAlpha',
  'echoZoom',
  'echoOrient',
  'cover',
])
  for (const v of numberPool) add(file({ name: 'n', color: { [k]: v } }));
for (const k of ['samples', 'r', 'g', 'b', 'a', 'thick'])
  for (const v of numberPool) add(file({ name: 'n', waves: [{ [k]: v }] }));
for (const k of [
  'sides',
  'x',
  'y',
  'rad',
  'ang',
  'r',
  'g',
  'b',
  'a',
  'r2',
  'g2',
  'b2',
  'a2',
  'instances',
])
  for (const v of numberPool) add(file({ name: 'n', shapes: [{ [k]: v }] }));
for (const raw of [
  '1e999',
  '-1e999',
  '1E2',
  '1e+2',
  '-0',
  '-0.0',
  '0.1e1',
  '9007199254740993',
  '1e-999',
])
  for (const k of ['zoom', 'hear', 'decay', 'dx']) add(`{"fh-preset":1,"name":"x","${k}":${raw}}`);
for (const k of ['zoom', 'rot', 'warp', 'hear'])
  for (const v of ['1', true, null, [], [1], {}, 'x', '']) add(file({ name: 'n' }, { [k]: v }));

// ---------- flags and nested things of the wrong kind ----------
for (const v of [true, false, 0, 1, 'true', null, [], {}])
  add(
    file({ name: 'n', wave: { additive: v } }),
    file({ name: 'n', waves: [{ additive: v, dots: v, spectrum: v }] }),
    file({ name: 'n', shapes: [{ additive: v }] }),
  );
for (const v of [null, 5, 'x', true, [], [1, 2], {}, { mode: 2, r: 0.5 }, [{ mode: 3 }]])
  add(
    file({ name: 'n', wave: v }),
    file({ name: 'n', color: v }),
    file({ name: 'n', waves: v }),
    file({ name: 'n', shapes: v }),
    file({ name: 'n', waves: [v] }),
    file({ name: 'n', shapes: [v, v] }),
  );
add(
  file({ name: 'n', waves: [{}, {}, {}, {}, {}] }),
  file({ name: 'n', shapes: Array.from({ length: 9 }, (_, i) => ({ sides: i + 3 })) }),
  file({ name: 'n', waves: [{ samples: 20 }, null, { samples: 30 }] }),
);

// ---------- formulas ----------
const GOOD = [
  'zoom = 1.01 + bass * 0.02;',
  'a = sin(time);',
  '',
  ' ',
  ';',
  'q1 = 1; q2 = q1 * 2;',
  'x = x + 1;',
];
const BAD = [
  'a = ;',
  'foo(1);',
  'a = 1 +',
  'sin = 3;',
  'a = "x";',
  'a = ' + '('.repeat(30) + '1' + ')'.repeat(30) + ';',
  'x'.repeat(6001),
  'a = 1;'.repeat(260),
  'a = b.c;',
  'a = 1 1;',
  'a = if(1,2);',
];
const wheres = [
  ['init'],
  ['frame'],
  ['pixel'],
  ['waves', 0, 'init'],
  ['waves', 0, 'frame'],
  ['waves', 0, 'point'],
  ['waves', 1, 'point'],
  ['shapes', 0, 'init'],
  ['shapes', 0, 'frame'],
  ['shapes', 3, 'frame'],
  ['shapes', 2, 'init'],
];
const withFormula = (where, src) => {
  const o = { name: 'f', waves: [{}, {}], shapes: [{}, {}, {}, {}] };
  if (where.length === 1) o[where[0]] = src;
  else o[where[0]][where[1]][where[2]] = src;
  return file(o);
};
for (const w of wheres) for (const f of [...GOOD, ...BAD]) add(withFormula(w, f));
for (const f of [5, null, true, [], {}, ['a = 1;']])
  add(
    file({ name: 'f', init: f, frame: f }),
    file({ name: 'f', waves: [{ point: f }], shapes: [{ frame: f }] }),
  );
// two problems: the first one in the order init, frame, pixel, waves, shapes is the one reported
add(
  file({ name: 'f', pixel: 'a = ;', frame: 'b = ;', init: 'c = ;' }),
  file({ name: 'f', shapes: [{ frame: 'a = ;' }], waves: [{ point: 'b = ;' }] }),
  file({ name: 'f', shapes: [{ init: 'a = ;', frame: 'b = ;' }] }),
  file({ name: 'f', waves: [{ init: 'a = ;' }, { init: 'b = ;' }] }),
  file({ name: '', frame: 'a = ;' }),
);
// total length of all formulas: 30000 is fine, 30001 is not
const slots = n => {
  const o = {
    name: 'big',
    init: pad(5000),
    frame: pad(5000),
    pixel: pad(5000),
    waves: [{ init: pad(5000), frame: pad(5000), point: pad(5000) }],
  };
  return o;
};
add(
  file(slots()),
  file({ ...slots(), frame: pad(5001) }),
  file({ ...slots(), shapes: [{ init: pad(5000) }] }),
  file({ ...slots(), shapes: [{ init: 'a=1;' }] }),
  file({ ...slots(), shapes: [{ init: ';' }] }),
  file({ ...slots(), shapes: [{ init: '', frame: ';' }] }),
  file({
    ...slots(),
    waves: [{ init: pad(5000), frame: pad(5000), point: pad(5000) }, { frame: 'a=1;' }],
  }),
);

// ---------- every built-in preset, as exported, and in the other dialect ----------
const builtins = Raw.LIST.map(r => Core.sanitizePreset(r))
  .filter(r => r.ok)
  .map(r => r.preset);
if (builtins.length !== Raw.LIST.length) throw new Error('a built-in preset is rejected');
for (const p of builtins) {
  add(Core.exportPreset(p), JSON.stringify({ 'fh-preset': 1, ...p, dialect: 'milk', hear: 2.5 }));
}
for (const r of Raw.LIST) add(file(r));

// ---------- built-ins with fields damaged at random ----------
const JUNK = [
  null,
  true,
  false,
  0,
  -1,
  0.5,
  2.5,
  99999,
  -99999,
  1e308,
  'x',
  '',
  '  padded  ',
  'a = ;',
  'zoom = 1.1;',
  [],
  {},
  [1, 2],
  [{}],
  { a: 1 },
  'x'.repeat(50),
  17,
  3.5,
  255.5,
];
function mutate(obj) {
  const o = JSON.parse(JSON.stringify(obj));
  const paths = [];
  (function walk(v, p) {
    if (v && typeof v === 'object')
      for (const k of Object.keys(v)) {
        paths.push([...p, k]);
        walk(v[k], [...p, k]);
      }
  })(o, []);
  const n = 1 + Math.floor(rnd() * 3);
  for (let i = 0; i < n; i++) {
    const p = pick(paths);
    let t = o;
    for (let j = 0; j < p.length - 1 && t; j++) t = t[p[j]];
    if (!t || typeof t !== 'object') continue;
    const k = p[p.length - 1];
    if (rnd() < 0.15) delete t[k];
    else t[k] = pick(JUNK);
  }
  return o;
}
for (let i = 0; i < 60; i++)
  add(JSON.stringify(mutate({ 'fh-preset': 1, ...builtins[i % builtins.length] })));

// ---------- write ----------
const lines = [];
for (const t of inputs) {
  const text = typeof t === 'string' ? t : String(t);
  lines.push('T ' + JSON.stringify(text), 'R ' + JSON.stringify(Core.parsePresetFile(text)));
}
fs.writeFileSync(path.join(__dirname, 'preset.golden'), lines.join('\n') + '\n');
console.log(`${inputs.length} files recorded`);
