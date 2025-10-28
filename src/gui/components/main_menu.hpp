#pragma once

#include "gui/gui_manager.hpp"
#include "history/history_manager.hpp"
#include "preferences/preference_manager.hpp"
#include <functional>

namespace ui {

struct MainMenuContext {
  history::HistoryManager &history;
  GuiManager &gui;
  PreferenceManager &preferences;
  PreferenceManager::UIPreferences &ui_prefs;
  bool &open_directory_dialog;
  std::function<void()> undo;
  std::function<void()> redo;
};

void render_main_menu(MainMenuContext &context);

} // namespace ui
