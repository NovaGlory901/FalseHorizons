// Files and folders with non-ASCII names: listing, loading pictures and presets, saving settings.
// On Windows these fail if any code goes through the ANSI code page; on Linux they pass anyway.
#include <cstdlib>
#include <iostream>
#include <string>

#include "check.hpp"
#include "fh/image.hpp"
#include "fh/library.hpp"
#include "fh/pathutf8.hpp"
#include "fh/settings.hpp"
#include "fh/userfiles.hpp"
#include "tests.hpp"

using namespace fh;
namespace fs = std::filesystem;

using test::check;

void unicode_tests() {
  test::section("unicode");
  // written as escapes so the source file's encoding does not matter: U+97F3 U+697D (Japanese
  // "music"), U+6062 U+590D (Chinese "recover"), U+8BBE U+7F6E ("settings")
  const std::string music = "\xE9\x9F\xB3\xE6\xA5\xBD";
  const std::string recover = "\xE6\x81\xA2\xE5\xA4\x8D";
  const std::string setting = "\xE8\xAE\xBE\xE7\xBD\xAE";

  const std::string text = music + "/" + recover + " caf\xC3\xA9";
  check("a path made from UTF-8 text gives the same text back",
        path_to_utf8(path_from_utf8(text)) == text);

  const fs::path root =
      fs::temp_directory_path() /
      path_from_utf8("fh-unicode-test-" + std::to_string(std::rand()) + "-" + music);
  const fs::path dir = root / path_from_utf8(recover);
  std::error_code ec;
  fs::remove_all(root, ec);
  check("a folder with non-ASCII names can be made", fs::create_directories(dir, ec) && !ec);

  const fs::path picture = dir / path_from_utf8(recover + ".png");
  const fs::path preset = dir / path_from_utf8(recover + ".fh-preset.json");
  fs::copy_file(fs::path(FH_DATA) / "halves.png", picture, ec);
  check("a picture can be copied there", !ec, ec.message());
  fs::copy_file(fs::path(FH_PRESETS) / "aurora-wire.fh-preset.json", preset, ec);
  check("a preset can be copied there", !ec, ec.message());

  check("pictures: a non-ASCII name looks like a picture", looks_like_picture(picture));
  const auto pics = list_pictures(dir);
  check("pictures: the folder lists it", pics.size() == 1 && pics[0] == picture);
  const ImageResult img = load_image_file(picture);
  check("pictures: it loads", img.ok, img.error);

  check("presets: a non-ASCII name looks like a preset", looks_like_preset(preset));
  const Library lib = load_library(dir);
  check("presets: the folder loads it without a problem",
        lib.presets.size() == 1 && lib.problems.empty(),
        lib.problems.empty() ? "" : lib.problems[0]);

  const fs::path file = dir / path_from_utf8(setting + ".json");
  Settings s;
  s.favorites.emplace_back("Aurora Wire");
  std::string error;
  check("settings: saves into a non-ASCII folder under a non-ASCII name",
        save_settings(file, s, error), error);
  check("settings: no .tmp file left behind", !fs::exists(fs::path(file) += ".tmp"));
  const SettingsLoad back = load_settings(file);
  check("settings: loads back",
        back.problem.empty() && back.settings.favorites.size() == 1 &&
            back.settings.favorites[0] == "Aurora Wire",
        back.problem);

  fs::remove_all(root, ec);
}
