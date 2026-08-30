#include "patch_session.hpp"

#include "audio/audio_command.hpp"
#include "audio/audio_manager.hpp"
#include "formats/patch_loader.hpp"
#include "formats/patch_registry.hpp"
#include "formats/ym2612_format_adapter.hpp"
#include "patches/patch_write.hpp"
#include "platform/file_dialog.hpp"
#include "platform/platform_config.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_patch_url.hpp"
#include "platform/web/web_storage_persistence.hpp"
#endif
#include "ym2612/channel.hpp"
#include "ym2612/types.hpp"
#include <algorithm>
#include <filesystem>
#include <string_view>
#include <utility>

namespace patches {

PatchSession::PatchSession(megatoy::system::PathService &directories,
                           PreferenceManager &preferences, AudioManager &audio,
                           PersistentParseCache *persistent_cache)
    : directories_(directories), preferences_(preferences), audio_(audio),
      repository_(std::make_unique<PatchRepository>(
          directories_.file_system(), preferences_.workspace(),
          directories_.paths().builtin_presets_root, persistent_cache)) {
  repository_->set_show_builtin_presets(preferences_.show_builtin_presets());
}

ym2612::Patch &PatchSession::current_patch() { return current_patch_; }

const ym2612::Patch &PatchSession::current_patch() const {
  return current_patch_;
}

const std::string &PatchSession::current_patch_path() const {
  return current_patch_path_;
}

void PatchSession::set_current_patch_path(const std::filesystem::path &path,
                                          RememberPatchPath remember) {
  if (path.empty()) {
    current_patch_path_.clear();
    current_patch_selection_path_.clear();
  } else if (!path.is_absolute()) {
    current_patch_path_ = path.generic_string();
    current_patch_selection_path_ = current_patch_path_;
    if (path.extension() == ".ginpkg") {
      current_patch_selection_path_ += "/latest";
    }
  } else {
    const auto relative = repository_->to_relative_path(path);
    current_patch_path_ = relative.generic_string();
    current_patch_selection_path_ = current_patch_path_;
    if (path.extension() == ".ginpkg") {
      current_patch_selection_path_ += "/latest";
    }
  }

  if (remember == RememberPatchPath::Yes) {
    preferences_.set_last_patch_path(current_patch_path_);
  }
}

const std::string &PatchSession::current_patch_selection_path() const {
  return current_patch_selection_path_;
}

void PatchSession::set_current_patch_selection_path(
    const std::filesystem::path &path) {
  if (path.empty()) {
    current_patch_selection_path_.clear();
    return;
  }
  current_patch_selection_path_ =
      (path.is_absolute() ? repository_->to_relative_path(path) : path)
          .generic_string();
}

PatchRepository &PatchSession::repository() { return *repository_; }

const PatchRepository &PatchSession::repository() const { return *repository_; }

bool PatchSession::is_modified() const {
  return original_patch_ != current_patch_;
}
void PatchSession::mark_as_clean() { original_patch_ = current_patch_; }

PatchSession::DefaultPatch PatchSession::default_patch_source() const {
  const auto &paths = directories_.paths();
  const auto init_patch_path = paths.builtin_presets_root / "init.dmp";
  auto &vfs = directories_.file_system();

  if (!paths.builtin_presets_root.empty() && vfs.exists(init_patch_path)) {
    auto load_result = formats::load_patch_from_file(init_patch_path);
    if (load_result.status == formats::PatchLoadStatus::Success) {
      return {std::move(load_result.patches[0]), init_patch_path};
    }
  }

  DefaultPatch fallback;
  fallback.patch.name.clear();
  fallback.patch.global = {
      .dac_enable = false,
      .lfo_enable = false,
      .lfo_frequency = 0,
  };

  fallback.patch.channel = {
      .left_speaker = true,
      .right_speaker = true,
      .amplitude_modulation_sensitivity = 0,
      .frequency_modulation_sensitivity = 0,
  };

  fallback.patch.instrument = {
      .feedback = 7,
      .algorithm = 3,
      .operators =
          {
              {31, 0, 0, 5, 0, 48, 0, 1, 3, 0, false, false},
              {31, 0, 0, 5, 0, 24, 0, 1, 1, 0, false, false},
              {31, 0, 0, 5, 0, 36, 0, 1, 2, 0, false, false},
              {31, 0, 0, 5, 0, 12, 0, 1, 4, 0, false, false},
          },
  };
  return fallback;
}

ym2612::Patch PatchSession::default_patch() const {
  return default_patch_source().patch;
}

void PatchSession::initialize_patch_defaults() {
  auto starting_point = default_patch_source();
  current_patch_ = std::move(starting_point.patch);
  if (starting_point.source.empty()) {
    set_current_patch_path({}, RememberPatchPath::No);
    mark_as_clean();
    return;
  }
  set_current_patch_path(starting_point.source, RememberPatchPath::No);
  set_file_identity(starting_point.source);
  mark_as_clean();
}

void PatchSession::sync_workspace() {
  std::optional<std::filesystem::path> absolute_patch_path;
  if (!current_patch_path_.empty()) {
    auto resolved = repository_->to_absolute_path(current_patch_path_);
    if (resolved.is_absolute()) {
      absolute_patch_path = std::move(resolved);
    }
  }

  repository_->set_show_builtin_presets(preferences_.show_builtin_presets());
  repository_->sync_workspace();

  if (current_patch_path_.empty()) {
    return;
  }
  if (!absolute_patch_path) {
    set_current_patch_path({});
    return;
  }
  const auto relative = repository_->to_relative_path(*absolute_patch_path);
  if (relative.is_absolute()) {
    set_current_patch_path({});
    return;
  }
  set_current_patch_path(relative);
}

bool PatchSession::load_patch_from_entry(const PatchEntry &entry) {
  ym2612::Patch patch;
  if (!repository_->load_patch(entry, patch)) {
    return false;
  }
  set_current_patch(patch, entry.source_relative_path.empty()
                               ? entry.relative_path
                               : entry.source_relative_path);
  set_current_patch_selection_path(entry.relative_path);
  mark_as_clean();
  return true;
}

bool PatchSession::restore_patch(const std::filesystem::path &relative_path) {
  if (relative_path.empty()) {
    return false;
  }
  const auto relative_string = relative_path.generic_string();
  const auto absolute = repository_->to_absolute_path(relative_path);
  std::error_code error;
  if (!absolute.is_absolute() || !std::filesystem::exists(absolute, error) ||
      error) {
    return false;
  }

  const auto find_entry =
      [&](const auto &self,
          const std::vector<PatchEntry> &entries) -> const PatchEntry * {
    for (const auto &entry : entries) {
      if (!entry.is_directory &&
          (entry.relative_path == relative_string ||
           entry.source_relative_path == relative_string)) {
        return &entry;
      }
      if (const auto *found = self(self, entry.children)) {
        return found;
      }
    }
    return nullptr;
  };
  const auto *entry = find_entry(find_entry, repository_->tree());
  return entry != nullptr && load_patch_from_entry(*entry);
}

void PatchSession::set_current_patch(const ym2612::Patch &patch,
                                     const std::filesystem::path &source_path,
                                     RememberPatchPath remember) {
  current_patch_ = patch;
  set_current_patch_path(source_path, remember);
  if (!source_path.empty()) {
    const auto format = formats::adapter::format_for_extension(
        lowercase_extension(source_path));
    if (!format || !formats::adapter::is_multi_patch(*format)) {
      set_file_identity(source_path);
    }
  }
  mark_as_clean(); // New patch loaded, not modified yet
  apply_patch_to_audio();
#if defined(MEGATOY_PLATFORM_WEB)
  platform::web::patch_url::sync_patch_to_url_if_needed(current_patch_);
#endif
}

void PatchSession::apply_patch_to_audio() {
  // Patch edits take the same route as notes so that a slider drag cannot
  // rewrite registers underneath the renderer.
  if (audio_.submit(audio::AudioCommand::apply_patch(
          current_patch_.global, current_patch_.channel,
          current_patch_.instrument))) {
    last_applied_ = current_patch_;
    has_applied_patch_ = true;
  }
}

bool PatchSession::apply_patch_to_audio_if_changed() {
  const bool audio_settings_changed =
      !has_applied_patch_ || current_patch_.global != last_applied_.global ||
      current_patch_.channel != last_applied_.channel ||
      current_patch_.instrument != last_applied_.instrument;
  if (!audio_settings_changed) {
    return false;
  }

  apply_patch_to_audio();
  return has_applied_patch_ && current_patch_.global == last_applied_.global &&
         current_patch_.channel == last_applied_.channel &&
         current_patch_.instrument == last_applied_.instrument;
}

std::optional<std::filesystem::path>
PatchSession::writable_source_folder() const {
  if (current_patch_path_.empty()) {
    return std::nullopt;
  }
  const auto absolute = repository_->to_absolute_path(current_patch_path_);
  if (absolute.empty() || !absolute.is_absolute()) {
    return std::nullopt;
  }
  const auto folder = absolute.parent_path();
  const auto *owner = preferences_.workspace().owner_of(folder);
  if (owner == nullptr || !owner->writable) {
    return std::nullopt;
  }
  return folder;
}

SaveResult
PatchSession::save_current_patch(std::string_view preferred_extension) {
  // Overwrite in place only when the source is a single-patch, writable
  // format sitting in a folder we may write to. Everything else -- read-only
  // formats, instrument banks, the built-in presets -- goes through Save As.
  if (const auto folder = writable_source_folder()) {
    const auto absolute = repository_->to_absolute_path(current_patch_path_);
    if (patches::can_overwrite_in_place(absolute)) {
      auto patch_to_write = current_patch_;
      patch_to_write.name = absolute.stem().string();
      if (patches::write_patch(patch_to_write, absolute)) {
        current_patch_.name = patch_to_write.name;
        mark_as_clean();
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
                                    /*overwrite=*/false);
}

SaveResult
PatchSession::save_current_patch_as_forced(std::string_view preferred_extension,
                                           std::string_view filename_stem) {
  return save_current_patch_as_impl(preferred_extension, filename_stem,
                                    /*overwrite=*/true);
}

SaveResult
PatchSession::save_current_patch_as_impl(std::string_view preferred_extension,
                                         std::string_view filename_stem,
                                         bool overwrite) {
  std::string suggested_name =
      filename_stem.empty() ? current_patch_.name : std::string(filename_stem);
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
    auto patch_to_write = current_patch_;
    patch_to_write.name = sanitized_name;
    auto result = repository_->save_patch(patch_to_write, sanitized_name,
                                          overwrite, extension);
    if (result.status == SavePatchResult::Status::Success) {
      auto loaded = formats::load_patch_from_file(result.path);
      if (loaded.status == formats::PatchLoadStatus::Success &&
          loaded.patches.size() == 1) {
        set_current_patch(loaded.patches.front(), result.path);
      } else {
        current_patch_.name = sanitized_name;
        set_current_patch_path(result.path);
        mark_as_clean();
      }
      return SaveResult::success(result.path);
    }
    if (result.status == SavePatchResult::Status::Duplicate) {
      return SaveResult::duplicated();
    }
    return SaveResult::error("No writable patch folder is available.");
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
  auto patch_to_write = current_patch_;
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
    current_patch_.name = selected.stem().string();
    set_current_patch_path(selected);
    mark_as_clean();
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
  if (relative.is_absolute() || !restore_patch(relative)) {
    set_current_patch(patch, target);
  }
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

bool PatchSession::note_on(ym2612::Note note, uint8_t velocity,
                           const PreferenceManager::UIPreferences &prefs) {
  // Handed to the audio thread rather than written here: the chip has exactly
  // one writer, and the note starts on the next audio callback instead of
  // waiting for whenever this frame happens to finish.
  audio_.set_note_options(prefs.use_velocity, prefs.velocity_sensitivity_depth,
                          prefs.steal_oldest_note_when_full);
  const uint8_t clamped_velocity =
      std::min<uint8_t>(velocity, static_cast<uint8_t>(127));
  return audio_.submit(audio::AudioCommand::note_on(note, clamped_velocity));
}

bool PatchSession::note_off(ym2612::Note note) {
  return audio_.submit(audio::AudioCommand::note_off(note));
}

bool PatchSession::note_is_active(const ym2612::Note &note) const {
  return audio_.notes().published_contains(note);
}

void PatchSession::release_all_notes() {
  audio_.submit(audio::AudioCommand::all_notes_off());
}

std::array<bool, 6> PatchSession::active_channels() const {
  return audio_.notes().published_channels();
}

const std::vector<ym2612::Note> PatchSession::active_notes() const {
  return audio_.notes().published_notes();
}

VoiceActivityFrame PatchSession::voice_activity() const {
  return audio_.voice_activity();
}

PatchSession::PatchSnapshot PatchSession::capture_snapshot() const {
  PatchSnapshot snapshot;
  snapshot.original_patch = original_patch_;
  snapshot.patch = current_patch_;
  snapshot.path = current_patch_path_;
  snapshot.selection_path = current_patch_selection_path_;
  return snapshot;
}

void PatchSession::restore_snapshot(const PatchSnapshot &snapshot) {
  current_patch_ = snapshot.patch;
  original_patch_ = snapshot.original_patch;
  if (snapshot.path.empty()) {
    set_current_patch_path({});
  } else {
    set_current_patch_path(snapshot.path);
  }
  set_current_patch_selection_path(snapshot.selection_path);
  apply_patch_to_audio();
}

bool PatchSession::can_save_in_place() const {
  const auto folder = writable_source_folder();
  if (!folder) {
    return false;
  }
  return patches::can_overwrite_in_place(
      repository_->to_absolute_path(current_patch_path_));
}

bool PatchSession::current_patch_is_user_patch() const {
  if (current_patch_path_.empty()) {
    return false;
  }
  return can_save_in_place();
}

void PatchSession::set_file_identity(const std::filesystem::path &path) {
  const auto stem = path.stem().string();
  current_patch_.name = stem;
  original_patch_.name = stem;
}

namespace {

/// `path` with `from` at its head swapped for `to`. Unchanged when `from` is
/// neither it nor one of its ancestors.
std::filesystem::path rebase(const std::filesystem::path &path,
                             const std::filesystem::path &from,
                             const std::filesystem::path &to) {
  if (path == from) {
    return to;
  }
  const auto relative = path.lexically_relative(from);
  if (relative.empty() || *relative.begin() == "..") {
    return path;
  }
  return to / relative;
}

} // namespace

bool PatchSession::rename_patch(const PatchEntry &entry,
                                const std::string &new_stem) {
  const auto old_path = entry.full_path;

  // A folder is registered in the workspace by path, so a renamed one has to
  // be followed there too or it comes back missing on the next launch. Looked
  // up before the rename: once the directory is gone its old path no longer
  // resolves to what is stored.
  std::optional<std::filesystem::path> workspace_folder;
  if (entry.is_directory) {
    if (const auto *folder = preferences_.workspace().find(old_path)) {
      if (preferences_.workspace_folder_is_protected(folder->path)) {
        return false;
      }
      workspace_folder = folder->path;
    }
  }

  // Resolved before the rename as well, because every relative path under the
  // renamed entry is about to change.
  std::optional<std::filesystem::path> current_absolute;
  if (!current_patch_path_.empty()) {
    auto resolved = repository_->to_absolute_path(current_patch_path_);
    if (resolved.is_absolute()) {
      current_absolute = std::move(resolved);
    }
  }
  std::string selection_suffix;
  if (!current_patch_path_.empty() &&
      current_patch_selection_path_.rfind(current_patch_path_, 0) == 0) {
    selection_suffix =
        current_patch_selection_path_.substr(current_patch_path_.size());
  }

  const bool is_directory = entry.is_directory;
  const auto target =
      old_path.parent_path() /
      (is_directory ? new_stem : new_stem + old_path.extension().string());

  if (!repository_->rename_patch(entry, new_stem)) {
    return false;
  }

  if (workspace_folder) {
    preferences_.rename_workspace_folder(*workspace_folder, target);
    repository_->sync_workspace();
  }

  if (!current_absolute) {
    return true;
  }

  // Follow the current patch to wherever it ended up, if it was inside what
  // was renamed at all. Anything else keeps the path it already had -- which
  // includes patches from outside the workspace entirely.
  const auto moved = rebase(*current_absolute, old_path, target);
  if (moved == *current_absolute) {
    return true;
  }

  const auto relative = repository_->to_relative_path(moved);
  if (relative.is_absolute()) {
    set_current_patch_path({});
    return true;
  }
  set_current_patch_path(relative);
  if (!selection_suffix.empty()) {
    set_current_patch_selection_path(relative.generic_string() +
                                     selection_suffix);
  }
  set_file_identity(moved);
  return true;
}

const char *PatchSession::save_label_for(bool is_user_patch) const {
  if (!is_user_patch) {
    return "Save As...";
  }
  // Saving a .ginpkg adds a version rather than replacing the file.
  return current_patch_path_.ends_with(".ginpkg") ? "Save Version"
                                                  : "Overwrite";
}

} // namespace patches
