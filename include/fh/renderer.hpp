// Draws a FrameState with OpenGL ES 2: the same feedback loop as the WebGL version in
// js/falsehorizon.js. Needs only a current ES 2 context, so the window can come from SDL2,
// Emscripten or the app's pop-out window. The picture lives in two textures that take turns and is
// drawn at a size derived from the window's. The fractal warp is applied to the mesh before
// drawing.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "fh/image.hpp"
#include "fh/runtime.hpp"
#include "fh/scene.hpp"

namespace fh {

using PictureId = int;  // a picture given to the renderer with add_picture; -1 means none

struct SceneDraw {
  PictureId picture = -1;
  ArtifactPlacement place;
};

// How strongly the scene goes into the picture. Tuned by eye.
constexpr double scene_into_loop = 0.30;  // small: the warp has to win
constexpr double scene_direct =
    0.20;  // how much of them is shown unwarped on top, rippling with the music
constexpr double background_into_loop =
    2.5;  // the background is added to the feedback each frame at
          // this multiple of the web version's album-art strength
constexpr double background_direct = 0.8;  // and shown (added to the finished picture) at this
                                           // strength, brighter where the visuals are

struct SceneInputs {
  PictureId background_a = -1,
            background_b = -1;  // the background, and the one fading in over it (-1 for none)
  double background_mix = 0;  // 0..1: how far B has faded in
  double background_amount = 0;  // 0..1: how present the background is (0 = not at all)
  std::vector<SceneDraw> artifacts;  // drawn in this order, the last on top
  double wobble = 0;  // 0..1, how much the shown-unwarped artifacts ripple (use the bass)
  double time = 0;  // seconds, for the ripple
};

class Renderer {
 public:
  Renderer();
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  Renderer(Renderer&&) = delete;
  Renderer& operator=(Renderer&&) = delete;

  // Builds the shaders and buffers. Returns false (with a reason a person can read) if the GPU
  // cannot do it.
  bool init(std::string& error);

  // Tells it how big the window is (in pixels). The picture is drawn at about 60% of that, between
  // 160 and 960 pixels wide, times `scale` (1 = normal; lower on a slow device). Changing the size
  // clears the picture. Call once before drawing, and again when the window changes.
  void resize(int window_width, int window_height, double scale = 1.0);

  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;
  [[nodiscard]] double aspect() const;  // width / height, what FrameInputs::aspect should be

  // Gives the renderer a picture to use as a background or an artifact. It is copied to the
  // graphics card; an animated one starts at its first frame. Returns -1 if the card could not take
  // it.
  PictureId add_picture(std::shared_ptr<const Image> image);
  void remove_picture(PictureId id);
  // Moves every animated picture on to the frame that should be showing at `time_ms` (any steadily
  // increasing clock). Call once a frame.
  void animate_pictures(double time_ms);

  // Draws one frame into the window. `gain` (0..1) is the flash guard's answer: the picture is
  // dimmed by it.
  void draw(const FrameState& state, double gain, const SceneInputs& scene = {});

  // The average brightness (0..1, perceptual weights) of the picture drawn last. Worked out on the
  // GPU and read back as a 16 x 9 image, which costs a little, so ask about every sixth frame. Feed
  // it to FlashGuard::feed.
  double measure();

  // Reads the window's current pixels (as RGBA, bottom row first) for a screenshot. Call after
  // draw() and before swapping.
  bool read_window(std::vector<std::uint8_t>& rgba, int& width, int& height);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fh
