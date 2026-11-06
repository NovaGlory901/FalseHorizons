// A folder of presets: *.fh-preset.json and MilkDrop *.milk files (translated, see milk.hpp).
#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "fh/milk.hpp"
#include "fh/preset.hpp"

namespace fh {

struct Library {
  std::vector<Preset> presets;  // the ones that passed, sorted by name
  std::vector<std::string> problems;  // one line per file that did not: "file.json: the reason"
  std::map<std::string, std::vector<std::string>>
      notes;  // by preset name: what a translated .milk file lost
};

// True for the file names a presets folder uses: "*.fh-preset.json" and "*.milk" (case does not
// matter for the second).
bool looks_like_preset(const std::filesystem::path& file);

// Reads every preset file directly inside `dir`. A missing folder gives an empty library and a
// problem line. If two files have the same preset name the later one is renamed "name (2)" and so
// on, so that every name is its own.
Library load_library(const std::filesystem::path& dir);

PresetResult load_preset_file(const std::filesystem::path& file);
MilkResult load_milk_file(const std::filesystem::path& file);

}  // namespace fh
