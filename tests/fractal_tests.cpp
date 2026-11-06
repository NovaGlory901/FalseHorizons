// Tests for the fractal warp (fractal.hpp): hand-written checks plus parity with
// tests/parity/fractal.golden.
#include "check.hpp"
#include "fh/fractal.hpp"
#include "tests.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

#include "fh/json.hpp"

using namespace fh;
using json::Value;

using test::check;

static std::vector<float> plain() {
  std::vector<float> m(mesh_floats);
  std::size_t k = 0;
  for (int j = 0; j <= mesh_h; j++) {
    for (int i = 0; i <= mesh_w; i++, k += 2) {
      m[k] = static_cast<float>(i) / mesh_w;
      m[k + 1] = static_cast<float>(j) / mesh_h;
    }
  }
  return m;
}

static bool near(double a, double b) {
  return std::fabs(a - b) <= 1e-6 * std::max(1.0, std::max(std::fabs(a), std::fabs(b)));
}

static bool same_summary(const Value& want, const std::vector<float>& a, std::string& why) {
  double sum = 0;
  double wsum = 0;
  for (std::size_t i = 0; i < a.size(); i++) {
    sum += a[i];
    wsum += static_cast<double>(i % 97 + 1) * a[i];
  }
  if (static_cast<std::size_t>(want.find("n")->as_number()) != a.size()) {
    why = "length";
    return false;
  }
  if (!near(want.find("sum")->as_number(), sum)) {
    why = "sum " + json::stringify(*want.find("sum")) + " vs " + std::to_string(sum);
    return false;
  }
  if (!near(want.find("wsum")->as_number(), wsum)) {
    why = "wsum";
    return false;
  }
  const auto& head = want.find("head")->items();
  const auto& tail = want.find("tail")->items();
  for (std::size_t i = 0; i < head.size(); i++) {
    if (!near(head[i].as_number(), a[i])) {
      why = "head " + std::to_string(i);
      return false;
    }
  }
  for (std::size_t i = 0; i < tail.size(); i++) {
    if (!near(tail[i].as_number(), a[a.size() - tail.size() + i])) {
      why = "tail " + std::to_string(i);
      return false;
    }
  }
  return true;
}

static void hand_tests() {
  check("a point inside the set is 1", fractal_height(0, 0) == 1 && fractal_height(-1, 0) == 1);
  check("a point far outside is between 0 and 1",
        fractal_height(100, 100) >= 0 && fractal_height(100, 100) < 0.1);
  check("the height is always 0..1", [] {
    for (double r = -3; r < 2; r += 0.07) {
      for (double i = -2; i < 2; i += 0.07) {
        const double h = fractal_height(r, i);
        if (!(h >= 0 && h <= 1)) {
          return false;
        }
      }
    }
    return true;
  }());
  check("modulation runs 0..1 and treats a non-number time as 0",
        modulation_index(0) == 0.5 && modulation_index(NAN) == 0.5 && modulation_index(1e9) >= 0 &&
            modulation_index(1e9) <= 1);
  {
    std::vector<float> m = plain();
    FrameInputs in;
    in.time = 5;
    fractal_warp(m, in, 0, false);
    check("amount 0 leaves the mesh alone", m == plain());
    fractal_warp(m, in, 0.01, false);
    check("amount 0.01 or less leaves the mesh alone", m == plain());
    fractal_warp(m, in, 0.6, false);
    check("it moves the mesh, but only a little, and keeps it finite",
          m != plain() &&
              std::ranges::all_of(m, [](float v) { return std::isfinite(v) && std::fabs(v) < 3; }));
  }
  {
    std::vector<float> small(10, 0.5F);
    const FrameInputs in;
    fractal_warp(small, in, 1, false);
    check("a mesh that is too small is left alone", small == std::vector<float>(10, 0.5F));
  }
  {
    FrameInputs in;
    in.time = NAN;
    in.bass = INFINITY;
    in.mid = NAN;
    in.aspect = NAN;
    std::vector<float> m = plain();
    fractal_warp(m, in, 1, false);
    check("nonsense inputs give a finite mesh",
          std::ranges::all_of(m, [](float v) { return std::isfinite(v); }));
  }
}

static bool run_case(const Value& c, std::string& why) {
  const std::string kind = c.find("kind")->as_string();
  if (kind == "height") {
    for (const Value& p : c.find("pts")->items()) {
      const auto& v = p.items();
      const double got = fractal_height(v[0].as_number(), v[1].as_number());
      if (!near(v[2].as_number(), got)) {
        why = "height at " + json::stringify(p) + " got " + std::to_string(got);
        return false;
      }
    }
    return true;
  }
  const std::string wave = c.find("wave")->as_string();
  std::vector<float> w;
  const double time = c.find("time")->as_number();
  if (wave == "sin") {
    for (int k = 0; k < 256; k++) {
      w.push_back(static_cast<float>(std::sin(k * 0.1 + time) * 0.8));
    }
  }
  if (wave == "short") {
    for (int k = 0; k < 100; k++) {
      w.push_back(static_cast<float>(std::sin(k) * 0.5));
    }
  }
  FrameInputs in;
  in.time = time;
  in.aspect = c.find("aspect")->as_number();
  in.bass = c.find("bass")->as_number();
  in.mid = c.find("mid")->as_number();
  in.wave = w;
  std::vector<float> m = plain();
  fractal_warp(m, in, c.find("amount")->as_number(), c.find("calm")->as_bool());
  return same_summary(*c.find("mesh"), m, why);
}

static void parity(const std::string& path) {
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
      check("parity: golden line " + std::to_string(cases + 1) + " is readable", false);
      bad++;
      break;
    }
    cases++;
    std::string why;
    bool ok = false;
    try {
      ok = run_case(parsed.value, why);
    } catch (const std::exception& e) {
      why = e.what();
    }
    if (!ok && ++bad <= 10) {
      check("parity: case " + std::to_string(cases) + " " + parsed.value.find("kind")->as_string(),
            false, why);
    }
  }
  check("parity: " + std::to_string(cases) + " cases, " + std::to_string(bad) +
            " differ from the JavaScript",
        cases > 50 && bad == 0);
}

void fractal_tests(const std::string& dir) {
  test::section("fractal");
  hand_tests();
  parity(dir + "/fractal.golden");
}
