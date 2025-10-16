#pragma once

#include "gui/gui_manager.hpp"
#include "preferences/preference_manager.hpp"
#include "gui/gui_runtime.hpp"

#include <string>

class GuiSubsystem {
public:
  explicit GuiSubsystem(PreferenceManager &preferences);

  bool initialize(const std::string &window_title, int width, int height);
  void shutdown();

  GuiManager &manager() { return gui_manager_; }
  const GuiManager &manager() const { return gui_manager_; }

  void sync_imgui_ini();
  void apply_theme();

private:
  PreferenceManager &preferences_;
  GuiManager gui_manager_;
  GuiRuntime runtime_;
};
