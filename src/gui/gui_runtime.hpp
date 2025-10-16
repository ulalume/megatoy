#pragma once

#include "gui/gui_manager.hpp"
#include "preferences/preference_manager.hpp"

#include <string>

class GuiRuntime {
public:
  GuiRuntime(GuiManager &manager, PreferenceManager &preferences);

  bool start(const std::string &window_title, int width, int height);
  void shutdown();
  void sync_imgui_ini_file();

  GuiManager &manager() { return manager_; }
  const GuiManager &manager() const { return manager_; }

private:
  GuiManager &manager_;
  PreferenceManager &preferences_;
};
