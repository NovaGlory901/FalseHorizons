// Reads pictures people supply (JPEG, PNG, WebP, GIF, BMP, TGA, PSD, PIC, PNM; WebP and GIF may be
// animated) into upright, straight-alpha RGBA, shrunk to what the graphics card can hold. Pictures
// are untrusted: size, dimensions, frame count and decoded size are checked against ImageLimits
// before and while decoding, and a long animation within the limits is thinned.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace fh {

struct ImageLimits {
  std::size_t max_file_bytes = 64U << 20;  // a bigger file is refused unread
  long long max_pixels = 64'000'000;  // per frame, as the file states it
  int max_side = 1024;
  int max_side_animated = 512;  // smaller because every frame is kept in memory
  int max_frames = 300;  // more frames than this are thinned, delays added up to keep the speed
  int max_frames_in_file = 2000;  // an animation with more frames than this is refused unread
  std::size_t max_decoded_bytes = 192U << 20;
};

struct Image {
  int width = 0, height = 0;
  std::vector<std::vector<std::uint8_t>>
      frames;  // width * height * 4 bytes each; one frame for a still picture
  std::vector<int>
      delays_ms;  // how long each frame is shown (at least 20 ms; a missing delay counts as 100)

  [[nodiscard]] bool animated() const { return frames.size() > 1; }
  [[nodiscard]] int total_ms() const;
  // Which frame is showing `time_ms` after the start (the animation loops). Always 0 for a still
  // picture.
  [[nodiscard]] std::size_t frame_at(double time_ms) const;
};

struct ImageResult {
  bool ok = false;
  std::string error;
  Image image;
};

ImageResult decode_image(std::span<const std::uint8_t> bytes, const ImageLimits& limits = {});
ImageResult load_image_file(const std::filesystem::path& file, const ImageLimits& limits = {});

// Shrinks an RGBA picture to new_w x new_h by averaging (alpha-aware). Exposed for tests.
std::vector<std::uint8_t> shrink_rgba(std::span<const std::uint8_t> src, int w, int h, int nw,
                                      int nh);

}  // namespace fh
