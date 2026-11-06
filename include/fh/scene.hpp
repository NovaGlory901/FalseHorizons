// Where FalseHorizon's artifacts are and how they move: pure arithmetic of time, no state and no
// drawing, so the same moment gives the same place and it can be tested.
#pragma once

#include <cstddef>
#include <cstdint>

namespace fh {

struct ArtifactPlacement {
  double x = 0.5,
         y = 0.5;  // middle of the artifact as a fraction of the picture, (0, 0) bottom left
  double width =
      0.2;  // its width as a fraction of the picture's width (its height keeps the picture's shape)
  double rotation = 0;  // radians, counterclockwise
  double alpha = 1;  // 0..1
};

class ArtifactField {
 public:
  explicit ArtifactField(std::uint32_t seed = 1) : seed_(seed) {}

  // Where artifact number `index` is `time_seconds` into the show. `bass` and `beat` (0..1) are
  // what the music is doing.
  [[nodiscard]] ArtifactPlacement place(std::size_t index, double time_seconds, double bass,
                                        double beat) const;

 private:
  std::uint32_t seed_;
};

// 0 at the start of a fade, 1 at the end, easing in and out.
double smooth_fade(double elapsed_ms, double duration_ms);

}  // namespace fh
