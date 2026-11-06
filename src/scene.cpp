#include "fh/scene.hpp"

#include <algorithm>
#include <cmath>

namespace fh {

namespace {

// A number from (seed, index, which) that looks random but is always the same.
double unit(std::uint32_t seed, std::size_t index, std::uint32_t which) {
  std::uint32_t h =
      seed * 2654435761U ^ static_cast<std::uint32_t>(index) * 2246822519U ^ which * 3266489917U;
  h ^= h >> 15;
  h *= 2246822519U;
  h ^= h >> 13;
  h *= 3266489917U;
  h ^= h >> 16;
  return static_cast<double>(h) / 4294967296.0;
}
double between(double lo, double hi, double u) { return lo + (hi - lo) * u; }

}  // namespace

ArtifactPlacement ArtifactField::place(std::size_t i, double t, double bass, double beat) const {
  if (!std::isfinite(t)) {
    t = 0;
  }
  bass = std::clamp(std::isfinite(bass) ? bass : 0.0, 0.0, 1.0);
  beat = std::clamp(std::isfinite(beat) ? beat : 0.0, 0.0, 1.0);
  const double u = 6.2831853071795864;
  ArtifactPlacement p;
  p.x = 0.5 + between(0.22, 0.40, unit(seed_, i, 1)) *
                  std::sin(between(0.05, 0.12, unit(seed_, i, 2)) * t + u * unit(seed_, i, 3));
  p.y = 0.5 + between(0.20, 0.36, unit(seed_, i, 4)) *
                  std::sin(between(0.04, 0.10, unit(seed_, i, 5)) * t + u * unit(seed_, i, 6));
  p.width = between(0.14, 0.30, unit(seed_, i, 7)) * (1.0 + 0.10 * bass + 0.12 * beat);
  p.rotation = between(0.10, 0.35, unit(seed_, i, 8)) *
               std::sin(between(0.05, 0.20, unit(seed_, i, 9)) * t + u * unit(seed_, i, 10));
  p.alpha = std::clamp(
      0.72 + 0.18 * std::sin(between(0.08, 0.25, unit(seed_, i, 11)) * t + u * unit(seed_, i, 12)),
      0.0, 1.0);
  return p;
}

double smooth_fade(double elapsed_ms, double duration_ms) {
  if (!(duration_ms > 0)) {
    return 1.0;
  }
  const double t = std::clamp(std::isfinite(elapsed_ms) ? elapsed_ms / duration_ms : 1.0, 0.0, 1.0);
  return t * t * (3 - 2 * t);
}

}  // namespace fh
