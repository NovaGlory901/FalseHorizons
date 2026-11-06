// Keeps a preset (or a mistake in one) from strobing. Port of FlashGuard in
// js/falsehorizon-core.js. Fed the average brightness, it returns a gain that caps the brightness
// and how fast it may rise, so full flashes stay under three a second (WCAG 2.3.1). Getting darker
// is always instant.
#pragma once

namespace fh {

class FlashGuard {
 public:
  explicit FlashGuard(bool calm = false) { set_calm(calm); }

  void set_calm(bool calm) {
    cap_ = calm ? 0.35 : 0.45;
    rise_ = calm ? 0.5 : 1.0;
  }

  // `luminance`: the picture's average brightness. `now_ms`: any steadily increasing clock. A
  // reading that is not a number counts as 0. A gap longer than half a second (a hidden tab) counts
  // as half a second, so a pause cannot let the picture jump.
  double feed(double luminance, double now_ms);

  [[nodiscard]] double gain() const { return gain_; }
  [[nodiscard]] double shown() const { return shown_; }
  [[nodiscard]] double cap() const { return cap_; }
  [[nodiscard]] double rise() const { return rise_; }

 private:
  double cap_ = 0.45, rise_ = 1.0;
  bool started_ = false;
  double last_ = 0, shown_ = 0, gain_ = 1;
};

}  // namespace fh
