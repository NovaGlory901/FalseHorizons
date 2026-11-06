// A preset running: the numbers for one frame (warp mesh, vertices to draw, colour settings). Port
// of PresetRuntime, builtinWave and blendStates in js/falsehorizon-core.js
// (tests/parity/runtime.golden). Draws nothing; the renderer does. Formula coordinates: x and y run
// 0..1, (0,0) at the bottom left.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "fh/expr.hpp"
#include "fh/preset.hpp"

namespace fh {

constexpr int mesh_w = 32,
              mesh_h =
                  24;  // the warp mesh has (mesh_w+1) x (mesh_h+1) points, each with a (u, v) pair
constexpr std::size_t mesh_floats = (mesh_w + 1) * (mesh_h + 1) * 2;

// What the formulas can read, set every frame. Anything left at its default is what the JavaScript
// version uses for "not given", except `aspect`: the JavaScript passes 0 to formulas when it is
// missing but draws as 1; here it is 1 for both, so always set it.
struct FrameInputs {
  double time = 0, frame = 0;
  double bass = 0, mid = 0, treb = 0, bass_att = 0, mid_att = 0, treb_att = 0, level = 0, beat = 0;
  double aspect = 1;
  double fps = 60, progress = 0;
  double meshx = mesh_w, meshy = mesh_h, pixelsx = 640, pixelsy = 360, aspectx = 1, aspecty = 1;
  std::span<const float>
      wave;  // 256 points, -1..1 (AudioFeatures::wave); missing or short counts as silence
  std::span<const float> spec;  // 64 bars, 0..1 (AudioFeatures::spec)
};

struct FrameOptions {
  double motion = 1;  // 0..1, how much the warp moves; 1 = as written
  double weight =
      1;  // 0..1, how visible the drawn shapes are: used while one preset fades into another
};

enum class DrawKind : std::uint8_t { Strip, Lines, Dots, Fan };

// Vertices are 6 floats each: x, y (-1..1 across the picture), r, g, b, a.
struct Draw {
  DrawKind kind = DrawKind::Strip;
  std::vector<float> verts;
  int count = 0;
  bool additive = true;
  int thick = 1;
};

struct ColorOut {
  double gamma = 1, hue = 0, bright = 1, solarize = 0, echo_alpha = 0, echo_zoom = 1,
         echo_orient = 0, cover = 0;
};

struct FrameState {
  double decay = 0.97;
  std::vector<float> mesh;  // mesh_floats values: for each grid point, row by row from the bottom,
                            // where its colour comes from (u, v)
  std::vector<Draw> draws;
  ColorOut color;
};

class PresetRuntime {
 public:
  // `preset` should have come through parsePresetFile / sanitizePreset. Throws std::runtime_error
  // if a formula does not compile anyway. `rng` is what rand() uses, shared by every formula of the
  // preset; empty means a random one.
  explicit PresetRuntime(const Preset& preset, const expr::Rng& rng = {});
  ~PresetRuntime();
  PresetRuntime(PresetRuntime&&) noexcept;
  PresetRuntime& operator=(PresetRuntime&&) noexcept;
  PresetRuntime(const PresetRuntime&) = delete;
  PresetRuntime& operator=(const PresetRuntime&) = delete;

  FrameState frame(const FrameInputs& inputs, FrameOptions opts = {});
  [[nodiscard]] const Preset& preset() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Two frames of the same picture steps (the preset fading out and the one fading in), mixed by t
// (0..1). The warps do not just trade places: in the middle of a change both act on the picture at
// once. With no `b` it is `a`; with no `a` (or t >= 1) it is `b`.
FrameState blend_states(const FrameState* a, const FrameState* b, double t);

}  // namespace fh
