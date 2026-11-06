#include "fh/milk.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>

namespace fh {

namespace {

constexpr std::size_t max_lines = 6000, max_line = 8000;

bool is_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}
bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_word(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }
char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

std::string trim(std::string_view s) {
  std::size_t a = 0;
  std::size_t b = s.size();
  while (a < b && is_space(s[a])) {
    a++;
  }
  while (b > a && is_space(s[b - 1])) {
    b--;
  }
  return std::string(s.substr(a, b - a));
}

// Control characters become spaces, runs of white space become one space, the ends are trimmed.
std::string clean(std::string_view s) {
  std::string out;
  bool space = true;
  for (const char ch : s) {
    const auto c = static_cast<unsigned char>(ch);
    const bool blank = c < 0x20 || c == 0x7f || ch == ' ';
    if (blank) {
      if (!space) {
        out += ' ';
      }
      space = true;
    } else {
      out += ch;
      space = false;
    }
  }
  while (!out.empty() && out.back() == ' ') {
    out.pop_back();
  }
  return out;
}

// JavaScript's parseFloat: the number at the start of the text (after white space), or nothing.
std::optional<double> parse_float_prefix(std::string_view s) {
  std::size_t i = 0;
  while (i < s.size() && is_space(s[i])) {
    i++;
  }
  const std::size_t start = i;
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
    i++;
  }
  if (s.substr(i, 8) == "Infinity") {
    return s[start] == '-' ? -HUGE_VAL : HUGE_VAL;
  }
  std::size_t digits = 0;
  while (i < s.size() && is_digit(s[i])) {
    i++;
    digits++;
  }
  if (i < s.size() && s[i] == '.') {
    std::size_t j = i + 1;
    std::size_t frac = 0;
    while (j < s.size() && is_digit(s[j])) {
      j++;
      frac++;
    }
    if (digits + frac > 0) {
      i = j;
      digits += frac;
    }
  }
  if (digits == 0) {
    return std::nullopt;
  }
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
    std::size_t j = i + 1;
    if (j < s.size() && (s[j] == '+' || s[j] == '-')) {
      j++;
    }
    if (j < s.size() && is_digit(s[j])) {
      while (j < s.size() && is_digit(s[j])) {
        j++;
      }
      i = j;
    }
  }
  const std::string number(s.substr(start, i - start));
  return std::strtod(number.c_str(), nullptr);
}

// JavaScript's Math.round: halves go up.
double js_round(double x) {
  const double f = std::floor(x);
  return x - f >= 0.5 ? f + 1 : f;
}

// The "key=value" lines, keys lowercased, in the order each key first appeared (a repeated key
// keeps its place and takes the later value).
struct Lines {
  std::vector<std::pair<std::string, std::string>> items;
  std::map<std::string, std::size_t> at;
  [[nodiscard]] const std::string* get(const std::string& key) const {
    const auto it = at.find(key);
    return it == at.end() ? nullptr : &items[it->second].second;
  }
};

Lines read_lines(std::string_view text) {
  Lines map;
  std::size_t pos = 0;
  std::size_t line_no = 0;
  while (pos <= text.size() && line_no < max_lines) {
    std::size_t end = text.find('\n', pos);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    std::string_view line = text.substr(pos, end - pos);
    if (end < text.size() && !line.empty() && line.back() == '\r') {
      line.remove_suffix(1);  // "\r\n" ends a line; a lone "\r" at the very end stays
    }
    pos = end + 1;
    line_no++;
    const std::size_t eq = line.find('=');
    if (eq == std::string_view::npos || eq < 1) {
      continue;
    }
    std::string key = trim(line.substr(0, eq));
    for (char& c : key) {
      c = lower(c);
    }
    if (key.empty() || key.size() > 64 || !std::ranges::all_of(key, [](char c) {
          return std::islower(static_cast<unsigned char>(c)) || is_digit(c) || c == '_';
        })) {
      continue;
    }
    std::string value(line.substr(eq + 1, max_line));
    const auto it = map.at.find(key);
    if (it == map.at.end()) {
      map.at[key] = map.items.size();
      map.items.emplace_back(key, std::move(value));
    } else {
      map.items[it->second].second = std::move(value);
    }
  }
  return map;
}

// Removes /* ... */ and then // to the end of the line (a lone carriage return stops a match, as
// `.` does in the JavaScript's regular expressions).
std::string strip_comments(const std::string& in) {
  std::string s;
  std::size_t pos = 0;
  while (pos < in.size()) {
    const std::size_t open = in.find("/*", pos);
    if (open == std::string::npos) {
      break;
    }
    const std::size_t close = in.find("*/", open + 2);
    if (close != std::string::npos && in.find('\r', open + 2) >= close) {
      s.append(in, pos, open - pos);
      s += ' ';
      pos = close + 2;
    } else {
      s.append(in, pos, open + 1 - pos);
      pos = open + 1;
    }
  }
  s.append(in, pos, std::string::npos);
  const std::size_t last_cr = s.rfind('\r');
  const std::size_t from = last_cr == std::string::npos ? 0 : last_cr + 1;
  const std::size_t slashes = s.find("//", from);
  if (slashes != std::string::npos) {
    s = s.substr(0, slashes) + " ";
  }
  return s;
}

std::string code(const Lines& map, const std::string& base) {
  std::vector<std::pair<double, const std::string*>> parts;
  for (const auto& kv : map.items) {
    const std::string& key = kv.first;
    if (key.size() <= base.size() || !key.starts_with(base)) {
      continue;
    }
    const std::string digits = key.substr(base.size());
    if (!std::ranges::all_of(digits, is_digit)) {
      continue;
    }
    parts.emplace_back(std::strtod(digits.c_str(), nullptr), &kv.second);
  }
  std::ranges::stable_sort(parts, [](const auto& a, const auto& b) { return a.first < b.first; });
  if (parts.size() > 999) {
    parts.resize(999);
  }
  std::string joined;
  for (std::size_t i = 0; i < parts.size(); i++) {
    if (i) {
      joined += ' ';
    }
    joined += strip_comments(*parts[i].second);
  }
  return trim(joined);
}

bool known(const std::string& k) {
  static const char* names[] = {
      "milkdrop_preset_version", "fdecay", "zoom", "rot", "nwavemode", "fwavealpha", "fgammaadj",
      "fvideoechoalpha"};
  for (const char* n : names) {
    if (k == n) {
      return true;
    }
  }
  const auto numbered = [&](const std::string& prefix) {
    return k.size() > prefix.size() && k.starts_with(prefix) &&
           std::all_of(k.begin() + static_cast<std::ptrdiff_t>(prefix.size()), k.end(), is_digit);
  };
  return numbered("per_frame_") || numbered("per_pixel_") || numbered("per_frame_init_");
}

// The first of megabuf( gmegabuf( loop( while( exec2( exec3( memcpy( assign( found as a word
// (spaces allowed before the bracket), or "".
std::string unsupported_call(const std::string& text) {
  static const char* words[] = {"megabuf", "gmegabuf", "loop",   "while",
                                "exec2",   "exec3",    "memcpy", "assign"};
  std::string low = text;
  for (char& c : low) {
    c = lower(c);
  }
  for (std::size_t p = 0; p < low.size(); p++) {
    if (p > 0 && is_word(low[p - 1])) {
      continue;
    }
    for (const char* w : words) {
      const std::string_view word(w);
      if (low.compare(p, word.size(), word) != 0) {
        continue;
      }
      std::size_t q = p + word.size();
      while (q < low.size() && is_space(low[q])) {
        q++;
      }
      if (q < low.size() && low[q] == '(') {
        return std::string(word);
      }
    }
  }
  return "";
}

bool has_bitwise(const std::string& s) {
  const auto is_bit = [](char c) { return c == '&' || c == '|'; };
  for (std::size_t i = 0; i < s.size(); i++) {
    if (is_bit(s[i]) && !(i > 0 && is_bit(s[i - 1])) && !(i + 1 < s.size() && is_bit(s[i + 1]))) {
      return true;
    }
  }
  return false;
}

bool ends_with(const std::string& s, const std::string& tail) {
  return s.size() >= tail.size() && s.ends_with(tail);
}

bool is_shader_key(const std::string& k) {
  for (const char* prefix : {"warp_", "comp_"}) {
    const std::string p(prefix);
    if (k.size() > p.size() && k.starts_with(p) &&
        std::all_of(k.begin() + static_cast<std::ptrdiff_t>(p.size()), k.end(), is_digit)) {
      return true;
    }
  }
  return false;
}

using json::Value;

}  // namespace

MilkResult convert_milk(std::string_view text_in, std::string_view file_name) {
  MilkResult res;
  if (trim(text_in).empty()) {
    res.error = "That file is empty.";
    return res;
  }
  if (text_in.size() > milk_max_text) {
    res.error = "That file is too big to be a MilkDrop preset (the limit is 100 KB).";
    return res;
  }
  if (text_in.find('\0') != std::string_view::npos) {
    res.error = "That is not a text file, so it is not a MilkDrop preset.";
    return res;
  }
  const Lines map = read_lines(text_in);
  if (!std::ranges::any_of(map.items, [](const auto& kv) { return known(kv.first); })) {
    res.error = "That does not look like a MilkDrop preset (.milk file).";
    return res;
  }
  auto num = [&](const std::string& key, double fallback) {
    const std::string* v = map.get(key);
    if (!v) {
      return fallback;
    }
    const std::optional<double> x = parse_float_prefix(*v);
    return (x && std::isfinite(*x)) ? *x : fallback;
  };
  const auto on = [&](const std::string& key) { return num(key, 0) >= 1; };
  res.notes.emplace_back(
      "Translated from a MilkDrop preset: it will not look exactly the same as it does there.");

  const std::string piece_init = code(map, "per_frame_init_");
  const std::string piece_frame = code(map, "per_frame_");
  const std::string piece_pixel = code(map, "per_pixel_");
  Value waves = Value::array();
  Value shapes = Value::array();
  int wave_count = 0;
  int shape_count = 0;
  int extra_waves = 0;
  int extra_shapes = 0;
  bool textured = false;
  std::string wave_code;
  std::string shape_code;  // the order the JavaScript checks them in: main formulas, then
                           // every wave's, then every shape's
  for (int i = 0; i < 4; i++) {
    const std::string n = std::to_string(i);
    const std::string wc = "wavecode_" + n + "_";
    const std::string sc = "shapecode_" + n + "_";
    if (on(wc + "enabled")) {
      if (wave_count >= 2) {
        extra_waves++;
      } else {
        Value w = Value::object();
        w.set("samples", Value::number(js_round(num(wc + "samples", 128))));
        w.set("r", Value::number(num(wc + "r", 1)));
        w.set("g", Value::number(num(wc + "g", 1)));
        w.set("b", Value::number(num(wc + "b", 1)));
        w.set("a", Value::number(num(wc + "a", 1)));
        w.set("additive", Value::boolean(on(wc + "badditive")));
        w.set("dots", Value::boolean(on(wc + "busedots")));
        w.set("thick", Value::number(on(wc + "bdrawthick") ? 2 : 1));
        w.set("spectrum", Value::boolean(on(wc + "bspectrum")));
        const std::string init = code(map, "wave_" + n + "_init");
        const std::string frame = code(map, "wave_" + n + "_per_frame");
        const std::string point = code(map, "wave_" + n + "_per_point");
        w.set("init", Value::string(init));
        w.set("frame", Value::string(frame));
        w.set("point", Value::string(point));
        wave_code += " " + init + " " + frame + " " + point;
        waves.push(std::move(w));
        wave_count++;
      }
    }
    if (on(sc + "enabled")) {
      textured = textured || on(sc + "textured");
      if (shape_count >= 4) {
        extra_shapes++;
      } else {
        Value s = Value::object();
        s.set("sides", Value::number(js_round(num(sc + "sides", 4))));
        s.set("x", Value::number(num(sc + "x", 0.5)));
        s.set("y", Value::number(num(sc + "y", 0.5)));
        s.set("rad", Value::number(num(sc + "rad", 0.1)));
        s.set("ang", Value::number(num(sc + "ang", 0)));
        s.set("r", Value::number(num(sc + "r", 1)));
        s.set("g", Value::number(num(sc + "g", 0)));
        s.set("b", Value::number(num(sc + "b", 0)));
        s.set("a", Value::number(num(sc + "a", 1)));
        s.set("r2", Value::number(num(sc + "r2", 0)));
        s.set("g2", Value::number(num(sc + "g2", 1)));
        s.set("b2", Value::number(num(sc + "b2", 0)));
        s.set("a2", Value::number(num(sc + "a2", 0)));
        s.set("additive", Value::boolean(on(sc + "additive")));
        s.set("instances", Value::number(js_round(num(sc + "num_inst", 1))));
        const std::string init = code(map, "shape_" + n + "_init");
        const std::string frame = code(map, "shape_" + n + "_per_frame");
        s.set("init", Value::string(init));
        s.set("frame", Value::string(frame));
        shape_code += " " + init + " " + frame;
        shapes.push(std::move(s));
        shape_count++;
      }
    }
  }

  // what FalseHorizon cannot run: refuse, with the reason, rather than guess
  const std::string all =
      piece_init + " " + piece_frame + " " + piece_pixel + wave_code + shape_code;
  const std::string bad = unsupported_call(all);
  if (!bad.empty()) {
    res.error = "This preset uses " + bad +
                "(), a loop or memory-array feature that FalseHorizon does not have (and must not, "
                "so a preset can never run forever).";
    return res;
  }
  if (has_bitwise(all)) {
    res.error =
        "This preset uses bitwise operators (a single & or |), which FalseHorizon does not "
        "support.";
    return res;
  }

  if (std::ranges::any_of(map.items, [](const auto& kv) { return is_shader_key(kv.first); })) {
    res.notes.emplace_back(
        "Its custom shader effects were ignored, so it will look simpler than in MilkDrop.");
  }
  if (extra_waves) {
    res.notes.push_back("Only the first 2 of its custom waveforms were kept (" +
                        std::to_string(extra_waves) + " left out).");
  }
  if (extra_shapes) {
    res.notes.push_back("Only the first 4 of its shapes were kept (" +
                        std::to_string(extra_shapes) + " left out).");
  }
  if (textured) {
    res.notes.emplace_back("Its textured shapes are drawn as plain colours.");
  }
  if (std::ranges::any_of(map.items, [&](const auto& kv) {
        const std::string& k = kv.first;
        const bool border = k.starts_with("ob_") || k.starts_with("ib_") || k.starts_with("mv_");
        return border && num(k, 0) > 0 &&
               (ends_with(k, "_a") || ends_with(k, "_x") || ends_with(k, "_y") ||
                ends_with(k, "_size"));
      })) {
    res.notes.emplace_back("Its borders and motion vectors were left out.");
  }

  // MilkDrop's eight built-in waveform shapes -> our five (circle, line, spectrum spokes, spectrum
  // line, two lines): the closest look
  static const int wave_mode[] = {0, 0, 0, 0, 1, 0, 1, 4};
  std::string name(file_name);
  if (const std::size_t dot = name.rfind('.'); dot != std::string::npos) {
    name.erase(dot);
  }
  name = clean(name);
  if (name.empty()) {
    name = "Imported preset";
  }

  Value raw = Value::object();
  raw.set("name", Value::string(name));
  raw.set("author", Value::string("imported .milk"));
  raw.set("dialect", Value::string("milk"));
  raw.set("hear", Value::number(milk_hear));
  raw.set("decay", Value::number(num("fdecay", 0.98)));
  raw.set("zoom", Value::number(num("zoom", 1)));
  raw.set("rot", Value::number(num("rot", 0)));
  raw.set("cx", Value::number(num("cx", 0.5)));
  raw.set("cy", Value::number(num("cy", 0.5)));
  raw.set("dx", Value::number(num("dx", 0)));
  raw.set("dy", Value::number(num("dy", 0)));
  raw.set("sx", Value::number(num("sx", 1)));
  raw.set("sy", Value::number(num("sy", 1)));
  raw.set("warp", Value::number(num("warp", 1)));
  Value wave = Value::object();
  wave.set("mode", Value::number(wave_mode[static_cast<int>(
                       std::max(0.0, std::min(7.0, js_round(num("nwavemode", 0)))))]));
  wave.set("r", Value::number(num("wave_r", 1)));
  wave.set("g", Value::number(num("wave_g", 1)));
  wave.set("b", Value::number(num("wave_b", 1)));
  wave.set("a", Value::number(num("fwavealpha", 0.8)));
  wave.set("scale", Value::number(num("fwavescale", 1)));
  wave.set("x", Value::number(num("wave_x", 0.5)));
  wave.set("y", Value::number(num("wave_y", 0.5)));
  wave.set("additive", Value::boolean(on("badditivewaves")));
  wave.set("thick", Value::number(on("bwavethick") ? 2 : 1));
  raw.set("wave", std::move(wave));
  Value color = Value::object();
  color.set("gamma", Value::number(num("fgammaadj", 1)));
  color.set("echoAlpha", Value::number(num("fvideoechoalpha", 0)));
  color.set("echoZoom", Value::number(num("fvideoechozoom", 1)));
  color.set("echoOrient", Value::number(js_round(num("nvideoechoorientation", 0))));
  color.set("solarize", Value::number(on("bsolarize") ? 0.5 : 0));
  raw.set("color", std::move(color));
  raw.set("init", Value::string(piece_init));
  raw.set("frame", Value::string(piece_frame));
  raw.set("pixel", Value::string(piece_pixel));
  raw.set("waves", std::move(waves));
  raw.set("shapes", std::move(shapes));

  PresetResult r = sanitize_preset(raw);
  if (!r.ok) {
    res.error = "This preset could not be translated. " + r.error;
    return res;
  }
  res.ok = true;
  res.preset = std::move(r.preset);
  return res;
}

}  // namespace fh
