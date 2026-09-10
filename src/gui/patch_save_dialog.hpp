#pragma once

// Where a patch goes: its name, its format, and the folder when there is a
// choice of them. Asked for in a modal wherever no native file dialog exists.

#include "formats/patch_registry.hpp"
#include "workspace/workspace.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace ui {

struct PatchSaveDialogState {
  /// Raised to open the dialog; the renderer takes it from there.
  bool requested = false;
  bool open = false;
  std::string stem;
  std::string extension;
  std::filesystem::path folder;
};

/// What was chosen, plus whether a file of that name is being replaced.
using PatchSaveDialogAction = std::function<void(
    const std::filesystem::path &folder, const std::string &stem,
    const std::string &extension, bool overwrite)>;

/// Draw the dialog while its state asks for it. `primary_label` names the
/// confirm button until it turns into Overwrite.
void render_patch_save_dialog(
    const char *title, const char *primary_label, PatchSaveDialogState &state,
    const std::vector<formats::SaveFormatInfo> &formats,
    const megatoy::workspace::Workspace &workspace,
    const PatchSaveDialogAction &on_save);

} // namespace ui
