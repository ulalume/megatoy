#include "gui_runtime.hpp"

#include "gui/components/preview/algorithm_preview.hpp"
#include "gui/components/preview/ssg_preview.hpp"

#include <filesystem>
#include <iostream>

GuiRuntime::GuiRuntime(GuiManager &manager, PreferenceManager &preferences)
    : manager_(manager), preferences_(preferences) {}

bool GuiRuntime::start(const std::string &window_title, int width, int height) {
  manager_.set_theme(preferences_.theme());
  if (!manager_.init(window_title, width, height)) {
    std::cerr << "Failed to initialize GUI system\n";
    return false;
  }

  sync_imgui_ini_file();

  if (!preferences_.initialize_file_dialog()) {
    std::cerr << "Native File Dialog unavailable; directory picker disabled\n";
  }

  return true;
}

void GuiRuntime::shutdown() {
  ui::reset_algorithm_preview_textures();
  ui::reset_ssg_preview_textures();
  manager_.shutdown();
}

void GuiRuntime::sync_imgui_ini_file() {
  const auto ini_path = preferences_.get_imgui_ini_file();
  manager_.set_imgui_ini_file(ini_path.generic_string());
}
