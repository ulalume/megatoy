#include "platform/platform_config.hpp"
#include "web_storage_dialogs.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include "gui/components/modal.hpp"
#include "platform/web/web_folder_delete.hpp"
#include <imgui.h>
#include <string>

namespace ui {
namespace {

constexpr const char *kPopupId = "Deleting Folder###FolderDelete";

} // namespace

using namespace platform::web;

void render_folder_delete_ui() {
  // No Cancel: the files are already gone from MEMFS, and stopping the flush
  // would leave browser storage holding the deleted files.
  const auto *name = folder_delete_name();
  const std::string title =
      name != nullptr ? "Deleting \"" + *name + "\"..." : std::string();
  progress_modal(kPopupId, take_folder_delete_finished(), name != nullptr,
                 title.c_str(), "Saving the change to browser storage.");
}

} // namespace ui

#endif
