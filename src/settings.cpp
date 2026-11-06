#include "fh/settings.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>

#include "fh/pathutf8.hpp"
#include "fh/preset.hpp"

namespace fh {

namespace {
using json::Value;

constexpr std::size_t max_file = 64 * 1024, max_name = 40;

int whole(const Value* v, int lo, int hi, int fallback) {
  double x = (v && v->is_number() && std::isfinite(v->as_number())) ? v->as_number() : fallback;
  x = std::min<double>(hi, std::max<double>(lo, x));
  const double f = std::floor(x);
  return static_cast<int>(x - f >= 0.5 ? f + 1 : f);  // Math.round
}
bool flag(const Value* v, bool fallback) { return v && v->is_bool() ? v->as_bool() : fallback; }
}  // namespace

Settings sanitize_settings(const Value& raw) {
  Settings s;
  if (!raw.is_object()) {
    return s;
  }
  s.intensity = whole(raw.find("intensity"), 0, 100, s.intensity);
  s.geometry = whole(raw.find("geometry"), 0, 100, s.geometry);
  s.backgrounds = whole(raw.find("backgroundAmount"), 0, 100, s.backgrounds);
  s.artifacts = whole(raw.find("artifactAmount"), 0, 100, s.artifacts);
  s.show_backgrounds = flag(raw.find("showBackgrounds"), s.show_backgrounds);
  s.show_artifacts = flag(raw.find("showArtifacts"), s.show_artifacts);
  s.auto_change = flag(raw.find("auto"), s.auto_change);
  s.cycle_sec = whole(raw.find("cycleSec"), 10, 300, s.cycle_sec);
  s.calm = flag(raw.find("calm"), s.calm);
  if (const Value* fav = raw.find("favorites"); fav && fav->is_array()) {
    for (const Value& n : fav->items()) {
      const std::string name = clean_preset_text(&n, max_name);
      if (!name.empty() && std::ranges::find(s.favorites, name) == s.favorites.end() &&
          s.favorites.size() < static_cast<std::size_t>(max_favorites)) {
        s.favorites.push_back(name);
      }
    }
  }
  s.last = clean_preset_text(raw.find("last"), max_name);
  return s;
}

Value to_json(const Settings& s) {
  Value o = Value::object();
  o.set("intensity", Value::number(s.intensity));
  o.set("geometry", Value::number(s.geometry));
  o.set("backgroundAmount", Value::number(s.backgrounds));
  o.set("artifactAmount", Value::number(s.artifacts));
  o.set("showBackgrounds", Value::boolean(s.show_backgrounds));
  o.set("showArtifacts", Value::boolean(s.show_artifacts));
  o.set("auto", Value::boolean(s.auto_change));
  o.set("cycleSec", Value::number(s.cycle_sec));
  o.set("calm", Value::boolean(s.calm));
  Value fav = Value::array();
  for (const std::string& n : s.favorites) {
    fav.push(Value::string(n));
  }
  o.set("favorites", std::move(fav));
  o.set("last", Value::string(s.last));
  return o;
}

SettingsLoad load_settings(const std::filesystem::path& file) {
  SettingsLoad r;
  std::error_code ec;
  if (!std::filesystem::exists(file, ec)) {
    return r;
  }
  const auto size = std::filesystem::file_size(file, ec);
  if (ec || size > max_file) {
    r.problem = path_to_utf8(file.filename()) +
                " could not be read (it is missing, or too big); using the defaults.";
    return r;
  }
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    r.problem = path_to_utf8(file.filename()) + " could not be opened; using the defaults.";
    return r;
  }
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const json::ParseResult parsed = json::parse(text);
  if (!parsed.ok) {
    r.problem =
        path_to_utf8(file.filename()) + " is not valid (" + parsed.error + "); using the defaults.";
    return r;
  }
  r.settings = sanitize_settings(parsed.value);
  return r;
}

bool save_settings(const std::filesystem::path& file, const Settings& s, std::string& error) {
  std::filesystem::path tmp = file;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      error = "could not write " + path_to_utf8(tmp);
      return false;
    }
    out << json::stringify(to_json(s), 2) << "\n";
    out.flush();
    if (!out) {
      error = "could not write " + path_to_utf8(tmp);
      std::error_code ec;
      std::filesystem::remove(tmp, ec);
      return false;
    }
  }
  std::error_code ec;
  std::filesystem::rename(tmp, file, ec);
  if (ec) {
    error = "could not replace " + path_to_utf8(file) + ": " + ec.message();
    std::filesystem::remove(tmp, ec);
    return false;
  }
  return true;
}

bool is_favorite(const Settings& s, const std::string& name) {
  return std::ranges::find(s.favorites, name) != s.favorites.end();
}

void toggle_favorite(Settings& s, const std::string& name) {
  const auto it = std::ranges::find(s.favorites, name);
  if (it != s.favorites.end()) {
    s.favorites.erase(it);
  } else if (s.favorites.size() < static_cast<std::size_t>(max_favorites)) {
    s.favorites.push_back(name);
  }
}

}  // namespace fh
