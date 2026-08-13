#pragma once

namespace ui {

// Results are announced as status toasts; only
// genuine decisions still open a dialog.
struct SaveExportState {
  bool overwrite_confirmation_pending = false;
  bool save_as_requested = false;
};

} // namespace ui
