#include "webp.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <utility>

#include <webp/decode.h>
#include <webp/demux.h>

namespace fh {

bool looks_like_webp(std::span<const std::uint8_t> b) {
  return b.size() >= 12 && std::memcmp(b.data(), "RIFF", 4) == 0 &&
         std::memcmp(b.data() + 8, "WEBP", 4) == 0;
}

namespace {
ImageResult fail(const std::string& why) {
  ImageResult r;
  r.error = why;
  return r;
}
}  // namespace

ImageResult decode_webp(std::span<const std::uint8_t> bytes, const ImageLimits& lim) {
  WebPBitstreamFeatures features;
  if (WebPGetFeatures(bytes.data(), bytes.size(), &features) != VP8_STATUS_OK) {
    return fail("That WebP picture is damaged.");
  }
  const int w = features.width;
  const int h = features.height;
  if (w <= 0 || h <= 0) {
    return fail("That picture has no size.");
  }
  if (static_cast<long long>(w) * h > lim.max_pixels) {
    return fail("That picture is too large (" + std::to_string(w) + " x " + std::to_string(h) +
                " pixels).");
  }

  WebPAnimDecoder* dec = nullptr;
  const WebPData data{bytes.data(), bytes.size()};
  int frame_count = 1;
  if (features.has_animation) {
    WebPAnimDecoderOptions opts;
    if (!WebPAnimDecoderOptionsInit(&opts)) {
      return fail("That WebP picture could not be read.");
    }
    opts.color_mode = MODE_RGBA;  // straight (not premultiplied) alpha
    opts.use_threads = 0;
    // The decoder keeps two canvases of the full size, and the frame count is known only once the
    // file's chunks are listed (no pixels read). The pixel limit above has already been applied,
    // and the demuxer only lists chunks.
    WebPDemuxer* demux = WebPDemux(&data);
    if (!demux) {
      return fail("That WebP animation is damaged.");
    }
    frame_count = static_cast<int>(WebPDemuxGetI(demux, WEBP_FF_FRAME_COUNT));
    WebPDemuxDelete(demux);
    if (frame_count < 1) {
      return fail("That WebP animation is damaged.");
    }
    if (frame_count > lim.max_frames_in_file) {
      return fail("That animation has too many frames (" + std::to_string(frame_count) +
                  ", the most allowed is " + std::to_string(lim.max_frames_in_file) + ").");
    }
  }

  const bool animated = frame_count > 1;
  const int max_side = animated ? lim.max_side_animated : lim.max_side;
  const double k =
      std::min(1.0, static_cast<double>(max_side) / static_cast<double>(std::max(w, h)));
  const int nw = std::max(1, static_cast<int>(std::lround(w * k)));
  const int nh = std::max(1, static_cast<int>(std::lround(h * k)));
  const std::size_t frame_bytes = static_cast<std::size_t>(nw) * static_cast<std::size_t>(nh) * 4;
  const int stride =
      std::max(1, (frame_count + std::max(1, lim.max_frames) - 1) /
                      std::max(1, lim.max_frames));  // a long animation keeps every stride-th frame
  const int kept_frames = (frame_count + stride - 1) / stride;
  if (frame_bytes * static_cast<std::size_t>(kept_frames) > lim.max_decoded_bytes) {
    return fail("That animation is too big to hold in memory.");
  }
  if (static_cast<unsigned long long>(w) * static_cast<unsigned long long>(h) * 4ULL *
          static_cast<unsigned long long>(frame_count) >
      3ULL * lim.max_decoded_bytes) {
    return fail("That animation is too big to decode.");
  }

  ImageResult res;
  Image& img = res.image;
  img.width = nw;
  img.height = nh;
  const auto keep = [&](const std::uint8_t* rgba, int fw, int fh) {
    if (fw == nw && fh == nh) {
      img.frames.emplace_back(rgba, rgba + frame_bytes);
    } else {
      img.frames.push_back(
          shrink_rgba(std::span<const std::uint8_t>(
                          rgba, static_cast<std::size_t>(fw) * static_cast<std::size_t>(fh) * 4),
                      fw, fh, nw, nh));
    }
  };

  if (features.has_animation) {
    WebPAnimDecoderOptions opts;
    WebPAnimDecoderOptionsInit(&opts);
    opts.color_mode = MODE_RGBA;
    opts.use_threads = 0;
    dec = WebPAnimDecoderNew(&data, &opts);
    if (!dec) {
      return fail("That WebP animation could not be read.");
    }
    WebPAnimInfo info;
    if (!WebPAnimDecoderGetInfo(dec, &info) || std::cmp_not_equal(info.canvas_width, w) ||
        std::cmp_not_equal(info.canvas_height, h)) {
      WebPAnimDecoderDelete(dec);
      return fail("That WebP animation could not be read.");
    }
    int previous = 0;
    int index = 0;  // skipped frames' time is added to the kept one before them
    while (WebPAnimDecoderHasMoreFrames(dec) && index < frame_count) {
      std::uint8_t* canvas = nullptr;
      int timestamp = 0;
      if (!WebPAnimDecoderGetNext(dec, &canvas, &timestamp)) {
        WebPAnimDecoderDelete(dec);
        return fail("That WebP animation is damaged.");
      }
      const int d = timestamp - previous;
      previous = timestamp;
      const int shown = d < 20 ? 100 : d;
      if (index % stride == 0) {
        if (std::cmp_greater_equal(img.frames.size(), kept_frames)) {
          break;
        }
        keep(canvas, w, h);
        img.delays_ms.push_back(shown);
      } else if (!img.delays_ms.empty()) {
        img.delays_ms.back() += shown;
      }
      index++;
    }
    WebPAnimDecoderDelete(dec);
    if (img.frames.empty()) {
      return fail("That WebP animation could not be read.");
    }
  } else {
    int pw = 0;
    int ph = 0;
    std::uint8_t* pixels = WebPDecodeRGBA(bytes.data(), bytes.size(), &pw, &ph);
    if (!pixels) {
      return fail("That WebP picture could not be read.");
    }
    if (pw != w || ph != h) {
      WebPFree(pixels);
      return fail("That WebP picture could not be read.");
    }
    keep(pixels, pw, ph);
    WebPFree(pixels);
    img.delays_ms.push_back(100);
  }
  res.ok = true;
  return res;
}

}  // namespace fh
