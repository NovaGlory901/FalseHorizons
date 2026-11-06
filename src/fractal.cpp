#include "fh/fractal.hpp"

#include <math.h>

#include <algorithm>
#include <cmath>

namespace fh {

namespace {
constexpr int max_iter = 24;
constexpr double bailout = 256;

double finite_or(double v, double fallback) { return std::isfinite(v) ? v : fallback; }

// cheap, jumpy, between -1 and 1 (the JavaScript's `a * b % 1` is fmod)
double noise2(double x, double y, double t) {
  return std::fmod(std::sin(x * 12.9898 + y * 78.233 + t * 3.1) * 43758.5453, 1.0);
}
}  // namespace

double modulation_index(double time, double speed) {
  return (std::sin(finite_or(time, 0) * speed) + 1) * 0.5;
}

double base_height(double gx, double gy, double time) {
  return 0.5 * std::sin(9 * std::hypot(gx, gy) - time * 1.3);
}

double fractal_height(double cr, double ci) {
  double zr = 0;
  double zi = 0;
  double r2 = 0;
  int i = 0;
  while (i < max_iter && (r2 = zr * zr + zi * zi) <= bailout * bailout) {
    const double t = zr * zr - zi * zi + cr;
    zi = 2 * zr * zi + ci;
    zr = t;
    i++;
  }
  if (i >= max_iter) {
    return 1;
  }
  // i + 1 - log2(log2|z|): a smooth number between whole steps
  return std::clamp((i + 1 - std::log2(std::log2(std::max(r2, 4.0)) / 2)) / max_iter, 0.0, 1.0);
}

void fractal_warp(std::vector<float>& mesh, const FrameInputs& in, double amount, bool calm) {
  if (!(amount > 0.01) || mesh.size() < mesh_floats) {
    return;
  }
  const double time = finite_or(in.time, 0);
  const double aspect = (std::isfinite(in.aspect) && in.aspect != 0) ? in.aspect : 1;
  const double bass = finite_or(in.bass, 0);
  const double mid = finite_or(in.mid, 0);
  const double m = modulation_index(time, calm ? morph_speed / 4 : morph_speed);
  const double glitch = m * m * m * (calm ? 0.3 : 1);
  const double yaw = 0.5 * std::sin(time * 0.23) + mid * 0.5;
  const double pitch = 0.45 + 0.25 * std::sin(time * 0.17);
  const double roll = time * 0.07;
  const double cy = std::cos(yaw);
  const double sy = std::sin(yaw);
  const double cp = std::cos(pitch);
  const double sp = std::sin(pitch);
  const double cr = std::cos(roll);
  const double sr = std::sin(roll);
  const double lift = 0.66 * (1 + 0.7 * bass);
  const double cam = 2.35;
  const double pull = warp_pull * amount;
  const double view = 1.5 + 0.4 * std::sin(time * 0.11);
  std::size_t k = 0;
  for (int j = 0; j <= mesh_h; j++) {
    for (int i = 0; i <= mesh_w; i++, k += 2) {
      const double x = static_cast<double>(i) / mesh_w;
      const double y = static_cast<double>(j) / mesh_h;
      const double gx = (x - 0.5) * aspect;
      const double gy = y - 0.5;
      const int col = std::min(255, std::max(0, static_cast<int>(std::floor(x * 255))));
      const double w = static_cast<std::size_t>(col) < in.wave.size()
                           ? static_cast<double>(in.wave[static_cast<std::size_t>(col)])
                           : 0.0;
      const double shake =
          glitch * (w * (0.15 + 0.5 * bass) + 0.12 * noise2(i, j, std::floor(time * 8)));
      const double zb = base_height(gx, gy, time);
      const double zf = fractal_height(-0.7 + (gx * cr - gy * sr) * view + shake,
                                       (gx * sr + gy * cr) * view + shake * 0.8) -
                        0.5;
      double px = gx;
      double py = gy;
      double z = ((1 - m) * zb + m * zf) * lift;
      double t = NAN;
      t = px * cy + z * sy;
      z = -px * sy + z * cy;
      px = t;
      t = py * cp - z * sp;
      z = py * sp + z * cp;
      py = t;
      const double s = cam / (cam - z);
      const double dive = 0.2 * m * pull;
      mesh[k] =
          static_cast<float>(mesh[k] + ((px * s) / aspect + 0.5 - x) * pull + (x - 0.5) * dive);
      mesh[k + 1] = static_cast<float>(mesh[k + 1] + (py * s + 0.5 - y) * pull + (y - 0.5) * dive);
    }
  }
}

}  // namespace fh
