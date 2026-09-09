#include "save_as_dialog.hpp"

#include <algorithm>

namespace ui {

std::string
default_save_as_extension(std::string_view current_extension,
                          const std::vector<formats::SaveFormatInfo> &formats) {
  const auto writable =
      std::find_if(formats.begin(), formats.end(), [&](const auto &format) {
        return format.extension == current_extension;
      });
  if (writable != formats.end()) {
    return writable->extension;
  }
  return std::string(kFallbackSaveAsExtension);
}

std::filesystem::path default_save_as_folder(
    const std::optional<std::filesystem::path> &writable_source_folder,
    const std::filesystem::path &default_folder) {
  return writable_source_folder.value_or(default_folder);
}

std::vector<megatoy::workspace::Folder>
save_as_folder_choices(const megatoy::workspace::Workspace &workspace) {
  std::vector<megatoy::workspace::Folder> choices;
  for (const auto &folder : workspace.folders()) {
    if (folder.writable) {
      choices.push_back(folder);
    }
  }
  return choices;
}

} // namespace ui
