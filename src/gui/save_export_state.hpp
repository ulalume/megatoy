#pragma once

#include "platform/platform_config.hpp"

#include <filesystem>
#include <optional>
#include <string>

#if defined(MEGATOY_PLATFORM_WEB)
#include <imgui.h>
#endif

namespace ui {

// Results are announced as status toasts; only
// genuine decisions still open a dialog.
struct SaveExportState {
  bool overwrite_confirmation_pending = false;
  bool save_as_requested = false;
  std::optional<std::string> pending_save_as_extension;
  std::optional<std::string> pending_save_as_stem;
  std::optional<std::filesystem::path> pending_save_as_folder;

#if defined(MEGATOY_PLATFORM_WEB)
  /// Bottom-left of the button that asked for Save As; the menu opens there.
  /// Unset until one is drawn, and then the menu opens at the pointer.
  std::optional<ImVec2> save_as_menu_anchor;

  /// The browser's Save to browser storage dialog.
  struct SaveAsDialogState {
    bool open = false;
    std::string stem;
    std::string extension;
    std::filesystem::path folder;
  } save_as_dialog;
#endif
};

} // namespace ui
