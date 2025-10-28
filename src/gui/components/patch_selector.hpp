#pragma once

#include "patches/patch_repository.hpp"
#include "patches/patch_session.hpp"
#include "preferences/preference_manager.hpp"
#include <imgui.h>

#include <filesystem>
#include <functional>

namespace ui {

struct PatchSelectorContext {
  patches::PatchRepository &repository;
  patches::PatchSession &session;
  PreferenceManager::UIPreferences &prefs;
  std::function<void(const patches::PatchEntry &)> safe_load_patch;
  std::function<void(const std::filesystem::path &)> reveal_in_file_manager;
};

// Function to render patch browser panel
void render_patch_selector(const char *title, PatchSelectorContext &context);

} // namespace ui
