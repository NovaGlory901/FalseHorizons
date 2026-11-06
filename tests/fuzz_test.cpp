// Junk in, a clean answer out: feeds random and damaged inputs (fixed seed) to everything that
// reads a file from a person. Each must answer (ok or an error sentence) and never crash, hang or
// read outside its input. Build with FH_SANITIZE=ON to catch memory mistakes.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <numbers>
#include <random>
#include <string>
#include <vector>

#include "fh/expr.hpp"
#include "fh/image.hpp"
#include "fh/json.hpp"
#include "fh/milk.hpp"
#include "fh/preset.hpp"

namespace {
// FH_FUZZ_SEED changes the seed and FH_FUZZ_SCALE multiplies the number of damaged pictures (for
// long runs); without them the run is the same every time.
std::uint64_t env_number(const char* name, std::uint64_t fallback) {
  const char* v = std::getenv(name);
  return v && *v ? std::strtoull(v, nullptr, 10) : fallback;
}
std::mt19937_64 rng(env_number("FH_FUZZ_SEED", 20261011));

std::string read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}

std::string noise(std::size_t n) {
  std::string s(n, '\0');
  for (auto& c : s) {
    c = static_cast<char>(rng() & 0xFF);
  }
  return s;
}

std::string damage(std::string s) {
  if (s.empty()) {
    return s;
  }
  const int edits = 1 + static_cast<int>(rng() % 8);
  for (int i = 0; i < edits; ++i) {
    switch (rng() % 5) {
      case 0:
        s[rng() % s.size()] = static_cast<char>(rng() & 0xFF);
        break;
      case 1:
        s.erase(rng() % s.size(), 1 + rng() % 20);
        break;
      case 2:
        s.insert(rng() % s.size(), noise(1 + rng() % 20));
        break;
      case 3:
        s.resize(rng() % (s.size() + 1));
        break;
      default: {
        const std::size_t a = rng() % s.size();
        const std::size_t b = rng() % s.size();
        std::swap(s[a], s[b]);
      }
    }
    if (s.empty()) {
      break;
    }
  }
  return s;
}

int fails = 0;
void check(const char* what, int count, double seconds, double allowed) {
  const bool slow = seconds > allowed;
  fails += slow;
  std::printf("%s %s: %d inputs, %.1f s%s\n", slow ? "BAD " : "ok  ", what, count, seconds,
              slow ? "   <- far too slow overall (a debug build with the sanitizers is slow, but "
                     "not this slow)"
                   : "");
}

template <class F>
void timed(const char* what, int count, double allowed, F&& body) {
  const auto t0 = std::chrono::steady_clock::now();
  body();
  check(what, count, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(),
        allowed);
}
}  // namespace

int main() {
  const std::string milk =
      "[preset00]\nfRating=3.000000\nfGammaAdj=1.800000\nfDecay=0.980000\nfZoom=1.010000\nfRot=0."
      "020000\nfWarpAnimSpeed=1.000000\nfWaveR=0.5\nfWaveG=0.5\nfWaveB=0.9\n"
      "nWaveMode=2\nbAdditiveWaves=1\nzoom=1.02\nrot=0.01\nwarp=0.5\nper_frame_init_1=q1=0;\nper_"
      "frame_1=wave_r=0.5+0.5*sin(time*1.13);\nper_frame_2=zoom=zoom+0.02*bass;\n"
      "per_pixel_1=rot=rot+0.1*sin(rad*5+time);\nper_pixel_2=zoom=zoom+0.01*cos(ang*3);\n";
  const std::string preset = read_file(std::string(FH_PRESETS) + "/aurora-wire.fh-preset.json");
  std::vector<std::string> pictures;
  for (const char* name :
       {"clear.png", "clear.webp", "clear-lossy.webp", "deferred-clear.gif", "halves.bmp",
        "halves.gif", "halves.jpg", "halves.png", "halves.ppm", "halves.tga"}) {
    pictures.push_back(read_file(std::string(FH_DATA) + "/" + name));
  }
  std::vector<std::string> formulas = {"a=1;",
                                       "x=sin(y)*cos(z)+pow(2,10);",
                                       "q1=if(above(a,b),sqrt(c),abs(d));",
                                       "i=0; loop(10, i=i+1);",
                                       "a=b/0;",
                                       "a=log(-1);",
                                       "a=pow(0,-1);",
                                       "a=asin(5)+acos(-5)+atan2(0,0);",
                                       "a=1e308*1e308;",
                                       "a=((((((((((1))))))))));",
                                       "a=megabuf(1e9);",
                                       "a=sqr(sqr(sqr(sqr(9))));",
                                       "x=rand(0)+rand(-1);"};

  timed("the .milk importer", 30000, 1800, [&] {
    for (int i = 0; i < 10000; ++i) {
      (void)fh::convert_milk(noise(rng() % 1500), "noise.milk");
    }
    for (int i = 0; i < 20000; ++i) {
      (void)fh::convert_milk(damage(milk), "damaged.milk");
    }
  });
  timed("the preset reader and the JSON reader", 40000, 1800, [&] {
    for (int i = 0; i < 10000; ++i) {
      const auto s = noise(rng() % 800);
      (void)fh::parse_preset_file(s);
      (void)fh::json::parse(s);
    }
    for (int i = 0; i < 30000; ++i) {
      const auto s = damage(preset);
      (void)fh::parse_preset_file(s);
      (void)fh::json::parse(s);
    }
    (void)fh::json::parse(std::string(5000, '['));
    (void)fh::json::parse(std::string(5000, '{'));
    (void)fh::parse_preset_file(std::string(200000, ' '));
  });
  timed("the formula compiler and evaluator", 60000, 1800, [&] {
    const double odd[] = {0, -0.0, 1, -1, 1e-300, 1e300, std::numbers::pi, -1e6, 1e6};
    double worst = 0;
    std::string worst_src;
    const auto one = [&](const std::string& src) {
      const auto t0 = std::chrono::steady_clock::now();
      fh::expr::Symbols symbols;
      const auto r = fh::expr::compile(src, symbols);
      if (r.ok) {
        std::vector<double> vars(r.program.slot_count() + 4);
        for (auto& v : vars) {
          v = odd[rng() % 9];
        }
        r.program.run(vars);
      }
      (void)fh::expr::check(src);
      const double took =
          std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
      if (took > worst) {
        worst = took;
        worst_src = src;
      }
    };
    for (int i = 0; i < 20000; ++i) {
      one(noise(rng() % 200));
    }
    for (int i = 0; i < 40000; ++i) {
      one(damage(formulas[rng() % formulas.size()]));
    }
    one(std::string(6001, 'a'));
    one("a=" + std::string(500, '(') + "1" + std::string(500, ')') + ";");
    std::printf("    slowest formula: %.3f s: %.120s\n", worst, worst_src.c_str());
    if (worst > 5.0) {
      ++fails;
      std::printf("BAD  a single formula took %.1f s (a hang): %.80s\n", worst, worst_src.c_str());
    }
  });
  const int picture_scale = static_cast<int>(env_number("FH_FUZZ_SCALE", 1));
  timed("the picture decoder (real pictures, damaged)", 30000 * picture_scale,
        1800.0 * picture_scale, [&] {
          const fh::ImageLimits small;
          for (int i = 0; i < 30000 * picture_scale; ++i) {
            std::string s = damage(pictures[rng() % pictures.size()]);
            (void)fh::decode_image({reinterpret_cast<const std::uint8_t*>(s.data()), s.size()},
                                   small);
          }
        });
  std::printf("\n%s\n", fails ? "FAILED" : "ALL PASSED");
  return fails ? 1 : 0;
}
