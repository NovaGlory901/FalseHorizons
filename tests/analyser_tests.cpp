// Tests for the FFT and the analyser (analyser.hpp).
#include "check.hpp"
#include "fh/analyser.hpp"
#include "fh/audio.hpp"
#include "tests.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <string>

using namespace fh;

using test::check;

static std::vector<float> sine(double hz, double rate, std::size_t n, double amp = 1.0,
                               double phase = 0) {
  std::vector<float> v(n);
  for (std::size_t i = 0; i < n; i++) {
    v[i] = static_cast<float>(
        amp * std::sin(2 * std::numbers::pi * hz * static_cast<double>(i) / rate + phase));
  }
  return v;
}
static std::size_t peak(const std::vector<std::uint8_t>& s) {
  return static_cast<std::size_t>(std::ranges::max_element(s) - s.begin());
}

void analyser_tests() {
  test::section("analyser");
  {
    const std::size_t n = 64;
    std::vector<std::complex<double>> a(n);
    std::vector<std::complex<double>> b(n);
    for (std::size_t i = 0; i < n; i++) {
      a[i] = {std::sin(i * 0.37) + 0.5 * std::cos(i * 1.9), std::cos(i * 0.11)};
    }
    for (std::size_t k = 0; k < n; k++) {
      for (std::size_t i = 0; i < n; i++) {
        b[k] += a[i] * std::polar(1.0, -2 * std::numbers::pi * static_cast<double>(k * i) /
                                           static_cast<double>(n));
      }
    }
    fft(a);
    double worst = 0;
    for (std::size_t k = 0; k < n; k++) {
      worst = std::max(worst, std::abs(a[k] - b[k]));
    }
    check("fft: matches the plain DFT", worst < 1e-9, std::to_string(worst));
    std::vector<std::complex<double>> bad(48);
    bool threw = false;
    try {
      fft(bad);
    } catch (const std::invalid_argument&) {
      threw = true;
    }
    check("fft: a size that is not a power of two is refused", threw);
  }
  // Tones are quiet (0.05) so they stay below the 255 ceiling; at full scale the top of the peak is
  // flat and its first bin is not the middle.
  for (const double hz : {100.0, 440.0, 1000.0, 5000.0, 12000.0}) {
    Analyser an;
    std::vector<std::uint8_t> sp;
    std::vector<std::uint8_t> wf;
    for (int i = 0; i < 6; i++) {
      an.push(sine(hz, 44100, 2048, 0.05, i));
      an.read(sp, wf);
    }
    const double bin_hz = 44100.0 / 2048;
    check("a " + std::to_string(static_cast<int>(hz)) + " Hz tone peaks at its bin",
          std::fabs(static_cast<double>(peak(sp)) * bin_hz - hz) <= bin_hz,
          "peak bin " + std::to_string(peak(sp)));
    check("a " + std::to_string(static_cast<int>(hz)) +
              " Hz tone is loud at its bin and quiet far away",
          sp[peak(sp)] > 150 && sp[(peak(sp) + 300) % 1024] < 40);
    check("sizes are 1024 bins and 2048 waveform bytes", sp.size() == 1024 && wf.size() == 2048);
  }
  {
    Analyser an;
    std::vector<std::uint8_t> sp;
    std::vector<std::uint8_t> wf;
    an.read(sp, wf);
    check("silence is zero in the spectrum and 128 in the waveform",
          std::ranges::all_of(sp, [](auto b) { return b == 0; }) &&
              std::ranges::all_of(wf, [](auto b) { return b == 128; }));
  }
  {
    Analyser an;
    std::vector<std::uint8_t> sp;
    std::vector<std::uint8_t> wf;
    std::vector<float> dc(2048, 1.0F);
    std::vector<float> neg(2048, -1.0F);
    an.push(dc);
    an.read(sp, wf);
    const auto hi = wf[5];
    an.push(neg);
    an.read(sp, wf);
    check("the waveform is 128 * (1 + sample), clamped to 0..255", hi == 255 && wf[5] == 0);
  }
  {
    Analyser an;
    std::vector<std::uint8_t> sp;
    std::vector<std::uint8_t> wf;
    an.push(sine(1000, 44100, 2048, 0.05));
    an.read(sp, wf);
    const auto first = sp[peak(sp)];
    for (int i = 0; i < 20; i++) {
      an.push(sine(1000, 44100, 2048, 0.05));
      an.read(sp, wf);
    }
    check("the spectrum smooths in over several frames", first < sp[peak(sp)]);
    for (int i = 0; i < 5; i++) {
      an.push(std::vector<float>(2048, 0.0F));
      an.read(sp, wf);
    }
    check("the spectrum smooths out again after the sound stops", sp[peak(sp)] < first + 60);
  }
  {
    Analyser an;
    std::vector<std::uint8_t> sp;
    std::vector<std::uint8_t> wf;
    std::vector<float> junk(5000, std::nanf(""));
    an.push(junk);
    an.read(sp, wf);
    check("samples that are not numbers count as silence",
          std::ranges::all_of(sp, [](auto b) { return b == 0; }));
    an.push(std::vector<float>(100000, 0.5F));
    an.read(sp, wf);
    check("pushing far more than it keeps is fine", wf.size() == 2048);
  }
  {
    Analyser an;
    AudioFeatures low;
    AudioFeatures high;
    std::vector<std::uint8_t> sp;
    std::vector<std::uint8_t> wf;
    // a continuous tone, pushed 1024 samples at a time (a phase jump between pushes would add noise
    // of its own)
    const auto chunk = [](double hz, int i) {
      std::vector<float> v(1024);
      for (int k = 0; k < 1024; k++) {
        v[static_cast<std::size_t>(k)] =
            static_cast<float>(0.05 * std::sin(2 * std::numbers::pi * hz * (i * 1024 + k) / 44100));
      }
      return v;
    };
    for (int i = 0; i < 20; i++) {
      an.push(chunk(100, i));
      an.read(sp, wf);
      low.update(sp, wf, 44100, i * 16);
    }
    an.reset();
    for (int i = 0; i < 20; i++) {
      an.push(chunk(6000, i));
      an.read(sp, wf);
      high.update(sp, wf, 44100, i * 16);
    }
    check("a 100 Hz tone is bass and a 6 kHz tone is treble",
          low.bass > 0.3 && low.treb < 0.01 && high.treb > 0.01 && high.bass < 0.01,
          "low bass " + std::to_string(low.bass) + " treb " + std::to_string(low.treb) +
              "; high bass " + std::to_string(high.bass) + " treb " + std::to_string(high.treb));
  }
}
