// Records what the JavaScript fractal (FalseHorizons/js/falsehorizon-core.js, the reference) does,
// so the C++ port can be held to it. Writes
// fractal.golden next to this file. Needs Node; the result is committed: node
// tests/parity/gen_fractal_golden.js
// One line of JSON each:
//  {"kind":"height","pts":[[cr,ci,h],...]} fractalHeight at points, some near the set's edge, some
// far out
// {"kind":"warp","time":T,"aspect":A,"bass":B,"mid":M,"amount":N,"calm":C,"wave":"sin"|"none"|"short","mesh":<summary
// of the mesh after>} The mesh starts as the plain grid (u = i/32, v = j/24) and is a Float32Array,
// as in the page.
const fs = require('fs'),
  path = require('path');
const C = require('../../js/falsehorizon-core.js');

let seed = 20261010;
const rnd = () => {
  seed |= 0;
  seed = (seed + 0x6d2b79f5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
};
const arr = a => {
  let sum = 0,
    wsum = 0;
  for (let i = 0; i < a.length; i++) {
    sum += a[i];
    wsum += ((i % 97) + 1) * a[i];
  }
  return {
    n: a.length,
    sum,
    wsum,
    head: Array.from(a.slice(0, 6)),
    tail: Array.from(a.slice(Math.max(0, a.length - 6))),
  };
};
const cases = [];

const pts = [];
for (let i = 0; i < 60; i++) {
  const cr = -2.2 + rnd() * 3.2,
    ci = -1.5 + rnd() * 3;
  pts.push([cr, ci, C.fractalHeight(cr, ci)]);
}
for (let i = 0; i < 20; i++) {
  const cr = -0.75 + rnd() * 0.1,
    ci = (rnd() - 0.5) * 0.2;
  pts.push([cr, ci, C.fractalHeight(cr, ci)]);
}
for (const [cr, ci] of [
  [0, 0],
  [-1, 0],
  [0.25, 0],
  [100, 100],
  [-0.7, 0],
  [2, 2],
  [1e6, 1e6],
])
  pts.push([cr, ci, C.fractalHeight(cr, ci)]);
cases.push({ kind: 'height', pts });

const plain = () => {
  const m = new Float32Array(33 * 25 * 2);
  let k = 0;
  for (let j = 0; j <= 24; j++)
    for (let i = 0; i <= 32; i++, k += 2) {
      m[k] = i / 32;
      m[k + 1] = j / 24;
    }
  return m;
};
function warp(time, aspect, bass, mid, amount, calm, wave) {
  const w =
    wave === 'sin'
      ? Float32Array.from({ length: 256 }, (_, k) => Math.sin(k * 0.1 + time) * 0.8)
      : wave === 'short'
        ? Float32Array.from({ length: 100 }, (_, k) => Math.sin(k) * 0.5)
        : undefined;
  const mesh = plain();
  C.fractalWarp(mesh, { time, aspect, bass, mid, wave: w }, amount, calm);
  cases.push({ kind: 'warp', time, aspect, bass, mid, amount, calm, wave, mesh: arr(mesh) });
}
for (let n = 0; n < 60; n++) {
  const t = n < 40 ? n * 2.3 : rnd() * 400;
  warp(
    t,
    [16 / 9, 1, 4 / 3, 0.5][n % 4],
    rnd(),
    rnd(),
    [0.6, 1, 0.3, 0.02][n % 4],
    n % 3 === 0,
    ['sin', 'none', 'short'][n % 3],
  );
}
warp(0, 16 / 9, 0, 0, 0, false, 'sin'); // amount 0: untouched
warp(5, 16 / 9, 0, 0, 0.01, false, 'sin'); // 0.01 is not above the limit
warp(5, 16 / 9, 0.5, 0.5, 0.011, false, 'sin');
warp(8.4, 0, 0.5, 0.5, 0.6, false, 'sin'); // aspect 0 counts as 1
warp(-3, 16 / 9, 2, 2, 1, false, 'sin');
warp(1e5, 16 / 9, 0.5, 0.5, 0.6, true, 'sin');

fs.writeFileSync(
  path.join(__dirname, 'fractal.golden'),
  cases.map(c => JSON.stringify(c)).join('\n') + '\n',
);
console.log(`wrote ${cases.length} cases`);
