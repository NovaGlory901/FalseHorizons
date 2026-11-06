// The user's choices, kept in settings.json next to presets/. The shared settings are checked
// exactly as sanitize in js/falsehorizon-core.js does (tests/parity/settings.golden). The file is
// untrusted: a damaged or hand-edited one gives defaults for whatever is wrong, never a crash.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "fh/json.hpp"

namespace fh {

constexpr int cycle_choices[] = {
    10, 15, 30, 60,
    120};  // seconds, what the panel offers (the file may hold any whole number 10..300)
constexpr int max_favorites = 30;

struct Settings {
  int intensity = 77;  // 0..100 how much the picture moves
  int geometry = 60;  // 0..100 how strongly the fractal bends it
  int backgrounds = 60;  // 0..100 how present the background picture is
  int artifacts = 100;  // 0..100 how visible the floating pictures are
  bool show_backgrounds = true, show_artifacts = true;
  bool auto_change = true;
  int cycle_sec = 30;  // 10..300 seconds between presets
  bool calm = false;  // less motion, 24 frames a second, stricter flash guard
  std::vector<std::string> favorites;  // preset names, at most max_favorites
  std::string last;  // the preset showing when the program was closed

  bool operator==(const Settings&) const = default;
};

Settings sanitize_settings(const json::Value& raw);  // anything in, usable settings out
json::Value to_json(const Settings& s);

struct SettingsLoad {
  Settings settings;
  std::string
      problem;  // empty if all was well (a missing file is not a problem: it is the first run)
};
SettingsLoad load_settings(const std::filesystem::path& file);
// Writes to a temporary file and moves it into place, so a crash cannot leave half a file. Returns
// false (with a reason) if the folder is not writable.
bool save_settings(const std::filesystem::path& file, const Settings& s, std::string& error);

void toggle_favorite(Settings& s,
                     const std::string& name);  // adds or removes; the list stays within its limit
bool is_favorite(const Settings& s, const std::string& name);

}  // namespace fh
