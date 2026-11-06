// Draws smaller on a slow machine and bigger again when there is room; the rule is frame() in
// falsehorizon.js: every 90 frames, an average over 30 ms scales to 80% (down to half), under 18 ms
// to 115% (up to full). Not used in calm mode.
#pragma once

#include <algorithm>

namespace fh {

class AdaptiveScale {
 public:
  // Call once per drawn frame with the time since the last one. Returns true when the scale just
  // changed (so the picture has to be resized).
  bool frame(double dt_ms, bool calm) {
    avg_dt_ += (std::min(dt_ms, 200.0) - avg_dt_) * 0.05;
    if (calm || ++slow_frames_ < 90) {
      return false;
    }
    slow_frames_ = 0;
    const double before = scale_;
    if (avg_dt_ > 30 && scale_ > 0.5) {
      scale_ = std::max(0.5, scale_ * 0.8);
    } else if (avg_dt_ < 18 && scale_ < 1) {
      scale_ = std::min(1.0, scale_ * 1.15);
    }
    return scale_ != before;
  }
  [[nodiscard]] double scale() const { return scale_; }
  [[nodiscard]] double average_ms() const { return avg_dt_; }

 private:
  double scale_ = 1, avg_dt_ = 16;
  int slow_frames_ = 0;
};

}  // namespace fh
