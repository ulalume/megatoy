#pragma once

#include <string>

namespace ui {

struct SaveExportState {
  std::string last_export_path;
  std::string last_export_error;

  enum class Pending {
    None,
    OverwriteConfirmation,
    SaveSuccess,
    ExportSuccess,
    Error,
  };

  Pending pending_popup = Pending::None;
};

} // namespace ui
