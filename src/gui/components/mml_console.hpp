#pragma once

#include "preferences/preference_manager.hpp"
#include "ym2612/patch.hpp"
#include <functional>
#include <imgui.h>

namespace ui {

struct MmlConsoleContext {
  PreferenceManager::UIPreferences &ui_prefs;
  std::function<const ym2612::Patch &()> current_patch;
};

void render_mml_console(const char *title, MmlConsoleContext &context);

} // namespace ui
