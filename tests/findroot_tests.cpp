// fh::find_root: finds a program's folders from where the program is.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "check.hpp"
#include "fh/findroot.hpp"
#include "tests.hpp"

namespace fs = std::filesystem;
using test::check;

void findroot_tests() {
  test::section("findroot");
  const fs::path tmp = fs::temp_directory_path() / ("fh-findroot-" + std::to_string(std::rand()));
  fs::remove_all(tmp);
  fs::create_directories(tmp / "installed" /
                         "presets");  // an installed copy: the folders sit next to the program
  fs::create_directories(tmp / "src" / "presets");  // a source folder with a build tree inside it
  fs::create_directories(tmp / "src" / "build-release");
  fs::create_directories(tmp / "src" / "a" / "b" / "c" / "d");
  fs::create_directories(tmp / "elsewhere");

  check("next to the program: that folder",
        fh::find_root(tmp / "installed", "presets") == tmp / "installed");
  check("a build tree: the source folder one level up",
        fh::find_root(tmp / "src" / "build-release", "presets") == tmp / "src");
  check("three levels down still finds it",
        fh::find_root(tmp / "src" / "a" / "b" / "c", "presets") == tmp / "src");
  check("four levels down is too far: the start folder is returned",
        fh::find_root(tmp / "src" / "a" / "b" / "c" / "d", "presets") ==
            tmp / "src" / "a" / "b" / "c" / "d");
  check("no such folder anywhere: the start folder, not a made-up place",
        fh::find_root(tmp / "elsewhere", "no-such-folder-anywhere") == tmp / "elsewhere");
  check("a file called like the marker does not count (it must be a folder)", [&] {
    std::ofstream(tmp / "elsewhere" / "presets") << "x";
    return fh::find_root(tmp / "elsewhere", "presets") == tmp / "elsewhere";
  }());
  check("the start folder is preferred over a parent that also has the marker", [&] {
    fs::create_directories(tmp / "src" / "build-release" / "presets");
    return fh::find_root(tmp / "src" / "build-release", "presets") == tmp / "src" / "build-release";
  }());
  fs::remove_all(tmp);
}
