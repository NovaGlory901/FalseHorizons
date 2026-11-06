#include "fh/library.hpp"
#include "fh/pathutf8.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>

namespace fh {

namespace {
bool read_small(const std::filesystem::path& file, std::uintmax_t most, std::string& text,
                std::string& error) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(file, ec);
  if (ec) {
    error = "The file could not be read.";
    return false;
  }
  if (size > most) {
    error = "That file is too big to be a preset.";
    return false;
  }
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    error = "The file could not be read.";
    return false;
  }
  text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return true;
}

bool has_suffix(std::string name, const std::string& suffix, bool any_case) {
  if (any_case) {
    for (char& c : name) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }
  return name.size() > suffix.size() && name.ends_with(suffix);
}
}  // namespace

bool looks_like_preset(const std::filesystem::path& file) {
  const std::string name = path_to_utf8(file.filename());
  return has_suffix(name, ".fh-preset.json", false) || has_suffix(name, ".milk", true);
}

PresetResult load_preset_file(const std::filesystem::path& file) {
  PresetResult r;
  std::string text;
  // the limit is in characters; this just avoids reading a huge file
  if (!read_small(file, static_cast<std::uintmax_t>(Limits::file) * 4, text, r.error)) {
    return r;
  }
  return parse_preset_file(text);
}

MilkResult load_milk_file(const std::filesystem::path& file) {
  MilkResult r;
  std::string text;
  if (!read_small(file, milk_max_text + 1, text, r.error)) {
    if (r.error == "That file is too big to be a preset.") {
      r.error = "That file is too big to be a MilkDrop preset (the limit is 100 KB).";
    }
    return r;
  }
  return convert_milk(text, path_to_utf8(file.filename()));
}

Library load_library(const std::filesystem::path& dir) {
  Library lib;
  std::error_code ec;
  const std::filesystem::directory_iterator it(dir, ec);
  if (ec) {
    lib.problems.push_back(path_to_utf8(dir) + ": the folder could not be opened");
    return lib;
  }
  std::vector<std::filesystem::path> files;
  for (const auto& e : it) {
    if (e.is_regular_file(ec) && looks_like_preset(e.path())) {
      files.push_back(e.path());
    }
  }
  std::ranges::sort(files);
  std::set<std::string> names;
  const auto unique = [&](std::string name) {
    if (!names.contains(name)) {
      return name;
    }
    for (int n = 2;; n++) {
      const std::string tail = " (" + std::to_string(n) + ")";
      std::string cut = name.substr(0, Limits::name - tail.size());
      while (!cut.empty() && (static_cast<unsigned char>(cut.back()) & 0xC0) == 0x80) {
        cut.pop_back();  // do not leave half a character
      }
      if (!names.contains(cut + tail)) {
        return cut + tail;
      }
    }
  };
  for (const auto& f : files) {
    const std::string file_name = path_to_utf8(f.filename());
    if (has_suffix(file_name, ".milk", true)) {
      MilkResult r = load_milk_file(f);
      if (!r.ok) {
        lib.problems.push_back(file_name + ": " + r.error);
        continue;
      }
      r.preset.name = unique(r.preset.name);
      names.insert(r.preset.name);
      lib.notes[r.preset.name] = std::move(r.notes);
      lib.presets.push_back(std::move(r.preset));
    } else {
      PresetResult r = load_preset_file(f);
      if (!r.ok) {
        lib.problems.push_back(file_name + ": " + r.error);
        continue;
      }
      r.preset.name = unique(r.preset.name);
      names.insert(r.preset.name);
      lib.presets.push_back(std::move(r.preset));
    }
  }
  std::ranges::stable_sort(lib.presets,
                           [](const Preset& a, const Preset& b) { return a.name < b.name; });
  return lib;
}

}  // namespace fh
