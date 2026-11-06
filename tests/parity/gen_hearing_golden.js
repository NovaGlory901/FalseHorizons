// Records what the JavaScript flash guard and audio analysis (FalseHorizons/js/falsehorizon-
// core.js: FlashGuard, bandEnergies, AudioFeatures, the reference) do with a list of inputs, so the
// C++ port can be held to the same answers. Writes hearing.golden next to this file. Needs Node;
// the
// result is committed, so only run it when the reference or the inputs change: node
// tests/parity/gen_hearing_golden.js
//
// One case is one line of JSON, so the C++ test reads it with its own JSON reader:
// {"kind":"guard","steps":[[luminance, ms, calm], ...],"gains":[...]} {"kind":"bands","bytes":<byte
// spec>,"rate":44100,"out":{bass,mid,treb,level}}
// {"kind":"audio","rate":44100,"steps":[{"ms":16,"f":<byte spec>|null,"t":<byte spec>|null},
// ...],"out":[{bass,...,beat}, ...],"wave":[...],"spec":[...]} A byte spec says how to build an
// array of bytes, so the golden file stays small and both sides build identical arrays:
//  {"k":"fill","n":len,"v":value} {"k":"band","n":len,"a":from,"b":to,"v":inside,"c":outside}
// {"k":"ramp","n":len} {"k":"noise","n":len,"s":seed,"m":max}
// Noise is a 32-bit linear congruential generator: s = (s * 1664525 + 1013904223) mod 2^32, byte =
// (s >>> 24) % (max + 1). "wave" and "spec" are the 256-point waveform and 64 bars after the last
// step.
const fs = require('fs'),
  path = require('path');
const C = require('../../js/falsehorizon-core.js');

let seed = 20261009;
const rnd = () => {
  seed |= 0;
  seed = (seed + 0x6d2b79f5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
};
const pick = list => list[Math.floor(rnd() * list.length)];
const int = (lo, hi) => lo + Math.floor(rnd() * (hi - lo + 1));

function build(spec) {
  if (!spec) return null;
  const a = new Uint8Array(spec.n);
  if (spec.k === 'fill') a.fill(spec.v);
  else if (spec.k === 'band')
    for (let i = 0; i < spec.n; i++) a[i] = i >= spec.a && i < spec.b ? spec.v : spec.c;
  else if (spec.k === 'ramp')
    for (let i = 0; i < spec.n; i++) a[i] = Math.floor((i * 255) / Math.max(1, spec.n - 1));
  else if (spec.k === 'noise') {
    let s = spec.s >>> 0;
    for (let i = 0; i < spec.n; i++) {
      s = (Math.imul(s, 1664525) + 1013904223) >>> 0;
      a[i] = (s >>> 24) % (spec.m + 1);
    }
  } else throw new Error('unknown spec ' + spec.k);
  return a;
}
const fill = (n, v) => ({ k: 'fill', n, v }),
  band = (n, a, b, v, c = 0) => ({ k: 'band', n, a, b, v, c }),
  noise = (n, s, m) => ({ k: 'noise', n, s, m }),
  ramp = n => ({ k: 'ramp', n });

const cases = [];

// ---------- the flash guard ----------
const guardCase = steps =>
  cases.push({
    kind: 'guard',
    steps,
    gains: (g =>
      steps.map(([L, ms, calm]) => {
        g.setCalm(calm);
        return g.feed(L, ms);
      }))(new C.FlashGuard()),
  });
const run = (levels, stepMs, calm = false) => levels.map((L, i) => [L, i * stepMs, calm]);
const strobe = n => Array.from({ length: n }, (_, i) => (i % 2 ? 0.95 : 0.02));
for (const calm of [false, true]) {
  for (const L of [0, 0.2, 0.45, 0.5, 0.95, 1, 5, -3])
    guardCase(run(new Array(60).fill(L), 100, calm));
  for (const stepMs of [1, 8, 16, 33, 100, 250, 499, 500, 501, 1000, 60000])
    guardCase(run(strobe(120), stepMs, calm));
  guardCase(run([0, 0, 0, 0.9, 0.9, 0.9, 0.9, 0.9, 0.9], 100, calm));
  guardCase(run([0.9, 0.9, 0.9, 0.9, 0.9, 0.9, 0.9, 0.05, 0.9, 0.9], 100, calm));
}
guardCase([
  [0, 0, false],
  [0.95, 600000, false],
  [0.95, 600016, false],
]);
guardCase([
  [0.5, 0, false],
  [0.5, 100, true],
  [0.5, 200, false],
  [0.9, 300, true],
  [0.9, 400, false],
]); // calm switched on and off while running
guardCase([
  [0.5, 1000, false],
  [0.9, 900, false],
  [0.9, 800, false],
  [0.9, 2000, false],
]); // a clock that goes backwards
for (let i = 0; i < 15; i++) {
  // random inputs at random speeds
  let t = int(0, 5000);
  guardCase(
    Array.from({ length: int(5, 80) }, () => {
      t += pick([0, 1, 5, 16, 16, 16, 33, 50, 100, 400, 700]);
      return [pick([0, 0.01, rnd(), rnd(), rnd() * 0.5, 0.95, 1, 1.5, -0.2]), t, rnd() < 0.2];
    }),
  );
}

// ---------- band energies ----------
const bandsCase = (bytes, rate) =>
  cases.push({ kind: 'bands', bytes, rate, out: C.bandEnergies(build(bytes), rate) });
for (const rate of [44100, 48000, 22050, 8000, 96000, 0]) {
  for (const n of [1024, 512, 2048, 256, 128]) {
    bandsCase(fill(n, 0), rate);
    bandsCase(fill(n, 255), rate);
    bandsCase(ramp(n), rate);
    bandsCase(noise(n, n + rate, 255), rate);
    bandsCase(band(n, 0, 6, 255), rate);
    bandsCase(band(n, 6, 60, 200), rate);
    bandsCase(band(n, 100, 250, 255, 7), rate);
  }
  for (const n of [1, 2, 3, 4, 5, 7, 8, 16, 31, 33]) {
    bandsCase(fill(n, 255), rate);
    bandsCase(ramp(n), rate);
    bandsCase(noise(n, 5 + n, 255), rate);
  }
}

// ---------- the whole listener, frame by frame ----------
const audioCase = (rate, steps) => {
  const f = new C.AudioFeatures(),
    out = [];
  for (const s of steps) {
    f.update(build(s.f), build(s.t), rate, s.ms);
    out.push({
      bass: f.bass,
      mid: f.mid,
      treb: f.treb,
      level: f.level,
      bass_att: f.bass_att,
      mid_att: f.mid_att,
      treb_att: f.treb_att,
      beat: f.beat,
    });
  }
  cases.push({
    kind: 'audio',
    rate,
    steps,
    out,
    wave: Array.from(f.wave),
    spec: Array.from(f.spec),
  });
};
const frames = (n, make, dt = 16, t0 = 0) =>
  Array.from({ length: n }, (_, i) => ({ ms: t0 + i * dt, ...make(i) }));
for (const rate of [44100, 48000, 8000, 0]) {
  const w = fill(1024, 128);
  audioCase(
    rate,
    frames(30, () => ({ f: fill(1024, 0), t: w })),
  ); // silence
  audioCase(
    rate,
    frames(30, () => ({ f: band(1024, 0, 6, 255), t: null })),
  ); // a low tone
  audioCase(
    rate,
    frames(30, () => ({ f: band(1024, 100, 250, 255), t: fill(1024, 255) })),
  ); // a high tone
  audioCase(
    rate,
    frames(40, i => ({ f: noise(1024, i, 255), t: noise(1024, 1000 + i, 255) })),
  ); // noise
  audioCase(
    rate,
    frames(100, i => ({ f: band(1024, 0, 6, (i * 20) % 100 < 40 ? 255 : 5, 5), t: null }), 20),
  ); // beats at 5 a second
  audioCase(
    rate,
    frames(300, i => ({ f: band(1024, 0, 6, (i * 20) % 600 < 100 ? 255 : 5, 5), t: null }), 20),
  ); // beats at 1.7 a second
  audioCase(rate, [
    ...frames(10, () => ({ f: fill(1024, 200), t: fill(1024, 255) })),
    ...frames(100, () => ({ f: null, t: null }), 16, 1000),
  ]); // the analyser goes away
  audioCase(
    rate,
    frames(20, i => ({ f: i % 3 ? band(512, 0, 4, 255) : null, t: i % 4 ? ramp(2048) : null })),
  ); // sizes and gaps mixed
  audioCase(
    rate,
    frames(20, () => ({ f: fill(4, 200), t: fill(4, 0) })),
  ); // tiny arrays
}
for (const n of [1, 2, 3, 5, 8, 100, 4096])
  audioCase(
    44100,
    frames(12, i => ({ f: noise(n, i + n, 255), t: noise(n, 50 + i, 255) })),
  );
for (let i = 0; i < 8; i++) {
  // random sessions
  let t = int(0, 3000);
  audioCase(
    pick([44100, 48000, 22050]),
    Array.from({ length: int(5, 50) }, () => {
      t += pick([0, 8, 16, 16, 16, 33, 100, 400, 800]);
      const kind = int(0, 4),
        n = pick([1024, 1024, 2048, 512, 64]);
      const f =
        kind === 0
          ? null
          : kind === 1
            ? band(n, 0, int(1, 8), int(100, 255), int(0, 30))
            : kind === 2
              ? noise(n, int(0, 99999), pick([30, 255]))
              : kind === 3
                ? ramp(n)
                : fill(n, int(0, 255));
      return { ms: t, f, t: rnd() < 0.3 ? null : noise(pick([1024, 2048]), int(0, 99999), 255) };
    }),
  );
}

fs.writeFileSync(
  path.join(__dirname, 'hearing.golden'),
  cases.map(c => JSON.stringify(c)).join('\n') + '\n',
);
console.log(`${cases.length} cases recorded`);
