#include "save_as_dialog.hpp"
#include "patches/filename_utils.hpp"

#include <algorithm>

namespace ui {

std::string duplicate_stem(std::string_view stem) {
  return std::string(stem.empty() ? kFallbackSaveAsStem : stem) + " copy";
}

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
save_as_folder_choices(const std::vector<megatoy::workspace::Folder> &folders) {
  std::vector<megatoy::workspace::Folder> choices;
  for (const auto &folder : folders) {
    if (folder.writable) {
      choices.push_back(folder);
    }
  }
  return choices;
}

std::filesystem::path save_as_target_path(const std::filesystem::path &folder,
                                          std::string_view stem,
                                          std::string_view extension) {
  // The same name the storage builds, so both agree on what already exists.
  const std::string name = patches::sanitize_filename(
      std::string(stem.empty() ? kFallbackSaveAsStem : stem));
  return folder / (name + std::string(extension));
}

std::filesystem::path
save_as_initial_folder(const std::vector<megatoy::workspace::Folder> &choices,
                       const std::filesystem::path &preferred) {
  const bool offered =
      std::any_of(choices.begin(), choices.end(),
                  [&](const auto &folder) { return folder.path == preferred; });
  if (offered) {
    return preferred;
  }
  // Empty when there is nowhere to write: the save itself says so.
  return choices.empty() ? std::filesystem::path{} : choices.front().path;
}

bool save_as_shows_folder_choice(
    const std::vector<megatoy::workspace::Folder> &choices) {
  return choices.size() > 1;
}

} // namespace ui
