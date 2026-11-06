// Fractal warp: a height field blending a ring wave and a Mandelbrot surface, added to the warp
// mesh so the feedback loop pulls the picture over it. Port of fractalWarp in
// js/falsehorizon-core.js (tests/parity/fractal.golden).
#pragma once

#include <vector>

#include "fh/runtime.hpp"

namespace fh {

constexpr double morph_speed =
    0.35;  // radians a second: one full wave -> fractal -> wave round trip takes about 18 seconds
constexpr double warp_pull = 0.15;  // about as strong as a preset's own warp

double modulation_index(double time, double speed = morph_speed);
double base_height(double gx, double gy, double time);
double fractal_height(double cr, double ci);  // 0..1; 1 for points that never escape

// Adds the surface to `mesh` in place. `amount` is the user's geometry setting (nothing below
// 0.01); `calm` slows the cycle.
void fractal_warp(std::vector<float>& mesh, const FrameInputs& in, double amount, bool calm);

}  // namespace fh
