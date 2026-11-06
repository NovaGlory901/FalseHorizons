// Imports community MilkDrop presets (.milk). Port of js/falsehorizon-milk.js (the reference;
// tests/parity/milk.golden). Approximate on purpose: custom shaders are ignored and presets whose
// formulas use loops or memory arrays are refused. The file is untrusted text: size-limited, known
// keys only, and every formula goes through sanitize_preset.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "fh/preset.hpp"

namespace fh {

constexpr std::size_t milk_max_text = 100 * 1024;
// Imported presets were written for a program whose bass reads higher, so they hear louder.
constexpr double milk_hear = 1.8;

struct MilkResult {
  bool ok = false;
  std::string error;
  Preset preset;
  std::vector<std::string>
      notes;  // what was left out or approximated; the first always says it is a translation
};

// `file_name` names the preset (without its ending). Never throws on bad input.
MilkResult convert_milk(std::string_view text, std::string_view file_name);

}  // namespace fh
