// Tests for the flash guard and the audio analysis: hand-written checks (including the WCAG flash
// rule) plus parity with tests/parity/hearing.golden (1e-9; 1e-6 for the float32 arrays).
#include "check.hpp"
#include "fh/audio.hpp"
#include "fh/flashguard.hpp"
#include "fh/json.hpp"
#include "tests.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

using namespace fh;
using json::Value;
using Bytes = std::vector<std::uint8_t>;

using test::check;
static bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }

struct GuardRun {
  std::vector<double> shown;
};
static GuardRun guard_run(const std::vector<double>& levels, double step_ms, bool calm = false) {
  FlashGuard g(calm);
  GuardRun r;
  for (std::size_t i = 0; i < levels.size(); i++) {
    r.shown.push_back(levels[i] * g.feed(levels[i], static_cast<double>(i) * step_ms));
  }
  return r;
}
static std::vector<double> repeat(double v, int n) {
  return std::vector<double>(static_cast<std::size_t>(n), v);
}
static std::vector<double> strobe(int n) {
  std::vector<double> v;
  v.reserve(n);
  for (int i = 0; i < n; i++) {
    v.push_back(i % 2 ? 0.95 : 0.02);
  }
  return v;
}

// The accessibility rule of thumb (WCAG 2.3.1): no more than 3 "flashes" a second, a flash being
// two opposite swings of at least 0.1 in relative (linear) luminance. Counted the same way as the
// JavaScript test: a swing is one unbroken rise or fall, which is stricter than counting single
// steps.
static int flashes_per_second(const std::vector<double>& shown, double step_ms) {
  struct Swing {
    double at, size;
  };
  std::vector<double> lin;
  lin.reserve(shown.size());
  for (const double v : shown) {
    lin.push_back(std::pow(v, 2.2));
  }
  std::vector<Swing> swings;
  std::size_t start = 0;
  int dir = 0;
  const auto sign = [](double x) { return x > 0 ? 1 : x < 0 ? -1 : 0; };
  for (std::size_t i = 1; i < lin.size(); i++) {
    const int d = sign(lin[i] - lin[i - 1]);
    if (d != 0 && dir != 0 && d != dir) {
      swings.push_back({static_cast<double>(i) * step_ms, lin[i - 1] - lin[start]});
      start = i - 1;
    }
    if (d != 0) {
      dir = d;
    }
  }
  swings.push_back({static_cast<double>(lin.size()) * step_ms, lin.back() - lin[start]});
  std::vector<double> flashes;
  for (std::size_t i = 1; i < swings.size(); i++) {
    if (std::fabs(swings[i].size) >= 0.1 && std::fabs(swings[i - 1].size) >= 0.1 &&
        sign(swings[i].size) != sign(swings[i - 1].size)) {
      flashes.push_back(swings[i].at);
    }
  }
  int best = 0;
  for (double t : flashes) {
    best = std::max(
        best, static_cast<int>(std::count_if(flashes.begin(), flashes.end(),
                                             [&](double u) { return u >= t && u < t + 1000; })));
  }
  return best;
}

static void guard_tests() {
  auto dark = guard_run(repeat(0.2, 40), 100);
  check("guard: a dark picture is left alone (after the first moment)",
        dark.shown[0] < 0.2 &&
            std::all_of(
                dark.shown.begin() + 2, dark.shown.end(), [](double s) { return near(s, 0.2); }));
  auto bright = guard_run(repeat(0.95, 80), 100);
  check("guard: a bright steady picture is held at the cap",
        *std::ranges::max_element(bright.shown) <= 0.45 + 1e-9 &&
            near(bright.shown.back(), 0.45, 1e-6));
  auto calm_run = guard_run(repeat(0.95, 80), 100, true);
  check("guard: the calm cap is lower", *std::ranges::max_element(calm_run.shown) <= 0.35 + 1e-9 &&
                                            near(calm_run.shown.back(), 0.35, 1e-6));
  for (const bool calm : {false, true}) {
    const double limit = calm ? 0.5 : 1.0;
    auto r = guard_run(strobe(200), 100, calm);
    bool ok = true;
    for (std::size_t i = 1; i < r.shown.size(); i++) {
      if (r.shown[i] - r.shown[i - 1] > limit * 0.1 + 1e-9) {
        ok = false;
      }
    }
    check(std::string("guard: a strobe cannot get brighter faster than the limit") +
              (calm ? " (calm)" : ""),
          ok);
  }
  auto jump = guard_run({0, 0, 0, 0.9, 0.9, 0.9, 0.9, 0.9, 0.9}, 100);
  check("guard: a jump from black to bright is brought up over a moment",
        jump.shown[3] < 0.2 && jump.shown[8] > jump.shown[3]);
  auto darker = guard_run({0.9, 0.9, 0.9, 0.9, 0.9, 0.9, 0.9, 0.05}, 100);
  check("guard: getting darker is at once", near(darker.shown[7], 0.05));
  std::string worst;
  for (const double step_ms :
       {1.0, 5.0, 8.0, 16.0, 20.0, 33.0, 50.0, 100.0, 200.0, 250.0, 333.0, 500.0, 1000.0}) {
    for (const bool calm : {false, true}) {
      std::vector<double> levels;
      for (int i = 0; i < static_cast<int>(std::min(8000.0, std::round(20000 / step_ms))); i++) {
        levels.push_back(i % 2 ? 0.95 : 0.0);
      }
      if (flashes_per_second(guard_run(levels, step_ms, calm).shown, step_ms) > 3) {
        worst = "step " + std::to_string(step_ms) + (calm ? " calm" : "");
      }
    }
  }
  check("guard: a strobe at any speed shows at most 3 flashes in any second", worst.empty(), worst);
  std::vector<double> raw;
  raw.reserve(100);
  for (int i = 0; i < 100; i++) {
    raw.push_back(i % 2 ? 0.95 : 0);
  }
  check("guard: without the guard the same strobe shows more than 3 flashes a second",
        flashes_per_second(raw, 100) > 3);
  {
    FlashGuard g;
    bool ok = true;
    int i = 0;
    for (const double l : {std::nan(""), std::numeric_limits<double>::infinity(),
                           -std::numeric_limits<double>::infinity(), -5.0, 7.0, 0.5}) {
      const double x = g.feed(l, i++ * 100.0);
      ok = ok && x >= 0 && x <= 1 && std::isfinite(x);
    }
    ok = ok && std::isfinite(g.feed(0.5, std::nan(""))) &&
         std::isfinite(g.feed(0.5, std::numeric_limits<double>::infinity()));
    check("guard: gains stay between 0 and 1 for bad readings and bad clocks", ok);
  }
  {
    FlashGuard g;
    g.feed(0, 0);
    const double gain = g.feed(0.95, 600000);
    check("guard: a long pause does not let it jump", 0.95 * gain <= 0.5 + 1e-9);
  }
}

static Bytes bins(int n, int (*f)(int)) {
  Bytes b(static_cast<std::size_t>(n));
  for (int i = 0; i < n; i++) {
    b[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(f(i));
  }
  return b;
}
static Bytes filled(int n, int v) {
  return Bytes(static_cast<std::size_t>(n), static_cast<std::uint8_t>(v));
}
static const Bytes none;

static void audio_tests() {
  {
    AudioFeatures f;
    f.update(filled(512, 0), filled(1024, 128), 44100, 0);
    check("silence is zero",
          f.bass == 0 && f.beat == 0 &&
              std::ranges::all_of(f.wave, [](float v) { return std::fabs(v) < 1e-9; }) &&
              std::ranges::all_of(f.spec, [](float v) { return v == 0; }));
  }
  {
    AudioFeatures low;
    AudioFeatures high;
    Bytes low_tone = bins(512, [](int k) { return k < 6 ? 255 : 0; });
    Bytes high_tone = bins(512, [](int k) { return k >= 100 && k < 250 ? 255 : 0; });
    for (int i = 0; i < 20; i++) {
      low.update(low_tone, none, 44100, i * 16);
      high.update(high_tone, none, 44100, i * 16);
    }
    check("a low tone is bass, a high one is treble",
          low.bass > 0.7 && low.treb < 0.05 && high.treb > 0.4 && high.bass < 0.05);
    check("the 64 bars follow the pitch, low to high",
          low.spec[1] > low.spec[40] && std::all_of(low.spec.begin() + 50, low.spec.end(),
                                                    [](float v) { return v < 0.05F; }));
  }
  {
    AudioFeatures f;
    Bytes quiet = bins(512, [](int) { return 20; });
    for (int i = 0; i < 100; i++) {
      f.update(quiet, none, 44100, i * 16);
    }
    const double calm_att = f.bass_att;
    f.update(bins(512, [](int k) { return k < 6 ? 255 : 20; }), none, 44100, 2000);
    check("bass_att is a slow average, so a hit stands out from it",
          f.bass - f.bass_att > 0.2 && calm_att < 0.2);
  }
  {
    int beats = 0;
    AudioFeatures g;
    Bytes hit = bins(512, [](int k) { return k < 6 ? 255 : 5; });
    Bytes rest = filled(512, 5);
    for (int t = 0; t < 10000; t += 20) {
      g.update((t % 100) < 40 ? hit : rest, none, 44100, t);
      if (g.beat == 1) {
        beats++;
      }
    }
    check("a beat is never faster than every 400 ms", beats > 3 && beats <= 25,
          std::to_string(beats));
  }
  {
    AudioFeatures f;
    f.update(bins(512, [](int k) { return k < 6 ? 255 : 0; }), none, 44100, 1000);
    const double first = f.beat;
    f.update(filled(512, 0), none, 44100, 1016);
    check("the beat fades after it happens", first == 1 && f.beat < 1 && f.beat > 0.5);
  }
  {
    AudioFeatures f;
    f.update(filled(512, 0), filled(1024, 255), 44100, 0);
    const float hi = f.wave[10];
    f.update(filled(512, 0), filled(1024, 0), 44100, 16);
    check("the waveform is -1 to 1", near(hi, 1, 1e-6) && near(f.wave[10], -1, 1e-6));
  }
  {
    AudioFeatures f;
    AudioFeatures loud;
    Bytes quiet = bins(512, [](int k) { return k < 6 ? 100 : 0; });
    for (int i = 0; i < 30; i++) {
      f.update(quiet, none, 44100, i * 16);
      loud.update(filled(512, 255), none, 44100, i * 16);
    }
    const double raw = band_energies(quiet, 44100).bass;
    check("everything is heard 20% louder, capped at 1",
          near(f.bass, raw * 1.2, 0.01) && loud.bass <= 1 && loud.level <= 1 && sensitivity == 1.2);
  }
  {
    AudioFeatures f;
    for (int i = 0; i < 10; i++) {
      f.update(filled(512, 200), filled(1024, 255), 44100, i * 16);
    }
    const double loud = f.bass;
    for (int i = 0; i < 200; i++) {
      f.update(none, none, 44100, 1000 + i * 16);
    }
    check("with no analyser everything fades to calm",
          loud > 0.5 && f.bass < 0.01 && f.beat < 0.01 && std::fabs(f.wave[5]) < 0.01);
  }
  {
    AudioFeatures f;
    f.update(filled(4, 255), filled(4, 255), 44100, 0);
    f.update(filled(1, 255), filled(1, 255), 0, 16);
    bool fin = std::isfinite(f.bass) && std::isfinite(f.mid) && std::isfinite(f.treb) &&
               std::isfinite(f.level);
    for (const float v : f.spec) {
      fin = fin && std::isfinite(v);
    }
    check("a tiny array does not break it", fin);
  }
  check("an empty spectrum has no bands and does not crash", [] {
    const BandEnergies e = band_energies(none, 44100);
    (void)e;
    return true;
  }());
}

static Bytes build(const Value* spec) {
  if (!spec || spec->is_null()) {
    return {};
  }
  const auto num = [&](const char* k) {
    const Value* v = spec->find(k);
    return v ? v->as_number() : 0.0;
  };
  const std::string kind = spec->find("k")->as_string();
  const int n = static_cast<int>(num("n"));
  Bytes out(static_cast<std::size_t>(n));
  if (kind == "fill") {
    std::ranges::fill(out, static_cast<std::uint8_t>(num("v")));
  } else if (kind == "band") {
    for (int i = 0; i < n; i++) {
      out[static_cast<std::size_t>(i)] =
          static_cast<std::uint8_t>(i >= num("a") && i < num("b") ? num("v") : num("c"));
    }
  } else if (kind == "ramp") {
    for (int i = 0; i < n; i++) {
      out[static_cast<std::size_t>(i)] =
          static_cast<std::uint8_t>(std::floor(i * 255.0 / std::max(1, n - 1)));
    }
  } else if (kind == "noise") {
    auto s = static_cast<std::uint32_t>(num("s"));
    const std::uint32_t m = static_cast<std::uint32_t>(num("m")) + 1;
    for (int i = 0; i < n; i++) {
      s = s * 1664525U + 1013904223U;
      out[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((s >> 24) % m);
    }
  }
  return out;
}

// The JSON writer turns a number that is not finite into null, the same as JSON.stringify did on
// the recording.
static bool agree(const Value* want, double got, double eps, std::string& why,
                  const std::string& at) {
  if (!want) {
    why = at + ": missing";
    return false;
  }
  if (!std::isfinite(got)) {
    if (want->is_null()) {
      return true;
    }
    why = at + ": got a non-number";
    return false;
  }
  if (!want->is_number() ||
      std::fabs(want->as_number() - got) > eps * std::max(1.0, std::fabs(got))) {
    why = at + ": " + (want->is_number() ? std::to_string(want->as_number()) : "null") + " vs " +
          std::to_string(got);
    return false;
  }
  return true;
}

static bool replay(const Value& c, std::string& why) {
  const std::string kind = c.find("kind")->as_string();
  if (kind == "guard") {
    FlashGuard g;
    const auto& steps = c.find("steps")->items();
    const auto& gains = c.find("gains")->items();
    for (std::size_t i = 0; i < steps.size(); i++) {
      g.set_calm(steps[i].items()[2].as_bool());
      const double gain = g.feed(steps[i].items()[0].as_number(), steps[i].items()[1].as_number());
      if (!agree(&gains[i], gain, 1e-9, why, "gain " + std::to_string(i))) {
        return false;
      }
    }
    return true;
  }
  if (kind == "bands") {
    const BandEnergies e = band_energies(build(c.find("bytes")), c.find("rate")->as_number());
    const Value* o = c.find("out");
    return agree(o->find("bass"), e.bass, 1e-9, why, "bass") &&
           agree(o->find("mid"), e.mid, 1e-9, why, "mid") &&
           agree(o->find("treb"), e.treb, 1e-9, why, "treb") &&
           agree(o->find("level"), e.level, 1e-9, why, "level");
  }
  AudioFeatures f;
  const double rate = c.find("rate")->as_number();
  const auto& steps = c.find("steps")->items();
  const auto& out = c.find("out")->items();
  for (std::size_t i = 0; i < steps.size(); i++) {
    f.update(build(steps[i].find("f")), build(steps[i].find("t")), rate,
             steps[i].find("ms")->as_number());
    const Value& o = out[i];
    const std::string at = "step " + std::to_string(i) + " ";
    if (!(agree(o.find("bass"), f.bass, 1e-9, why, at + "bass") &&
          agree(o.find("mid"), f.mid, 1e-9, why, at + "mid") &&
          agree(o.find("treb"), f.treb, 1e-9, why, at + "treb") &&
          agree(o.find("level"), f.level, 1e-9, why, at + "level") &&
          agree(o.find("bass_att"), f.bass_att, 1e-9, why, at + "bass_att") &&
          agree(o.find("mid_att"), f.mid_att, 1e-9, why, at + "mid_att") &&
          agree(o.find("treb_att"), f.treb_att, 1e-9, why, at + "treb_att") &&
          agree(o.find("beat"), f.beat, 1e-9, why, at + "beat"))) {
      return false;
    }
  }
  const auto& wave = c.find("wave")->items();
  const auto& spec = c.find("spec")->items();
  for (std::size_t i = 0; i < wave.size(); i++) {
    if (!agree(&wave[i], f.wave[i], 1e-6, why, "wave " + std::to_string(i))) {
      return false;
    }
  }
  for (std::size_t i = 0; i < spec.size(); i++) {
    if (!agree(&spec[i], f.spec[i], 1e-6, why, "spec " + std::to_string(i))) {
      return false;
    }
  }
  return true;
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
    if (!replay(parsed.value, why)) {
      bad++;
      if (bad <= 10) {
        check("parity: case " + std::to_string(cases) + " (" +
                  parsed.value.find("kind")->as_string() + ")",
              false, why);
      }
    }
  }
  check("parity: " + std::to_string(cases) + " cases, " + std::to_string(bad) +
            " differ from the JavaScript",
        cases > 400 && bad == 0);
}

void hearing_tests(const std::string& dir) {
  test::section("hearing");
  guard_tests();
  audio_tests();
  parity(dir + "/hearing.golden");
}
