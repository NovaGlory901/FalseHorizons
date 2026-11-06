#include "panel.hpp"

#include <algorithm>
#include <cstdio>

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"

namespace app {

bool Panel::init(SDL_Window* window, void* gl_context) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;  // do not leave an imgui.ini behind: the program keeps its settings in
                             // settings.json only
  io.LogFilename = nullptr;
  ImGui::StyleColorsDark();
  ImGuiStyle& st = ImGui::GetStyle();
  st.WindowRounding = 6;
  st.FrameRounding = 4;
  st.Colors[ImGuiCol_WindowBg] = ImVec4(0.04F, 0.04F, 0.07F, 0.86F);
  float dpi = 1;
  if (window) {
    int ww = 0;
    int wh = 0;
    int dw = 0;
    int dh = 0;
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_GL_GetDrawableSize(window, &dw, &dh);
    if (ww > 0) {
      dpi = static_cast<float>(dw) / static_cast<float>(ww);
    }
  }
  st.FontScaleMain = 1.25F * dpi;
  st.ScaleAllSizes(dpi);
  if (!ImGui_ImplSDL2_InitForOpenGL(window, gl_context)) {
    return false;
  }
  if (!ImGui_ImplOpenGL3_Init("#version 100")) {
    return false;
  }
  ready_ = true;
  return true;
}

void Panel::shutdown() {
  if (!ready_) {
    return;
  }
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  ready_ = false;
}

bool Panel::handle_event(const SDL_Event& e) {
  if (!ready_) {
    return false;
  }
  ImGui_ImplSDL2_ProcessEvent(&e);
  if (!visible) {
    return false;
  }
  const ImGuiIO& io = ImGui::GetIO();
  switch (e.type) {
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL:
      return io.WantCaptureMouse;
    case SDL_KEYDOWN:
    case SDL_KEYUP:
    case SDL_TEXTINPUT:
      return io.WantTextInput;  // only typing into a box; the shortcut keys always work, even right
                                // after a click on a slider
    default:
      return false;
  }
}

PanelResult Panel::draw(PanelInfo& info) {
  PanelResult out;
  if (!ready_) {
    return out;
  }
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();
  if (visible) {
    fh::Settings& s = info.settings;
    const float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImVec2(em * 0.8F, em * 0.8F), ImGuiCond_FirstUseEver);
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(
        ImVec2(std::min(em * 31, screen.x - em * 1.6F), std::min(em * 44, screen.y - em * 1.6F)),
        ImGuiCond_FirstUseEver);
    ImGui::Begin("FalseHorizon", &visible, ImGuiWindowFlags_NoCollapse);

    ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.5F);
    ImGui::TextDisabled("Listening to: %s", info.listening.c_str());
    ImGui::TextDisabled("%.0f frames a second, drawn at %dx%d", info.fps, info.picture_w,
                        info.picture_h);
    if (ImGui::Button("Switch sound")) {
      out.toggle_sound = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Full screen")) {
      out.fullscreen = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Quit")) {
      out.quit = true;
    }

    ImGui::SeparatorText("Look");
    ImGui::SliderInt("Motion", &s.intensity, 0, 100);
    ImGui::SliderInt("Fractal", &s.geometry, 0, 100);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("How strongly a slowly changing fractal surface bends the picture.");
    }
    ImGui::Checkbox("Calm mode", &s.calm);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Less motion, 24 frames a second and a stricter flash guard.");
    }

    ImGui::SeparatorText("Your pictures");
    ImGui::Checkbox("##bg", &s.show_backgrounds);
    ImGui::SameLine();
    ImGui::SliderInt("Background", &s.backgrounds, 0, 100);
    ImGui::Checkbox("##art", &s.show_artifacts);
    ImGui::SameLine();
    ImGui::SliderInt("Artifacts", &s.artifacts, 0, 100);
    ImGui::Text("%d background%s, %d artifact%s", info.background_count,
                info.background_count == 1 ? "" : "s", info.artifact_count,
                info.artifact_count == 1 ? "" : "s");
    if (ImGui::Button("Open backgrounds folder")) {
      out.open_backgrounds = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Open artifacts folder")) {
      out.open_artifacts = true;
    }
    ImGui::TextDisabled("Move a picture or GIF into a folder to use it.");
    ImGui::TextDisabled("Take it out to stop using it.");

    ImGui::SeparatorText("Presets");
    ImGui::Checkbox("Change by itself", &s.auto_change);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(em * 5);
    const char* labels[] = {"10 s", "15 s", "30 s", "1 min", "2 min"};
    int choice = -1;
    for (int i = 0; i < 5; i++) {
      if (fh::cycle_choices[i] == s.cycle_sec) {
        choice = i;
      }
    }
    const char* shown = choice >= 0 ? labels[choice] : "custom";
    if (ImGui::BeginCombo("every", shown)) {
      for (int i = 0; i < 5; i++) {
        if (ImGui::Selectable(labels[i], i == choice)) {
          s.cycle_sec = fh::cycle_choices[i];
        }
      }
      ImGui::EndCombo();
    }
    if (ImGui::Button("Previous")) {
      out.prev = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Next")) {
      out.next = true;
    }
    ImGui::SameLine();
    const bool liked = fh::is_favorite(s, info.current);
    if (ImGui::Button(liked ? "Unlike" : "Like")) {
      fh::toggle_favorite(s, info.current);
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Liked presets come up more often when it changes by itself.");
    }

    ImGui::BeginChild("list", ImVec2(0, em * 9), ImGuiChildFlags_Borders);
    for (const fh::Preset& p : info.presets) {
      const bool fav = fh::is_favorite(s, p.name);
      const std::string label = (fav ? "* " : "  ") + p.name;
      if (ImGui::Selectable(label.c_str(), p.name == info.current)) {
        out.choose = p.name;
      }
    }
    ImGui::EndChild();
    if (!info.notes.empty()) {
      ImGui::TextWrapped("Translated from MilkDrop:");
      for (const std::string& n : info.notes) {
        ImGui::BulletText("%s", n.c_str());
      }
    }
    if (ImGui::Button("Open presets folder")) {
      out.open_presets = true;
    }
    ImGui::TextDisabled("It reads .fh-preset.json and MilkDrop .milk files.");
    if (!info.problems.empty()) {
      ImGui::TextColored(ImVec4(1, 0.7F, 0.3F, 1), "%d preset file%s could not be used:",
                         static_cast<int>(info.problems.size()),
                         info.problems.size() == 1 ? "" : "s");
      for (std::size_t i = 0; i < info.problems.size() && i < 5; i++) {
        ImGui::BulletText("%s", info.problems[i].c_str());
      }
    }
    if (!info.saved_note.empty()) {
      ImGui::TextDisabled("%s", info.saved_note.c_str());
    }
    ImGui::PopItemWidth();
    ImGui::TextDisabled("Keys: Tab panel, N next, P back, L like,");
    ImGui::TextDisabled("Space pause, C calm, B next background,");
    ImGui::TextDisabled("1 and 2 hide the pictures, F full screen");
    ImGui::End();
  }
  ImGui::Render();
  return out;
}

void Panel::render() {
  if (!ready_ || !visible) {
    return;
  }
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

}  // namespace app
