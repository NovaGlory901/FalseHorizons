// The settings panel of falsehorizons (Dear ImGui), hidden until Tab is pressed. It only changes
// the Settings it is given and reports what the person asked for (PanelResult).
#pragma once

#include <SDL.h>

#include <string>
#include <vector>

#include "fh/preset.hpp"
#include "fh/settings.hpp"

namespace app {

struct PanelInfo {
  fh::Settings& settings;
  const std::vector<fh::Preset>& presets;  // sorted by name
  std::string current;
  std::vector<std::string>
      notes;  // what a translated .milk lost, for the current preset (empty for the others)
  int background_count = 0, artifact_count = 0;
  std::string background_folder, artifact_folder, preset_folder;
  std::string listening;  // "Speakers (monitor)" or "the demo song"
  std::vector<std::string> problems;
  std::string saved_note;  // "settings saved" / why they could not be
  double fps = 0, draw_scale = 1;
  int picture_w = 0, picture_h = 0;
};

struct PanelResult {
  bool next = false, prev = false;
  std::string choose;
  bool toggle_sound = false;
  bool open_backgrounds = false, open_artifacts = false, open_presets = false;
  bool fullscreen = false, quit = false;
};

class Panel {
 public:
  bool init(SDL_Window* window, void* gl_context);
  void shutdown();
  // Give every SDL event to this first. True means the panel used it (a click on it, typing into
  // it), so the program should not.
  bool handle_event(const SDL_Event& e);
  PanelResult draw(PanelInfo& info);  // call once a frame, before render()
  void render();
  bool visible = false;

 private:
  bool ready_ = false;
};

}  // namespace app
