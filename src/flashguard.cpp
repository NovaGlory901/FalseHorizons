#include "fh/flashguard.hpp"

#include <algorithm>
#include <cmath>

namespace fh {

double FlashGuard::feed(double luminance, double now_ms) {
  const double l = std::clamp(std::isfinite(luminance) ? luminance : 0.0, 0.0, 1.0);
  double dt = 0.1;
  if (started_) {
    const double gap = (now_ms - last_) / 1000;
    dt = std::isfinite(gap) ? std::clamp(gap, 0.0, 0.5)
                            : 0.0;  // a clock that goes backwards counts as no time passing
  }
  started_ = true;
  if (std::isfinite(now_ms)) {
    last_ = now_ms;
  }
  const double allowed = std::min(cap_, shown_ + rise_ * dt);
  if (l <= allowed) {
    shown_ = l;
    gain_ = 1;
  } else {
    shown_ = allowed;
    gain_ = allowed / l;
  }
  return gain_;
}

}  // namespace fh
