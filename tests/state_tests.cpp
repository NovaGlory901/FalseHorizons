// Tests for the picker, the settings and the presets folder: hand-written checks plus parity with
// tests/parity/picker.golden and settings.golden.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "check.hpp"
#include "fh/adapt.hpp"
#include "fh/json.hpp"
#include "fh/library.hpp"
#include "fh/picker.hpp"
#include "fh/settings.hpp"
#include "tests.hpp"

#ifndef FH_PARITY_DIR
#define FH_PARITY_DIR "tests/parity"
#endif

using namespace fh;
using json::Value;
namespace fs = std::filesystem;

using test::check;

// The same seeded generator as gen_state_golden.js (mulberry32), in unsigned 32-bit arithmetic.
struct Seeded {
  std::uint32_t s;
  double next() {
    s += 0x6D2B79F5U;
    std::uint32_t t = (s ^ (s >> 15)) * (1U | s);
    t = (t + ((t ^ (t >> 7)) * (61U | t))) ^ t;
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
  }
};

static std::vector<std::string> names(std::size_t n) {
  std::vector<std::string> v;
  v.reserve(n);
  for (std::size_t i = 0; i < n; i++) {
    v.push_back(std::string("Preset ") + static_cast<char>('A' + i));
  }
  return v;
}

static void picker_tests() {
  {
    PresetPicker p([] { return 0.0; });
    check("picker: nothing to pick from gives nothing", !p.next({}) && !p.prev());
    p.show("A");
    check("picker: nothing before the first", !p.prev());
  }
  {
    PresetPicker p;
    const auto pool = names(8);
    std::vector<std::string> seen;
    bool repeat = false;
    for (int i = 0; i < 200; i++) {
      auto n = p.next(pool);
      if (!n) {
        repeat = true;
        break;
      }
      for (std::size_t k = seen.size() > 4 ? seen.size() - 4 : 0; k < seen.size(); k++) {
        if (seen[k] == *n) {
          repeat = true;
        }
      }
      seen.push_back(*n);
    }
    check("picker: never one of the last four shown", !repeat);
  }
  {
    PresetPicker p([] { return 0.0; });
    const auto pool = names(5);
    const auto a = p.next(pool);
    const auto b = p.next(pool);
    const auto c = p.next(pool);
    const auto back = p.prev();
    const auto back2 = p.prev();
    check("picker: previous steps back through what was shown",
          a && b && c && back == b && back2 == a);
    check("picker: next retraces before picking again", p.next(pool) == b && p.next(pool) == c);
    p.prev();
    p.show("Z");
    check("picker: showing something after stepping back forgets what was ahead",
          p.history().back() == "Z" && p.index() == static_cast<int>(p.history().size()) - 1);
  }
  {
    PresetPicker p([] { return 0.99999; });
    for (int i = 0; i < 250; i++) {
      p.next(names(14));
    }
    check("picker: the history never holds more than 100", p.history().size() == 100);
    PresetPicker q([] { return 1.0; });  // a generator that answers 1 must not run off the end
    check("picker: a random answer of exactly 1 still picks something",
          q.next(names(3)).has_value());
  }
}

static void adapt_tests() {
  {
    AdaptiveScale a;
    bool changed = false;
    for (int i = 0; i < 2000; i++) {
      changed = a.frame(16.7, false) || changed;
    }
    check("adapt: at 60 frames a second it stays at full size", !changed && a.scale() == 1);
  }
  {
    AdaptiveScale a;
    for (int i = 0; i < 3000; i++) {
      a.frame(45, false);
    }
    check("adapt: a slow machine ends up drawing at half size, and no smaller", a.scale() == 0.5);
    for (int i = 0; i < 3000; i++) {
      a.frame(10, false);
    }
    check("adapt: it grows back to full size when there is room", a.scale() == 1);
  }
  {
    AdaptiveScale a;
    for (int i = 0; i < 3000; i++) {
      a.frame(45, true);
    }
    check("adapt: calm mode never changes the size", a.scale() == 1);
    AdaptiveScale b;
    int steps = 0;
    for (int i = 0; i < 900; i++) {
      steps += b.frame(40, false) ? 1 : 0;
    }
    check("adapt: it steps down gradually, not in one jump", steps >= 3 && b.scale() == 0.5);
    AdaptiveScale c;
    for (int i = 0; i < 1000; i++) {
      c.frame(5000, false);  // one long stall counts as at most 200 ms
    }
    check("adapt: a huge frame time is capped", c.average_ms() <= 200.0001);
  }
}

static void settings_tests() {
  check("settings: not an object gives the defaults",
        sanitize_settings(Value::number(5)) == Settings{} &&
            sanitize_settings(Value()) == Settings{});
  {
    const auto parsed = json::parse(
        R"({"intensity":150,"geometry":-4,"backgroundAmount":"x","auto":false,"cycleSec":5,"calm":true,"favorites":["A","A","  B  ",3,""],"last":"Slow Tide","showArtifacts":false})");
    const Settings s = sanitize_settings(parsed.value);
    check("settings: values are forced into range, bad ones get the default",
          s.intensity == 100 && s.geometry == 0 && s.backgrounds == 60 && !s.auto_change &&
              s.cycle_sec == 10 && s.calm && !s.show_artifacts && s.show_backgrounds);
    check("settings: favourites are cleaned and unique",
          s.favorites == std::vector<std::string>({"A", "B"}) && s.last == "Slow Tide");
  }
  {
    Settings s;
    s.intensity = 12;
    s.geometry = 99;
    s.backgrounds = 3;
    s.artifacts = 44;
    s.show_backgrounds = false;
    s.auto_change = false;
    s.cycle_sec = 120;
    s.calm = true;
    s.favorites = {"x", "y"};
    s.last = "y";
    check("settings: keeping them as JSON and reading them back changes nothing",
          sanitize_settings(json::parse(json::stringify(to_json(s))).value) == s);
  }
  {
    Settings s;
    for (int i = 0; i < 40; i++) {
      toggle_favorite(s, "p" + std::to_string(i));
    }
    check("settings: at most 30 favourites",
          s.favorites.size() == static_cast<std::size_t>(max_favorites));
    toggle_favorite(s, "p3");
    check("settings: toggling a favourite removes it",
          !is_favorite(s, "p3") && s.favorites.size() == 29);
    toggle_favorite(s, "p3");
    check("settings: toggling a favourite again adds it back", is_favorite(s, "p3"));
  }
  {
    const fs::path dir = fs::temp_directory_path() / "fh_state_test_settings";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path file = dir / "settings.json";
    check("settings: a missing file is the first run, not a problem",
          load_settings(file).problem.empty() && load_settings(file).settings == Settings{});
    Settings s;
    s.intensity = 31;
    s.favorites = {"One"};
    std::string err;
    check("settings: saving works", save_settings(file, s, err), err);
    check("settings: loading gives the same settings back, with no leftover temporary file",
          load_settings(file).settings == s && !fs::exists(file.string() + ".tmp"));
    {
      std::ofstream(file) << "{ not json";
    }
    const SettingsLoad bad = load_settings(file);
    check("settings: a damaged file gives the defaults and says so",
          !bad.problem.empty() && bad.settings == Settings{});
    {
      std::ofstream(file, std::ios::binary) << std::string(70000, ' ');
    }
    check("settings: a huge file is refused", !load_settings(file).problem.empty());
    check("settings: saving into a folder that is not there says so",
          !save_settings(dir / "nope" / "settings.json", s, err) && !err.empty());
    fs::remove_all(dir);
  }
}

static void library_tests() {
  const fs::path dir = fs::temp_directory_path() / "fh_state_test_presets";
  fs::remove_all(dir);
  fs::create_directories(dir);
  {
    std::ofstream(dir / "a.fh-preset.json") << R"({"fh-preset":1,"name":"Plain"})";
  }
  {
    std::ofstream(dir / "b.fh-preset.json") << R"({"fh-preset":1,"name":"Plain"})";
  }
  {
    std::ofstream(dir / "Tide.MILK") << "[preset00]\nfDecay=0.9\nzoom=1.02\nwarp_1=x\n";
  }
  {
    std::ofstream(dir / "Loopy.milk") << "[preset00]\nfDecay=0.9\nper_frame_1=loop(3, q1 = 1);\n";
  }
  {
    std::ofstream(dir / "notes.txt") << "hello";
  }
  {
    std::ofstream(dir / "broken.fh-preset.json") << "{";
  }
  Library lib = load_library(dir);
  std::vector<std::string> got;
  got.reserve(lib.presets.size());
  for (const Preset& p : lib.presets) {
    got.push_back(p.name);
  }
  check("library: json and milk files load, sorted by name, same names made different",
        got == std::vector<std::string>({"Plain", "Plain (2)", "Tide"}));
  check("library: a translated file keeps its notes",
        lib.notes.contains("Tide") && lib.notes["Tide"].size() >= 2);
  check("library: a refused milk file and a broken json file are listed as problems",
        lib.problems.size() == 2);
  check("library: looksLikePreset", looks_like_preset("x.fh-preset.json") &&
                                        looks_like_preset("x.MiLk") &&
                                        !looks_like_preset("x.json") &&
                                        !looks_like_preset("x.txt") && !looks_like_preset(".milk"));
  fs::remove_all(dir);
}

static bool same_list(const Value& want, const std::vector<std::string>& got) {
  if (want.items().size() != got.size()) {
    return false;
  }
  for (std::size_t i = 0; i < got.size(); i++) {
    if (want.items()[i].as_string() != got[i]) {
      return false;
    }
  }
  return true;
}

static void picker_parity(const std::string& path) {
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
      check("parity: picker line " + std::to_string(cases + 1) + " is readable", false);
      bad++;
      break;
    }
    cases++;
    const Value& c = parsed.value;
    Seeded rng{static_cast<std::uint32_t>(c.find("seed")->as_number())};
    PresetPicker p([&] { return rng.next(); });
    const auto& ops = c.find("ops")->items();
    const auto& out = c.find("out")->items();
    std::string why;
    for (std::size_t i = 0; i < ops.size() && why.empty(); i++) {
      const std::string kind = ops[i].items()[0].as_string();
      std::optional<std::string> got;
      if (kind == "show") {
        p.show(ops[i].items()[1].as_string());
      } else if (kind == "next") {
        got = p.next(names(static_cast<std::size_t>(ops[i].items()[1].as_number())));
      } else {
        got = p.prev();
      }
      if (kind != "show") {
        const bool want_none = out[i].is_null();
        if (want_none != !got.has_value() || (got && *got != out[i].as_string())) {
          why = "op " + std::to_string(i) + " (" + kind + ")";
        }
      }
    }
    if (why.empty() && (!same_list(*c.find("history"), p.history()) ||
                        p.index() != static_cast<int>(c.find("i")->as_number()))) {
      why = "the final history";
    }
    if (!why.empty() && ++bad <= 5) {
      check("parity: picker case " + std::to_string(cases), false, why);
    }
  }
  check("parity: " + std::to_string(cases) + " picker runs, " + std::to_string(bad) +
            " differ from the JavaScript",
        cases >= 30 && bad == 0);
}

static void settings_parity(const std::string& path) {
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
      check("parity: settings line " + std::to_string(cases + 1) + " is readable", false);
      bad++;
      break;
    }
    cases++;
    const Value* raw = parsed.value.find("raw");
    const Value& want = *parsed.value.find("want");
    const Settings s = sanitize_settings(raw ? *raw : Value());
    const bool ok = s.intensity == want.find("intensity")->as_number() &&
                    s.geometry == want.find("geometry")->as_number() &&
                    s.auto_change == want.find("auto")->as_bool() &&
                    s.cycle_sec == want.find("cycleSec")->as_number() &&
                    same_list(*want.find("favorites"), s.favorites) &&
                    s.last == want.find("last")->as_string();
    if (!ok && ++bad <= 5) {
      check("parity: settings case " + std::to_string(cases), false, line.substr(0, 200));
    }
  }
  check("parity: " + std::to_string(cases) + " settings, " + std::to_string(bad) +
            " differ from the JavaScript",
        cases >= 150 && bad == 0);
}

void state_tests(const std::string& dir) {
  test::section("state");
  picker_tests();
  adapt_tests();
  settings_tests();
  library_tests();
  picker_parity(dir + "/picker.golden");
  settings_parity(dir + "/settings.golden");
}
