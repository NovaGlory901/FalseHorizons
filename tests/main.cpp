// All of FalseHorizons' unit tests in one program. With no argument it runs every group; with a
// group name it runs only that one (the names are in the table below). The time-budgeted
// fuzz_test is a separate program.
#include <iostream>
#include <string>

#include "check.hpp"
#include "tests.hpp"

#ifndef FH_PARITY_DIR
#define FH_PARITY_DIR "tests/parity"
#endif

namespace {
struct Group {
  const char* name;
  void (*run)(const std::string& parity_dir);
};

const Group groups[] = {
    {"expr", expr_tests},
    {"preset", preset_tests},
    {"analyser", [](const std::string&) { analyser_tests(); }},
    {"image", [](const std::string&) { image_tests(); }},
    {"userfiles", [](const std::string&) { userfiles_tests(); }},
    {"unicode", [](const std::string&) { unicode_tests(); }},
    {"findroot", [](const std::string&) { findroot_tests(); }},
    {"scene", [](const std::string&) { scene_tests(); }},
    {"hearing", hearing_tests},
    {"runtime", runtime_tests},
    {"fractal", fractal_tests},
    {"milk", milk_tests},
    {"state", state_tests},
};
}  // namespace

int main(int argc, char* const* argv) {
  const std::string wanted = argc > 1 ? argv[1] : "";
  bool found = wanted.empty();
  for (const Group& g : groups) {
    if (wanted.empty() || wanted == g.name) {
      g.run(FH_PARITY_DIR);
      found = true;
    }
  }
  if (!found) {
    std::cerr << "unknown group '" << wanted << "'\n";
    return 2;
  }
  return test::finish();
}
