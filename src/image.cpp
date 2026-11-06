#include "fh/image.hpp"

#ifdef FH_HAVE_WEBP
#include "webp.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>

#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace fh {

int Image::total_ms() const {
  int t = 0;
  for (const int d : delays_ms) {
    t += d;
  }
  return t;
}

std::size_t Image::frame_at(double time_ms) const {
  if (frames.size() < 2) {
    return 0;
  }
  const int total = total_ms();
  if (total <= 0 || !std::isfinite(time_ms)) {
    return 0;
  }
  double t = std::fmod(time_ms, static_cast<double>(total));
  if (t < 0) {
    t += total;
  }
  for (std::size_t i = 0; i < frames.size(); i++) {
    t -= delays_ms[i];
    if (t < 0) {
      return i;
    }
  }
  return frames.size() - 1;
}

std::vector<std::uint8_t> shrink_rgba(std::span<const std::uint8_t> src, int w, int h, int nw,
                                      int nh) {
  std::vector<std::uint8_t> out(static_cast<std::size_t>(nw) * static_cast<std::size_t>(nh) * 4);
  for (int y = 0; y < nh; y++) {
    const int y0 = static_cast<int>(static_cast<long long>(y) * h / nh);
    const int y1 = std::max(y0 + 1, static_cast<int>(static_cast<long long>(y + 1) * h / nh));
    for (int x = 0; x < nw; x++) {
      const int x0 = static_cast<int>(static_cast<long long>(x) * w / nw);
      const int x1 = std::max(x0 + 1, static_cast<int>(static_cast<long long>(x + 1) * w / nw));
      double r = 0;
      double g = 0;
      double b = 0;
      double a = 0;
      for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
          const std::uint8_t* p = &src[(static_cast<std::size_t>(yy) * static_cast<std::size_t>(w) +
                                        static_cast<std::size_t>(xx)) *
                                       4];
          const double al = p[3];
          r += p[0] * al;
          g += p[1] * al;
          b += p[2] * al;
          a += al;  // colours are averaged weighted by alpha, so a clear pixel does not tint its
                    // neighbours
        }
      }
      const auto n = static_cast<double>((y1 - y0) * (x1 - x0));
      std::uint8_t* o = &out[(static_cast<std::size_t>(y) * static_cast<std::size_t>(nw) +
                              static_cast<std::size_t>(x)) *
                             4];
      if (a > 0) {
        o[0] = static_cast<std::uint8_t>(std::lround(r / a));
        o[1] = static_cast<std::uint8_t>(std::lround(g / a));
        o[2] = static_cast<std::uint8_t>(std::lround(b / a));
      } else {
        o[0] = o[1] = o[2] = 0;
      }
      o[3] = static_cast<std::uint8_t>(std::lround(a / n));
    }
  }
  return out;
}

namespace {

ImageResult fail(const std::string& why) {
  ImageResult r;
  r.error = why;
  return r;
}

bool is_gif(std::span<const std::uint8_t> b) {
  return b.size() >= 10 &&
         (std::memcmp(b.data(), "GIF87a", 6) == 0 || std::memcmp(b.data(), "GIF89a", 6) == 0);
}

// Walks a GIF's blocks without decoding anything, to count the frames and learn the screen size, so
// a file with a thousand frames is refused before the decoder (which keeps every frame) is asked to
// read it. Returns false if the file is cut short or malformed.
bool scan_gif(std::span<const std::uint8_t> b, int& width, int& height, int& frames) {
  if (b.size() < 13) {
    return false;
  }
  width = b[6] | (b[7] << 8);
  height = b[8] | (b[9] << 8);
  std::size_t i = 13;
  if (b[10] & 0x80) {
    i += 3U * (1U << ((b[10] & 7) + 1));
  }
  frames = 0;
  const auto skip_sub_blocks = [&]() {
    while (i < b.size()) {
      const std::size_t n = b[i++];
      if (n == 0) {
        return true;
      }
      i += n;
    }
    return false;
  };
  while (i < b.size()) {
    const std::uint8_t tag = b[i++];
    if (tag == 0x3B) {
      return true;
    }
    if (tag == 0x21) {
      if (i >= b.size()) {
        return false;
      }
      i++;
      if (!skip_sub_blocks()) {
        return false;
      }
    }  // extension: label, then data blocks
    else if (tag == 0x2C) {  // image: descriptor, maybe a colour table, LZW size, data blocks
      if (i + 9 > b.size()) {
        return false;
      }
      const std::uint8_t flags = b[i + 8];
      i += 9;
      if (flags & 0x80) {
        i += 3U * (1U << ((flags & 7) + 1));
      }
      if (i >= b.size()) {
        return false;
      }
      i++;
      if (!skip_sub_blocks()) {
        return false;
      }
      if (++frames > 100000) {
        return true;  // far past any limit; stop counting
      }
    } else {
      return false;
    }
  }
  return frames > 0;  // no trailer, but frames: the decoder copes with that
}

// The camera's "which way up" note in a JPEG (EXIF orientation, 1..8; 1 if there is none or it is
// unreadable). Phone photos are often stored sideways with this note saying how to turn them; the
// decoder library ignores it. Every read is bounds-checked: the file is untrusted.
int exif_orientation(std::span<const std::uint8_t> b) {
  if (b.size() < 4 || b[0] != 0xFF || b[1] != 0xD8) {
    return 1;
  }
  std::size_t i = 2;
  while (i + 4 <= b.size()) {
    if (b[i] != 0xFF) {
      return 1;
    }
    const std::uint8_t marker = b[i + 1];
    if (marker == 0xFF) {
      i++;
      continue;
    }
    if (marker == 0xDA || marker == 0xD9) {
      return 1;  // image data / end: no EXIF came first
    }
    const std::size_t len = (static_cast<std::size_t>(b[i + 2]) << 8) | b[i + 3];
    if (len < 2 || i + 2 + len > b.size()) {
      return 1;
    }
    if (marker == 0xE1 && len >= 8 && std::memcmp(&b[i + 4], "Exif\0\0", 6) == 0) {
      const std::uint8_t* t = &b[i + 10];
      const std::size_t tlen = len - 8;
      if (tlen < 8) {
        return 1;
      }
      const bool big = t[0] == 'M';
      if (!(big || t[0] == 'I') || t[1] != t[0]) {
        return 1;
      }
      const auto u16 = [&](std::size_t at) {
        return big ? static_cast<unsigned>((t[at] << 8) | t[at + 1])
                   : static_cast<unsigned>((t[at + 1] << 8) | t[at]);
      };
      const auto u32 = [&](std::size_t at) {
        return big ? ((static_cast<std::size_t>(t[at]) << 24) | (t[at + 1] << 16) |
                      (t[at + 2] << 8) | t[at + 3])
                   : ((static_cast<std::size_t>(t[at + 3]) << 24) | (t[at + 2] << 16) |
                      (t[at + 1] << 8) | t[at]);
      };
      if (u16(2) != 42) {
        return 1;
      }
      const std::size_t ifd = u32(4);
      if (ifd + 2 > tlen) {
        return 1;
      }
      const std::size_t count = u16(ifd);
      for (std::size_t e = 0; e < count; e++) {
        const std::size_t at = ifd + 2 + e * 12;
        if (at + 12 > tlen) {
          return 1;
        }
        if (u16(at) == 0x0112 && u16(at + 2) == 3) {  // tag 274, type SHORT
          const unsigned o = u16(at + 8);
          return (o >= 1 && o <= 8) ? static_cast<int>(o) : 1;
        }
      }
      return 1;
    }
    i += 2 + len;
  }
  return 1;
}

// Turns a picture the way EXIF orientation `o` says (2 mirror, 3 upside down, 4 flipped, 5
// transposed, 6 turn clockwise, 7 transverse, 8 turn counterclockwise). For 5..8 the width and
// height swap.
std::vector<std::uint8_t> orient(const std::vector<std::uint8_t>& src, int& w, int& h, int o) {
  if (o < 2 || o > 8) {
    return src;
  }
  const int old_w = w;
  const int old_h = h;
  const int nw = o >= 5 ? old_h : old_w;
  const int nh = o >= 5 ? old_w : old_h;
  std::vector<std::uint8_t> out(src.size());
  for (int dy = 0; dy < nh; dy++) {
    for (int dx = 0; dx < nw; dx++) {
      int sx = 0;
      int sy = 0;
      switch (o) {
        case 2:
          sx = old_w - 1 - dx;
          sy = dy;
          break;
        case 3:
          sx = old_w - 1 - dx;
          sy = old_h - 1 - dy;
          break;
        case 4:
          sx = dx;
          sy = old_h - 1 - dy;
          break;
        case 5:
          sx = dy;
          sy = dx;
          break;
        case 6:
          sx = dy;
          sy = old_h - 1 - dx;
          break;
        case 7:
          sx = old_w - 1 - dy;
          sy = old_h - 1 - dx;
          break;
        default:
          sx = old_w - 1 - dy;
          sy = dx;
          break;
      }
      std::memcpy(&out[(static_cast<std::size_t>(dy) * static_cast<std::size_t>(nw) +
                        static_cast<std::size_t>(dx)) *
                       4],
                  &src[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(old_w) +
                        static_cast<std::size_t>(sx)) *
                       4],
                  4);
    }
  }
  w = nw;
  h = nh;
  return out;
}

}  // namespace

ImageResult decode_image(std::span<const std::uint8_t> bytes, const ImageLimits& lim) {
  if (bytes.empty()) {
    return fail("The file is empty.");
  }
  if (bytes.size() > lim.max_file_bytes) {
    return fail("That picture file is too big.");
  }
  if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return fail("That picture file is too big.");
  }
  const int len = static_cast<int>(bytes.size());

  if (bytes.size() >= 12 && std::memcmp(bytes.data(), "RIFF", 4) == 0 &&
      std::memcmp(bytes.data() + 8, "WEBP", 4) == 0) {
#ifdef FH_HAVE_WEBP
    return decode_webp(bytes, lim);
#else
    return fail(
        "This build of the program cannot read WebP pictures. Save the picture as PNG or JPEG "
        "instead.");
#endif
  }

  int w = 0;
  int h = 0;
  int comp = 0;
  int declared_frames = 1;
  if (is_gif(bytes)) {
    if (!scan_gif(bytes, w, h, declared_frames)) {
      return fail("That GIF is damaged.");
    }
    if (declared_frames > lim.max_frames_in_file) {
      return fail("That GIF has too many frames (" + std::to_string(declared_frames) +
                  ", the most allowed is " + std::to_string(lim.max_frames_in_file) + ").");
    }
  } else if (!stbi_info_from_memory(bytes.data(), len, &w, &h, &comp)) {
    const char* why = stbi_failure_reason();
    if (why && std::strstr(why, "too large")) {
      return fail("That picture is too large.");
    }
    return fail(
        "That is not a picture this program can read (it reads JPEG, PNG, WebP, BMP, GIF, TGA, "
        "PSD, PIC and PNM).");
  }
  if (w <= 0 || h <= 0) {
    return fail("That picture has no size.");
  }
  if (static_cast<long long>(w) * h > lim.max_pixels) {
    return fail("That picture is too large (" + std::to_string(w) + " x " + std::to_string(h) +
                " pixels).");
  }

  const bool maybe_animated = declared_frames > 1;
  const int max_side = maybe_animated ? lim.max_side_animated : lim.max_side;
  const double k =
      std::min(1.0, static_cast<double>(max_side) / static_cast<double>(std::max(w, h)));
  const int nw = std::max(1, static_cast<int>(std::lround(w * k)));
  const int nh = std::max(1, static_cast<int>(std::lround(h * k)));
  const std::size_t frame_bytes = static_cast<std::size_t>(nw) * static_cast<std::size_t>(nh) * 4;
  const int stride =
      std::max(1, (declared_frames + std::max(1, lim.max_frames) - 1) /
                      std::max(1, lim.max_frames));  // a long animation keeps every stride-th frame
  const int kept_frames = (declared_frames + stride - 1) / stride;
  if (frame_bytes * static_cast<std::size_t>(kept_frames) > lim.max_decoded_bytes) {
    return fail("That animation is too big to hold in memory.");
  }
  // The decoder itself holds every frame at full size before it is shrunk, so that has to fit too.
  if (static_cast<unsigned long long>(w) * static_cast<unsigned long long>(h) * 4ULL *
          static_cast<unsigned long long>(declared_frames) >
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

  if (is_gif(bytes)) {
    int* delays = nullptr;
    int gw = 0;
    int gh = 0;
    int frames = 0;
    int gcomp = 0;
    stbi_uc* data =
        stbi_load_gif_from_memory(bytes.data(), len, &delays, &gw, &gh, &frames, &gcomp, 4);
    if (!data) {
      if (delays) {
        stbi_image_free(delays);
      }
      return fail("That GIF could not be read.");
    }
    if (gw != w || gh != h || frames < 1 ||
        frames != declared_frames) {  // (stb stops at the first bad frame and hands back what it
                                      // had: do not show half an animation)
      stbi_image_free(data);
      if (delays) {
        stbi_image_free(delays);
      }
      return fail("That GIF could not be read.");
    }
    const std::size_t full_frame = static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh) * 4;
    const auto delay_of = [&](int f) {
      const int d = delays ? delays[f] : 0;
      return d < 20 ? 100 : d;
    };
    for (int f = 0; f < frames; f += stride) {
      keep(data + static_cast<std::size_t>(f) * full_frame, gw, gh);
      int shown =
          0;  // the skipped frames' time is added to the one kept, so the speed stays the same
      for (int g = f; g < std::min(frames, f + stride); g++) {
        shown += delay_of(g);
      }
      img.delays_ms.push_back(shown);
    }
    stbi_image_free(data);
    if (delays) {
      stbi_image_free(delays);
    }
  } else {
    int pw = 0;
    int ph = 0;
    int pcomp = 0;
    stbi_uc* data = stbi_load_from_memory(bytes.data(), len, &pw, &ph, &pcomp, 4);
    if (!data) {
      return fail(std::string("That picture could not be read: ") +
                  (stbi_failure_reason() ? stbi_failure_reason() : "unknown problem") + ".");
    }
    if (pw != w || ph != h) {
      stbi_image_free(data);
      return fail("That picture could not be read.");
    }
    keep(data, pw, ph);
    stbi_image_free(data);
    img.delays_ms.push_back(100);
    const int o = exif_orientation(bytes);
    if (o > 1) {
      img.frames[0] = orient(img.frames[0], img.width, img.height, o);
    }
  }
  res.ok = true;
  return res;
}

ImageResult load_image_file(const std::filesystem::path& file, const ImageLimits& lim) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(file, ec);
  if (ec) {
    return fail("The file could not be read.");
  }
  if (size > lim.max_file_bytes) {
    return fail("That picture file is too big.");
  }
  std::ifstream in(file, std::ios::binary);
  if (!in) {
    return fail("The file could not be read.");
  }
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
  return decode_image(bytes, lim);
}

}  // namespace fh
