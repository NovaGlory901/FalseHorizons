// A visual preset and the check for a .fh-preset.json file. Port of sanitizePreset,
// parsePresetFile and exportPreset in js/falsehorizon-core.js (tests/parity/preset.golden). A file
// is untrusted: numbers are forced into range, texts cleaned and shortened, lists cut, every
// formula must compile; what gets through can be run without checking again.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "fh/json.hpp"

namespace fh {

struct Limits {
  static constexpr int file = 65536, name = 40, formula_total = 30000, waves = 2, shapes = 4,
                       instances = 16;
};

struct MainWave {
  int mode = 0;
  double r = 1, g = 1, b = 1, a = 0.8, scale = 1, x = 0.5, y = 0.5;
  bool additive = true;
  int thick = 1;
};

struct ColorSettings {
  double gamma = 1, hue = 0, bright = 1, solarize = 0, echo_alpha = 0, echo_zoom = 1;
  int echo_orient = 0;
  double cover = 0;
};

struct CustomWave {
  int samples = 128;
  double r = 1, g = 1, b = 1, a = 0.8;
  bool additive = true, dots = false;
  int thick = 1;
  bool spectrum = false;
  std::string init, frame, point;
};

struct Shape {
  int sides = 4;
  double x = 0.5, y = 0.5, rad = 0.15, ang = 0;
  double r = 1, g = 1, b = 1, a = 0.6, r2 = 1, g2 = 1, b2 = 1, a2 = 0;
  bool additive = true;
  int instances = 1;
  std::string init, frame;
};

struct Preset {
  std::string name, author, dialect = "fh";
  double hear = 1;
  double zoom = 1, rot = 0, cx = 0.5, cy = 0.5, dx = 0, dy = 0, sx = 1, sy = 1, warp = 0,
         decay = 0.97;
  MainWave wave;
  ColorSettings color;
  std::string init, frame, pixel;
  std::vector<CustomWave> waves;
  std::vector<Shape> shapes;
};

struct PresetResult {
  bool ok = false;
  std::string error;
  Preset preset;
};

// Checks a parsed JSON value (a file's content, or something from storage).
PresetResult sanitize_preset(const json::Value& raw);

PresetResult parse_preset_file(std::string_view text);

// The same cleaning every text in a preset gets: control characters become spaces, the ends are
// trimmed, at most `max` UTF-16 units are kept. Anything that is not a string gives "". Used for
// the other things people store (favourite names in the settings).
std::string clean_preset_text(const json::Value* v, std::size_t max);

json::Value to_json(const Preset& preset);  // the same shape the JavaScript produces (without the
                                            // "fh-preset" marker)
std::string export_preset(
    const Preset& preset);  // file text: {"fh-preset": 1, ...} indented by one space

}  // namespace fh
