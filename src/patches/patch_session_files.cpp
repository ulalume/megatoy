#include "patch_session.hpp"

#include "formats/patch_loader.hpp"
#include "formats/patch_registry.hpp"
#include "patches/patch_write.hpp"
#include "platform/file_dialog.hpp"
#include "platform/platform_config.hpp"
#include <utility>

#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_storage_persistence.hpp"
#endif

namespace patches {

std::optional<std::filesystem::path>
PatchSession::writable_source_folder() const {
  return writable_source_folder(active_document());
}

std::optional<std::filesystem::path>
PatchSession::writable_source_folder(const PatchDocument &document) const {
  const auto &path = document.path();
  if (path.empty()) {
    return std::nullopt;
  }
  const auto absolute = repository_->to_absolute_path(path);
  if (absolute.empty() || !absolute.is_absolute()) {
    return std::nullopt;
  }
  const auto folder = absolute.parent_path();
  if (const auto *owner = preferences_.workspace().owner_of(folder)) {
    return owner->writable ? std::optional(folder) : std::nullopt;
  }
#if defined(MEGATOY_PLATFORM_WEB)
  // A file dropped on the page is a copy that goes away with it.
  return std::nullopt;
#else
  // A file opened from elsewhere is the user's own; the presets are the app's.
  const auto inside_presets =
      folder.lexically_relative(directories_.paths().builtin_presets_root);
  if (!inside_presets.empty() && *inside_presets.begin() != "..") {
    return std::nullopt;
  }
  return folder;
#endif
}

SaveResult
PatchSession::save_current_patch(std::string_view preferred_extension) {
  // Overwrite in place only when the source is a single-patch, writable
  // format sitting in a folder we may write to. Everything else -- read-only
  // formats, instrument banks, the built-in presets -- goes through Save As.
  if (const auto folder = writable_source_folder()) {
    const auto absolute = repository_->to_absolute_path(current_patch_path());
    if (patches::can_overwrite_in_place(absolute)) {
      auto patch_to_write = current_patch();
      patch_to_write.name = absolute.stem().string();
      if (patches::write_patch(patch_to_write, absolute)) {
        current_patch().name = patch_to_write.name;
        mark_as_clean();
        remember_disk_time(active_document());
        repository_->refresh();
#if defined(MEGATOY_PLATFORM_WEB)
        platform::web::request_storage_persist();
#endif
        return SaveResult::success(absolute);
      }
      return SaveResult::error("Failed to write " + absolute.string());
    }
  }

  return save_current_patch_as(preferred_extension);
}

SaveResult
PatchSession::save_current_patch_as(std::string_view preferred_extension,
                                    std::string_view filename_stem) {
  return save_current_patch_as_impl(preferred_extension, filename_stem,
                                    /*overwrite=*/false, {});
}

SaveResult
PatchSession::save_current_patch_as_forced(std::string_view preferred_extension,
                                           std::string_view filename_stem) {
  return save_current_patch_as_impl(preferred_extension, filename_stem,
                                    /*overwrite=*/true, {});
}

SaveResult
PatchSession::save_current_patch_as_in(const std::filesystem::path &folder,
                                       std::string_view preferred_extension,
                                       std::string_view filename_stem) {
  return save_current_patch_as_impl(preferred_extension, filename_stem,
                                    /*overwrite=*/false, folder);
}

SaveResult PatchSession::save_current_patch_as_in_forced(
    const std::filesystem::path &folder, std::string_view preferred_extension,
    std::string_view filename_stem) {
  return save_current_patch_as_impl(preferred_extension, filename_stem,
                                    /*overwrite=*/true, folder);
}

SaveResult PatchSession::save_current_patch_as_impl(
    std::string_view preferred_extension, std::string_view filename_stem,
    bool overwrite, const std::filesystem::path &folder) {
  std::string suggested_name =
      filename_stem.empty() ? current_patch().name : std::string(filename_stem);
  const std::string sanitized_name =
      sanitize_filename(suggested_name.empty() ? "patch" : suggested_name);

  std::string extension(preferred_extension);
  if (extension.empty()) {
    extension = ".gin";
  }
  if (extension.front() != '.') {
    extension = "." + extension;
  }

  if (megatoy::platform::is_web()) {
    auto patch_to_write = current_patch();
    patch_to_write.name = sanitized_name;
    auto result =
        folder.empty()
            ? repository_->save_patch(patch_to_write, sanitized_name, overwrite,
                                      extension)
            : repository_->save_patch_in(folder, patch_to_write, sanitized_name,
                                         overwrite, extension);
    if (result.status == SavePatchResult::Status::Success) {
      auto loaded = formats::load_patch_from_file(result.path);
      if (loaded.status == formats::PatchLoadStatus::Success &&
          loaded.patches.size() == 1) {
        set_current_patch(loaded.patches.front(), result.path);
      } else {
        current_patch().name = sanitized_name;
        set_current_patch_path(result.path);
        mark_as_clean();
        remember_disk_time(active_document());
      }
      return SaveResult::success(result.path);
    }
    if (result.status == SavePatchResult::Status::Duplicate) {
      return SaveResult::duplicated();
    }
    return SaveResult::error(folder.empty()
                                 ? "No writable patch folder is available."
                                 : "Cannot write into \"" +
                                       folder.filename().string() + "\".");
  }

  // Start in the folder the patch came from when that folder is writable,
  // otherwise wherever the user last saved something.
  const auto start_directory =
      writable_source_folder().value_or(preferences_.last_save_directory());

  std::vector<platform::file_dialog::FileFilter> filters;
  auto selected_format = find_save_format(extension);
  const std::string filter_label =
      selected_format ? selected_format->label : "megatoy";
  filters.push_back({filter_label, {extension.substr(1)}});
  filters.push_back({"All Files", {"*"}});

  std::filesystem::path selected;
  const auto dialog = platform::file_dialog::save_file(
      start_directory, sanitized_name + extension, filters, selected);
  if (dialog == platform::file_dialog::DialogResult::Cancelled) {
    return SaveResult::cancelled();
  }
  if (dialog != platform::file_dialog::DialogResult::Ok) {
    return SaveResult::error("Failed to save patch");
  }

  selected = append_extension_if_missing(std::move(selected), extension);
  auto patch_to_write = current_patch();
  patch_to_write.name = selected.stem().string();
  if (!patches::write_patch(patch_to_write, selected)) {
    return SaveResult::error("Failed to write " + selected.string());
  }

  preferences_.set_last_save_directory(selected.parent_path());
  auto loaded = formats::load_patch_from_file(selected);
  if (loaded.status == formats::PatchLoadStatus::Success &&
      loaded.patches.size() == 1) {
    set_current_patch(loaded.patches.front(), selected);
  } else {
    current_patch().name = selected.stem().string();
    set_current_patch_path(selected);
    mark_as_clean();
    remember_disk_time(active_document());
  }
  repository_->refresh();
  return SaveResult::success(selected);
}

bool PatchSession::can_create_patch_in(
    const std::filesystem::path &folder) const {
  const auto *owner = preferences_.workspace().owner_of(folder);
  // writable is only ever set on a folder that is also available.
  return owner != nullptr && owner->writable;
}

SaveResult PatchSession::create_patch_in(const std::filesystem::path &folder,
                                         const std::string &stem,
                                         std::string_view extension) {
  if (!can_create_patch_in(folder)) {
    return SaveResult::error("Cannot write into \"" +
                             folder.filename().string() + "\".");
  }
  if (const auto problem = new_patch_name_error(stem, extension, folder);
      !problem.empty()) {
    return SaveResult::error(problem);
  }

  const auto target = append_extension_if_missing(folder / stem, extension);
  auto patch = default_patch();
  // The filename is the patch's name, the same rule Save As follows.
  patch.name = target.stem().string();
  if (!patches::write_patch(patch, target)) {
    return SaveResult::error("Failed to write " + target.string());
  }

  repository_->refresh();
#if defined(MEGATOY_PLATFORM_WEB)
  platform::web::request_storage_persist();
#endif

  // Selecting it is the browser's own path, so a container format lands on
  // the instrument inside it just as a click would. If the refreshed tree
  // does not have it, the file still exists -- open it directly rather than
  // leave the editor on the previous patch.
  const auto relative = repository_->to_relative_path(target);
  if (relative.is_absolute() || !open_path(relative, /*pinned=*/true)) {
    place_document(patch, target, {}, /*pinned=*/true);
  }
  return SaveResult::success(target);
}

SaveResult PatchSession::create_folder_in(const std::filesystem::path &parent,
                                          const std::string &name) {
  if (!can_create_patch_in(parent)) {
    return SaveResult::error("Cannot write into \"" +
                             parent.filename().string() + "\".");
  }
  if (const auto problem = new_folder_name_error(name, parent);
      !problem.empty()) {
    return SaveResult::error(problem);
  }

  const auto target = parent / name;
  std::error_code error;
  if (!std::filesystem::create_directory(target, error)) {
    return SaveResult::error("Could not create \"" + name + "\".");
  }

  repository_->refresh();
#if defined(MEGATOY_PLATFORM_WEB)
  platform::web::request_storage_persist();
#endif
  return SaveResult::success(target);
}

std::optional<SaveFormatInfo>
PatchSession::find_save_format(const std::string &extension) const {
  auto formats = formats::PatchRegistry::instance().save_formats();
  std::string ext_norm = extension;
  if (!ext_norm.empty() && ext_norm.front() != '.') {
    ext_norm = "." + ext_norm;
  }
  for (const auto &fmt : formats) {
    if (fmt.extension == ext_norm) {
      return fmt;
    }
  }
  return std::nullopt;
}

std::vector<SaveFormatInfo> PatchSession::save_formats() const {
  return formats::PatchRegistry::instance().save_formats();
}

bool PatchSession::can_save_in_place() const {
  return can_save_in_place(active_document());
}

bool PatchSession::can_save_in_place(const PatchDocument &document) const {
  const auto folder = writable_source_folder(document);
  if (!folder) {
    return false;
  }
  return patches::can_overwrite_in_place(
      repository_->to_absolute_path(document.path()));
}

bool PatchSession::current_patch_is_user_patch() const {
  return is_user_patch(active_document());
}

bool PatchSession::is_user_patch(const PatchDocument &document) const {
  if (document.path().empty()) {
    return false;
  }
  return can_save_in_place(document);
}

const char *PatchSession::save_label_for(bool is_user_patch) const {
  return save_label_for(active_document(), is_user_patch);
}

const char *PatchSession::save_label_for(const PatchDocument &document,
                                         bool is_user_patch) const {
  if (!is_user_patch) {
    return "Save As...";
  }
  // Saving a .ginpkg adds a version rather than replacing the file.
  return lowercase_extension(document.path()) == ".ginpkg" ? "Save Version"
                                                           : "Overwrite";
}

} // namespace patches
