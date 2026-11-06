#include "fh/runtime.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <string>

namespace fh {

namespace {

using expr::Program;
using expr::Symbols;

constexpr double pi = std::numbers::pi;
constexpr double add_gain =
    0.4;  // light drawn on top is added every frame and the feedback keeps
          // it, so it is drawn at 40% strength or everything washes to white
double strength(bool additive) { return additive ? add_gain : 1.0; }
double clamp(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }
double finite(double v, double fallback) { return std::isfinite(v) ? v : fallback; }
double lerp(double a, double b, double t) { return a + (b - a) * t; }
double orv(double v, double fallback) {
  return (v == 0 || std::isnan(v)) ? fallback : v;
}  // JavaScript's `v || fallback`
int round_js(double v) { return static_cast<int>(std::floor(v + 0.5)); }  // Math.round

// The values a preset can steer every frame: [lowest, highest, starting value], in the order the
// JavaScript lists them.
struct Range {
  const char* name;
  double lo, hi, def;
};
constexpr Range ranges[] = {
    {"zoom", 0.8, 1.3, 1},      {"rot", -0.5, 0.5, 0},    {"cx", -0.5, 1.5, 0.5},
    {"cy", -0.5, 1.5, 0.5},     {"dx", -0.1, 0.1, 0},     {"dy", -0.1, 0.1, 0},
    {"sx", 0.5, 2, 1},          {"sy", 0.5, 2, 1},        {"warp", 0, 4, 0},
    {"decay", 0.5, 0.99, 0.97}, {"wave_r", 0, 1, 1},      {"wave_g", 0, 1, 1},
    {"wave_b", 0, 1, 1},        {"wave_a", 0, 1, 0.8},    {"wave_scale", 0, 4, 1},
    {"wave_x", 0, 1, 0.5},      {"wave_y", 0, 1, 0.5},    {"gamma", 0.5, 2, 1},
    {"hue", -1000, 1000, 0},    {"bright", 0, 2, 1},      {"solarize", 0, 1, 0},
    {"echo_alpha", 0, 0.5, 0},  {"echo_zoom", 0.5, 3, 1}, {"echo_orient", 0, 3, 0},
    {"cover", 0, 1, 0},
};
constexpr int range_count = static_cast<int>(sizeof ranges / sizeof ranges[0]);
enum Out {
  ZOOM,
  ROT,
  CX,
  CY,
  DX,
  DY,
  SX,
  SY,
  WARP,
  DECAY,
  WaveR,
  WaveG,
  WaveB,
  WaveA,
  WaveScale,
  WaveX,
  WaveY,
  GAMMA,
  HUE,
  BRIGHT,
  SOLARIZE,
  EchoAlpha,
  EchoZoom,
  EchoOrient,
  COVER
};
constexpr const char* warp_names[] = {"zoom", "rot", "cx", "cy", "dx", "dy", "sx", "sy", "warp"};
constexpr const char* input_names[] = {"time",     "frame",   "bass",     "mid",    "treb",
                                       "bass_att", "mid_att", "treb_att", "level",  "beat",
                                       "aspect",   "fps",     "progress", "meshx",  "meshy",
                                       "pixelsx",  "pixelsy", "aspectx",  "aspecty"};
constexpr int input_count = static_cast<int>(sizeof input_names / sizeof input_names[0]);
constexpr int heard_from = 2, heard_to = 7;  // bass .. treb_att in input_names: an imported preset
                                             // (written for louder numbers) gets these scaled up

struct Space {
  Symbols sym;
  std::vector<double> v;
  std::array<int, input_count> in{};
  std::array<int, 32> q{};

  // Called after every formula that uses this space has been compiled, so the array is big enough
  // for all of them.
  void finish() {
    for (int i = 0; i < input_count; i++) {
      in[static_cast<std::size_t>(i)] = sym.slot(input_names[i]);
    }
    for (int i = 0; i < 32; i++) {
      q[static_cast<std::size_t>(i)] = sym.slot("q" + std::to_string(i + 1));
    }
    v.assign(sym.size(), 0.0);
  }
  double& at(int slot) { return v[static_cast<std::size_t>(slot)]; }
  void copy_inputs(const FrameInputs& f, double hear) {
    const double values[input_count] = {f.time,     f.frame,   f.bass,     f.mid,    f.treb,
                                        f.bass_att, f.mid_att, f.treb_att, f.level,  f.beat,
                                        f.aspect,   f.fps,     f.progress, f.meshx,  f.meshy,
                                        f.pixelsx,  f.pixelsy, f.aspectx,  f.aspecty};
    for (int i = 0; i < input_count; i++) {
      at(in[static_cast<std::size_t>(i)]) = values[i];
    }
    if (hear != 1) {
      for (int i = heard_from; i <= heard_to; i++) {
        at(in[static_cast<std::size_t>(i)]) *= hear;
      }
    }
  }
};

Program compile_in(Space& s, const std::string& src, const expr::Rng& rng,
                   const std::string& where) {
  expr::Options opt;
  opt.rng = rng;
  expr::CompileResult r = expr::compile(src, s.sym, opt);
  if (!r.ok) {
    throw std::runtime_error("The " + where + " formula has a problem: " + r.error);
  }
  return std::move(r.program);
}

float f32(double v) { return static_cast<float>(v); }

}  // namespace

struct PresetRuntime::Impl {
  Preset preset;
  bool milk = false;
  double hear = 1;

  Space main;
  Program init_prog, frame_prog;
  std::array<int, range_count> out_slot{};

  Space pixel;
  Program pixel_prog;
  std::array<int, 9> px_warp{};
  int px_x = 0, px_y = 0, px_rad = 0, px_ang = 0;
  std::vector<int> pixel_locals;

  struct Wave {
    CustomWave spec;
    Space space;
    Program init, frame, point;
    int r = 0, g = 0, b = 0, a = 0, sample = 0, value1 = 0, value2 = 0, x = 0, y = 0;
  };
  struct ShapeRt {
    Shape spec;
    Space space;
    Program init, frame;
    int sides = 0, x = 0, y = 0, rad = 0, ang = 0, r = 0, g = 0, b = 0, a = 0, r2 = 0, g2 = 0,
        b2 = 0, a2 = 0, instance = 0, num_inst = 0;
  };
  std::vector<Wave> waves;
  std::vector<ShapeRt> shapes;

  std::vector<float> uv = std::vector<float>(mesh_floats);

  std::array<double, range_count> base() const {
    const Preset& p = preset;
    const MainWave& w = p.wave;
    const ColorSettings& c = p.color;
    return {
        p.zoom, p.rot,    p.cx,       p.cy,         p.dx,        p.dy,
        p.sx,   p.sy,     p.warp,     p.decay,      w.r,         w.g,
        w.b,    w.a,      w.scale,    w.x,          w.y,         c.gamma,
        c.hue,  c.bright, c.solarize, c.echo_alpha, c.echo_zoom, static_cast<double>(c.echo_orient),
        c.cover};
  }
  void reset_outputs() {
    auto b = base();
    for (int i = 0; i < range_count; i++) {
      main.at(out_slot[static_cast<std::size_t>(i)]) = b[static_cast<std::size_t>(i)];
    }
  }

  void mesh_for(const std::array<double, range_count>& f, const FrameInputs& inp, double motion);
  void waves_and_shapes(const std::array<double, range_count>& f, const FrameInputs& inp,
                        const std::array<float, 256>& wave, const std::array<float, 64>& spec,
                        double weight, std::vector<Draw>& draws);
  void builtin_wave(const std::array<double, range_count>& f, double aspect,
                    const std::array<float, 256>& wave, const std::array<float, 64>& spec,
                    double weight, std::vector<Draw>& draws) const;
};

PresetRuntime::PresetRuntime(const Preset& preset, const expr::Rng& rng)
    : impl_(std::make_unique<Impl>()) {
  Impl& m = *impl_;
  m.preset = preset;
  m.milk = preset.dialect == "milk";
  m.hear = preset.hear;

  m.init_prog = compile_in(m.main, preset.init, rng, "init");
  m.frame_prog = compile_in(m.main, preset.frame, rng, "frame");
  m.pixel_prog = compile_in(m.pixel, preset.pixel, rng, "pixel");

  m.waves.resize(preset.waves.size());
  for (std::size_t i = 0; i < preset.waves.size(); i++) {
    Impl::Wave& w = m.waves[i];
    const std::string n = std::to_string(i + 1);
    w.spec = preset.waves[i];
    w.init = compile_in(w.space, w.spec.init, rng, "wave " + n + " init");
    w.frame = compile_in(w.space, w.spec.frame, rng, "wave " + n + " frame");
    w.point = compile_in(w.space, w.spec.point, rng, "wave " + n + " point");
    w.r = w.space.sym.slot("r");
    w.g = w.space.sym.slot("g");
    w.b = w.space.sym.slot("b");
    w.a = w.space.sym.slot("a");
    w.sample = w.space.sym.slot("sample");
    w.value1 = w.space.sym.slot("value1");
    w.value2 = w.space.sym.slot("value2");
    w.x = w.space.sym.slot("x");
    w.y = w.space.sym.slot("y");
    w.space.finish();
  }
  m.shapes.resize(preset.shapes.size());
  for (std::size_t i = 0; i < preset.shapes.size(); i++) {
    Impl::ShapeRt& s = m.shapes[i];
    const std::string n = std::to_string(i + 1);
    s.spec = preset.shapes[i];
    s.init = compile_in(s.space, s.spec.init, rng, "shape " + n + " init");
    s.frame = compile_in(s.space, s.spec.frame, rng, "shape " + n);
    const auto slot = [&](const char* name) { return s.space.sym.slot(name); };
    s.sides = slot("sides");
    s.x = slot("x");
    s.y = slot("y");
    s.rad = slot("rad");
    s.ang = slot("ang");
    s.r = slot("r");
    s.g = slot("g");
    s.b = slot("b");
    s.a = slot("a");
    s.r2 = slot("r2");
    s.g2 = slot("g2");
    s.b2 = slot("b2");
    s.a2 = slot("a2");
    s.instance = slot("instance");
    s.num_inst = slot("num_inst");
    s.space.finish();
  }

  for (int i = 0; i < range_count; i++) {
    m.out_slot[static_cast<std::size_t>(i)] = m.main.sym.slot(ranges[i].name);
  }
  m.main.finish();

  // The pixel formula's own variables (anything it uses that is not an input, a warp value,
  // x/y/rad/ang or a q) start at 0 for every mesh point.
  for (int i = 0; i < 9; i++) {
    m.px_warp[static_cast<std::size_t>(i)] = m.pixel.sym.slot(warp_names[i]);
  }
  m.px_x = m.pixel.sym.slot("x");
  m.px_y = m.pixel.sym.slot("y");
  m.px_rad = m.pixel.sym.slot("rad");
  m.px_ang = m.pixel.sym.slot("ang");
  m.pixel.finish();
  {
    std::vector<std::string> names = m.pixel_prog.reads();
    names.insert(names.end(), m.pixel_prog.writes().begin(), m.pixel_prog.writes().end());
    const auto kept = [&](const std::string& n) {
      for (const char* k : input_names) {
        if (n == k) {
          return true;
        }
      }
      for (const char* k : warp_names) {
        if (n == k) {
          return true;
        }
      }
      if (n == "x" || n == "y" || n == "rad" || n == "ang") {
        return true;
      }
      if (n.size() >= 2 && n[0] == 'q') {
        int num = 0;
        bool digits = true;
        for (std::size_t i = 1; i < n.size(); i++) {
          if (n[i] < '0' || n[i] > '9') {
            digits = false;
          } else {
            num = num * 10 + (n[i] - '0');
          }
          if (num > 100) {
            break;
          }
        }
        if (digits && n[1] != '0' && num >= 1 && num <= 32) {
          return true;
        }
      }
      return false;
    };
    std::vector<int> seen;
    for (const std::string& n : names) {
      if (kept(n)) {
        continue;
      }
      const int slot = m.pixel.sym.find(n);
      if (slot >= 0 && std::ranges::find(seen, slot) == seen.end()) {
        seen.push_back(slot);
        m.pixel_locals.push_back(slot);
      }
    }
  }

  m.reset_outputs();
  m.init_prog.run(m.main.v);
  for (auto& w : m.waves) {
    w.init.run(w.space.v);
  }
  for (auto& s : m.shapes) {
    s.init.run(s.space.v);
  }
}

PresetRuntime::~PresetRuntime() = default;
PresetRuntime::PresetRuntime(PresetRuntime&&) noexcept = default;
PresetRuntime& PresetRuntime::operator=(PresetRuntime&&) noexcept = default;
const Preset& PresetRuntime::preset() const { return impl_->preset; }

FrameState PresetRuntime::frame(const FrameInputs& inp, FrameOptions opts) {
  Impl& m = *impl_;
  Space& v = m.main;
  m.reset_outputs();
  v.copy_inputs(inp, m.hear);
  m.frame_prog.run(v.v);
  const auto out = [&](int k) -> double& { return v.at(m.out_slot[static_cast<std::size_t>(k)]); };
  if (m.milk) {
    out(CY) = 1 - out(CY);
    out(DY) = -out(DY);
    out(ROT) = -out(ROT);
    out(WaveY) = 1 - out(WaveY);
  }  // imported presets count y downwards and turn the other way
  std::array<double, range_count> f{};
  for (int k = 0; k < range_count; k++) {
    f[static_cast<std::size_t>(k)] =
        clamp(finite(out(k), ranges[k].def), ranges[k].lo, ranges[k].hi);
  }

  std::array<float, 256> wave{};
  std::array<float, 64> spec{};
  for (std::size_t i = 0; i < wave.size() && i < inp.wave.size(); i++) {
    wave[i] = inp.wave[i];
  }
  for (std::size_t i = 0; i < spec.size() && i < inp.spec.size(); i++) {
    spec[i] = inp.spec[i];
  }

  m.mesh_for(f, inp, opts.motion);
  FrameState st;
  m.waves_and_shapes(f, inp, wave, spec, opts.weight, st.draws);
  st.decay = f[DECAY];
  st.mesh = m.uv;
  st.color = {f[GAMMA],
              f[HUE],
              f[BRIGHT],
              f[SOLARIZE],
              f[EchoAlpha],
              f[EchoZoom],
              static_cast<double>(round_js(f[EchoOrient])),
              f[COVER]};
  return st;
}

void PresetRuntime::Impl::mesh_for(const std::array<double, range_count>& f, const FrameInputs& inp,
                                   double motion) {
  const double aspect = orv(inp.aspect, 1);
  const double corner = std::hypot(aspect * 0.5, 0.5);
  const double time = orv(inp.time, 0);
  const bool has_pixel = pixel_prog.statements() > 0;
  if (has_pixel) {
    pixel.copy_inputs(inp, hear);
    for (std::size_t i = 0; i < 32; i++) {
      pixel.at(pixel.q[i]) = main.at(main.q[i]);
    }
  }
  std::size_t k = 0;
  for (int j = 0; j <= mesh_h; j++) {
    for (int i = 0; i <= mesh_w; i++) {
      const double x = static_cast<double>(i) / mesh_w;
      const double y = static_cast<double>(j) / mesh_h;
      const double gx = (x - 0.5) * aspect;
      const double gy = y - 0.5;
      double zoom = f[ZOOM];
      double rot = f[ROT];
      double cx = f[CX];
      double cy = f[CY];
      double dx = f[DX];
      double dy = f[DY];
      double sx = f[SX];
      double sy = f[SY];
      double warp = f[WARP];
      if (has_pixel) {
        const double warp_in[9] = {f[ZOOM], f[ROT], f[CX], f[CY],  f[DX],
                                   f[DY],   f[SX],  f[SY], f[WARP]};
        for (std::size_t n = 0; n < 9; n++) {
          pixel.at(px_warp[n]) = warp_in[n];
        }
        for (const int slot : pixel_locals) {
          pixel.at(slot) = 0;
        }
        pixel.at(px_x) = x;
        pixel.at(px_y) = y;
        pixel.at(px_rad) = std::hypot(gx, gy) / corner;
        pixel.at(px_ang) = std::atan2(gy, gx);
        if (milk) {
          pixel.at(px_y) = 1 - y;
          pixel.at(px_ang) = -pixel.at(px_ang);
          pixel.at(px_warp[3]) = 1 - f[CY];
          pixel.at(px_warp[5]) = -f[DY];
          pixel.at(px_warp[1]) = -f[ROT];
        }
        pixel_prog.run(pixel.v);
        const auto pv = [&](std::size_t n, int idx, double def) {
          return clamp(finite(pixel.at(px_warp[n]), def), ranges[idx].lo, ranges[idx].hi);
        };
        zoom = pv(0, ZOOM, 1);
        rot = pv(1, ROT, 0);
        cx = pv(2, CX, 0.5);
        cy = pv(3, CY, 0.5);
        dx = pv(4, DX, 0);
        dy = pv(5, DY, 0);
        sx = pv(6, SX, 1);
        sy = pv(7, SY, 1);
        warp = pv(8, WARP, 0);
        if (milk) {
          cy = 1 - cy;
          dy = -dy;
          rot = -rot;
        }
      }
      const double mo = motion;
      zoom = 1 + (zoom - 1) * mo;
      sx = 1 + (sx - 1) * mo;
      sy = 1 + (sy - 1) * mo;
      rot *= mo;
      dx *= mo;
      dy *= mo;
      warp *= mo;
      double px = (x - cx) * aspect / (sx * zoom);
      double py = (y - cy) / (sy * zoom);
      const double cs = std::cos(rot);
      const double sn = std::sin(rot);
      const double rx = cs * px + sn * py;
      const double ry = -sn * px + cs * py;
      px = rx;
      py = ry;
      const double w = 0.012 * warp;
      const double wu = w * (std::sin(time * 0.33 + y * 9 + x * 2.1) +
                             0.5 * std::sin(time * 0.61 - y * 5 + x * 7));
      const double wv = w * (std::cos(time * 0.29 + x * 8 - y * 3.1) +
                             0.5 * std::cos(time * 0.53 + x * 4 + y * 6));
      uv[k++] = f32(px / aspect + cx - dx + wu);
      uv[k++] = f32(py + cy - dy + wv);
    }
  }
}

void PresetRuntime::Impl::builtin_wave(const std::array<double, range_count>& f, double aspect,
                                       const std::array<float, 256>& wave,
                                       const std::array<float, 64>& spec64, double weight,
                                       std::vector<Draw>& draws) const {
  const MainWave& spec = preset.wave;
  const double r = f[WaveR];
  const double g = f[WaveG];
  const double b = f[WaveB];
  const double a = f[WaveA] * weight * strength(spec.additive);
  const double sc = f[WaveScale];
  const double wx = f[WaveX];
  const double wy = f[WaveY];
  const auto mk = [&](int n, DrawKind kind, auto fill) {
    Draw d;
    d.kind = kind;
    d.count = n;
    d.additive = spec.additive;
    d.thick = spec.thick;
    d.verts.resize(static_cast<std::size_t>(n) * 6);
    for (int i = 0; i < n; i++) {
      const std::array<double, 2> p = fill(i);
      const std::size_t o = static_cast<std::size_t>(i) * 6;
      d.verts[o] = f32(clamp(p[0], -0.5, 1.5) * 2 - 1);
      d.verts[o + 1] = f32(clamp(p[1], -0.5, 1.5) * 2 - 1);
      d.verts[o + 2] = f32(r);
      d.verts[o + 3] = f32(g);
      d.verts[o + 4] = f32(b);
      d.verts[o + 5] = f32(a);
    }
    draws.push_back(std::move(d));
  };
  auto w = [&](int i) { return static_cast<double>(wave[static_cast<std::size_t>(i)]); };
  auto s = [&](int i) { return static_cast<double>(spec64[static_cast<std::size_t>(i)]); };
  switch (spec.mode) {
    case 1:
      mk(128, DrawKind::Strip,
         [&](int i) { return std::array<double, 2>{i / 127.0, wy + 0.3 * sc * w(i * 2)}; });
      break;
    case 2:
      mk(128, DrawKind::Lines, [&](int i) {
        const int k = i >> 1;
        const double th = k * 2 * pi / 64;
        const double rad = 0.1 + (i & 1) * 0.3 * sc * s(k);
        return std::array<double, 2>{wx + rad * std::cos(th) / aspect, wy + rad * std::sin(th)};
      });
      break;
    case 3:
      mk(64, DrawKind::Strip,
         [&](int i) { return std::array<double, 2>{i / 63.0, wy - 0.35 + 0.7 * sc * s(i)}; });
      break;
    case 4:
      mk(128, DrawKind::Strip,
         [&](int i) { return std::array<double, 2>{i / 127.0, wy - 0.15 + 0.2 * sc * w(i * 2)}; });
      mk(128, DrawKind::Strip, [&](int i) {
        return std::array<double, 2>{i / 127.0, wy + 0.15 + 0.2 * sc * w(255 - i * 2)};
      });
      break;
    default:
      mk(129, DrawKind::Strip, [&](int i) {
        const double th = (i % 128) * 2 * pi / 128;
        const double rad = 0.2 * sc * (1 + 0.8 * w((i % 128) * 2));
        return std::array<double, 2>{wx + rad * std::cos(th) / aspect, wy + rad * std::sin(th)};
      });
  }
}

void PresetRuntime::Impl::waves_and_shapes(const std::array<double, range_count>& f,
                                           const FrameInputs& inp,
                                           const std::array<float, 256>& wave,
                                           const std::array<float, 64>& spec, double weight,
                                           std::vector<Draw>& draws) {
  const double aspect = orv(inp.aspect, 1);
  if (f[WaveA] * weight > 0.003 && f[WaveScale] > 0) {
    builtin_wave(f, aspect, wave, spec, weight, draws);
  }

  for (Wave& w : waves) {
    Space& v = w.space;
    const CustomWave& s = w.spec;
    v.copy_inputs(inp, hear);
    for (std::size_t i = 0; i < 32; i++) {
      v.at(v.q[i]) = main.at(main.q[i]);
    }
    v.at(w.r) = s.r;
    v.at(w.g) = s.g;
    v.at(w.b) = s.b;
    v.at(w.a) = s.a;
    w.frame.run(v.v);
    const double st = strength(s.additive);
    const double r = clamp(finite(v.at(w.r), 0), 0, 1);
    const double g = clamp(finite(v.at(w.g), 0), 0, 1);
    const double b = clamp(finite(v.at(w.b), 0), 0, 1);
    const double a = clamp(finite(v.at(w.a), 0), 0, 1) * weight * st;
    if (a <= 0.003) {
      continue;
    }
    const int n = s.samples;
    Draw d;
    d.kind = s.dots ? DrawKind::Dots : DrawKind::Strip;
    d.count = n;
    d.additive = s.additive;
    d.thick = s.thick;
    d.verts.resize(static_cast<std::size_t>(n) * 6);
    for (int i = 0; i < n; i++) {
      const double t = static_cast<double>(i) / (n - 1);
      const double value1 =
          s.spectrum ? static_cast<double>(
                           spec[static_cast<std::size_t>(std::min(63.0, std::floor(t * 63)))])
                     : static_cast<double>(
                           wave[static_cast<std::size_t>(std::min(255.0, std::floor(t * 255)))]);
      v.at(w.sample) = t;
      v.at(w.value1) = value1;
      v.at(w.value2) = value1;
      v.at(w.x) = t;
      v.at(w.y) = 0.5 + value1 * 0.25;
      v.at(w.r) = r;
      v.at(w.g) = g;
      v.at(w.b) = b;
      v.at(w.a) = a;
      w.point.run(v.v);
      const std::size_t o = static_cast<std::size_t>(i) * 6;
      d.verts[o] = f32(clamp(finite(v.at(w.x), 0.5), -0.5, 1.5) * 2 - 1);
      d.verts[o + 1] =
          f32(clamp(milk ? 1 - finite(v.at(w.y), 0.5) : finite(v.at(w.y), 0.5), -0.5, 1.5) * 2 - 1);
      d.verts[o + 2] = f32(clamp(finite(v.at(w.r), r), 0, 1));
      d.verts[o + 3] = f32(clamp(finite(v.at(w.g), g), 0, 1));
      d.verts[o + 4] = f32(clamp(finite(v.at(w.b), b), 0, 1));
      d.verts[o + 5] = f32(clamp(finite(v.at(w.a), a / weight / st), 0, 1) * weight * st);
    }
    draws.push_back(std::move(d));
  }

  for (ShapeRt& sh : shapes) {
    Space& v = sh.space;
    const Shape& s = sh.spec;
    for (int inst = 0; inst < s.instances; inst++) {  // a shape can be drawn several times, each
                                                      // run of its formula told which one it is
      v.copy_inputs(inp, hear);
      for (std::size_t i = 0; i < 32; i++) {
        v.at(v.q[i]) = main.at(main.q[i]);
      }
      v.at(sh.sides) = s.sides;
      v.at(sh.x) = s.x;
      v.at(sh.y) = s.y;
      v.at(sh.rad) = s.rad;
      v.at(sh.ang) = s.ang;
      v.at(sh.r) = s.r;
      v.at(sh.g) = s.g;
      v.at(sh.b) = s.b;
      v.at(sh.a) = s.a;
      v.at(sh.r2) = s.r2;
      v.at(sh.g2) = s.g2;
      v.at(sh.b2) = s.b2;
      v.at(sh.a2) = s.a2;
      v.at(sh.instance) = inst;
      v.at(sh.num_inst) = s.instances;
      sh.frame.run(v.v);
      const double k = weight * strength(s.additive);
      const double a = clamp(finite(v.at(sh.a), 0), 0, 1) * k;
      const double a2 = clamp(finite(v.at(sh.a2), 0), 0, 1) * k;
      if (a <= 0.003 && a2 <= 0.003) {
        continue;
      }
      const int sides = round_js(clamp(finite(v.at(sh.sides), 4), 3, 100));
      const double x = clamp(finite(v.at(sh.x), 0.5), -0.5, 1.5);
      const double y =
          clamp(milk ? 1 - finite(v.at(sh.y), 0.5) : finite(v.at(sh.y), 0.5), -0.5, 1.5);
      const double rad = clamp(finite(v.at(sh.rad), 0.1), 0.001, 2);
      const double ang =
          clamp(milk ? -finite(v.at(sh.ang), 0) : finite(v.at(sh.ang), 0), -100, 100);
      Draw d;
      d.kind = DrawKind::Fan;
      d.count = sides + 2;
      d.additive = s.additive;
      d.thick = 1;
      d.verts.resize(static_cast<std::size_t>(sides + 2) * 6);
      const auto put = [&](int n, double px, double py, double r, double g, double b, double al) {
        const std::size_t o = static_cast<std::size_t>(n) * 6;
        d.verts[o] = f32(px * 2 - 1);
        d.verts[o + 1] = f32(py * 2 - 1);
        d.verts[o + 2] = f32(clamp(finite(r, 0), 0, 1));
        d.verts[o + 3] = f32(clamp(finite(g, 0), 0, 1));
        d.verts[o + 4] = f32(clamp(finite(b, 0), 0, 1));
        d.verts[o + 5] = f32(al);
      };
      put(0, x, y, v.at(sh.r), v.at(sh.g), v.at(sh.b), a);
      for (int n = 0; n <= sides; n++) {
        const double th = ang + (n % sides) * 2 * pi / sides;
        put(n + 1, x + rad * std::cos(th) / aspect, y + rad * std::sin(th), v.at(sh.r2),
            v.at(sh.g2), v.at(sh.b2), a2);
      }
      draws.push_back(std::move(d));
    }
  }
}

FrameState blend_states(const FrameState* a, const FrameState* b, double t) {
  if (!b) {
    return a ? *a : FrameState{};
  }
  if (!a || t >= 1) {
    return *b;
  }
  constexpr double cross = 0.5;  // how much of each warp is added on top of the mix at the halfway
                                 // point (0 = a plain crossfade)
  const double bump = std::sin(pi * t);
  FrameState out;
  out.mesh.resize(a->mesh.size());
  const std::size_t w = mesh_w + 1;
  for (std::size_t i = 0; i < out.mesh.size(); i++) {
    const std::size_t g = i >> 1;
    const double home =
        (i & 1) ? static_cast<double>(g / w) / mesh_h
                : static_cast<double>(g % w) / mesh_w;  // where this point sits with no warp at all
    const double da = a->mesh[i] - home;
    const double db = b->mesh[i] - home;
    out.mesh[i] = f32(home + lerp(da, db, t) + bump * cross * (da + db));
  }
  const ColorOut& ca = a->color;
  const ColorOut& cb = b->color;
  out.color = {lerp(ca.gamma, cb.gamma, t),
               lerp(ca.hue, cb.hue, t),
               lerp(ca.bright, cb.bright, t),
               lerp(ca.solarize, cb.solarize, t),
               lerp(ca.echo_alpha, cb.echo_alpha, t),
               lerp(ca.echo_zoom, cb.echo_zoom, t),
               t < 0.5 ? ca.echo_orient : cb.echo_orient,
               lerp(ca.cover, cb.cover, t)};
  out.color.echo_alpha = std::min(0.5, out.color.echo_alpha + 0.25 * bump);
  const double decay = lerp(a->decay, b->decay, t);
  out.decay = std::min(0.99, decay + (std::max(a->decay, b->decay) - decay) * bump);
  out.draws = a->draws;
  out.draws.insert(out.draws.end(), b->draws.begin(), b->draws.end());
  return out;
}

}  // namespace fh
