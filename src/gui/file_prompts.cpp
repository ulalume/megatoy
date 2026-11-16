#include "file_prompts.hpp"

#include "platform/file_dialog.hpp"
#include "platform/platform_config.hpp"

#include <filesystem>
#include <utility>
#include <vector>

namespace ui {

patches::SavePathAnswer ask_save_path(const patches::SavePathRequest &request) {
  using Status = patches::SavePathAnswer::Status;
  std::vector<platform::file_dialog::FileFilter> filters;
  for (const auto &format :
       patches::save_dialog_formats(request.formats, request.extension)) {
    filters.push_back({format.display_name(), {format.extension.substr(1)}});
  }

  std::filesystem::path selected;
  switch (platform::file_dialog::save_file(request.start_directory,
                                           request.stem, filters, selected)) {
  case platform::file_dialog::DialogResult::Ok:
    return {Status::Chosen, std::move(selected)};
  case platform::file_dialog::DialogResult::Cancelled:
    return {Status::Cancelled, {}};
  case platform::file_dialog::DialogResult::Error:
    break;
  }
  return {Status::Failed, {}};
}

void request_add_workspace_folder(PreferenceManager &preferences,
                                  std::function<void()> on_changed,
                                  PreferenceManager::ConfirmReplace confirm) {
#if defined(MEGATOY_PLATFORM_WEB)
  (void)confirm;
  preferences.import_workspace_folder(std::move(on_changed));
#else
  std::filesystem::path chosen;
  if (platform::file_dialog::pick_folder(preferences.last_save_directory(),
                                         chosen) !=
      platform::file_dialog::DialogResult::Ok) {
    return;
  }
  preferences.add_chosen_workspace_folder(chosen, std::move(on_changed),
                                          std::move(confirm));
#endif
}

} // namespace ui
