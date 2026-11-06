// Tests for the running preset (runtime.hpp): hand-written checks plus parity with
// tests/parity/runtime.golden (see gen_runtime_golden.js; 1e-6 relative because vertices are
// float32).
#include "check.hpp"
#include "fh/runtime.hpp"
#include "tests.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

using namespace fh;
using json::Value;

using test::check;

// The same seeded generator as gen_runtime_golden.js (mulberry32), in unsigned 32-bit arithmetic.
struct Seeded {
  std::uint32_t s;
  double next() {
    s += 0x6D2B79F5U;
    std::uint32_t t = (s ^ (s >> 15)) * (1U | s);
    t = (t + ((t ^ (t >> 7)) * (61U | t))) ^ t;
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
  }
};

// The same made-up input as inputAt() in gen_runtime_golden.js.
struct Input {
  FrameInputs in;
  std::array<float, 256> wave{};
  std::array<float, 64> spec{};
};
static void input_at(Input& x, int i, double aspect, double t0) {
  const double bass = 0.5 + 0.5 * std::sin(i * 0.7);
  const double mid = 0.5 + 0.5 * std::sin(i * 0.3 + 1);
  const double treb = 0.5 + 0.5 * std::sin(i * 1.1 + 2);
  for (int k = 0; k < 256; k++) {
    x.wave[static_cast<std::size_t>(k)] = static_cast<float>(std::sin(k * 0.1 + i) * 0.8);
  }
  for (int k = 0; k < 64; k++) {
    x.spec[static_cast<std::size_t>(k)] =
        static_cast<float>(0.5 + 0.5 * std::sin(k * 0.2 + i * 0.3));
  }
  FrameInputs f;
  f.time = t0 + i / 60.0;
  f.frame = i;
  f.bass = bass;
  f.mid = mid;
  f.treb = treb;
  f.bass_att = 0.4 + 0.3 * std::sin(i * 0.1);
  f.mid_att = 0.4 + 0.3 * std::sin(i * 0.12 + 1);
  f.treb_att = 0.4 + 0.3 * std::sin(i * 0.14 + 2);
  f.level = (bass * 2 + mid + treb) / 4;
  f.beat = i % 7 == 0 ? 1 : std::pow(0.9, i % 7);
  f.aspect = aspect;
  f.fps = 60;
  f.progress = i / 12.0;
  f.wave = x.wave;
  f.spec = x.spec;
  x.in = f;
}

static Value arr(const std::vector<float>& a) {
  double sum = 0;
  double wsum = 0;
  for (std::size_t i = 0; i < a.size(); i++) {
    sum += a[i];
    wsum += static_cast<double>(i % 97 + 1) * a[i];
  }
  Value o = Value::object();
  o.set("n", Value::number(static_cast<double>(a.size())));
  o.set("sum", Value::number(sum));
  o.set("wsum", Value::number(wsum));
  Value head = Value::array();
  Value tail = Value::array();
  for (std::size_t i = 0; i < 6 && i < a.size(); i++) {
    head.push(Value::number(a[i]));
  }
  for (std::size_t i = a.size() > 6 ? a.size() - 6 : 0; i < a.size(); i++) {
    tail.push(Value::number(a[i]));
  }
  o.set("head", std::move(head));
  o.set("tail", std::move(tail));
  return o;
}
static Value summary(const FrameState& s) {
  Value o = Value::object();
  o.set("decay", Value::number(s.decay));
  Value c = Value::object();
  c.set("gamma", Value::number(s.color.gamma));
  c.set("hue", Value::number(s.color.hue));
  c.set("bright", Value::number(s.color.bright));
  c.set("solarize", Value::number(s.color.solarize));
  c.set("echoAlpha", Value::number(s.color.echo_alpha));
  c.set("echoZoom", Value::number(s.color.echo_zoom));
  c.set("echoOrient", Value::number(s.color.echo_orient));
  c.set("cover", Value::number(s.color.cover));
  o.set("color", std::move(c));
  o.set("mesh", arr(s.mesh));
  Value draws = Value::array();
  for (const Draw& d : s.draws) {
    Value e = Value::object();
    static const char* kinds[] = {"strip", "lines", "dots", "fan"};
    e.set("kind", Value::string(kinds[static_cast<int>(d.kind)]));
    e.set("count", Value::number(d.count));
    e.set("additive", Value::boolean(d.additive));
    e.set("thick", Value::number(d.thick));
    e.set("verts", arr(d.verts));
    draws.push(std::move(e));
  }
  o.set("draws", std::move(draws));
  return o;
}

// Same shape, same strings, numbers within 1e-6 relative (non-finite on our side must be null on
// theirs).
static bool same(const Value& want, const Value& got, const std::string& at, std::string& why) {
  if (want.type() != got.type()) {
    why = at + ": different types";
    return false;
  }
  switch (want.type()) {
    case Value::Type::Null:
      return true;
    case Value::Type::Bool:
      if (want.as_bool() != got.as_bool()) {
        why = at + ": different flags";
        return false;
      }
      return true;
    case Value::Type::Number: {
      const double a = want.as_number();
      const double b = got.as_number();
      if (std::fabs(a - b) > 1e-6 * std::max(1.0, std::max(std::fabs(a), std::fabs(b)))) {
        why = at + ": want " + json::stringify(want) + " got " + json::stringify(got);
        return false;
      }
      return true;
    }
    case Value::Type::String:
      if (want.as_string() != got.as_string()) {
        why = at + ": \"" + want.as_string() + "\" vs \"" + got.as_string() + "\"";
        return false;
      }
      return true;
    case Value::Type::Array:
      if (want.items().size() != got.items().size()) {
        why = at + ": " + std::to_string(want.items().size()) + " vs " +
              std::to_string(got.items().size()) + " items";
        return false;
      }
      for (std::size_t i = 0; i < want.items().size(); i++) {
        if (!same(want.items()[i], got.items()[i], at + "[" + std::to_string(i) + "]", why)) {
          return false;
        }
      }
      return true;
    case Value::Type::Object:
      if (want.members().size() != got.members().size()) {
        why = at + ": different keys";
        return false;
      }
      for (const auto& m : want.members()) {
        const Value* o = got.find(m.first);
        if (!o) {
          why = at + ": missing key " + m.first;
          return false;
        }
        if (!same(m.second, *o, at + "." + m.first, why)) {
          return false;
        }
      }
      return true;
  }
  return false;
}

static Preset load(const std::string& body) {
  const PresetResult r = parse_preset_file("{\"fh-preset\":1," + body + "}");
  if (!r.ok) {
    check("the test preset loads", false, r.error);
  }
  return r.preset;
}

static void hand_tests() {
  Input in;
  input_at(in, 3, 16.0 / 9, 0);
  {
    PresetRuntime rt(load("\"name\":\"plain\""));
    FrameState s = rt.frame(in.in);
    check("a preset with no formulas leaves the mesh where it started", [&] {
      if (s.mesh.size() != mesh_floats) {
        return false;
      }
      for (int j = 0; j <= mesh_h; j++) {
        for (int i = 0; i <= mesh_w; i++) {
          const std::size_t k = static_cast<std::size_t>(j * (mesh_w + 1) + i) * 2;
          if (std::fabs(s.mesh[k] - static_cast<float>(i) / mesh_w) > 1e-5 ||
              std::fabs(s.mesh[k + 1] - static_cast<float>(j) / mesh_h) > 1e-5) {
            return false;
          }
        }
      }
      return true;
    }());
    check("the defaults come through",
          s.decay == 0.97 && s.color.gamma == 1 && s.color.bright == 1 && s.color.echo_orient == 0);
    check("the plain wave is drawn (a circle: 129 points)",
          s.draws.size() == 1 && s.draws[0].count == 129 && s.draws[0].kind == DrawKind::Strip);
  }
  {
    PresetRuntime rt(load("\"name\":\"z\",\"frame\":\"zoom = 9; decay = 5; wave_a = 0;\""));
    const FrameState s = rt.frame(in.in);
    check("values a formula sets are forced into range", s.decay == 0.99 && s.draws.empty());
  }
  {
    PresetRuntime a(load("\"name\":\"r\",\"frame\":\"zoom = 1 + rand(10) * 0.01;\""));
    PresetRuntime b(load("\"name\":\"r\",\"frame\":\"zoom = 1 + rand(10) * 0.01;\""));
    const FrameState sa = a.frame(in.in);
    const FrameState sb = b.frame(in.in);
    check("two random runs differ", sa.mesh != sb.mesh);
    Seeded s1{5};
    Seeded s2{5};
    PresetRuntime c(load("\"name\":\"r\",\"frame\":\"zoom = 1 + rand(10) * 0.01;\""),
                    [&] { return s1.next(); });
    PresetRuntime d(load("\"name\":\"r\",\"frame\":\"zoom = 1 + rand(10) * 0.01;\""),
                    [&] { return s2.next(); });
    check("the same seed gives the same frame", c.frame(in.in).mesh == d.frame(in.in).mesh);
  }
  {
    Preset bad = load("\"name\":\"x\"");
    bad.frame = "a = ;";
    bool threw = false;
    try {
      const PresetRuntime rt(bad);
    } catch (const std::exception&) {
      threw = true;
    }
    check("a formula that does not compile is an exception, not a crash", threw);
  }
  {
    PresetRuntime rt(
        load("\"name\":\"x\",\"wave\":{\"a\":0.9},\"shapes\":[{\"a\":0.9},{\"a\":0.9}]"));
    const FrameInputs bare;
    FrameState s = rt.frame(bare);
    check("missing waveform and spectrum count as silence",
          s.draws.size() == 3 &&
              std::ranges::all_of(s.mesh, [](float v) { return std::isfinite(v); }));
    FrameOptions off;
    off.weight = 0;
    check("weight 0 draws nothing", rt.frame(bare, off).draws.empty());
  }
  {
    PresetRuntime p(load(
        "\"name\":\"a\",\"zoom\":1.1,\"decay\":0.9,\"color\":{\"gamma\":1.5,\"echoOrient\":1}"));
    PresetRuntime q(
        load("\"name\":\"b\",\"zoom\":0.9,\"decay\":0.95,\"color\":{\"gamma\":0.5,\"echoOrient\":"
             "2}"));
    const FrameState a = p.frame(in.in);
    const FrameState b = q.frame(in.in);
    check("blend: at t=0 with nothing to blend to the result is a",
          blend_states(&a, nullptr, 0.5).mesh == a.mesh);
    check("blend: at t=1 the result is b",
          blend_states(&a, &b, 1).mesh == b.mesh && blend_states(nullptr, &b, 0.2).mesh == b.mesh);
    const FrameState m = blend_states(&a, &b, 0.5);
    check("blend: the middle mixes the colours and holds echo steady, draws are both",
          std::fabs(m.color.gamma - 1.0) < 1e-12 && m.color.echo_orient == 2 &&
              m.draws.size() == a.draws.size() + b.draws.size() && m.color.echo_alpha > 0.2 &&
              m.decay > 0.9 && m.decay <= 0.99);
    const FrameState z = blend_states(&a, &b, 0);
    check("blend: at t=0 the mesh is a's", z.mesh == a.mesh);
  }
}

static bool run_case(const Value& c, std::string& why) {
  const std::string kind = c.find("kind")->as_string();
  const PresetResult pr = sanitize_preset(*c.find("p"));
  if (!pr.ok) {
    why = "preset rejected: " + pr.error;
    return false;
  }
  Seeded rng{static_cast<std::uint32_t>(c.find("seed")->as_number())};
  if (kind == "run") {
    PresetRuntime rt(pr.preset, [&] { return rng.next(); });
    FrameOptions opt;
    opt.motion = c.find("motion")->as_number();
    opt.weight = c.find("weight")->as_number();
    const double aspect = c.find("aspect")->as_number();
    const double t0 = c.find("t0")->as_number();
    const Value* frames = c.find("frames");
    Input in;
    for (int i = 0; i < 12; i++) {
      input_at(in, i, aspect, t0);
      const FrameState s = rt.frame(in.in, opt);
      if (const Value* want = frames->find(std::to_string(i))) {
        if (!same(*want, summary(s), "frame " + std::to_string(i), why)) {
          return false;
        }
      }
    }
    return true;
  }
  const PresetResult qr = sanitize_preset(*c.find("q"));
  if (!qr.ok) {
    why = "second preset rejected";
    return false;
  }
  Seeded rng2{static_cast<std::uint32_t>(c.find("seed")->as_number()) + 1};
  PresetRuntime ra(pr.preset, [&] { return rng.next(); });
  PresetRuntime rb(qr.preset, [&] { return rng2.next(); });
  FrameState sa;
  FrameState sb;
  Input in;
  for (int i = 0; i < 6; i++) {
    input_at(in, i, 16.0 / 9, 3);
    FrameOptions oa;
    FrameOptions ob;
    oa.weight = 0.7;
    ob.weight = 0.4;
    sa = ra.frame(in.in, oa);
    sb = rb.frame(in.in, ob);
  }
  const auto& ts = c.find("ts")->items();
  const auto& blends = c.find("blends")->items();
  for (std::size_t i = 0; i < ts.size(); i++) {
    if (!same(blends[i], summary(blend_states(&sa, &sb, ts[i].as_number())),
              "blend t=" + json::stringify(ts[i]), why)) {
      return false;
    }
  }
  return true;
}

static void parity(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    check("parity: open " + path, false);
    return;
  }
  int cases = 0;
  int bad = 0;
  std::string line;
  while (std::getline(in, line)) {
    const auto parsed = json::parse(line);
    if (!parsed.ok) {
      check("parity: golden line " + std::to_string(cases + 1) + " is readable", false);
      bad++;
      break;
    }
    cases++;
    std::string why;
    bool ok = false;
    try {
      ok = run_case(parsed.value, why);
    } catch (const std::exception& e) {
      why = e.what();
    }
    if (!ok) {
      bad++;
      const Value* p = parsed.value.find("p");
      const Value* name = p ? p->find("name") : nullptr;
      if (bad <= 10) {
        check("parity: case " + std::to_string(cases) + " " +
                  parsed.value.find("kind")->as_string() + " \"" + (name ? name->as_string() : "") +
                  "\"",
              false, why);
      }
    }
  }
  check("parity: " + std::to_string(cases) + " cases, " + std::to_string(bad) +
            " differ from the JavaScript",
        cases > 150 && bad == 0);
}

void runtime_tests(const std::string& dir) {
  test::section("runtime");
  hand_tests();
  parity(dir + "/runtime.golden");
}
