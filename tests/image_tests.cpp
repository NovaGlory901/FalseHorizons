// Tests for reading pictures (image.hpp): small pictures in tests/data/ (recipe in its README.md),
// the limits that protect against hostile files, and a damaged-file loop meant for the sanitizers.
#include "check.hpp"
#include "fh/image.hpp"
#include "tests.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <utility>

#ifndef FH_DATA
#define FH_DATA "tests/data"
#endif

using namespace fh;

using test::check;

static std::vector<std::uint8_t> read_all(const std::string& file) {
  std::ifstream in(std::string(FH_DATA) + "/" + file, std::ios::binary);
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
}
static ImageResult load(const std::string& file, const ImageLimits& lim = {}) {
  return load_image_file(std::string(FH_DATA) + "/" + file, lim);
}

struct Px {
  int r, g, b, a;
};
static Px at(const Image& im, std::size_t frame, int x, int y) {
  const std::uint8_t* p =
      &im.frames[frame][(static_cast<std::size_t>(y) * static_cast<std::size_t>(im.width) +
                         static_cast<std::size_t>(x)) *
                        4];
  return {p[0], p[1], p[2], p[3]};
}
static bool near(Px p, int r, int g, int b, int a, int tol = 0) {
  return std::abs(p.r - r) <= tol && std::abs(p.g - g) <= tol && std::abs(p.b - b) <= tol &&
         std::abs(p.a - a) <= tol;
}

void image_tests() {
  test::section("image");
  for (const char* f :
       {"halves.png", "halves.bmp", "halves.jpg", "halves.tga", "halves.ppm", "halves.gif"}) {
    const ImageResult r = load(f);
    const bool lossy = std::string(f) == "halves.jpg";
    const int tol = lossy ? 12 : 3;  // a JPEG is close, not exact; the GIF palette is adaptive
    bool ok = r.ok && r.image.width == 8 && r.image.height == 4 && r.image.frames.size() == 1;
    if (ok) {
      ok = near(at(r.image, 0, 1, 1), 255, 0, 0, 255, tol) &&
           near(at(r.image, 0, 6, 2), 0, 0, 255, 255, tol);
    }
    check(std::string(f) + " reads as 8x4, red left, blue right", ok, r.error);
    check(std::string(f) + " is not animated",
          r.ok && !r.image.animated() && r.image.frame_at(12345) == 0);
  }
  {
    const ImageResult r = load("clear.png");
    check("transparency is kept (straight alpha)", r.ok &&
                                                       near(at(r.image, 0, 1, 1), 0, 255, 0, 0) &&
                                                       near(at(r.image, 0, 6, 1), 0, 255, 0, 255));
  }
  {
    const ImageResult r = load("rgb.gif");
    check("an animated GIF has its three frames",
          r.ok && r.image.animated() && r.image.frames.size() == 3 && r.image.width == 6 &&
              r.image.height == 6,
          r.error);
    if (r.ok && r.image.frames.size() == 3) {
      check("an animated GIF keeps its frame delays",
            r.image.delays_ms == std::vector<int>({50, 100, 200}) && r.image.total_ms() == 350);
      check("an animated GIF has the right colour in each frame",
            near(at(r.image, 0, 2, 2), 255, 0, 0, 255, 3) &&
                near(at(r.image, 1, 2, 2), 0, 255, 0, 255, 3) &&
                near(at(r.image, 2, 2, 2), 0, 0, 255, 255, 3));
      check("frameAt follows the delays and loops",
            r.image.frame_at(0) == 0 && r.image.frame_at(49) == 0 && r.image.frame_at(50) == 1 &&
                r.image.frame_at(149) == 1 && r.image.frame_at(150) == 2 &&
                r.image.frame_at(349) == 2 && r.image.frame_at(350) == 0 &&
                r.image.frame_at(350 * 1000 + 60) == 1 && r.image.frame_at(-10) == 2 &&
                r.image.frame_at(std::nan("")) == 0);
    }
  }
  {
    const ImageResult r = load("wide.png");
    check("a 2000x500 picture is shrunk to 1024 wide, keeping its shape",
          r.ok && r.image.width == 1024 && r.image.height == 256,
          std::to_string(r.image.width) + "x" + std::to_string(r.image.height));
    if (r.ok) {
      check("a shrunk picture still has its colours running left to right",
            at(r.image, 0, 0, 128).r < 5 && at(r.image, 0, 1023, 128).r > 250 &&
                std::abs(at(r.image, 0, 512, 0).r - 127) < 4);
    }
    ImageLimits small;
    small.max_side = 100;
    const ImageResult s = load("wide.png", small);
    check("the size limit can be lowered", s.ok && s.image.width == 100 && s.image.height == 25);
  }
#ifdef FH_HAVE_WEBP
  {
    const ImageResult r = load("halves.webp");
    check("a WebP reads as 8x4, red left, blue right",
          r.ok && r.image.width == 8 && r.image.height == 4 && !r.image.animated() &&
              near(at(r.image, 0, 1, 1), 255, 0, 0, 255, 3) &&
              near(at(r.image, 0, 6, 2), 0, 0, 255, 255, 3),
          r.error);
    const ImageResult c = load("clear.webp");
    check("a lossless WebP keeps transparency (straight alpha)",
          c.ok && near(at(c.image, 0, 1, 1), 0, 255, 0, 0) &&
              near(at(c.image, 0, 6, 1), 0, 255, 0, 255),
          c.error);
    const ImageResult l = load("clear-lossy.webp");
    check("a lossy WebP with transparency reads too",
          l.ok && at(l.image, 0, 1, 1).a < 10 && at(l.image, 0, 6, 1).a > 245 &&
              at(l.image, 0, 6, 1).g > 200,
          l.error);
    const ImageResult a = load("rgb.webp");
    check("an animated WebP has its three frames, delays and colours",
          a.ok && a.image.animated() && a.image.frames.size() == 3 &&
              a.image.delays_ms == std::vector<int>({50, 100, 200}) &&
              near(at(a.image, 0, 2, 2), 255, 0, 0, 255, 3) &&
              near(at(a.image, 1, 2, 2), 0, 255, 0, 255, 3) &&
              near(at(a.image, 2, 2, 2), 0, 0, 255, 255, 3),
          a.error);
    const ImageResult w = load("wide.webp");
    check("a 2000x500 WebP is shrunk to 1024 wide",
          w.ok && w.image.width == 1024 && w.image.height == 256, w.error);
    ImageResult m = load("manyframes.webp");
    const int m_total =
        m.ok ? std::accumulate(m.image.delays_ms.begin(), m.image.delays_ms.end(), 0) : 0;
    ImageLimits strict;
    strict.max_frames_in_file = 300;
    const ImageResult ms = load("manyframes.webp", strict);
    check(
        "a WebP animation with 400 frames is thinned to 200 (every second frame) and keeps "
        "its running time; one over the file limit is refused",
        m.ok && m.image.frames.size() == 200 && m.image.frames.size() == m.image.delays_ms.size() &&
            m_total ==
                [&] {
                  ImageLimits u;
                  u.max_frames = 1000;
                  ImageResult f = load("manyframes.webp", u);
                  return f.ok ? std::accumulate(f.image.delays_ms.begin(), f.image.delays_ms.end(),
                                                0)
                              : -1;
                }() &&
            !ms.ok && ms.error.find("too many frames") != std::string::npos,
        m.error + " " + std::to_string(m.image.frames.size()));
    const ImageResult hc = load("huge-claim.webp");
    check("a 34-byte WebP that claims 16383x16383 is refused before decoding",
          !hc.ok && hc.error.find("too large") != std::string::npos, hc.error);
    check("a WebP over the byte limit is refused", !load("wide.webp",
                                                         [] {
                                                           ImageLimits lim;
                                                           lim.max_file_bytes = 100;
                                                           return lim;
                                                         }())
                                                        .ok);
    check("a WebP animation over the memory limit is refused", !load("rgb.webp",
                                                                     [] {
                                                                       ImageLimits lim;
                                                                       lim.max_decoded_bytes = 100;
                                                                       return lim;
                                                                     }())
                                                                    .ok);
  }
#else
  check("without libwebp a WebP is refused with a clear reason",
        !load("halves.webp").ok && load("halves.webp").error.find("WebP") != std::string::npos);
#endif
  // phone photos: the picture is stored turned, with a note (EXIF orientation 1..8) saying how to
  // turn it back
  for (int o = 1; o <= 8; o++) {
    const ImageResult r = load("orient" + std::to_string(o) + ".jpg");
    bool ok = r.ok && r.image.width == 16 && r.image.height == 32;
    if (ok) {
      ok = near(at(r.image, 0, 1, 1), 0, 255, 0, 255, 60) &&
           near(at(r.image, 0, 8, 10), 255, 0, 0, 255, 60) &&
           near(at(r.image, 0, 8, 25), 0, 0, 255, 255, 60);
    }
    check("EXIF orientation " + std::to_string(o) +
              " is turned upright (green corner top left, red above blue)",
          ok,
          r.ok ? std::to_string(r.image.width) + "x" + std::to_string(r.image.height) : r.error);
  }
  // a GIF whose compressed data never sends a second "clear" code (the table fills up and stays
  // full): stock stb_image failed on these after the first frames
  {
    ImageResult r = load("deferred-clear.gif");
    const int first[3][6] = {
        {165, 77, 202, 24, 37, 48},
        {100, 16, 39, 212, 216, 4},
        {234, 2, 219, 101, 205, 18}};  // the palette index of each frame's first six pixels; the
                                       // palette maps i to (i, i*7 % 256, i*13 % 256)
    bool same = r.ok && r.image.frames.size() == 3 && r.image.width == 96 && r.image.height == 96;
    for (int f = 0; same && f < 3; f++) {
      for (int x = 0; x < 6; x++) {
        const int i = first[f][x];
        const std::uint8_t* px =
            &r.image.frames[static_cast<std::size_t>(f)][static_cast<std::size_t>(x) * 4];
        if (std::cmp_not_equal(px[0], i) || px[1] != (i * 7) % 256 || px[2] != (i * 13) % 256) {
          same = false;
        }
      }
    }
    check(
        "a GIF with a deferred clear code (full table, no new clear) decodes all three "
        "frames with the right colours",
        same, r.error);
    check("a GIF with a deferred clear code has 50 ms frame delays",
          r.ok && r.image.delays_ms == std::vector<int>({50, 50, 50}));
  }
  {
    ImageResult r = load("manyframes.gif");
    ImageLimits more;
    more.max_frames = 500;
    ImageResult full = load("manyframes.gif", more);
    const int r_total =
        r.ok ? std::accumulate(r.image.delays_ms.begin(), r.image.delays_ms.end(), 0) : -1;
    const int f_total =
        full.ok ? std::accumulate(full.image.delays_ms.begin(), full.image.delays_ms.end(), 0) : -2;
    check(
        "a GIF with 400 frames is thinned to 200 (every second frame) with the same running "
        "time, and kept whole if the limit allows",
        r.ok && r.image.frames.size() == 200 && r.image.delays_ms.size() == 200 && full.ok &&
            full.image.frames.size() == 400 && r_total == f_total,
        r.error + " " + std::to_string(r.image.frames.size()) + " " + std::to_string(r_total) +
            " vs " + std::to_string(f_total));
    ImageLimits strict_gif;
    strict_gif.max_frames_in_file = 300;
    const ImageResult refused = load("manyframes.gif", strict_gif);
    check("an animation with more frames than the file limit allows is refused",
          !refused.ok && refused.error.find("too many frames") != std::string::npos, refused.error);
    check("an animation too big for the memory limit is refused", !load("manyframes.gif",
                                                                        [&] {
                                                                          ImageLimits lim;
                                                                          lim.max_frames = 500;
                                                                          lim.max_decoded_bytes =
                                                                              1000;
                                                                          return lim;
                                                                        }())
                                                                       .ok);
  }
  {
    const ImageResult r = load("huge-claim.png");
    check("a small file that claims 9000x9000 is refused before decoding",
          !r.ok && r.error.find("too large") != std::string::npos, r.error);
    const ImageResult r2 = load("huger-claim.png");
    check("one that claims 40000x40000 is refused too", !r2.ok, r2.error);
    ImageLimits file_cap;
    file_cap.max_file_bytes = 100;
    check("a file over the byte limit is refused", !load("wide.png", file_cap).ok);
  }
  {
    const ImageResult r = load("notpicture.txt");
    check("text is not a picture", !r.ok && r.error.find("not a picture") != std::string::npos,
          r.error);
    check("an empty input is refused", !decode_image({}).ok);
    check("a missing file is refused", !load("nothing-here.png").ok);
  }
  {
    std::vector<std::uint8_t> px = {
        255, 0,   0, 255, 0, 0,   255, 255,
        0,   255, 0, 0,   0, 255, 0,   255};  // 2x2: red, blue / clear green, green
    auto out = shrink_rgba(px, 2, 2, 1, 1);
    check("shrink: colours are averaged weighted by alpha (a clear pixel does not tint)",
          out.size() == 4 && out[3] == 191 /* (255*3+0)/4 */ && out[1] > 60 &&
              std::abs(out[0] - 85) <= 1,
          std::to_string(out[0]) + "," + std::to_string(out[1]) + "," + std::to_string(out[2]) +
              "," + std::to_string(out[3]));
  }
  // Damaged and random files must give an answer, never a crash (run this under the sanitizers).
  {
    std::mt19937 gen(1234);
    int tried = 0;
    int accepted = 0;
    for (const char* f :
         {"halves.png", "halves.bmp", "halves.jpg", "halves.tga", "halves.ppm", "halves.gif",
          "rgb.gif", "clear.png", "wide.png", "orient6.jpg", "orient8.jpg"
#ifdef FH_HAVE_WEBP
          ,
          "halves.webp", "clear.webp", "clear-lossy.webp", "rgb.webp", "wide.webp"
#endif
         }) {
      const std::vector<std::uint8_t> original = read_all(f);
      for (int n = 0; n < 150; n++) {
        std::vector<std::uint8_t> b = original;
        const int how = static_cast<int>(gen() % 4);
        if (how == 0) {
          b.resize(gen() % (b.size() + 1));  // cut short
        } else if (how == 1) {
          for (int k = 0; k < 1 + static_cast<int>(gen() % 8); k++) {
            b[gen() % b.size()] = static_cast<std::uint8_t>(gen());  // a few bytes changed
          }
        } else if (how == 2) {
          const std::size_t at0 = gen() % b.size();
          for (std::size_t k = at0; k < std::min(b.size(), at0 + 16); k++) {
            b[k] = 0xFF;
          }
        } else {
          const std::size_t at0 = gen() % b.size();
          b.insert(b.begin() + static_cast<std::ptrdiff_t>(at0), static_cast<std::uint8_t>(gen()));
        }
        ImageLimits lim;
        lim.max_pixels =
            4'000'000;  // keeps a damaged header from asking for gigabytes in this loop
        const ImageResult r = decode_image(b, lim);
        tried++;
        if (r.ok) {
          accepted++;
          if (r.image.frames.empty() || r.image.frames.size() != r.image.delays_ms.size()) {
            check("damaged file gave an inconsistent result", false, f);
          }
          for (const auto& fr : r.image.frames) {
            if (fr.size() != static_cast<std::size_t>(r.image.width) *
                                 static_cast<std::size_t>(r.image.height) * 4) {
              check("damaged file gave a frame of the wrong size", false, f);
              break;
            }
          }
        }
      }
    }
    for (int n = 0; n < 300; n++) {
      std::vector<std::uint8_t> b(gen() % 2000);
      for (auto& x : b) {
        x = static_cast<std::uint8_t>(gen());
      }
      if (n % 3 == 0 && b.size() > 6) {
        std::copy_n("GIF89a", 6, b.begin());  // random bytes that pretend to be a GIF
      }
      if (n % 3 == 1 && b.size() > 8) {
        std::copy_n("\x89PNG\r\n\x1a\n", 8, b.begin());  // ... or a PNG
      }
      decode_image(b);
      tried++;
    }
    check("" + std::to_string(tried) +
              " damaged and random files were all answered without a crash (" +
              std::to_string(accepted) + " still decoded)",
          true);
  }
}
