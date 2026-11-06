// falsehorizons: runs FalseHorizon on the sound the computer is playing (or, with --fake or when
// nothing can be heard, on a made-up song).
//
//   falsehorizons [--folder DIR] [--presets DIR] [--backgrounds DIR] [--artifacts DIR]
//           [--settings FILE] [--preset NAME] [--size WxH] [--device NAME] [--fake]
//           [--list-devices] [--where]
//           [--panel] [--shot FRAMES FILE.ppm [--live]]
//
// --shot saves the window after that many frames as a PPM and quits. It uses the made-up song and
// steady time so two runs match (unless --live) and touches no settings unless --settings is given.
#include <SDL.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "fh/adapt.hpp"
#include "fh/audio.hpp"
#include "fh/capture.hpp"
#include "fh/findroot.hpp"
#include "fh/flashguard.hpp"
#include "fh/fractal.hpp"
#include "fh/library.hpp"
#include "fh/pathutf8.hpp"
#include "fh/picker.hpp"
#include "fh/renderer.hpp"
#include "fh/runtime.hpp"
#include "fh/scene.hpp"
#include "fh/settings.hpp"
#include "fh/userfiles.hpp"
#include "panel.hpp"

#ifndef FH_VERSION
#define FH_VERSION "0"
#endif

namespace {

// Gives the same kind of bytes a browser's analyser would: a 512-bar spectrum (0..22 kHz) and 1024
// waveform samples.
class FakeSong {
 public:
  void fill(double t, std::vector<std::uint8_t>& spectrum, std::vector<std::uint8_t>& waveform) {
    spectrum.assign(512, 6);
    waveform.assign(1024, 128);
    const double beat_pos = std::fmod(t * 2.0, 1.0);  // 120 beats a minute
    const double kick = std::exp(-beat_pos * 7.0);
    const double snare = (std::fmod(t, 1.0) >= 0.5) ? std::exp(-std::fmod(t, 0.5) * 10.0) : 0.0;
    const double hat = std::exp(-std::fmod(t * 4.0, 1.0) * 9.0);
    for (int k = 0; k < 7; k++) {
      spectrum[static_cast<std::size_t>(k)] = clamp_byte(40 + 215 * kick * (1.0 - k * 0.08));
    }
    const int tone = 18 + static_cast<int>(12 + 10 * std::sin(t * 0.7));
    for (int k = -2; k <= 2; k++) {
      spectrum[static_cast<std::size_t>(tone + k)] =
          clamp_byte(160 - std::abs(k) * 40 + 30 * snare * 3);
    }
    for (int k = 60; k < 200; k++) {
      spectrum[static_cast<std::size_t>(k)] =
          clamp_byte(20 + 150 * snare * rnd() + 70 * hat * rnd());
    }
    for (int k = 200; k < 400; k++) {
      spectrum[static_cast<std::size_t>(k)] = clamp_byte(8 + 90 * hat * rnd());
    }
    for (int i = 0; i < 1024; i++) {
      const double x = i / 44100.0 + t;
      const double s = 0.45 * kick * std::sin(2 * std::numbers::pi * 55 * x) +
                       0.2 * std::sin(2 * std::numbers::pi * 440 * x) +
                       0.1 * snare * (rnd() - 0.5) * 2;
      waveform[static_cast<std::size_t>(i)] = clamp_byte(128 + 120 * s);
    }
  }

 private:
  std::mt19937 gen_{7};
  double rnd() { return std::uniform_real_distribution<double>(0, 1)(gen_); }
  static std::uint8_t clamp_byte(double v) {
    return static_cast<std::uint8_t>(std::clamp(v, 0.0, 255.0));
  }
};

// Backgrounds (one at a time, changing every so often if there are several) and artifacts (floating
// pictures), kept in step with two folders: a picture moved in appears within a couple of seconds,
// one taken out disappears. Everything is loaded onto the graphics card.
struct UserScene {
  struct Entry {
    std::filesystem::path path;
    fh::PictureId id = -1;
    double added_ms = 0;
  };
  struct Kind {
    fh::FolderWatcher watcher;
    std::vector<Entry> entries;
    std::set<std::filesystem::path>
        failed;  // could not be read; tried again only if the file changes
    const char* name;
    std::size_t limit;
    std::map<std::filesystem::path, unsigned>
        pending;  // being read by the loader thread (with the number of the request, so an answer
                  // for a replaced file is thrown away)
  };
  // Pictures are decoded on a thread of their own, one at a time, so the program starts (and keeps
  // drawing) at once however many or how big they are; only the last step, handing the finished
  // picture to the graphics card, happens on the main thread (one per frame).
  struct Job {
    Kind* kind;
    std::filesystem::path path;
    unsigned serial;
  };
  struct Done {
    Kind* kind{};
    std::filesystem::path path;
    unsigned serial{};
    fh::ImageResult result;
  };
  std::mutex load_lock;
  std::condition_variable load_wake;
  std::deque<Job> jobs;
  std::deque<Done> finished;
  std::thread loader;
  bool stop_loader = false;
  unsigned next_serial = 1;
  fh::Renderer& renderer;
  Kind backgrounds, artifacts;
  std::size_t bg = 0;
  int bg_next = -1;  // the one fading in, or -1
  double bg_fade_start = 0, bg_since = 0, last_scan = -1e9;
  bool show_backgrounds = true, show_artifacts = true;
  double background_amount = 0.6, artifact_amount = 1;
  fh::ArtifactField field{1};
  std::string notice;
  double notice_until = 0;
  static constexpr double fade_ms = 2600, cycle_ms = 20000, artifact_fade_ms = 1500,
                          scan_every_ms = 2000;

  UserScene(fh::Renderer& r, const std::filesystem::path& background_folder,
            const std::filesystem::path& artifact_folder)
      : renderer(r),
        backgrounds{fh::FolderWatcher(background_folder), {}, {}, "background", 40, {}},
        artifacts{fh::FolderWatcher(artifact_folder), {}, {}, "artifact", 12, {}} {
    loader = std::thread([this] { load_loop(); });
  }
  UserScene(const UserScene&) = delete;
  UserScene& operator=(const UserScene&) = delete;
  UserScene(UserScene&&) = delete;
  UserScene& operator=(UserScene&&) = delete;
  ~UserScene() {
    {
      const std::scoped_lock lk(load_lock);
      stop_loader = true;
      jobs.clear();
    }
    load_wake.notify_all();
    if (loader.joinable()) {
      loader.join();
    }
  }

  void load_loop() {
    std::unique_lock<std::mutex> lk(load_lock);
    for (;;) {
      load_wake.wait(lk, [&] { return stop_loader || !jobs.empty(); });
      if (stop_loader) {
        return;
      }
      Job j = std::move(jobs.front());
      jobs.pop_front();
      lk.unlock();
      fh::ImageResult r = fh::load_image_file(j.path);
      lk.lock();
      finished.push_back({j.kind, std::move(j.path), j.serial, std::move(r)});
    }
  }

  void say(const std::string& text, double now_ms) {
    notice = text;
    notice_until = now_ms + 6000;
    std::printf("%s\n", text.c_str());
    std::fflush(stdout);
  }

  static Entry* find(Kind& k, const std::filesystem::path& p) {
    for (Entry& e : k.entries) {
      if (e.path == p) {
        return &e;
      }
    }
    return nullptr;
  }
  void drop(Kind& k, const std::filesystem::path& p) {
    for (std::size_t i = 0; i < k.entries.size(); i++) {
      if (k.entries[i].path == p) {
        renderer.remove_picture(k.entries[i].id);
        k.entries.erase(k.entries.begin() + static_cast<std::ptrdiff_t>(i));
        return;
      }
    }
  }

  // Brings one kind in step with its folder: notes what came and went, and hands new pictures to
  // the loader thread.
  void reconcile(Kind& k, double now_ms) {
    const fh::FolderWatcher::Changes ch = k.watcher.poll();
    for (const auto& p : ch.changed) {
      drop(k, p);
      k.failed.erase(p);
      k.pending.erase(p);
    }  // a replaced picture is read again
    for (const auto& p : ch.added) {
      k.failed.erase(p);
    }
    for (const auto& p : ch.removed) {
      drop(k, p);
      k.failed.erase(p);
      k.pending.erase(p);
      say(std::string("removed ") + k.name + ": " + fh::path_to_utf8(p.filename()), now_ms);
    }
    for (const auto& p : fh::list_pictures(k.watcher.folder())) {
      if (find(k, p) || k.failed.contains(p) || k.pending.contains(p)) {
        continue;
      }
      if (k.entries.size() + k.pending.size() >= k.limit) {
        break;  // as many as can be shown; the rest wait for room
      }
      const unsigned serial = next_serial++;
      k.pending[p] = serial;
      {
        const std::scoped_lock lk(load_lock);
        jobs.push_back({&k, p, serial});
      }
      load_wake.notify_one();
    }
  }

  void collect(double now_ms) {
    Done d;
    {
      const std::scoped_lock lk(load_lock);
      if (finished.empty()) {
        return;
      }
      d = std::move(finished.front());
      finished.pop_front();
    }
    Kind& k = *d.kind;
    const auto it = k.pending.find(d.path);
    if (it == k.pending.end() || it->second != d.serial) {
      return;  // the file was taken out or replaced while it was being read
    }
    k.pending.erase(it);
    const fh::PictureId id =
        d.result.ok
            ? renderer.add_picture(std::make_shared<const fh::Image>(std::move(d.result.image)))
            : -1;
    if (id < 0) {
      k.failed.insert(d.path);
      say("could not use " + fh::path_to_utf8(d.path.filename()) + ": " +
              (d.result.ok ? "the graphics card could not take it" : d.result.error),
          now_ms);
      return;
    }
    std::filesystem::path current;
    if (!backgrounds.entries.empty()) {
      current = backgrounds.entries[bg].path;
    }
    k.entries.push_back({d.path, id, now_ms});
    say(std::string("added ") + k.name + ": " + fh::path_to_utf8(d.path.filename()), now_ms);
    if (current.empty() && &k == &backgrounds) {
      bg_since = now_ms;
    }
  }

  // For a screenshot run: waits until every picture has been read, so the saved picture is the same
  // every time.
  void settle(double now_ms) {
    update(now_ms);
    while (!backgrounds.pending.empty() || !artifacts.pending.empty()) {
      collect(now_ms);
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }

  void update(double now_ms) {
    collect(now_ms);
    if (now_ms - last_scan < scan_every_ms) {
      return;
    }
    last_scan = now_ms;
    std::filesystem::path current;
    if (!backgrounds.entries.empty()) {
      current = backgrounds.entries[bg].path;
    }
    reconcile(backgrounds, now_ms);
    reconcile(artifacts, now_ms);
    bg = 0;
    for (std::size_t i = 0; i < backgrounds.entries.size(); i++) {
      if (backgrounds.entries[i].path == current) {
        bg = i;
      }
    }
    if (bg_next >= 0 && static_cast<std::size_t>(bg_next) >= backgrounds.entries.size()) {
      bg_next = -1;
    }
    if (backgrounds.entries.size() < 2) {
      bg_next = -1;
    }
    if (current.empty() && !backgrounds.entries.empty()) {
      bg_since = now_ms;
    }
  }
  void next_background(double now_ms) {
    if (backgrounds.entries.size() < 2) {
      return;
    }
    if (bg_next >= 0) {
      bg = static_cast<std::size_t>(bg_next);
      bg_next = -1;
    }  // a change in the middle of a change: finish the first at once
    bg_next = static_cast<int>((bg + 1) % backgrounds.entries.size());
    bg_fade_start = now_ms;
    bg_since = now_ms;
  }

  fh::SceneInputs inputs(double now_ms, double bass, double beat) {
    update(now_ms);
    fh::SceneInputs in;
    in.time = now_ms / 1000.0;
    in.wobble = bass;
    const auto& bgs = backgrounds.entries;
    if (show_backgrounds && !bgs.empty()) {
      if (bg_next >= 0 && now_ms - bg_fade_start >= fade_ms) {
        bg = static_cast<std::size_t>(bg_next);
        bg_next = -1;
      }
      if (bg_next < 0 && bgs.size() > 1 && now_ms - bg_since > cycle_ms) {
        next_background(now_ms);
      }
      in.background_amount = background_amount;
      in.background_a = bgs[bg].id;
      if (bg_next >= 0) {
        in.background_b = bgs[static_cast<std::size_t>(bg_next)].id;
        const double t = std::min(1.0, (now_ms - bg_fade_start) / fade_ms);
        in.background_mix = t * t * (3 - 2 * t);
      }
    }
    if (show_artifacts) {
      for (std::size_t i = 0; i < artifacts.entries.size(); i++) {
        fh::SceneDraw d;
        d.picture = artifacts.entries[i].id;
        d.place = field.place(i, now_ms / 1000.0, bass, beat);
        d.place.alpha *= artifact_amount *
                         fh::smooth_fade(now_ms - artifacts.entries[i].added_ms, artifact_fade_ms);
        in.artifacts.push_back(d);
      }
    }
    return in;
  }
};

// Changing preset: the old one fades out as the new one fades in (as in the web version); the
// PresetPicker decides which comes next.
struct Player {
  std::vector<fh::Preset> presets;
  std::unique_ptr<fh::PresetRuntime> cur, to;
  std::string cur_name, to_name;
  double fade_start = 0, fade_dur = 2500, since = 0;
  fh::PresetPicker picker;
  std::mt19937 gen{std::random_device{}()};

  [[nodiscard]] const fh::Preset* find(const std::string& name) const {
    for (const fh::Preset& p : presets) {
      if (p.name == name) {
        return &p;
      }
    }
    return nullptr;
  }
  [[nodiscard]] std::vector<std::string> names() const {
    std::vector<std::string> v;
    v.reserve(presets.size());
    for (const fh::Preset& p : presets) {
      v.push_back(p.name);
    }
    return v;
  }
  bool start(const std::string& name, double now_ms) {
    const fh::Preset* p = find(name);
    if (!p) {
      return false;
    }
    try {
      cur = std::make_unique<fh::PresetRuntime>(*p);
    } catch (const std::exception& e) {
      std::fprintf(stderr, "preset \"%s\" failed: %s\n", name.c_str(), e.what());
      return false;
    }
    cur_name = name;
    to.reset();
    since = now_ms;
    picker.show(name);
    return true;
  }
  // Fades to a preset. `by_hand`: remember it so that Previous can come back to it.
  bool change(const std::string& name, double now_ms, bool calm, bool by_hand) {
    const fh::Preset* p = find(name);
    if (!p) {
      return false;
    }
    std::unique_ptr<fh::PresetRuntime> rt;
    try {
      rt = std::make_unique<fh::PresetRuntime>(*p);
    } catch (const std::exception& e) {
      std::fprintf(stderr, "preset \"%s\" failed: %s\n", name.c_str(), e.what());
      return false;
    }
    if (to) {
      cur = std::move(to);
      cur_name = to_name;
    }  // a change in the middle of a change: finish the first at once
    to = std::move(rt);
    to_name = name;
    fade_start = now_ms;
    fade_dur = calm ? 3500 : 2500;
    since = now_ms;
    if (by_hand && (picker.index() < 0 ||
                    picker.history()[static_cast<std::size_t>(picker.index())] != name)) {
      picker.show(name);
    }
    return true;
  }
  // Half the time, if some presets are liked, only those are in the running (as in the web
  // version).
  std::vector<std::string> pool(const std::vector<std::string>& liked) {
    const std::vector<std::string> all = names();
    std::vector<std::string> liked_here;
    for (const std::string& n : liked) {
      if (std::ranges::find(all, n) != all.end()) {
        liked_here.push_back(n);
      }
    }
    return (!liked_here.empty() && std::uniform_real_distribution<double>(0, 1)(gen) < 0.5)
               ? liked_here
               : all;
  }
  bool step(int dir, const std::vector<std::string>& liked, double now_ms, bool calm) {
    std::optional<std::string> name = dir > 0 ? picker.next(pool(liked)) : picker.prev();
    if (!name && dir < 0) {  // nothing earlier was shown: Previous picks a different one instead of
                             // doing nothing
      std::vector<std::string> others;
      for (const std::string& n : names()) {
        if (n != this->name()) {
          others.push_back(n);
        }
      }
      if (others.empty()) {
        return false;
      }
      return change(others[std::uniform_int_distribution<std::size_t>(0, others.size() - 1)(gen)],
                    now_ms, calm, true);
    }
    return name && change(*name, now_ms, calm, false);
  }
  [[nodiscard]] const std::string& name() const { return to ? to_name : cur_name; }
};

// Opens a folder in the computer's file manager (a file:// address, with anything odd in the path
// written as %XX).
void open_folder(const std::filesystem::path& folder) {
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
#ifdef _WIN32
  // Windows opens a plain folder path in Explorer (a file:// address would need file:///C:/ and
  // care with the drive colon).
  const std::string win_path =
      fh::path_to_utf8(std::filesystem::absolute(folder, ec).make_preferred());
  if (SDL_OpenURL(win_path.c_str()) != 0)
    std::fprintf(stderr, "could not open the folder %s: %s\n", win_path.c_str(), SDL_GetError());
  return;
#endif
  const std::u8string generic = std::filesystem::absolute(folder, ec).generic_u8string();
  const std::string raw(generic.begin(), generic.end());
  std::string url = "file://";
  for (const unsigned char c : raw) {
    if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
      url += static_cast<char>(c);
    } else {
      char buf[4];
      std::snprintf(buf, sizeof buf, "%%%02X", c);
      url += buf;
    }
  }
  if (SDL_OpenURL(url.c_str()) != 0) {
    std::fprintf(stderr, "could not open the folder %s: %s\n", raw.c_str(), SDL_GetError());
  }
}

void write_ppm(const std::string& path, const std::vector<std::uint8_t>& rgba, int w, int h) {
  std::ofstream out(fh::path_from_utf8(path), std::ios::binary);
  out << "P6\n" << w << " " << h << "\n255\n";
  for (int y = h - 1; y >= 0; y--) {
    for (int x = 0; x < w; x++) {
      const std::uint8_t* p = &rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                                     static_cast<std::size_t>(x)) *
                                    4];
      out.put(static_cast<char>(p[0])).put(static_cast<char>(p[1])).put(static_cast<char>(p[2]));
    }
  }
}

}  // namespace

// Whole-string decimal parse; false for anything else (unlike atoi and sscanf, which accept junk).
static bool parse_int(std::string_view text, int& out) {
  const char* end = text.data() + text.size();
  const auto [ptr, ec] = std::from_chars(text.data(), end, out);
  return ec == std::errc() && ptr == end;
}

static bool parse_size(std::string_view text, int& w, int& h) {
  const auto x = text.find('x');
  return x != std::string_view::npos && parse_int(text.substr(0, x), w) &&
         parse_int(text.substr(x + 1), h);
}

static int run(int argc, char* const* argv) {
#ifdef _WIN32
  // The program is a window program (no console box when started from the Start menu). Started from
  // a terminal, print there (for --where, --version, --list-devices and the messages), unless the
  // output is already going somewhere (a file or a pipe).
  {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if ((out == nullptr || out == INVALID_HANDLE_VALUE) && AttachConsole(ATTACH_PARENT_PROCESS)) {
      std::freopen("CONOUT$", "w", stdout);
      std::freopen("CONOUT$", "w", stderr);
    }
  }
#endif
  std::string folder_arg;
  std::string presets_arg;
  std::string backgrounds_arg;
  std::string artifacts_arg;
  std::string settings_arg;
  std::string wanted;
  std::string shot_file;
  std::string device_hint;
  int width = 960;
  int height = 540;
  int shot_frames = 0;
  bool fake = false;
  bool live = false;
  bool list_devices = false;
  bool where = false;
  bool show_panel = false;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--folder" && i + 1 < argc) {
      folder_arg = argv[++i];
    } else if (a == "--presets" && i + 1 < argc) {
      presets_arg = argv[++i];
    } else if (a == "--backgrounds" && i + 1 < argc) {
      backgrounds_arg = argv[++i];
    } else if (a == "--artifacts" && i + 1 < argc) {
      artifacts_arg = argv[++i];
    } else if (a == "--settings" && i + 1 < argc) {
      settings_arg = argv[++i];
    } else if (a == "--preset" && i + 1 < argc) {
      wanted = argv[++i];
    } else if (a == "--size" && i + 1 < argc) {
      if (!parse_size(argv[++i], width, height)) {
        std::fprintf(stderr, "--size wants WIDTHxHEIGHT\n");
        return 2;
      }
    } else if (a == "--device" && i + 1 < argc) {
      device_hint = argv[++i];
    } else if (a == "--fake") {
      fake = true;
    } else if (a == "--live") {
      live = true;
    } else if (a == "--list-devices") {
      list_devices = true;
    } else if (a == "--where") {
      where = true;
    } else if (a == "--version") {
      std::printf("falsehorizons %s\n", FH_VERSION);
      return 0;
    } else if (a == "--panel") {
      show_panel = true;
    } else if (a == "--shot" && i + 2 < argc) {
      if (!parse_int(argv[++i], shot_frames)) {
        std::fprintf(stderr, "--shot wants a frame count\n");
        return 2;
      }
      shot_file = argv[++i];
    } else {
      std::fprintf(
          stderr,
          "usage: falsehorizons [--folder DIR] [--presets DIR] [--backgrounds DIR] "
          "[--artifacts DIR] [--settings FILE] [--preset NAME] [--size WxH] [--device NAME] "
          "[--fake] "
          "[--list-devices] [--where] [--version] [--panel] [--shot FRAMES FILE.ppm [--live]]\n");
      return 2;
    }
  }

  // The folders sit next to the program if it has a presets/ folder there (an installed copy), else
  // in the nearest parent that has one (a build tree inside the source folder).
  std::filesystem::path root = fh::path_from_utf8(folder_arg);
  if (root.empty()) {
    root = ".";
    if (char* base = SDL_GetBasePath()) {
      root = fh::find_root(fh::path_from_utf8(base), "presets");
      SDL_free(base);
    }
  }
  const std::filesystem::path preset_dir =
      presets_arg.empty() ? root / "presets" : fh::path_from_utf8(presets_arg);
  const std::filesystem::path background_dir =
      backgrounds_arg.empty() ? root / "backgrounds" : fh::path_from_utf8(backgrounds_arg);
  const std::filesystem::path artifact_dir =
      artifacts_arg.empty() ? root / "artifacts" : fh::path_from_utf8(artifacts_arg);
  const bool headless = shot_frames > 0;
  const bool keep_settings =
      !headless || !settings_arg.empty();  // a screenshot run leaves no files behind unless asked
  const std::filesystem::path settings_file =
      settings_arg.empty() ? root / "settings.json" : fh::path_from_utf8(settings_arg);

  if (where) {
    std::printf("presets:     %s\nbackgrounds: %s\nartifacts:   %s\nsettings:    %s\n",
                fh::path_to_utf8(preset_dir).c_str(), fh::path_to_utf8(background_dir).c_str(),
                fh::path_to_utf8(artifact_dir).c_str(), fh::path_to_utf8(settings_file).c_str());
    return 0;
  }

  if (list_devices) {
    std::string err;
    const auto devices = fh::AudioCapture::list_devices(err);
    if (!err.empty()) {
      std::fprintf(stderr, "%s\n", err.c_str());
    }
    for (const auto& d : devices) {
      std::printf("%s%s%s\n", d.name.c_str(), d.monitor ? "   [listens to an output]" : "",
                  d.is_default ? "   [default]" : "");
    }
    return err.empty() ? 0 : 1;
  }

  fh::Library lib = fh::load_library(preset_dir);
  for (const std::string& p : lib.problems) {
    std::fprintf(stderr, "preset problem: %s\n", p.c_str());
  }
  if (lib.presets.empty()) {
    std::fprintf(stderr, "no presets found in %s (try --folder DIR)\n",
                 fh::path_to_utf8(preset_dir).c_str());
    return 1;
  }

  fh::Settings settings;
  if (keep_settings) {
    const fh::SettingsLoad loaded = fh::load_settings(settings_file);
    if (!loaded.problem.empty()) {
      std::fprintf(stderr, "%s\n", loaded.problem.c_str());
    }
    settings = loaded.settings;
  }

#ifdef _WIN32
  // The program links ANGLE's libGLESv2, so the context must come from ANGLE's EGL too (not a
  // native WGL "ES profile" context, which libGLESv2 could not use).
  SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
#endif
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "SDL could not start: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_Window* window =
      SDL_CreateWindow("FalseHorizon", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width,
                       height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
  if (!window) {
    std::fprintf(stderr, "could not open a window: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GLContext gl = SDL_GL_CreateContext(window);
  if (!gl) {
    std::fprintf(stderr, "could not get an OpenGL ES 2 context: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_SetSwapInterval(1);

  fh::Renderer renderer;
  std::string error;
  if (!renderer.init(error)) {
    std::fprintf(stderr, "the renderer could not start: %s\n", error.c_str());
    return 1;
  }
  fh::AdaptiveScale adapt;
  const auto fit_to_window = [&] {
    int w = 0;
    int h = 0;
    SDL_GL_GetDrawableSize(window, &w, &h);
    renderer.resize(w, h, adapt.scale());
  };
  fit_to_window();

  app::Panel panel;
  if (!panel.init(window, gl)) {
    std::fprintf(stderr, "the settings panel could not start\n");
  }
  panel.visible = show_panel;

  UserScene scene(renderer, background_dir, artifact_dir);
  if (headless && !live) {
    scene.settle(0);  // (a screenshot with simulated time waits for the pictures so it comes out
  }
  // the same every time; a live one does not)
  fh::FolderWatcher preset_watcher(preset_dir, fh::looks_like_preset);
  preset_watcher.poll();

  Player player;
  player.presets = lib.presets;
  std::string first_name = player.presets.front().name;
  if (!wanted.empty()) {
    if (!player.find(wanted)) {
      std::fprintf(stderr, "no preset called \"%s\"\n", wanted.c_str());
      return 1;
    }
    first_name = wanted;
  } else if (player.find(settings.last)) {
    first_name = settings.last;
  } else {
    const auto names = player.names();
    first_name = names[std::uniform_int_distribution<std::size_t>(0, names.size() - 1)(player.gen)];
  }
  if (!player.start(first_name, 0)) {
    for (const std::string& n : player.names()) {
      if (player.start(n, 0)) {
        break;
      }
    }
    if (!player.cur) {
      std::fprintf(stderr, "no preset could be started\n");
      return 1;
    }
  }

  FakeSong song;
  fh::AudioCapture capture;
  bool use_capture = false;
  if (!fake && (!headless || live || !device_hint.empty())) {
    std::string err;
    if (capture.start(device_hint, err)) {
      use_capture = true;
      std::printf("listening to: %s (%.0f Hz)\n", capture.device_name().c_str(),
                  capture.sample_rate());
    } else {
      std::fprintf(stderr, "could not listen to the sound (%s); using the made-up song\n",
                   err.c_str());
    }
  }
  fh::AudioFeatures features;
  fh::FlashGuard guard(settings.calm);
  std::vector<std::uint8_t> spectrum;
  std::vector<std::uint8_t> waveform;
  bool running = true;
  bool was_calm = settings.calm;
  double gain = 1;
  long frames = 0;
  const Uint64 freq = SDL_GetPerformanceFrequency();
  const Uint64 t0 = SDL_GetPerformanceCounter();
  double last_shown = 0;
  double last_draw = 0;
  double last_preset_scan = 0;
  double last_frame_at = 0;
  int fps_frames = 0;
  double fps = 0;
  fh::Settings saved_copy = settings;
  double changed_at = -1;
  std::string saved_note;
  const auto save_now = [&] {
    if (!keep_settings) {
      return;
    }
    std::string err;
    saved_note = fh::save_settings(settings_file, settings, err)
                     ? "Settings saved."
                     : "Settings could not be saved: " + err;
    saved_copy = settings;
    changed_at = -1;
  };
  const auto now_ms = [&]() {
    return static_cast<double>(SDL_GetPerformanceCounter() - t0) * 1000.0 /
           static_cast<double>(freq);
  };
  const auto switch_sound = [&] {
    if (use_capture) {
      capture.stop();
      use_capture = false;
    } else {
      std::string err;
      if (capture.start(device_hint, err)) {
        use_capture = true;
      } else {
        std::fprintf(stderr, "could not listen to the sound: %s\n", err.c_str());
      }
    }
  };
  const auto toggle_fullscreen = [&] {
    SDL_SetWindowFullscreen(window, (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP)
                                        ? 0
                                        : SDL_WINDOW_FULLSCREEN_DESKTOP);
  };

  while (running) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        running = false;
        continue;
      }
      if (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                                        e.window.event == SDL_WINDOWEVENT_RESIZED)) {
        fit_to_window();
        continue;
      }
      if (panel.handle_event(e)) {
        continue;
      }
      if (e.type == SDL_KEYDOWN) {
        const double now = now_ms();
        switch (e.key.keysym.sym) {
          case SDLK_TAB:
            panel.visible = !panel.visible;
            break;
          case SDLK_ESCAPE:
            if (panel.visible) {
              panel.visible = false;
            } else {
              running = false;
            }
            break;
          case SDLK_q:
            running = false;
            break;
          case SDLK_n:
          case SDLK_RIGHT:
            player.step(+1, settings.favorites, now, settings.calm);
            break;
          case SDLK_p:
          case SDLK_LEFT:
            player.step(-1, settings.favorites, now, settings.calm);
            break;
          case SDLK_l:
            fh::toggle_favorite(settings, player.name());
            scene.say(fh::is_favorite(settings, player.name())
                          ? "liked " + player.name()
                          : "no longer liked: " + player.name(),
                      now);
            break;
          case SDLK_SPACE:
            settings.auto_change = !settings.auto_change;
            break;
          case SDLK_b:
            scene.next_background(now);
            break;
          case SDLK_1:
            settings.show_backgrounds = !settings.show_backgrounds;
            break;
          case SDLK_2:
            settings.show_artifacts = !settings.show_artifacts;
            break;
          case SDLK_a:
            switch_sound();
            break;
          case SDLK_c:
            settings.calm = !settings.calm;
            break;
          case SDLK_f:
            toggle_fullscreen();
            break;
          default:
            break;
        }
      }
    }
    if (settings.calm != was_calm) {
      was_calm = settings.calm;
      guard.set_calm(settings.calm);
    }
    const bool calm = settings.calm;

    const double now =
        (headless && !use_capture)
            ? static_cast<double>(frames) *
                  (1000.0 /
                   60.0)  // a screenshot run uses steady time, not the clock, unless it listens
            : now_ms();
    if (headless && use_capture) {
      SDL_Delay(16);  // a listening screenshot run has to let sound arrive
    }
    if (calm && !headless && now - last_draw < 1000.0 / 24 - 2) {
      SDL_Delay(2);
      continue;
    }
    if (last_frame_at > 0 && adapt.frame(now - last_frame_at, calm)) {
      fit_to_window();
    }
    last_frame_at = now + 1e-9;
    last_draw = now;

    if (!headless && now - last_preset_scan > 2000) {
      last_preset_scan = now;
      if (preset_watcher.poll().any()) {
        fh::Library fresh = fh::load_library(preset_dir);
        for (const std::string& p : fresh.problems) {
          std::fprintf(stderr, "preset problem: %s\n", p.c_str());
        }
        if (!fresh.presets.empty()) {
          scene.say("presets reloaded (" + std::to_string(fresh.presets.size()) + ")", now);
          player.presets = fresh.presets;
          lib = std::move(fresh);
        } else {
          scene.say("no usable presets in the folder; keeping the old ones", now);
        }
      }
    }

    if (use_capture) {
      capture.read(spectrum, waveform);
      features.update(spectrum, waveform, capture.sample_rate(), now);
    } else {
      song.fill(now / 1000.0, spectrum, waveform);
      features.update(spectrum, waveform, 44100, now);
    }

    fh::FrameInputs in;
    in.time = now / 1000.0;
    in.frame = static_cast<double>(frames++);
    in.bass = features.bass;
    in.mid = features.mid;
    in.treb = features.treb;
    in.bass_att = features.bass_att;
    in.mid_att = features.mid_att;
    in.treb_att = features.treb_att;
    in.level = features.level;
    in.beat = calm ? features.beat * 0.4 : features.beat;
    in.aspect = renderer.aspect();
    in.pixelsx = renderer.width();
    in.pixelsy = renderer.height();
    in.aspectx = renderer.width() > renderer.height()
                     ? static_cast<double>(renderer.height()) / renderer.width()
                     : 1;
    in.aspecty = renderer.width() > renderer.height()
                     ? 1
                     : static_cast<double>(renderer.width()) / renderer.height();
    in.fps = fps > 0 ? fps : 60;
    in.wave = features.wave;
    in.spec = features.spec;

    if (!player.to && settings.auto_change && now - player.since > settings.cycle_sec * 1000.0) {
      player.step(+1, settings.favorites, now, calm);
    }

    double t = 0;
    if (player.to) {
      t = std::min(1.0, (now - player.fade_start) / player.fade_dur);
      t = t * t * (3 - 2 * t);
    }
    const double motion = (0.1 + 0.9 * settings.intensity / 100.0) * (calm ? 0.4 : 1.0);
    const double bump =
        std::sin(std::numbers::pi *
                 t);  // in the middle of a change both presets draw at nearly full strength
    fh::FrameState state;
    try {
      fh::FrameOptions oa;
      oa.motion = motion;
      oa.weight = player.to ? std::min(1.0, 1 - t + 0.6 * bump) : 1.0;
      fh::FrameState a = player.cur->frame(in, oa);
      if (player.to) {
        fh::FrameOptions ob;
        ob.motion = motion;
        ob.weight = std::min(1.0, t + 0.6 * bump);
        const fh::FrameState b = player.to->frame(in, ob);
        state = fh::blend_states(&a, &b, t);
        if (now - player.fade_start >= player.fade_dur) {
          player.cur = std::move(player.to);
          player.cur_name = player.to_name;
        }
      } else {
        state = std::move(a);
      }
    } catch (const std::exception& ex) {
      std::fprintf(stderr, "preset failed: %s\n", ex.what());
      return 1;
    }
    if (settings.geometry > 0) {
      fh::fractal_warp(state.mesh, in, settings.geometry / 100.0, calm);
    }

    renderer.animate_pictures(now);
    scene.show_backgrounds = settings.show_backgrounds;
    scene.show_artifacts = settings.show_artifacts;
    scene.background_amount = settings.backgrounds / 100.0;
    scene.artifact_amount = settings.artifacts / 100.0;
    renderer.draw(state, gain, scene.inputs(now, features.bass, features.beat));
    if (frames % 6 == 1) {
      gain = guard.feed(renderer.measure(), now);
    }

    {
      app::PanelInfo info{settings, player.presets};
      info.current = player.name();
      if (const auto it = lib.notes.find(info.current); it != lib.notes.end()) {
        info.notes = it->second;
      }
      info.background_count = static_cast<int>(scene.backgrounds.entries.size());
      info.artifact_count = static_cast<int>(scene.artifacts.entries.size());
      info.background_folder = fh::path_to_utf8(background_dir);
      info.artifact_folder = fh::path_to_utf8(artifact_dir);
      info.preset_folder = fh::path_to_utf8(preset_dir);
      info.listening = use_capture ? capture.device_name() : "a made-up demo song";
      info.problems = lib.problems;
      info.saved_note = saved_note;
      info.fps = fps;
      info.draw_scale = adapt.scale();
      info.picture_w = renderer.width();
      info.picture_h = renderer.height();
      const app::PanelResult pr = panel.draw(info);
      panel.render();
      if (pr.next) {
        player.step(+1, settings.favorites, now, calm);
      }
      if (pr.prev) {
        player.step(-1, settings.favorites, now, calm);
      }
      if (!pr.choose.empty() && pr.choose != player.name()) {
        player.change(pr.choose, now, calm, true);
      }
      if (pr.toggle_sound) {
        switch_sound();
      }
      if (pr.open_backgrounds) {
        open_folder(background_dir);
      }
      if (pr.open_artifacts) {
        open_folder(artifact_dir);
      }
      if (pr.open_presets) {
        open_folder(preset_dir);
      }
      if (pr.fullscreen) {
        toggle_fullscreen();
      }
      if (pr.quit) {
        running = false;
      }
    }

    if (headless && frames >= shot_frames) {
      std::vector<std::uint8_t> px;
      int w = 0;
      int h = 0;
      if (!renderer.read_window(px, w, h)) {
        std::fprintf(stderr, "could not read the window\n");
        return 1;
      }
      write_ppm(shot_file, px, w, h);
      std::printf(
          "saved %s (%dx%d) after %ld frames; preset \"%s\", flash guard gain %.2f, bass %.2f mid "
          "%.2f treb %.2f\n",
          shot_file.c_str(), w, h, frames, player.name().c_str(), gain, features.bass, features.mid,
          features.treb);
      break;
    }
    SDL_GL_SwapWindow(window);

    // keep the choices: a second after the last change, and again on the way out
    settings.last = player.name();
    if (!(settings == saved_copy)) {
      if (changed_at < 0) {
        changed_at = now;
      } else if (now - changed_at > 1500) {
        save_now();
      }
    }

    fps_frames++;
    if (now - last_shown > 1000) {
      fps = fps_frames * 1000.0 / std::max(1.0, now - last_shown);
      const std::string title = "FalseHorizon - " + player.name() + (calm ? " (calm)" : "") +
                                (use_capture ? "" : " (demo song)") + " - " +
                                std::to_string(static_cast<int>(std::lround(fps))) + " fps" +
                                (now < scene.notice_until ? " - " + scene.notice : "");
      SDL_SetWindowTitle(window, title.c_str());
      fps_frames = 0;
      last_shown = now;
    }
  }
  if (!(settings == saved_copy)) {
    save_now();
  }
  panel.shutdown();
  SDL_GL_DeleteContext(gl);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}

// SDL renames main to SDL_main on Windows and needs exactly this signature to link.
int main(int argc, char* argv[]) {
  try {
    return run(argc, argv);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "fatal: %s\n", e.what());
  } catch (...) {
    std::fprintf(stderr, "fatal: unknown error\n");
  }
  return 1;
}
