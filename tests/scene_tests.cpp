// Tests for artifact movement (include/fh/scene.hpp).
#include "check.hpp"
#include "fh/scene.hpp"
#include "tests.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <string>

using namespace fh;

using test::check;

void scene_tests() {
  test::section("scene");
  ArtifactField f(7);
  check("the same artifact at the same moment is always in the same place", [&] {
    const ArtifactPlacement a = f.place(3, 12.5, 0.4, 0.2);
    const ArtifactPlacement b = ArtifactField(7).place(3, 12.5, 0.4, 0.2);
    return a.x == b.x && a.y == b.y && a.width == b.width && a.rotation == b.rotation &&
           a.alpha == b.alpha;
  }());
  check("a different seed gives a different path",
        f.place(0, 10, 0, 0).x != ArtifactField(8).place(0, 10, 0, 0).x);
  {
    bool inside = true;
    double min_x = 9;
    double max_x = -9;
    double min_w = 9;
    double max_w = -9;
    for (std::size_t i = 0; i < 40; i++) {
      for (double t = 0; t < 600; t += 0.37) {
        for (const double m : {0.0, 1.0}) {
          const ArtifactPlacement p = f.place(i, t, m, m);
          inside = inside && p.x > 0.05 && p.x < 0.95 && p.y > 0.1 && p.y < 0.9 && p.width > 0.1 &&
                   p.width < 0.45 && std::fabs(p.rotation) <= 0.35 + 1e-9 && p.alpha >= 0 &&
                   p.alpha <= 1;
          min_x = std::min(min_x, p.x);
          max_x = std::max(max_x, p.x);
          min_w = std::min(min_w, p.width);
          max_w = std::max(max_w, p.width);
        }
      }
    }
    check("artifacts stay inside the picture, at a sensible size, tilted a little", inside);
    check("artifacts between them cover most of the picture's width", min_x < 0.2 && max_x > 0.8,
          std::to_string(min_x) + ".." + std::to_string(max_x));
  }
  {
    std::set<double> xs;
    for (std::size_t i = 0; i < 20; i++) {
      xs.insert(f.place(i, 30, 0, 0).x);
    }
    check("twenty artifacts are in twenty different places", xs.size() == 20);
  }
  {
    double worst = 0;
    for (std::size_t i = 0; i < 10; i++) {
      for (double t = 0; t < 300; t += 1.0 / 60) {
        const ArtifactPlacement a = f.place(i, t, 0.3, 0);
        const ArtifactPlacement b = f.place(i, t + 1.0 / 60, 0.3, 0);
        worst = std::max(worst, std::hypot(a.x - b.x, a.y - b.y));
      }
    }
    check("they drift slowly (never more than 0.5% of the picture a frame at 60 fps)",
          worst < 0.005, std::to_string(worst));
  }
  {
    const ArtifactPlacement quiet = f.place(2, 20, 0, 0);
    const ArtifactPlacement loud = f.place(2, 20, 1, 1);
    check("bass and a beat make an artifact bigger, and nothing else",
          loud.width > quiet.width * 1.15 && loud.x == quiet.x && loud.y == quiet.y);
  }
  {
    const double nan = std::nan("");
    const double inf = std::numeric_limits<double>::infinity();
    const ArtifactPlacement p = f.place(1, nan, nan, inf);
    check("bad numbers do not give bad places", std::isfinite(p.x) && std::isfinite(p.y) &&
                                                    std::isfinite(p.width) &&
                                                    std::isfinite(p.alpha));
  }
  check("fade: starts at 0, ends at 1, eases",
        smooth_fade(0, 1000) == 0 && smooth_fade(1000, 1000) == 1 && smooth_fade(2000, 1000) == 1 &&
            smooth_fade(500, 1000) == 0.5 && smooth_fade(250, 1000) < 0.25 &&
            smooth_fade(-5, 1000) == 0 && smooth_fade(10, 0) == 1);
}
