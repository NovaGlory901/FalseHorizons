// Records what the JavaScript .milk importer (FalseHorizons/js/falsehorizon-milk.js, the reference)
// makes of a pile of files, good, odd and damaged, so the C++ port can be held to the same answers.
// Writes milk.golden next to this file. Needs Node; the result is committed: node
// tests/parity/gen_milk_golden.js One line of JSON each: {"text":<file text>,"name":<file
// name>,"r":{"ok":true,"preset":{...},"notes":[...]} or {"ok":false,"error":"..."}} The files are
// written for these tests in the real .milk layout; no third-party preset is bundled or copied.
// ASCII only.
const fs = require('fs'),
  path = require('path');
const M = require('../../js/falsehorizon-milk.js');

let seed = 20261011;
const rnd = () => {
  seed |= 0;
  seed = (seed + 0x6d2b79f5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
};
const pick = list => list[Math.floor(rnd() * list.length)];

const SIMPLE = `MILKDROP_PRESET_VERSION=201
PSVERSION=2
PSVERSION_WARP=2
PSVERSION_COMP=2
[preset00]
fRating=3.000000
fGammaAdj=1.500000
fDecay=0.960000
fVideoEchoZoom=1.800000
fVideoEchoAlpha=0.300000
nVideoEchoOrientation=3
nWaveMode=4
bAdditiveWaves=1
bWaveThick=1
fWaveAlpha=0.900000
fWaveScale=1.500000
fWarpScale=1.0
zoom=1.010000
rot=0.000000
cx=0.500000
cy=0.500000
dx=0.000000
dy=0.000000
warp=0.500000
sx=1.000000
sy=1.000000
wave_r=0.200000
wave_g=0.800000
wave_b=1.000000
wave_x=0.500000
wave_y=0.500000
per_frame_init_1=q8 = 0.5;
per_frame_1=// pulse on the bass
per_frame_2=zoom = 1 + 0.02*max(bass-1, 0); /* a comment */
per_frame_3=rot = 0.01*sin(time*0.3);
per_frame_4=q1 = bass_att; wave_r = 0.5 + 0.5*sin(time);
per_pixel_1=zoom = zoom + 0.01*rad*q1;
per_pixel_2=rot = rot + 0.02*sin(ang*3);
`;
const BIG =
  SIMPLE +
  `wavecode_0_enabled=1
wavecode_0_samples=200
wavecode_0_bSpectrum=1
wavecode_0_bUseDots=1
wavecode_0_bDrawThick=1
wavecode_0_bAdditive=1
wavecode_0_r=0.1
wavecode_0_g=0.9
wavecode_0_b=0.5
wavecode_0_a=0.8
wave_0_init1=t1 = 3;
wave_0_per_frame1=r = 0.5 + 0.5*sin(time + t1);
wave_0_per_point1=x = sample; y = 0.3 + value1 * 0.5;
wavecode_1_enabled=1
wavecode_2_enabled=1
wavecode_3_enabled=1
shapecode_0_enabled=1
shapecode_0_sides=6
shapecode_0_additive=1
shapecode_0_num_inst=5
shapecode_0_textured=1
shapecode_0_x=0.5
shapecode_0_y=0.5
shapecode_0_rad=0.1
shapecode_0_r=1
shapecode_0_g=0.5
shapecode_0_b=0
shapecode_0_a=1
shape_0_init1=q3 = 2;
shape_0_per_frame1=x = 0.5 + 0.3*cos(instance * 1.256 + time); y = 0.5 + 0.3*sin(instance * 1.256 + time);
shapecode_1_enabled=1
shapecode_2_enabled=1
shapecode_3_enabled=1
shapecode_4_enabled=1
warp_1=\`shader_body\`
warp_2={
comp_1=\`shader_body\`
`;
const SMALL =
  '[preset00]\nfDecay=0.9\nfWaveAlpha=0.5\nzoom=1.02\nper_frame_1=rot = 0.1*sin(time);\n';
const BASES = [SIMPLE, BIG, SMALL];
const NAMES = [
  'My Test Preset.milk',
  'x.milk',
  'a.b.c.milk',
  '.milk',
  'no extension',
  '  spaced   out  name .MILK',
  'Tab\there.milk',
  'A very long preset name that goes on and on past the forty character limit.milk',
  '',
];

const BITS = [
  '/',
  '*',
  '&',
  '|',
  '\r',
  '=',
  '\n',
  '(',
  ')',
  ' ',
  '0',
  '9',
  '.',
  '-',
  'e',
  'E',
  '+',
  'loop(',
  'while (',
  'megabuf(',
  'GMEGABUF (',
  'assign ',
  'exec2(',
  'x//y',
  '/*',
  '*/',
  '//',
  'Infinity',
  '1e999',
  '0x10',
  '1e',
  ' .5',
  '5.',
  '-.5e-1',
  '\t',
  '\u007f',
  '\u0001',
  '&&',
  '||',
  '_',
  'a',
];
function mutate(text) {
  let t = text;
  const n = 1 + Math.floor(rnd() * 4);
  for (let k = 0; k < n; k++) {
    const kind = Math.floor(rnd() * 8),
      lines = t.split('\n');
    if (kind === 0 && lines.length > 1) lines.splice(Math.floor(rnd() * lines.length), 1);
    else if (kind === 1)
      lines.splice(Math.floor(rnd() * lines.length), 0, lines[Math.floor(rnd() * lines.length)]);
    else if (kind === 2) {
      const i = Math.floor(rnd() * lines.length);
      const at = Math.floor(rnd() * (lines[i].length + 1));
      lines[i] = lines[i].slice(0, at) + pick(BITS) + lines[i].slice(at);
    } else if (kind === 3) {
      const i = Math.floor(rnd() * lines.length);
      const at = Math.floor(rnd() * lines[i].length);
      lines[i] = lines[i].slice(0, at) + lines[i].slice(at + 1 + Math.floor(rnd() * 3));
    } else if (kind === 4) return lines.join('\r\n');
    else if (kind === 5)
      lines.push(
        pick([
          'wavecode_1_enabled=1',
          'shapecode_0_enabled=1',
          'shapecode_2_enabled=1',
          'wavecode_3_enabled=1',
          'warp_3=x',
          'comp_9=y',
          'ob_size=0.5',
          'mv_x=3',
          'ib_a=1',
          'ob_r=1',
          'bSolarize=1',
          'nWaveMode=' + Math.floor(rnd() * 12 - 2),
          'nVideoEchoOrientation=' + Math.floor(rnd() * 6),
          'per_frame_9=q1 = q1 + 1;',
          'per_frame_09=q2 = 4;',
          'per_frame_1=zoom = 1;',
          'PER_FRAME_5=q3=1;',
          'zoom=' +
            pick([
              '1e',
              '1e999',
              '.5',
              ' 1.5abc',
              '--1',
              '+2',
              'Infinity',
              '-Infinity',
              '0x10',
              'NaN',
              '5.',
            ]),
        ]),
      );
    else if (kind === 6) {
      const i = Math.floor(rnd() * lines.length);
      lines[i] = lines[i].replace(
        /=.*/,
        '=' + pick(['', ' ', 'abc', '1', '0.5', '-3', '1e3', '999999999999', '\t7']),
      );
    } else if (kind === 7) {
      const i = Math.floor(rnd() * lines.length);
      lines[i] =
        pick(['', ' ', '[preset00]', '=', '=5', 'a=', '  zoom  =  1.1', 'Zoom=1.1']) +
        lines[i].slice(0, 20);
    }
    t = lines.join('\n');
  }
  return t;
}

const cases = [];
function add(text, name) {
  let r;
  try {
    r = M.convert(text, name);
  } catch (e) {
    r = { threw: String(e) };
  }
  if (r.threw) throw new Error('JavaScript threw on a case: ' + r.threw);
  cases.push({
    text,
    name,
    r: r.ok ? { ok: true, preset: r.preset, notes: r.notes } : { ok: false, error: r.error },
  });
}
BASES.forEach((b, i) => NAMES.forEach(n => add(b, n)));
for (const t of [
  '',
  '   \n  ',
  '[preset00]',
  'hello world',
  'a=b\nc=d',
  'zoom=1',
  'FDECAY=0.5',
  'per_frame_1=q1=1;',
  'per_frame_init_1=q1=1;',
  'nWaveMode=3',
  'zoom=1\0',
  'x'.repeat(102401),
  'zoom=1\n' + 'a=1\n'.repeat(7000),
  'zoom=1\n' + 'per_frame_1=' + 'q1=1;'.repeat(3000),
])
  add(t, 'edge.milk');
for (let i = 0; i < 150; i++) add(mutate(pick(BASES)), pick(NAMES));
// loops and bitwise placement
for (const f of [
  'loop(3,x=1)',
  'xloop(3,x=1)',
  'loop (3, x=1)',
  'LOOP\t(3)',
  'a_loop(1)',
  'while(1)',
  'megabuf (1)',
  'gmegabuf(1)',
  'my_megabuf(1)',
  'a&b',
  'a&&b',
  'a|b',
  'a||b',
  'a|&b',
  'a&',
  '|a',
]) {
  add('[preset00]\nfDecay=0.9\nper_frame_1=q1 = ' + f + ';\n', 'x.milk');
  add('[preset00]\nfDecay=0.9\nwavecode_0_enabled=1\nwave_0_per_point1=x = ' + f + ';\n', 'x.milk');
  add(
    '[preset00]\nfDecay=0.9\nshapecode_1_enabled=1\nshape_1_per_frame1=x = ' + f + ';\n',
    'x.milk',
  );
}
fs.writeFileSync(
  path.join(__dirname, 'milk.golden'),
  cases.map(c => JSON.stringify(c)).join('\n') + '\n',
);
const okCount = cases.filter(c => c.r.ok).length;
console.log(
  `wrote ${cases.length} cases (${okCount} converted, ${cases.length - okCount} refused)`,
);
