#pragma once

#include "formats/ginpkg.hpp"
#include "gui/window_title.hpp"
#include "patches/patch_session.hpp"
#include "preferences/preference_manager.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ui {

struct PatchHistoryState {
  std::string loaded_path;
  std::vector<formats::ginpkg::HistoryEntry> versions;
  std::string current_data;
  std::string error_message;
  bool refresh_requested = true;
  std::string selected_uuid;
  std::optional<std::filesystem::file_time_type> last_write_time;
};

struct PatchHistoryContext {
  patches::PatchSession &patch_session;
  PreferenceManager::UIPreferences &prefs;
};

void render_patch_history(const char *title, PatchHistoryContext &context,
                          PatchHistoryState &state);

} // namespace ui
