#pragma once

#include <optional>
#include <string>

namespace ui {

// Results are announced as status toasts; only
// genuine decisions still open a dialog.
struct SaveExportState {
  bool overwrite_confirmation_pending = false;
  bool save_as_requested = false;
  std::optional<std::string> pending_save_as_extension;
};

} // namespace ui
