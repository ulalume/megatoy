#pragma once

#include <string>

namespace ui {

// Results (saved, exported, failed) are announced as status toasts; only
// genuine decisions still open a dialog.
struct SaveExportState {
  bool overwrite_confirmation_pending = false;

  struct DuplicateDialog {
    bool open = false;
    std::string name;
  } duplicate;
};

} // namespace ui
