#include "fh/renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <GLES2/gl2.h>

namespace fh {

namespace {

const char* const precision =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump "
    "float;\n#endif\n";

// The background: two pictures (the old one and the one fading in) drawn to fill the frame without
// stretching. Pictures are kept with their top row first, so v is flipped here.
const char* const cover_glsl =
    "uniform sampler2D uCoverA, uCoverB; uniform vec2 uCoverSizeA, uCoverSizeB, uRes; uniform "
    "float uCoverMix;\n"
    "vec2 fit(vec2 uv, vec2 size) {\n"
    "  float ca = uRes.x / uRes.y, ia = size.x / size.y;\n"
    "  vec2 c = uv - 0.5;\n"
    "  if (ca > ia) c.y *= ia / ca; else c.x *= ca / ia;\n"
    "  return vec2(c.x + 0.5, 0.5 - c.y);\n"
    "}\n"
    "vec3 coverColor(vec2 uv) { return mix(texture2D(uCoverA, fit(uv, uCoverSizeA)).rgb, "
    "texture2D(uCoverB, fit(uv, uCoverSizeB)).rgb, uCoverMix); }\n";

// Where the previous picture is read from, for every point of the warp mesh. Past the edge the
// picture folds back instead of smearing.
const char* warp_vert =
    "attribute vec2 aPos; attribute vec2 aUv; varying vec2 vUv; varying vec2 vScreen;\n"
    "void main() { vUv = aUv; vScreen = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
// uInject: how much of the background is added each frame (scaled by the fade so it settles at a
// fixed brightness however long the trails are). uSceneAmt: how hard the artifacts (premultiplied,
// in uScene) are mixed into the loop.
const std::string warp_frag =
    std::string(
        "varying vec2 vUv; varying vec2 vScreen;\n"
        "uniform sampler2D uPrev, uScene; uniform float uDecay, uFade, uInject, uSceneAmt;\n") +
    cover_glsl +
    "vec2 mirror(vec2 p) { return abs(mod(p + 1.0, 2.0) - 1.0); }\n"
    "void main() {\n"
    "  vec3 c = texture2D(uPrev, mirror(vUv)).rgb * uDecay;\n"
    "  c = max(c - vec3(uFade), 0.0);   // a hair off every frame so slow fades really reach black "
    "(8-bit rounding would stall them)\n"
    "  if (uInject > 0.0) c += coverColor(vScreen) * uInject;\n"
    "  if (uSceneAmt > 0.0) { vec4 s = texture2D(uScene, vScreen); c = mix(c, s.rgb / max(s.a, "
    "0.001), s.a * uSceneAmt); }\n"
    "  gl_FragColor = vec4(min(c, 1.0), 1.0);\n"
    "}\n";

// An artifact: a picture on a rotated rectangle. uHalf is the rectangle's half size in units where
// the picture's height is 2; the rectangle is turned there, then squeezed to the picture's width
// (uAspect = width / height).
const char* sprite_vert =
    "attribute vec2 aPos; uniform vec2 uCenter, uHalf; uniform float uCos, uSin, uAspect; varying "
    "vec2 vUv;\n"
    "void main() {\n"
    "  vec2 p = aPos * uHalf;\n"
    "  p = vec2(uCos * p.x - uSin * p.y, uSin * p.x + uCos * p.y);\n"
    "  p.x /= uAspect;\n"
    "  vUv = vec2(aPos.x * 0.5 + 0.5, 0.5 - aPos.y * 0.5);\n"
    "  gl_Position = vec4(uCenter + p, 0.0, 1.0);\n"
    "}\n";
const char* sprite_frag =
    "varying vec2 vUv; uniform sampler2D uTex; uniform float uAlpha; void main() { vec4 c = "
    "texture2D(uTex, vUv); gl_FragColor = vec4(c.rgb, c.a * uAlpha); }\n";

const char* fill_vert =
    "attribute vec2 aPos; attribute vec4 aCol; uniform vec2 uOff; uniform float uPoint; varying "
    "vec4 vCol;\n"
    "void main() { vCol = aCol; gl_PointSize = uPoint; gl_Position = vec4(aPos + uOff, 0.0, 1.0); "
    "}\n";
const char* fill_frag = "varying vec4 vCol; void main() { gl_FragColor = vCol; }\n";

const char* full_vert =
    "attribute vec2 aPos; varying vec2 vUv; void main() { vUv = aPos * 0.5 + 0.5; gl_Position = "
    "vec4(aPos, 0.0, 1.0); }\n";

const std::string composite_frag =
    std::string(
        "varying vec2 vUv;\n"
        "uniform sampler2D uTex, uScene;\n"
        "uniform float uGamma, uHue, uBright, uSolar, uEchoA, uEchoZ, uEchoO, uCoverAmt, "
        "uSceneDirect, uWobble, uClock;\n") +
    cover_glsl +
    "vec2 mirror(vec2 p) { return abs(mod(p + 1.0, 2.0) - 1.0); }\n"
    "void main() {\n"
    "  vec3 c = texture2D(uTex, vUv).rgb;\n"
    "  if (uEchoA > 0.001) {                                  // a mirrored, zoomed copy laid over "
    "the picture\n"
    "    vec2 e = (vUv - 0.5) / uEchoZ + 0.5;\n"
    "    if (uEchoO > 0.5 && uEchoO < 1.5 || uEchoO > 2.5) e.x = 1.0 - e.x;\n"
    "    if (uEchoO > 1.5) e.y = 1.0 - e.y;\n"
    "    c = mix(c, texture2D(uTex, mirror(e)).rgb, uEchoA);\n"
    "  }\n"
    "  c = pow(max(c, vec3(0.0)), vec3(1.0 / uGamma));\n"
    "  c = mix(c, abs(c * 2.0 - 1.0), uSolar);\n"
    "  float h = uHue * 6.28318;\n"
    "  vec3 k = vec3(0.57735);\n"
    "  float cs = cos(h), sn = sin(h);\n"
    "  c = c * cs + cross(k, c) * sn + k * dot(k, c) * (1.0 - cs);   // turn the colours around "
    "the grey axis\n"
    "  if (uCoverAmt > 0.0) c += coverColor(vUv) * uCoverAmt * (0.4 + dot(c, vec3(0.299, 0.587, "
    "0.114)));\n"
    "  if (uSceneDirect > 0.0) {                              // a little of the artifacts "
    "straight, but rippling with the music\n"
    "    vec2 d = vec2(sin(vUv.y * 14.0 + uClock * 1.3), cos(vUv.x * 11.0 - uClock * 1.1)) * "
    "uWobble;\n"
    "    c += texture2D(uScene, mirror(vUv + d)).rgb * uSceneDirect;\n"
    "  }\n"
    "  gl_FragColor = vec4(clamp(c * uBright, 0.0, 1.0), 1.0);\n"
    "}\n";

// The picture's average brightness, worked out into a 16 x 9 image (144 pixels) that is then read
// back.
const char* const measure_frag =
    "varying vec2 vUv; uniform sampler2D uTex;\n"
    "void main() {\n"
    "  vec2 cell = vec2(1.0 / 16.0, 1.0 / 9.0), o = floor(vUv * vec2(16.0, 9.0)) * cell;\n"
    "  float s = 0.0;\n"
    "  for (int i = 0; i < 8; i++) for (int j = 0; j < 8; j++) s += dot(texture2D(uTex, o + "
    "(vec2(float(i), float(j)) + 0.5) / 8.0 * cell).rgb, vec3(0.299, 0.587, 0.114));\n"
    "  s /= 64.0;\n"
    "  gl_FragColor = vec4(s, s, s, 1.0);\n"
    "}\n";

struct Prog {
  GLuint id = 0;
  GLint loc(const char* name) const { return glGetUniformLocation(id, name); }
};

struct Target {
  GLuint tex = 0, fbo = 0;
  int w = 0, h = 0;
};

}  // namespace

struct Renderer::Impl {
  Prog warp, fill, composite, measure, sprite;
  GLuint tri_buf = 0, quad_buf = 0, mesh_pos = 0, mesh_uv = 0, dyn_buf = 0, idx_buf = 0, blank = 0;
  GLsizei index_count = 0;
  Target targets[2], small, scene;

  struct Picture {
    std::shared_ptr<const Image> image;
    GLuint tex = 0;
    std::size_t shown = 0;
  };
  std::vector<Picture> pictures;  // a slot is free when its image is empty
  Picture* picture(PictureId id) {
    return (id >= 0 && static_cast<std::size_t>(id) < pictures.size() &&
            pictures[static_cast<std::size_t>(id)].image)
               ? &pictures[static_cast<std::size_t>(id)]
               : nullptr;
  }
  void upload(Picture& p, std::size_t frame, bool first) {
    const Image& im = *p.image;
    glBindTexture(GL_TEXTURE_2D, p.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (first) {
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, im.width, im.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                   im.frames[frame].data());
    } else {
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, im.width, im.height, GL_RGBA, GL_UNSIGNED_BYTE,
                      im.frames[frame].data());
    }
    p.shown = frame;
  }
  static void bind_unit(int unit, GLuint tex) {
    glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
    glBindTexture(GL_TEXTURE_2D, tex);
  }
  void cover_uniforms(const Prog& pr, PictureId a, PictureId b, double mix, int target_w,
                      int target_h) {
    Picture* pa = picture(a);
    Picture* pb = picture(b);
    if (!pb) {
      pb = pa;
    }
    if (!pa) {
      pa = pb;
    }
    bind_unit(1, pa ? pa->tex : blank);
    bind_unit(2, pb ? pb->tex : blank);
    glUniform1i(pr.loc("uCoverA"), 1);
    glUniform1i(pr.loc("uCoverB"), 2);
    glUniform2f(pr.loc("uCoverSizeA"), pa ? static_cast<float>(pa->image->width) : 1.0F,
                pa ? static_cast<float>(pa->image->height) : 1.0F);
    glUniform2f(pr.loc("uCoverSizeB"), pb ? static_cast<float>(pb->image->width) : 1.0F,
                pb ? static_cast<float>(pb->image->height) : 1.0F);
    glUniform1f(pr.loc("uCoverMix"),
                picture(b) && picture(a) ? static_cast<float>(std::clamp(mix, 0.0, 1.0)) : 0.0F);
    glUniform2f(pr.loc("uRes"), static_cast<float>(target_w), static_cast<float>(target_h));
  }
  int read = 0;
  int win_w = 640, win_h = 360;
  std::string error;

  GLuint compile(GLenum type, const std::string& src) {
    const GLuint sh = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
      char log[1024] = {};
      glGetShaderInfoLog(sh, sizeof log - 1, nullptr, log);
      error = std::string("shader failed to compile: ") + log;
      glDeleteShader(sh);
      return 0;
    }
    return sh;
  }
  bool program(Prog& out, const char* vert, const std::string& frag,
               std::initializer_list<const char*> attribs) {
    const GLuint v = compile(GL_VERTEX_SHADER, vert);
    const GLuint f = compile(GL_FRAGMENT_SHADER, std::string(precision) + frag);
    if (!v || !f) {
      return false;
    }
    const GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    GLuint i = 0;
    for (const char* a : attribs) {
      glBindAttribLocation(p, i++, a);
    }
    glLinkProgram(p);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
      char log[1024] = {};
      glGetProgramInfoLog(p, sizeof log - 1, nullptr, log);
      error = std::string("program failed to link: ") + log;
      glDeleteProgram(p);
      return false;
    }
    out.id = p;
    return true;
  }

  static GLuint new_buffer(GLenum type, const void* data, GLsizeiptr bytes, GLenum usage) {
    GLuint b = 0;
    glGenBuffers(1, &b);
    glBindBuffer(type, b);
    glBufferData(type, bytes, data, usage);
    return b;
  }

  void make_target(Target& t, int w, int h) {
    if (t.tex) {
      glDeleteTextures(1, &t.tex);
    }
    if (t.fbo) {
      glDeleteFramebuffers(1, &t.fbo);
    }
    t.w = w;
    t.h = h;
    glGenTextures(1, &t.tex);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
  }

  void draw_fill(const Draw& d, int w) {
    glBindBuffer(GL_ARRAY_BUFFER, dyn_buf);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(d.verts.size() * sizeof(float)),
                 d.verts.data(), GL_STREAM_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(0));
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 24, reinterpret_cast<void*>(8));
    glBlendFunc(GL_SRC_ALPHA, d.additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
    const GLenum mode = d.kind == DrawKind::Strip   ? GL_LINE_STRIP
                        : d.kind == DrawKind::Lines ? GL_LINES
                        : d.kind == DrawKind::Dots  ? GL_POINTS
                                                    : GL_TRIANGLE_FAN;
    glUniform1f(fill.loc("uPoint"),
                d.kind == DrawKind::Dots ? 2.0F + static_cast<float>(d.thick) * 2.0F : 1.0F);
    // A thicker line is the same line drawn again a pixel to the side (GL ES has no line width).
    const float px = 2.0F / static_cast<float>(w);
    const float all[5][2] = {{0, 0}, {px, 0}, {0, px}, {-px, 0}, {0, -px}};
    const int count = (d.thick > 1 && d.kind != DrawKind::Fan && d.kind != DrawKind::Dots)
                          ? (d.thick == 2 ? 3 : 5)
                          : 1;
    const GLint off = fill.loc("uOff");
    for (int i = 0; i < count; i++) {
      glUniform2f(off, all[i][0], all[i][1]);
      glDrawArrays(mode, 0, d.count);
    }
  }
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;  // the GL context owns the objects; they go with it

bool Renderer::init(std::string& error) {
  Impl& m = *impl_;
  if (!m.program(m.warp, warp_vert, warp_frag, {"aPos", "aUv"}) ||
      !m.program(m.fill, fill_vert, fill_frag, {"aPos", "aCol"}) ||
      !m.program(m.composite, full_vert, composite_frag, {"aPos"}) ||
      !m.program(m.measure, full_vert, measure_frag, {"aPos"}) ||
      !m.program(m.sprite, sprite_vert, sprite_frag, {"aPos"})) {
    error = m.error;
    return false;
  }
  std::vector<float> pos;
  pos.reserve(mesh_floats);
  for (int j = 0; j <= mesh_h; j++) {
    for (int i = 0; i <= mesh_w; i++) {
      pos.push_back(static_cast<float>(i) / mesh_w * 2 - 1);
      pos.push_back(static_cast<float>(j) / mesh_h * 2 - 1);
    }
  }
  std::vector<std::uint16_t> idx;
  for (int j = 0; j < mesh_h; j++) {
    for (int i = 0; i < mesh_w; i++) {
      const auto a = static_cast<std::uint16_t>(j * (mesh_w + 1) + i);
      const std::uint16_t b = a + 1;
      const auto c = static_cast<std::uint16_t>(a + mesh_w + 1);
      const std::uint16_t d = c + 1;
      for (std::uint16_t v : {a, b, c, b, d, c}) {
        idx.push_back(v);
      }
    }
  }
  const float tri[] = {-1, -1, 3, -1, -1, 3};
  m.tri_buf = Impl::new_buffer(GL_ARRAY_BUFFER, tri, sizeof tri, GL_STATIC_DRAW);
  const float quad[] = {-1, -1, 1, -1, -1, 1, 1, 1};
  m.quad_buf = Impl::new_buffer(GL_ARRAY_BUFFER, quad, sizeof quad, GL_STATIC_DRAW);
  glGenTextures(
      1, &m.blank);  // a 1x1 black picture for the background slots when there is no background
  glBindTexture(GL_TEXTURE_2D, m.blank);
  const std::uint8_t black[4] = {0, 0, 0, 255};
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, black);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  m.mesh_pos =
      Impl::new_buffer(GL_ARRAY_BUFFER, pos.data(),
                       static_cast<GLsizeiptr>(pos.size() * sizeof(float)), GL_STATIC_DRAW);
  m.mesh_uv =
      Impl::new_buffer(GL_ARRAY_BUFFER, pos.data(),
                       static_cast<GLsizeiptr>(pos.size() * sizeof(float)), GL_DYNAMIC_DRAW);
  m.dyn_buf = Impl::new_buffer(GL_ARRAY_BUFFER, nullptr, 1536, GL_STREAM_DRAW);
  m.idx_buf =
      Impl::new_buffer(GL_ELEMENT_ARRAY_BUFFER, idx.data(),
                       static_cast<GLsizeiptr>(idx.size() * sizeof(std::uint16_t)), GL_STATIC_DRAW);
  m.index_count = static_cast<GLsizei>(idx.size());
  m.make_target(m.small, 16, 9);
  resize(m.win_w, m.win_h);
  return true;
}

void Renderer::resize(int window_width, int window_height, double scale) {
  Impl& m = *impl_;
  m.win_w = std::max(1, window_width);
  m.win_h = std::max(1, window_height);
  const int w = std::max(160, std::min(960, static_cast<int>(std::lround(m.win_w * 0.6 * scale))));
  const int h =
      std::max(90, static_cast<int>(std::lround(static_cast<double>(w) * m.win_h / m.win_w)));
  if (m.targets[0].tex && m.targets[0].w == w && m.targets[0].h == h) {
    return;
  }
  m.make_target(m.targets[0], w, h);
  m.make_target(m.targets[1], w, h);
  m.make_target(
      m.scene, std::max(1, static_cast<int>(std::lround(w * 0.67))),
      std::max(1, static_cast<int>(std::lround(
                      h * 0.67))));  // artifacts are drawn smaller: the feedback blurs them anyway
  m.read = 0;
}

int Renderer::width() const { return impl_->targets[0].w; }
int Renderer::height() const { return impl_->targets[0].h; }
double Renderer::aspect() const {
  return impl_->targets[0].h ? static_cast<double>(impl_->targets[0].w) / impl_->targets[0].h : 1.0;
}

PictureId Renderer::add_picture(std::shared_ptr<const Image> image) {
  Impl& m = *impl_;
  if (!image || image->frames.empty() || image->width <= 0 || image->height <= 0) {
    return -1;
  }
  std::size_t slot = 0;
  while (slot < m.pictures.size() && m.pictures[slot].image) {
    slot++;
  }
  if (slot == m.pictures.size()) {
    m.pictures.emplace_back();
  }
  Impl::Picture& p = m.pictures[slot];
  p.image = std::move(image);
  glGenTextures(1, &p.tex);
  glBindTexture(GL_TEXTURE_2D, p.tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  m.upload(p, 0, true);
  if (glGetError() != GL_NO_ERROR) {  // the card could not take it (out of memory)
    glDeleteTextures(1, &p.tex);
    p = Impl::Picture{};
    return -1;
  }
  return static_cast<PictureId>(slot);
}

void Renderer::remove_picture(PictureId id) {
  Impl& m = *impl_;
  if (Impl::Picture* p = m.picture(id)) {
    glDeleteTextures(1, &p->tex);
    *p = Impl::Picture{};
  }
}

void Renderer::animate_pictures(double time_ms) {
  Impl& m = *impl_;
  for (Impl::Picture& p : m.pictures) {
    if (!p.image || !p.image->animated()) {
      continue;
    }
    const std::size_t f = p.image->frame_at(time_ms);
    if (f != p.shown) {
      m.upload(p, f, false);
    }
  }
}

void Renderer::draw(const FrameState& state, double gain, const SceneInputs& sc) {
  Impl& m = *impl_;
  const Target& rd = m.targets[m.read];
  const Target& wr = m.targets[1 - m.read];
  const ColorOut& c = state.color;

  const bool has_background =
      sc.background_amount > 0 && (m.picture(sc.background_a) || m.picture(sc.background_b));
  const double eff =
      has_background ? std::clamp(sc.background_amount, 0.0, 1.0) * (0.6 + 0.4 * c.cover) : 0.0;

  bool has_artifacts = false;
  for (const SceneDraw& d : sc.artifacts) {
    if (m.picture(d.picture)) {
      has_artifacts = true;
      break;
    }
  }
  if (has_artifacts) {
    glBindFramebuffer(GL_FRAMEBUFFER, m.scene.fbo);
    glViewport(0, 0, m.scene.w, m.scene.h);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m.sprite.id);
    glBindBuffer(GL_ARRAY_BUFFER, m.quad_buf);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    const double aspect = static_cast<double>(wr.w) / wr.h;
    glUniform1f(m.sprite.loc("uAspect"), static_cast<float>(aspect));
    glUniform1i(m.sprite.loc("uTex"), 0);
    for (const SceneDraw& d : sc.artifacts) {
      const Impl::Picture* pic = m.picture(d.picture);
      if (!pic) {
        continue;
      }
      const double half_w =
          d.place.width * aspect / 2.0 * 2.0;  // half the width, where the picture's height is 2
      const double half_h = half_w * pic->image->height / pic->image->width;
      Impl::bind_unit(0, pic->tex);
      glUniform2f(m.sprite.loc("uCenter"), static_cast<float>(d.place.x * 2 - 1),
                  static_cast<float>(d.place.y * 2 - 1));
      glUniform2f(m.sprite.loc("uHalf"), static_cast<float>(half_w), static_cast<float>(half_h));
      glUniform1f(m.sprite.loc("uCos"), static_cast<float>(std::cos(d.place.rotation)));
      glUniform1f(m.sprite.loc("uSin"), static_cast<float>(std::sin(d.place.rotation)));
      glUniform1f(m.sprite.loc("uAlpha"), static_cast<float>(std::clamp(d.place.alpha, 0.0, 1.0)));
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    glDisable(GL_BLEND);
  }

  // 1. the previous picture, redrawn through the warp mesh and faded; the background and artifacts
  // go in here, so they get warped with it
  glBindFramebuffer(GL_FRAMEBUFFER, wr.fbo);
  glViewport(0, 0, wr.w, wr.h);
  glDisable(GL_BLEND);
  glUseProgram(m.warp.id);
  glBindBuffer(GL_ARRAY_BUFFER, m.mesh_pos);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glBindBuffer(GL_ARRAY_BUFFER, m.mesh_uv);
  if (state.mesh.size() == mesh_floats) {
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(mesh_floats * sizeof(float)),
                    state.mesh.data());
  }
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  Impl::bind_unit(0, rd.tex);
  glUniform1i(m.warp.loc("uPrev"), 0);
  m.cover_uniforms(m.warp, sc.background_a, sc.background_b, sc.background_mix, wr.w, wr.h);
  Impl::bind_unit(3, has_artifacts ? m.scene.tex : m.blank);
  glUniform1i(m.warp.loc("uScene"), 3);
  glUniform1f(m.warp.loc("uSceneAmt"), has_artifacts ? static_cast<float>(scene_into_loop) : 0.0F);
  glUniform1f(m.warp.loc("uDecay"), static_cast<float>(state.decay));
  glUniform1f(m.warp.loc("uFade"), 0.6F / 255.0F);
  glUniform1f(m.warp.loc("uInject"),
              static_cast<float>((1 - state.decay) * eff * background_into_loop));
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m.idx_buf);
  glDrawElements(GL_TRIANGLES, m.index_count, GL_UNSIGNED_SHORT, nullptr);

  if (!state.draws.empty()) {
    glUseProgram(m.fill.id);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnable(GL_BLEND);
    for (const Draw& d : state.draws) {
      m.draw_fill(d, wr.w);
    }
    glDisable(GL_BLEND);
  }

  glDisableVertexAttribArray(1);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, m.win_w, m.win_h);
  glUseProgram(m.composite.id);
  glBindBuffer(GL_ARRAY_BUFFER, m.tri_buf);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  Impl::bind_unit(0, wr.tex);
  glUniform1i(m.composite.loc("uTex"), 0);
  m.cover_uniforms(m.composite, sc.background_a, sc.background_b, sc.background_mix, m.win_w,
                   m.win_h);
  Impl::bind_unit(3, has_artifacts ? m.scene.tex : m.blank);
  glUniform1i(m.composite.loc("uScene"), 3);
  glUniform1f(m.composite.loc("uSceneDirect"),
              has_artifacts ? static_cast<float>(scene_direct) : 0.0F);
  glUniform1f(m.composite.loc("uWobble"),
              static_cast<float>(0.004 + 0.016 * std::clamp(sc.wobble, 0.0, 1.0)));
  glUniform1f(m.composite.loc("uClock"), static_cast<float>(sc.time));
  glUniform1f(m.composite.loc("uCoverAmt"), static_cast<float>(background_direct * eff));
  glUniform1f(m.composite.loc("uGamma"), static_cast<float>(c.gamma));
  glUniform1f(m.composite.loc("uHue"), static_cast<float>(c.hue));
  glUniform1f(m.composite.loc("uBright"),
              static_cast<float>(c.bright * std::clamp(gain, 0.0, 1.0)));
  glUniform1f(m.composite.loc("uSolar"), static_cast<float>(c.solarize));
  glUniform1f(m.composite.loc("uEchoA"), static_cast<float>(c.echo_alpha));
  glUniform1f(m.composite.loc("uEchoZ"), static_cast<float>(c.echo_zoom));
  glUniform1f(m.composite.loc("uEchoO"), static_cast<float>(c.echo_orient));
  glDrawArrays(GL_TRIANGLES, 0, 3);

  m.read = 1 - m.read;  // what was just drawn is next frame's "previous picture"
}

double Renderer::measure() {
  const Impl& m = *impl_;
  const Target& last = m.targets[m.read];
  glDisableVertexAttribArray(1);
  glBindFramebuffer(GL_FRAMEBUFFER, m.small.fbo);
  glViewport(0, 0, 16, 9);
  glUseProgram(m.measure.id);
  glBindBuffer(GL_ARRAY_BUFFER, m.tri_buf);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, last.tex);
  glUniform1i(m.measure.loc("uTex"), 0);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  std::uint8_t px[16 * 9 * 4];
  glReadPixels(0, 0, 16, 9, GL_RGBA, GL_UNSIGNED_BYTE, px);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  double sum = 0;
  for (int i = 0; i < 16 * 9 * 4; i += 4) {
    sum += px[i];
  }
  return sum / (16 * 9) / 255;
}

bool Renderer::read_window(std::vector<std::uint8_t>& rgba, int& width, int& height) {
  const Impl& m = *impl_;
  width = m.win_w;
  height = m.win_h;
  rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
  return glGetError() == GL_NO_ERROR;
}

}  // namespace fh
