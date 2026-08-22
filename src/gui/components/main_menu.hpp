#pragma once

#include "gui/components/operator_commands.hpp"
#include "gui/gui_manager.hpp"
#include "gui/save_export_state.hpp"
#include "history/history_manager.hpp"
#include "patches/patch_session.hpp"
#include "preferences/preference_manager.hpp"
#include <functional>

namespace ui {

struct MainMenuContext {
  history::HistoryManager &history;
  GuiManager &gui;
  PreferenceManager &preferences;
  PreferenceManager::UIPreferences &ui_prefs;
  bool &open_add_folder_dialog;
  std::function<void()> sync_workspace;
  std::function<void(const std::filesystem::path &)> remove_workspace_folder;
  patches::PatchSession &patch_session;
  SaveExportState &save_state;
  std::function<void()> undo;
  std::function<void()> redo;
  OperatorEditState &operator_edit;
  /// Opens and closes one undo step around an operator command.
  std::function<void(const std::string &label)> begin_patch_history;
  std::function<void()> commit_patch_history;
};

void render_main_menu(MainMenuContext &context);

} // namespace ui
