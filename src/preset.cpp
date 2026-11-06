#include "fh/preset.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "fh/expr.hpp"

namespace fh {

namespace {

using json::Value;

std::u32string decode(std::string_view s) {
  std::u32string out;
  for (std::size_t i = 0; i < s.size();) {
    const auto c = static_cast<unsigned char>(s[i]);
    const int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (n == 0 || i + static_cast<std::size_t>(n) > s.size()) {
      out += U'�';
      i++;
      continue;
    }
    char32_t cp = n == 1 ? c : c & (0xFF >> (n + 1));
    bool good = true;
    for (int k = 1; k < n; k++) {
      const auto cc = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
      if ((cc >> 6) != 2) {
        good = false;
        break;
      }
      cp = (cp << 6) | (cc & 0x3F);
    }
    if (!good) {
      out += U'�';
      i++;
      continue;
    }
    out += cp;
    i += static_cast<std::size_t>(n);
  }
  return out;
}

void encode(std::string& out, char32_t c) {
  if (c < 0x80) {
    out += static_cast<char>(c);
  } else if (c < 0x800) {
    out += static_cast<char>(0xC0 | (c >> 6));
    out += static_cast<char>(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    out += static_cast<char>(0xE0 | (c >> 12));
    out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (c & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (c >> 18));
    out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (c & 0x3F));
  }
}

std::size_t units(char32_t c) { return c > 0xFFFF ? 2 : 1; }

std::size_t length16(std::string_view s) {
  std::size_t n = 0;
  for (const char32_t c : decode(s)) {
    n += units(c);
  }
  return n;
}

bool js_space(char32_t c) {
  return (c >= 9 && c <= 13) || c == 32 || c == 0xA0 || c == 0x1680 ||
         (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F ||
         c == 0x3000 || c == 0xFEFF;
}

// text(): control characters become spaces, the ends are trimmed, and at most `max` UTF-16 units
// are kept (a character that would be cut is dropped).
std::string clean_text(const Value* v, std::size_t max) {
  if (!v || !v->is_string()) {
    return "";
  }
  std::u32string s = decode(v->as_string());
  for (char32_t& c : s) {
    if (c < 0x20 || c == 0x7F) {
      c = U' ';
    }
  }
  std::size_t a = 0;
  std::size_t b = s.size();
  while (a < b && js_space(s[a])) {
    a++;
  }
  while (b > a && js_space(s[b - 1])) {
    b--;
  }
  std::string out;
  std::size_t used = 0;
  for (std::size_t i = a; i < b; i++) {
    used += units(s[i]);
    if (used > max) {
      break;
    }
    encode(out, s[i]);
  }
  return out;
}

double clamp(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }
double num(const Value* v, double lo, double hi, double fallback) {
  const double x =
      (v && v->is_number() && std::isfinite(v->as_number())) ? v->as_number() : fallback;
  return clamp(x, lo, hi);
}
int whole(const Value* v, double lo, double hi, double fallback) {
  return static_cast<int>(std::floor(num(v, lo, hi, fallback) + 0.5));
}  // Math.round
bool flag(const Value* v, bool fallback) { return v && v->is_bool() ? v->as_bool() : fallback; }

// Anything that is an object or array in JavaScript terms (`typeof x === 'object'`, not null): its
// fields are looked up, an array simply has none.
const Value* object_or(const Value* v, const Value& none) {
  return v && (v->is_object() || v->is_array()) ? v : &none;
}
const Value* field(const Value* v, std::string_view key) { return v ? v->find(key) : nullptr; }

}  // namespace

std::string clean_preset_text(const Value* v, std::size_t max) { return clean_text(v, max); }

PresetResult sanitize_preset(const Value& raw) {
  PresetResult res;
  if (!raw.is_object()) {
    res.error = "That is not a visual preset.";
    return res;
  }
  const Value none = Value::object();
  const auto get = [&](std::string_view k) { return raw.find(k); };

  Preset p;
  p.name = clean_text(get("name"), Limits::name);
  if (p.name.empty()) {
    res.error = "The preset needs a name.";
    return res;
  }

  std::size_t formula_chars = 0;
  std::string problem;
  const auto formula = [&](const Value* v, const std::string& where) {
    std::string src = v && v->is_string() ? v->as_string() : "";
    formula_chars += length16(src);
    const std::string err = expr::check(src);
    if (!err.empty() && problem.empty()) {
      problem = "The " + where + " formula has a problem: " + err + ".";
    }
    return src;
  };

  p.author = clean_text(get("author"), Limits::name);
  const Value* dialect = get("dialect");
  p.dialect = dialect && dialect->is_string() && dialect->as_string() == "milk" ? "milk" : "fh";
  p.hear = num(get("hear"), 1, 3, 1);
  p.zoom = num(get("zoom"), 0.8, 1.3, 1);
  p.rot = num(get("rot"), -0.5, 0.5, 0);
  p.cx = num(get("cx"), -0.5, 1.5, 0.5);
  p.cy = num(get("cy"), -0.5, 1.5, 0.5);
  p.dx = num(get("dx"), -0.1, 0.1, 0);
  p.dy = num(get("dy"), -0.1, 0.1, 0);
  p.sx = num(get("sx"), 0.5, 2, 1);
  p.sy = num(get("sy"), 0.5, 2, 1);
  p.warp = num(get("warp"), 0, 4, 0);
  p.decay = num(get("decay"), 0.5, 0.99, 0.97);

  const Value* w = object_or(get("wave"), none);
  p.wave.mode = whole(field(w, "mode"), 0, 4, 0);
  p.wave.r = num(field(w, "r"), 0, 1, 1);
  p.wave.g = num(field(w, "g"), 0, 1, 1);
  p.wave.b = num(field(w, "b"), 0, 1, 1);
  p.wave.a = num(field(w, "a"), 0, 1, 0.8);
  p.wave.scale = num(field(w, "scale"), 0, 4, 1);
  p.wave.x = num(field(w, "x"), 0, 1, 0.5);
  p.wave.y = num(field(w, "y"), 0, 1, 0.5);
  p.wave.additive = flag(field(w, "additive"), true);
  p.wave.thick = whole(field(w, "thick"), 1, 3, 1);

  const Value* c = object_or(get("color"), none);
  p.color.gamma = num(field(c, "gamma"), 0.5, 2, 1);
  p.color.hue = num(field(c, "hue"), -1000, 1000, 0);
  p.color.bright = num(field(c, "bright"), 0, 2, 1);
  p.color.solarize = num(field(c, "solarize"), 0, 1, 0);
  p.color.echo_alpha = num(field(c, "echoAlpha"), 0, 0.5, 0);
  p.color.echo_zoom = num(field(c, "echoZoom"), 0.5, 3, 1);
  p.color.echo_orient = whole(field(c, "echoOrient"), 0, 3, 0);
  p.color.cover = num(field(c, "cover"), 0, 1, 0);

  p.init = formula(get("init"), "init");
  p.frame = formula(get("frame"), "frame");
  p.pixel = formula(get("pixel"), "pixel");

  const Value* waves = get("waves");
  if (waves && waves->is_array()) {
    for (std::size_t i = 0;
         i < waves->items().size() && i < static_cast<std::size_t>(Limits::waves); i++) {
      const Value* s = object_or(&waves->items()[i], none);
      const std::string n = std::to_string(i + 1);
      CustomWave cw;
      cw.samples = whole(field(s, "samples"), 16, 256, 128);
      cw.r = num(field(s, "r"), 0, 1, 1);
      cw.g = num(field(s, "g"), 0, 1, 1);
      cw.b = num(field(s, "b"), 0, 1, 1);
      cw.a = num(field(s, "a"), 0, 1, 0.8);
      cw.additive = flag(field(s, "additive"), true);
      cw.dots = flag(field(s, "dots"), false);
      cw.thick = whole(field(s, "thick"), 1, 3, 1);
      cw.spectrum = flag(field(s, "spectrum"), false);
      cw.init = formula(field(s, "init"), "wave " + n + " init");
      cw.frame = formula(field(s, "frame"), "wave " + n + " frame");
      cw.point = formula(field(s, "point"), "wave " + n + " point");
      p.waves.push_back(std::move(cw));
    }
  }

  const Value* shapes = get("shapes");
  if (shapes && shapes->is_array()) {
    for (std::size_t i = 0;
         i < shapes->items().size() && i < static_cast<std::size_t>(Limits::shapes); i++) {
      const Value* s = object_or(&shapes->items()[i], none);
      const std::string n = std::to_string(i + 1);
      Shape sh;
      sh.sides = whole(field(s, "sides"), 3, 100, 4);
      sh.x = num(field(s, "x"), -0.5, 1.5, 0.5);
      sh.y = num(field(s, "y"), -0.5, 1.5, 0.5);
      sh.rad = num(field(s, "rad"), 0.001, 2, 0.15);
      sh.ang = num(field(s, "ang"), -100, 100, 0);
      sh.r = num(field(s, "r"), 0, 1, 1);
      sh.g = num(field(s, "g"), 0, 1, 1);
      sh.b = num(field(s, "b"), 0, 1, 1);
      sh.a = num(field(s, "a"), 0, 1, 0.6);
      sh.r2 = num(field(s, "r2"), 0, 1, 1);
      sh.g2 = num(field(s, "g2"), 0, 1, 1);
      sh.b2 = num(field(s, "b2"), 0, 1, 1);
      sh.a2 = num(field(s, "a2"), 0, 1, 0);
      sh.additive = flag(field(s, "additive"), true);
      sh.instances = whole(field(s, "instances"), 1, Limits::instances, 1);
      sh.init = formula(field(s, "init"), "shape " + n + " init");
      sh.frame = formula(field(s, "frame"), "shape " + n);
      p.shapes.push_back(std::move(sh));
    }
  }

  if (!problem.empty()) {
    res.error = problem;
    return res;
  }
  if (formula_chars > static_cast<std::size_t>(Limits::formula_total)) {
    res.error = "The formulas add up to more than " + std::to_string(Limits::formula_total) +
                " characters.";
    return res;
  }
  res.ok = true;
  res.preset = std::move(p);
  return res;
}

PresetResult parse_preset_file(std::string_view text) {
  PresetResult res;
  if (length16(text) > static_cast<std::size_t>(Limits::file)) {
    res.error = "That file is too big to be a visual preset.";
    return res;
  }
  const json::ParseResult parsed = json::parse(text);
  if (!parsed.ok) {
    res.error = "That file is not a visual preset (it is not valid JSON).";
    return res;
  }
  const Value* marker = parsed.value.find("fh-preset");
  if (!parsed.value.is_object() || !marker || !marker->is_number() || marker->as_number() != 1) {
    res.error = "That file is not a FalseHorizons preset.";
    return res;
  }
  return sanitize_preset(parsed.value);
}

Value to_json(const Preset& p) {
  using V = Value;
  const auto n = [](double x) { return V::number(x); };
  const auto s = [](const std::string& x) { return V::string(x); };
  const auto b = [](bool x) { return V::boolean(x); };

  V o = V::object();
  o.set("name", s(p.name));
  o.set("author", s(p.author));
  o.set("dialect", s(p.dialect));
  o.set("hear", n(p.hear));
  o.set("zoom", n(p.zoom));
  o.set("rot", n(p.rot));
  o.set("cx", n(p.cx));
  o.set("cy", n(p.cy));
  o.set("dx", n(p.dx));
  o.set("dy", n(p.dy));
  o.set("sx", n(p.sx));
  o.set("sy", n(p.sy));
  o.set("warp", n(p.warp));
  o.set("decay", n(p.decay));

  V w = V::object();
  w.set("mode", n(p.wave.mode));
  w.set("r", n(p.wave.r));
  w.set("g", n(p.wave.g));
  w.set("b", n(p.wave.b));
  w.set("a", n(p.wave.a));
  w.set("scale", n(p.wave.scale));
  w.set("x", n(p.wave.x));
  w.set("y", n(p.wave.y));
  w.set("additive", b(p.wave.additive));
  w.set("thick", n(p.wave.thick));
  o.set("wave", std::move(w));

  V c = V::object();
  c.set("gamma", n(p.color.gamma));
  c.set("hue", n(p.color.hue));
  c.set("bright", n(p.color.bright));
  c.set("solarize", n(p.color.solarize));
  c.set("echoAlpha", n(p.color.echo_alpha));
  c.set("echoZoom", n(p.color.echo_zoom));
  c.set("echoOrient", n(p.color.echo_orient));
  c.set("cover", n(p.color.cover));
  o.set("color", std::move(c));

  o.set("init", s(p.init));
  o.set("frame", s(p.frame));
  o.set("pixel", s(p.pixel));

  V waves = V::array();
  for (const CustomWave& cw : p.waves) {
    V e = V::object();
    e.set("samples", n(cw.samples));
    e.set("r", n(cw.r));
    e.set("g", n(cw.g));
    e.set("b", n(cw.b));
    e.set("a", n(cw.a));
    e.set("additive", b(cw.additive));
    e.set("dots", b(cw.dots));
    e.set("thick", n(cw.thick));
    e.set("spectrum", b(cw.spectrum));
    e.set("init", s(cw.init));
    e.set("frame", s(cw.frame));
    e.set("point", s(cw.point));
    waves.push(std::move(e));
  }
  o.set("waves", std::move(waves));

  V shapes = V::array();
  for (const Shape& sh : p.shapes) {
    V e = V::object();
    e.set("sides", n(sh.sides));
    e.set("x", n(sh.x));
    e.set("y", n(sh.y));
    e.set("rad", n(sh.rad));
    e.set("ang", n(sh.ang));
    e.set("r", n(sh.r));
    e.set("g", n(sh.g));
    e.set("b", n(sh.b));
    e.set("a", n(sh.a));
    e.set("r2", n(sh.r2));
    e.set("g2", n(sh.g2));
    e.set("b2", n(sh.b2));
    e.set("a2", n(sh.a2));
    e.set("additive", b(sh.additive));
    e.set("instances", n(sh.instances));
    e.set("init", s(sh.init));
    e.set("frame", s(sh.frame));
    shapes.push(std::move(e));
  }
  o.set("shapes", std::move(shapes));
  return o;
}

std::string export_preset(const Preset& preset) {
  Value out = Value::object();
  out.set("fh-preset", Value::number(1));
  const Value body = to_json(preset);
  for (const auto& m : body.members()) {
    out.set(m.first, m.second);
  }
  return json::stringify(out, 1);
}

}  // namespace fh
