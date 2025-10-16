#include "gui_subsystem.hpp"

GuiSubsystem::GuiSubsystem(PreferenceManager &preferences)
    : preferences_(preferences), gui_manager_(),
      runtime_(gui_manager_, preferences_) {}

bool GuiSubsystem::initialize(const std::string &window_title, int width,
                              int height) {
  gui_manager_.set_theme(preferences_.theme());
  if (!runtime_.start(window_title, width, height)) {
    return false;
  }
  preferences_.set_theme(gui_manager_.theme());
  return true;
}

void GuiSubsystem::shutdown() { runtime_.shutdown(); }

void GuiSubsystem::sync_imgui_ini() { runtime_.sync_imgui_ini_file(); }

void GuiSubsystem::apply_theme() {
  gui_manager_.set_theme(preferences_.theme());
}

