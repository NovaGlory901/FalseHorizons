// Records what the JavaScript PresetPicker and settings checker (FalseHorizons/js/falsehorizon-
// core.js, the reference) do, so the C++ port can be held to the same answers. Writes picker.golden
// and settings.golden next to this file. Needs Node; the results are committed: node
// tests/parity/gen_state_golden.js picker.golden, one line each:
// {"seed":N,"ops":[["show","a"],["next",3],["prev"]],"out":[null,"b",...],"history":[...],"i":N}
// ("next" gives the pool as its size: the first N of the 14 names; the random numbers come from the
// seeded generator below, the same one the C++ test builds)
// settings.golden, one line each: {"raw":<anything>,"want":{intensity, auto, cycleSec, geometry,
// favorites, last}} (only the settings both versions have)
const fs = require('fs'),
  path = require('path');
const C = require('../../js/falsehorizon-core.js');

let seed = 20261012;
const rnd = () => {
  seed |= 0;
  seed = (seed + 0x6d2b79f5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
};
const pick = list => list[Math.floor(rnd() * list.length)];
const seeded = s0 => {
  let s = s0 | 0;
  return () => {
    s = (s + 0x6d2b79f5) | 0;
    let t = Math.imul(s ^ (s >>> 15), 1 | s);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
};

// ---------- picker ----------
const NAMES = Array.from({ length: 14 }, (_, i) => 'Preset ' + String.fromCharCode(65 + i));
const picker = [];
for (let n = 0; n < 40; n++) {
  const s = 1 + Math.floor(rnd() * 1e6),
    poolSize = [0, 1, 2, 3, 5, 8, 14][n % 7],
    ops = [],
    out = [];
  const p = new C.PresetPicker(seeded(s));
  // some long enough to pass the 100-entry history limit
  const steps = 5 + Math.floor(rnd() * (n < 100 ? 40 : 260));
  for (let k = 0; k < steps; k++) {
    const r = rnd();
    if (r < 0.1) {
      const name = pick(NAMES);
      ops.push(['show', name]);
      p.show(name);
      out.push(null);
    } else if (r < 0.7) {
      const pool = NAMES.slice(0, Math.max(0, poolSize + (rnd() < 0.2 ? 1 : 0)));
      ops.push(['next', pool.length]);
      out.push(p.next(pool));
    } else {
      ops.push(['prev']);
      out.push(p.prev());
    }
  }
  picker.push({ seed: s, ops, out, history: p.history, i: p.i });
}
fs.writeFileSync(
  path.join(__dirname, 'picker.golden'),
  picker.map(c => JSON.stringify(c)).join('\n') + '\n',
);

// ---------- settings ----------
const WEIRD = [
  undefined,
  null,
  true,
  false,
  0,
  1,
  -1,
  5,
  10,
  50,
  77,
  99.5,
  100,
  101,
  250,
  299.5,
  300,
  301,
  -0.4,
  0.5,
  1.5,
  2.5,
  1e9,
  -1e9,
  'abc',
  '50',
  '',
  [],
  [1],
  {},
  { a: 1 },
  'x'.repeat(100),
  ' pad \t',
  'a\nb',
];
const val = () => pick(WEIRD);
const favList = () => {
  const n = Math.floor(rnd() * 40),
    l = [];
  for (let i = 0; i < n; i++)
    l.push(
      rnd() < 0.15
        ? val()
        : rnd() < 0.2
          ? pick(NAMES.slice(0, 5))
          : pick(NAMES) + (rnd() < 0.1 ? ' '.repeat(3) : '') + (rnd() < 0.05 ? 'z'.repeat(50) : ''),
    );
  return l;
};
const settings = [];
const add = raw => {
  const s = C.sanitize(raw);
  settings.push({
    raw,
    want: {
      intensity: s.intensity,
      auto: s.auto,
      cycleSec: s.cycleSec,
      geometry: s.geometry,
      favorites: s.favorites,
      last: s.last,
    },
  });
};
for (const raw of [
  null,
  undefined,
  5,
  'x',
  [],
  {},
  { intensity: 10 },
  { favorites: 'no' },
  { favorites: {} },
])
  add(raw);
for (let n = 0; n < 150; n++) {
  const raw = {};
  if (rnd() < 0.8) raw.intensity = val();
  if (rnd() < 0.8) raw.geometry = val();
  if (rnd() < 0.8) raw.auto = val();
  if (rnd() < 0.8) raw.cycleSec = val();
  if (rnd() < 0.8) raw.last = rnd() < 0.5 ? val() : pick(NAMES);
  if (rnd() < 0.8) raw.favorites = rnd() < 0.9 ? favList() : val();
  if (rnd() < 0.3) raw.extra = val();
  add(raw);
}
fs.writeFileSync(
  path.join(__dirname, 'settings.golden'),
  settings.map(c => JSON.stringify(c)).join('\n') + '\n',
);
console.log(`wrote ${picker.length} picker cases and ${settings.length} settings cases`);
