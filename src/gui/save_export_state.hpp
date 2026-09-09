#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace ui {

// Results are announced as status toasts; only
// genuine decisions still open a dialog.
struct SaveExportState {
  bool overwrite_confirmation_pending = false;
  bool save_as_requested = false;
  std::optional<std::string> pending_save_as_extension;
  std::optional<std::string> pending_save_as_stem;
  std::optional<std::filesystem::path> pending_save_as_folder;

  /// The browser's Save As dialog: name, format and folder in one place.
  struct SaveAsDialogState {
    bool open = false;
    std::string stem;
    std::string extension;
    std::filesystem::path folder;
  } save_as_dialog;
};

} // namespace ui
