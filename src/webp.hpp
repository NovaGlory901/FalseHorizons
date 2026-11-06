// WebP pictures (still and animated) through libwebp, built only when it is found (FH_HAVE_WEBP).
// Internal to the image reader: use decode_image in fh/image.hpp.
#pragma once

#include <cstdint>
#include <span>

#include "fh/image.hpp"

namespace fh {

bool looks_like_webp(std::span<const std::uint8_t> bytes);  // "RIFF" size "WEBP"
ImageResult decode_webp(std::span<const std::uint8_t> bytes, const ImageLimits& limits);

}  // namespace fh
