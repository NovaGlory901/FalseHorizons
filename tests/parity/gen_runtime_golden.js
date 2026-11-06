// Records what the JavaScript PresetRuntime (FalseHorizons/js/falsehorizon-core.js, the reference)
// makes of a preset, frame by frame, so the C++ port can be held to the same answers. Writes
// runtime.golden next to this file. Needs Node; the result is committed, so only run it when the
// reference or the presets change:      node tests/parity/gen_runtime_golden.js
//
// One case is one line of JSON: {"kind":"run","p":<preset, as in a
// file>,"seed":N,"aspect":A,"t0":T,"motion":M,"weight":W,"frames":{"0":<summary>,"1":...}}
// {"kind":"blend","p":<preset>,"q":<preset>,"seed":N,"ts":[...],"blends":[<summary>,...]} The
// preset runs for 12 frames on made-up input (inputAt below; the C++ test has the same formulas)
// and the frames listed are recorded. rand() gets a seeded generator shared by all of the preset's
// formulas, the same one the C++ test builds. A summary is what the renderer would be given, boiled
// down: decay, the colour settings, and for the mesh and for each draw call a list summary
// {"n":length,"sum":..., "wsum":sum of (i%97+1)*value, "head":[first 6], "tail":[last 6]} A damaged
// value anywhere in a few thousand numbers would change sum or wsum, so this stays small without
// hiding mistakes.
const fs = require('fs'),
  path = require('path');
const C = require('../../js/falsehorizon-core.js');
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
const seeded = s0 => {
  let s = s0 | 0;
  return () => {
    s = (s + 0x6d2b79f5) | 0;
    let t = Math.imul(s ^ (s >>> 15), 1 | s);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
};

const FRAMES = 12,
  RECORD = [0, 1, 3, 6, 11];
function inputAt(i, aspect, t0) {
  const bass = 0.5 + 0.5 * Math.sin(i * 0.7),
    mid = 0.5 + 0.5 * Math.sin(i * 0.3 + 1),
    treb = 0.5 + 0.5 * Math.sin(i * 1.1 + 2);
  const wave = new Float32Array(256),
    spec = new Float32Array(64);
  for (let k = 0; k < 256; k++) wave[k] = Math.sin(k * 0.1 + i) * 0.8;
  for (let k = 0; k < 64; k++) spec[k] = 0.5 + 0.5 * Math.sin(k * 0.2 + i * 0.3);
  return {
    time: t0 + i / 60,
    frame: i,
    bass,
    mid,
    treb,
    bass_att: 0.4 + 0.3 * Math.sin(i * 0.1),
    mid_att: 0.4 + 0.3 * Math.sin(i * 0.12 + 1),
    treb_att: 0.4 + 0.3 * Math.sin(i * 0.14 + 2),
    level: (bass * 2 + mid + treb) / 4,
    beat: i % 7 === 0 ? 1 : Math.pow(0.9, i % 7),
    aspect,
    fps: 60,
    progress: i / 12,
    wave,
    spec,
  };
}
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
const summary = s => ({
  decay: s.decay,
  color: s.color,
  mesh: arr(s.mesh),
  draws: s.draws.map(d => ({
    kind: d.kind,
    count: d.count,
    additive: d.additive,
    thick: d.thick,
    verts: arr(d.verts),
  })),
});

const cases = [];
const sane = raw => {
  const r = C.sanitizePreset(raw);
  return r.ok ? r.preset : null;
};
function runCase(raw, { aspect = 16 / 9, t0 = 0, motion = 1, weight = 1, seedN = 1 } = {}) {
  const preset = sane(raw);
  if (!preset) return;
  const rt = new C.PresetRuntime(preset, seeded(seedN)),
    frames = {};
  for (let i = 0; i < FRAMES; i++) {
    const s = rt.frame(inputAt(i, aspect, t0), { motion, weight });
    if (RECORD.includes(i)) frames[i] = summary(s);
  }
  cases.push({ kind: 'run', p: raw, seed: seedN, aspect, t0, motion, weight, frames });
}
function blendCase(rawA, rawB, seedN) {
  const a = sane(rawA),
    b = sane(rawB);
  if (!a || !b) return;
  const ra = new C.PresetRuntime(a, seeded(seedN)),
    rb = new C.PresetRuntime(b, seeded(seedN + 1));
  let sa, sb;
  for (let i = 0; i < 6; i++) {
    sa = ra.frame(inputAt(i, 16 / 9, 3), { weight: 0.7 });
    sb = rb.frame(inputAt(i, 16 / 9, 3), { weight: 0.4 });
  }
  const ts = [0, 0.1, 0.3, 0.5, 0.8, 0.999, 1];
  cases.push({
    kind: 'blend',
    p: rawA,
    q: rawB,
    seed: seedN,
    ts,
    blends: ts.map(t => summary(C.blendStates(sa, sb, t))),
  });
}

// ---------- every built-in preset, in three setups, and as an imported (milk) preset ----------
Raw.LIST.forEach((raw, i) => {
  runCase(raw, { seedN: 100 + i });
  runCase(raw, { aspect: 1, t0: 40, motion: 0.5, weight: 0.5, seedN: 200 + i });
  runCase(raw, { aspect: 9 / 16, t0: 1000.5, motion: 0, weight: 0.2, seedN: 300 + i });
  runCase({ ...raw, dialect: 'milk', hear: 2 }, { seedN: 400 + i });
});

// ---------- hand-written presets for the corners ----------
const hand = [
  ...[0, 1, 2, 3, 4, 5, -1].flatMap(mode =>
    [false, true].map(additive => ({
      name: 'wave mode ' + mode,
      wave: {
        mode,
        additive,
        thick: 2,
        a: 0.9,
        scale: 1.3,
        x: 0.4,
        y: 0.6,
        r: 0.2,
        g: 0.5,
        b: 0.9,
      },
    })),
  ),
  { name: 'wave off by alpha', wave: { a: 0 } },
  { name: 'wave off by scale', wave: { scale: 0 } },
  {
    name: 'wave moved by formula',
    frame:
      'wave_x = 0.2 + 0.6 * bass; wave_y = 0.3 + 0.4 * mid; wave_scale = 2 * treb; wave_a = 0.5 + 0.5 * sin(time);',
    wave: { mode: 2 },
  },
  {
    name: 'custom waves',
    waves: [
      { samples: 16, a: 0.9, r: 1, g: 0.2, b: 0.2, point: 'y = 0.5 + value1 * 0.3; r = sample;' },
      {
        samples: 256,
        dots: true,
        spectrum: true,
        a: 0.7,
        additive: false,
        thick: 3,
        frame: 'a = 0.4 + 0.4 * sin(time * 3); r = bass;',
        point: 'x = sample; y = 0.2 + value1 * 0.6; g = value2;',
      },
    ],
  },
  { name: 'custom wave dark', waves: [{ a: 0 }, { a: 0.9, frame: 'a = 0;' }] },
  { name: 'custom wave runaway', waves: [{ point: 'x = 9; y = 9; r = 7; g = 0 - 3; a = 5;' }] },
  {
    name: 'shapes',
    shapes: [
      { sides: 3, x: 0.3, y: 0.3, rad: 0.2, ang: 1, a: 0.8, a2: 0.1, r2: 0.2, g2: 0.9, b2: 0.4 },
      {
        sides: 100,
        instances: 4,
        frame:
          'x = 0.2 + 0.2 * instance; y = 0.5; rad = 0.05 * (num_inst - instance); ang = time * (instance + 1);',
      },
      {
        sides: 7,
        additive: false,
        a: 0.5,
        a2: 0.5,
        frame:
          'sides = 3 + 5 * bass; rad = 0.1 + 0.2 * mid; x = 0.5 + 0.3 * sin(time); y = 0.5 + 0.3 * cos(time);',
      },
      { a: 0, a2: 0 },
    ],
  },
  {
    name: 'shape runaway',
    shapes: [
      {
        frame:
          'sides = 1000; x = 9; y = 0 - 9; rad = 50; ang = 500; r = 9; r2 = 0 - 3; a = 3; a2 = 2;',
      },
    ],
  },
  {
    name: 'pixel formulas',
    pixel:
      'zoom = zoom + 0.05 * sin(rad * 10 + time); rot = rot + 0.02 * ang; dx = 0.01 * cos(x * 6); dy = 0.01 * sin(y * 6); local1 = x * y; sx = 1 + 0.1 * local1;',
    zoom: 1.01,
  },
  {
    name: 'pixel locals reset',
    pixel: 'n = n + 1; zoom = 1 + 0.001 * n; cx = 0.5 + 0.0001 * m; m = m + 2;',
  },
  {
    name: 'pixel runaway',
    pixel:
      'zoom = 99; rot = 99; cx = 99; cy = 0 - 99; dx = 9; dy = 9; sx = 0; sy = 0 - 5; warp = 99;',
  },
  {
    name: 'q numbers flow',
    init: 'q1 = 3; q2 = 5;',
    frame: 'q1 = q1 + 1; q3 = q1 * q2; zoom = 1 + q3 * 0.0001;',
    pixel: 'zoom = 1 + q1 * 0.0005; rot = q3 * 0.0001;',
    waves: [{ point: 'y = 0.5 + q1 * 0.01;' }],
    shapes: [{ frame: 'rad = 0.05 + q1 * 0.01;' }],
  },
  {
    name: 'init state',
    init: 'a = 7; b = a * 2;',
    frame: 'a = a + 1; zoom = 1 + a * 0.001; decay = 0.9 + b * 0.001;',
    waves: [{ init: 'k = 4;', point: 'y = k * 0.1;' }],
    shapes: [{ init: 'k = 5;', frame: 'rad = k * 0.02;' }],
  },
  {
    name: 'random',
    frame: 'rot = (rand(100) - 50) * 0.001; zoom = 1 + rand(10) * 0.002;',
    pixel: 'dx = (rand(100) - 50) * 0.0001;',
    waves: [{ point: 'x = sample + rand(10) * 0.001;' }],
  },
  {
    name: 'every output',
    frame:
      'zoom = 1.1; rot = 0.1; cx = 0.4; cy = 0.6; dx = 0.01; dy = 0.02; sx = 1.2; sy = 0.9; warp = 2; decay = 0.9; wave_r = 0.3; wave_g = 0.4; wave_b = 0.5; wave_a = 0.6; wave_scale = 0.7; wave_x = 0.2; wave_y = 0.3; gamma = 1.4; hue = 5; bright = 1.3; solarize = 0.4; echo_alpha = 0.3; echo_zoom = 2; echo_orient = 1.5; cover = 0.7;',
  },
  {
    name: 'out of range',
    frame:
      'zoom = 9; rot = 9; cx = 9; cy = 0 - 9; dx = 9; dy = 9; sx = 0; sy = 99; warp = 99; decay = 5; wave_r = 5; wave_a = 0 - 1; wave_scale = 99; gamma = 0; hue = 99999; bright = 99; solarize = 9; echo_alpha = 9; echo_zoom = 0; echo_orient = 9; cover = 9;',
  },
  {
    name: 'hear scaled',
    hear: 3,
    frame: 'zoom = 1 + bass * 0.1; rot = treb_att * 0.1; wave_x = level;',
  },
  {
    name: 'milk flips',
    dialect: 'milk',
    frame: 'cy = 0.3; dy = 0.01; rot = 0.1; wave_y = 0.2;',
    pixel: 'cy = 0.3; dy = 0.01; rot = 0.05 * ang + y;',
    waves: [{ point: 'y = 0.2; ang = 3;' }],
    shapes: [{ y: 0.2, ang: 0.5, a: 0.9 }],
  },
  { name: 'ripples', warp: 4, frame: 'warp = 4;' },
  { name: 'no formulas at all' },
];
hand.forEach((raw, i) => {
  const base = { author: 'test', ...raw };
  runCase(base, { seedN: 1000 + i });
  runCase(base, { aspect: 1.25, t0: 7.5, motion: 0.6, weight: 0.8, seedN: 2000 + i });
  runCase(base, { aspect: 0, weight: 0, seedN: 3000 + i });
});

// ---------- built-ins with fields damaged at random ----------
const JUNK = [
  null,
  true,
  0,
  -1,
  0.5,
  2.5,
  99999,
  -99999,
  'zoom = 1.1; rot = 0.2;',
  'a = ;',
  [],
  {},
  17,
  3.5,
  255.5,
  'x = x + 1;',
];
function mutate(obj) {
  const o = JSON.parse(JSON.stringify(obj)),
    paths = [];
  (function walk(v, p) {
    if (v && typeof v === 'object')
      for (const k of Object.keys(v)) {
        paths.push([...p, k]);
        walk(v[k], [...p, k]);
      }
  })(o, []);
  for (let i = 0, n = 1 + Math.floor(rnd() * 3); i < n; i++) {
    const p = pick(paths);
    let t = o;
    for (let j = 0; j < p.length - 1 && t; j++) t = t[p[j]];
    if (!t || typeof t !== 'object') continue;
    t[p[p.length - 1]] = pick(JUNK);
  }
  return o;
}
for (let i = 0; i < 34; i++)
  runCase(mutate(Raw.LIST[i % Raw.LIST.length]), {
    aspect: pick([16 / 9, 1, 0.7]),
    motion: pick([1, 0.5]),
    weight: pick([1, 0.6]),
    seedN: 5000 + i,
  });

// ---------- blends ----------
for (let i = 0; i < 12; i++)
  blendCase(Raw.LIST[i % Raw.LIST.length], Raw.LIST[(i * 5 + 3) % Raw.LIST.length], 7000 + i);
for (let i = 0; i < 6; i++) blendCase(hand[i * 3], hand[i * 3 + 20], 8000 + i);

fs.writeFileSync(
  path.join(__dirname, 'runtime.golden'),
  cases.map(c => JSON.stringify(c)).join('\n') + '\n',
);
console.log(`${cases.length} cases recorded`);
