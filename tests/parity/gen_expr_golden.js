// Records what the JavaScript equation language (FalseHorizons/js/falsehorizon-expr.js, the
// reference) does with a list of formulas, so the C++ port can be held to the same answers. Writes
// expr.golden next to this file. Needs Node; the result is committed, so only run it when the
// reference or the list of formulas changes:      node tests/parity/gen_expr_golden.js
//
// One case is four or five lines:
//   F <formula>          (\n, \t, \r and \\ are written as escapes)
//   I name=value ...     the variables the formula starts with
//   R v1 v2 ...          what rand() gives, in turn, starting over at the end
//   E <message>          the formula was refused, with this message          -- or --
//   M reads=a,b writes=c statements=N      what the compiled formula says about itself
//   V name=value ...     every name it uses, after running it
// Everything is plain ASCII: the C++ version counts bytes where JavaScript counts UTF-16 units, so
// they only agree on ASCII (all presets are).
const fs = require('fs'),
  path = require('path');
const E = require('../../js/falsehorizon-expr.js');

const POOL = ['a', 'b', 'c', 'd', 'bass', 'mid', 'treb', 'time', 'x', 'y'];
let seed = 20261009;
const rnd = () => {
  seed |= 0;
  seed = (seed + 0x6d2b79f5) | 0;
  let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
  t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
  return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
};
const pick = list => list[Math.floor(rnd() * list.length)];
const esc = s =>
  s.replace(/\\/g, '\\\\').replace(/\n/g, '\\n').replace(/\t/g, '\\t').replace(/\r/g, '\\r');

// ---------- the formulas ----------
const hand = [];
const add = (...f) => hand.push(...f);
add(
  'a = 1 + 2 * 3;',
  'a = (1 + 2) * 3;',
  'a = 10 - 4 - 3;',
  'a = 2 * 3 % 4;',
  'a = 8 / 2 / 2;',
  'a = 2 ^ 3 ^ 2;',
  'a = -2 ^ 2;',
  'a = 2 ^ -1;',
  'a = (-2) ^ 2;',
  'a = .5 + 1e3 + 1.5e-1;',
  'a = 5. + 1.e2;',
  'a = 3 > 2 && 1 < 2;',
  'a = 3 < 2 || 0;',
  'a = !0; b = !5;',
  'a = 2 == 2; b = 2 != 2; c = 2 <= 2; d = 3 >= 4;',
  'a = if(1, 5, rand(1));',
  'a = if(0, 5, rand(1));',
  'a = 0 && rand(1);',
  'a = 1 || rand(1);',
  'a = 1 && rand(1);',
  'a = 0 || rand(1);',
  'a = rand(10);',
  'a = rand(rand(10));',
  'a = rand(1) + rand(1) * rand(1);',
  'a = rand(1) / rand(1);',
  'a = 0 / rand(1);',
  'a = rand(1) / 0;',
  'a = rand(5) % rand(3);',
  'a = rand(5) % 0;',
  'a = bass % 3;',
  'a = -bass % 3;',
  'a = bass % -3;',
  'a = 7 % 0;',
  'a = rand(2) % 1.5 + rand(2);',
  'a = sin(0); b = cos(0); c = tan(1); d = atan(1);',
  'a = asin(2);',
  'a = acos(0.5);',
  'a = atan2(1, 1);',
  'a = abs(-3);',
  'a = sqrt(16); b = sqrt(0 - 4);',
  'a = exp(1); b = exp(1000);',
  'a = log(0); b = log(2.718281828); c = log10(1000); d = log10(0);',
  'a = band(1, 0); b = bor(1, 0); c = bnot(5);',
  'a = floor(2.7); b = ceil(2.1); c = int(-2.7); d = frac(2.25);',
  'a = sign(-9); b = sign(0); c = sign(4); d = sqr(3);',
  'a = min(2, 5); b = max(2, 5); c = pow(2, 10); d = mod(-1, 3);',
  'a = pow(-8, 0.5); b = pow(2, 0.5); c = pow(0, 0); d = pow(1, 1000000);',
  'a = pow(10, 100);',
  'a = above(2, 1); b = below(2, 1); c = equal(1, 1);',
  'a = sigmoid(0, 3); b = sigmoid(100, 3); c = sigmoid(-100, 3);',
  'a = clamp(5, 0, 1); b = clamp(-5, 0, 1); c = clamp(5, 1, 0); d = clamp(0.5, 0, 1);',
  'a = mix(0, 10, 0.25);',
  'a = smoothstep(0, 1, 2); b = smoothstep(0, 1, 0.5); c = smoothstep(1, 1, 0.5); d = smoothstep(1, 1, 2);',
  'a = 2; a += 3; a *= 2; a -= 1; a /= 3;',
  'a = 4; a /= 0;',
  'x = 5; x += 1; x -= 7; x *= -3; x /= 0.5;',
  'A = 2; b = a + c;',
  'zoom = 1 + bass * 0.5; rot = sin(time) * 0.01;',
  'a = 1 / 0;',
  'a = 5 % 0;',
  'a = 1e30 * 1e30;',
  'a = 1e999;',
  'a = 1e-999;',
  'a = 1e999 - 1e999;',
  'a = 0.1 + 0.2;',
  'a = 1e6 + 1; b = -1e6 - 1;',
  'a = 123456789 * 987654321;',
  '',
  ' ;; ; ',
  ';',
  'a = 1;;; b = 2;',
  '  a  =  1  ;  ',
  'a=1;b=2;c=a+b;',
  'A = B + C; B = 1;',
  'a = bass + MID + Treb;',
  'a = (((((((((((((((((((((((1)))))))))))))))))))))));',
  'a = ' + '('.repeat(24) + '1' + ')'.repeat(24) + ';',
  'a = ' + '('.repeat(25) + '1' + ')'.repeat(25) + ';',
  'a = ' + '-'.repeat(24) + '1;',
  'a = ' + '-'.repeat(25) + '1;',
  'a = ' + '!'.repeat(30) + '1;',
  'a = ' + '+'.repeat(100) + '1;',
  'a = ' + '2^'.repeat(23) + '1;',
  'a = ' + '2^'.repeat(30) + '1;',
  'a = ' + '1/'.repeat(300) + '2;',
  'a = ' + '7%'.repeat(300) + '3;',
  'a = ' + '1+'.repeat(1400) + '1;',
  'a = ' + '1+'.repeat(1600) + '1;',
  'a = ' + 'sin('.repeat(23) + '1' + ')'.repeat(23) + ';',
  'a = ' + 'if(1,'.repeat(10) + '1' + ',2)'.repeat(10) + ';',
  'a = ' + 'min(1,'.repeat(20) + '1' + ')'.repeat(20) + ';',
  'a = ' + 'clamp(0,0,'.repeat(10) + '1' + ')'.repeat(10) + ';',
  'a = 1;'.repeat(250),
  'a=1;'.repeat(251),
  'a = 1;'.repeat(1200),
  Array.from({ length: 120 }, (_, i) => `n${i} = 1;`).join(''),
  Array.from({ length: 121 }, (_, i) => `n${i} = 1;`).join(''),
  Array.from({ length: 250 }, (_, i) => `v${i % 100} = ${i} + 1;`).join(''),
  'x'.repeat(6000),
  'a = 1;' + ' '.repeat(5990),
  'a = 1;' + ' '.repeat(5995),
  // refused, with a message a person can read
  'a = foo(1);',
  'a = sin(1, 2);',
  'a = sin();',
  'a = min(1);',
  'a = if(1, 2);',
  'a = clamp(1, 2, 3, 4);',
  '1 + 2;',
  'a = ;',
  'a = 1 +',
  'a = sin;',
  'a = rand;',
  'sin = 3;',
  'if = 3;',
  'a = "hi";',
  "a = 'x';",
  'a = b.c;',
  'a = b[0];',
  'a = {};',
  'a = (1 + 2;',
  'a = 1 + 2);',
  'a = eval(1);',
  'a = 1 b = 2;',
  'a == 1;',
  'a = `1`;',
  '__proto__ = 1;',
  'a = constructor;',
  'prototype = 3;',
  'a = __x;',
  'a = _x + __x;',
  'a' + 'b'.repeat(40) + ' = 1;',
  'a' + 'b'.repeat(31) + ' = 1;',
  'a' + 'b'.repeat(32) + ' = 1;',
  'abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuv = 1;',
  'a = 1; // x',
  'a = () => 1;',
  'a = 1 1;',
  'a = 1 1.50;',
  'a = 1 0.0000001;',
  'a = 1 1e21;',
  'a = 1 123456789012345678901234567890;',
  'a = 1 1e300;',
  'a = 1 0.000001;',
  'a = 1 100000000000000000000;',
  'a = 1 1e-7;',
  'a = 1 5e-324;',
  'a = 1 2 3;',
  'a = 1 .5;',
  'a = 1 b;',
  'a = 1 (2);',
  'a = (1) (2);',
  'a = 1 ,;',
  'a = 1 =;',
  'a = = 1;',
  'a += ;',
  '+= 1;',
  'a =+ 1;',
  'a = 1 +* 2;',
  'a = 1 * / 2;',
  'a = @;',
  'a = #;',
  'a = $;',
  'a = ;',
  'a = a ? b : c;',
  'a = 1 & 2;',
  'a = 1 | 2;',
  'a = 1 && ;',
  'a = 1 <> 2;',
  'a = 1 =< 2;',
  'a = 1 === 2;',
  'a = 1 ^;',
  'a = ^ 1;',
  'a = !;',
  'a = -;',
  'a = ();',
  'a = (,);',
  'a = sin(,);',
  'a = sin(1,);',
  'a = sin(,1);',
  'a = atan2(1 2);',
  'a = 1e;',
  'a = 1e+;',
  'a = 1e+5;',
  'a = 1e5e5;',
  'a = 1..2;',
  'a = .;',
  'a = 1.2.3;',
  'a = 0x10;',
  'a = 1_000;',
  'a = 2x;',
  'a = x2;',
  'a\t=\t1\n;\r\n',
  'a = 1;\n\nb = 2;\n',
  'a = 1\n+ 2;',
  'a=\u0001;',
  'a = \u007f;',
  'a = b = 1;',
  'a = (b = 1);',
  'a ++;',
  'a = a++;',
  'a = 1; b = a; a = b + a; c = a * b;',
  'if(1, a = 1, 2);',
  'a = if(1, 2, 3',
  'a = if(1 2 3);',
);

const NUMS = [
  '0',
  '1',
  '2',
  '3',
  '10',
  '0.5',
  '.25',
  '1e3',
  '1.5e-1',
  '0.00001',
  '1e-5',
  '1e30',
  '100000',
  '7',
  '0.1',
  '255',
  '6.28318',
];
const BIN = ['+', '-', '*', '/', '%', '^', '==', '!=', '<', '>', '<=', '>=', '&&', '||'];
const FN1 = [
  'sin',
  'cos',
  'tan',
  'asin',
  'acos',
  'atan',
  'sqrt',
  'abs',
  'exp',
  'log',
  'log10',
  'bnot',
  'floor',
  'ceil',
  'int',
  'frac',
  'sign',
  'sqr',
  'rand',
];
const FN2 = [
  'atan2',
  'band',
  'bor',
  'min',
  'max',
  'pow',
  'mod',
  'above',
  'below',
  'equal',
  'sigmoid',
];
const FN3 = ['clamp', 'mix', 'smoothstep', 'if'];
function expr(d) {
  const r = rnd();
  if (d <= 0 || r < 0.22) return rnd() < 0.5 ? pick(NUMS) : pick(POOL);
  if (r < 0.58) return `${expr(d - 1)} ${pick(BIN)} ${expr(d - 1)}`;
  if (r < 0.66) return `${pick(['-', '!', '+'])}${expr(d - 1)}`;
  if (r < 0.74) return `(${expr(d - 1)})`;
  if (r < 0.88) return `${pick(FN1)}(${expr(d - 1)})`;
  if (r < 0.95) return `${pick(FN2)}(${expr(d - 1)}, ${expr(d - 1)})`;
  return `${pick(FN3)}(${expr(d - 1)}, ${expr(d - 1)}, ${expr(d - 1)})`;
}
function program() {
  const n = 1 + Math.floor(rnd() * 4),
    out = [];
  for (let i = 0; i < n; i++)
    out.push(
      `${pick(POOL)} ${pick(['=', '=', '=', '+=', '-=', '*=', '/='])} ${expr(1 + Math.floor(rnd() * 4))};`,
    );
  return out.join(' ');
}
const JUNK = '();,+-*/%^=<>!&|.eE0 _x@#"\'[]{}?:~$\\';
function mutate(s) {
  if (!s.length) return pick(JUNK.split(''));
  const i = Math.floor(rnd() * s.length),
    r = rnd();
  if (r < 0.35) return s.slice(0, i) + s.slice(i + 1);
  if (r < 0.7) return s.slice(0, i) + pick(JUNK.split('')) + s.slice(i);
  if (r < 0.85) return s.slice(0, i);
  return s.slice(0, i) + pick(JUNK.split('')) + s.slice(i + 1);
}
const fuzz = [];
for (let i = 0; i < 80; i++) fuzz.push(program());
for (let i = 0; i < 60; i++) {
  let s = program();
  for (let k = 0, n = 1 + Math.floor(rnd() * 2); k < n; k++) s = mutate(s);
  fuzz.push(s);
}

// ---------- run them ----------
const out = [
  '# Written by gen_expr_golden.js from FalseHorizons/js/falsehorizon-expr.js. Do not edit by hand.',
];
let ok = 0,
  refused = 0;
for (const src of [...hand, ...fuzz]) {
  if (/[^\x00-\x7f]/.test(src)) throw new Error('non-ASCII formula: ' + src);
  const vars = Object.create(null);
  const init = POOL.map(n => {
    const v = Math.round((rnd() * 10 - 5) * 1000) / 1000;
    vars[n] = v;
    return `${n}=${v}`;
  });
  const rngList = [0.25, 0.5, 0.75, 0.1, 0.9, 0.333].map(x => x);
  let k = 0;
  const rng = () => rngList[k++ % rngList.length];
  out.push(`F ${esc(src)}`, `I ${init.join(' ')}`, `R ${rngList.join(' ')}`);
  let program;
  try {
    program = E.compile(src, { rng });
  } catch (e) {
    if (!(e instanceof E.ExprError)) throw e;
    out.push(`E ${esc(e.message)}`);
    refused++;
    continue;
  }
  program.run(vars);
  out.push(
    `M reads=${program.reads.join(',')} writes=${program.writes.join(',')} statements=${program.size}`,
  );
  out.push('V ' + program.reads.map(n => `${n}=${vars[n] === undefined ? 0 : vars[n]}`).join(' '));
  ok++;
}
fs.writeFileSync(path.join(__dirname, 'expr.golden'), out.join('\n') + '\n');
console.log(
  `expr.golden: ${ok + refused} formulas (${ok} run, ${refused} refused), ${(out.join('\n').length / 1024).toFixed(0)} KB`,
);
