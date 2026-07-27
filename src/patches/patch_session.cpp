#include "patch_session.hpp"

#include "audio/audio_command.hpp"
#include "audio/audio_manager.hpp"
#include "formats/patch_loader.hpp"
#include "formats/ym2612_format_adapter.hpp"
#include "formats/ginpkg.hpp"
#include "formats/patch_registry.hpp"
#include "platform/file_dialog.hpp"
#include "platform/platform_config.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_patch_url.hpp"
#endif
#include "ym2612/channel.hpp"
#include "ym2612/types.hpp"
#include <algorithm>
#include <filesystem>
#include <string_view>
#include <utility>

namespace patches {

PatchSession::PatchSession(megatoy::system::PathService &directories,
                           PreferenceManager &preferences, AudioManager &audio)
    : directories_(directories), preferences_(preferences), audio_(audio),
      repository_(std::make_unique<PatchRepository>(
          directories_.file_system(), preferences_.workspace(),
          directories_.paths().builtin_presets_root)) {
  repository_->set_show_builtin_presets(preferences_.show_builtin_presets());
}

ym2612::Patch &PatchSession::current_patch() { return current_patch_; }

const ym2612::Patch &PatchSession::current_patch() const {
  return current_patch_;
}

const std::string &PatchSession::current_patch_path() const {
  return current_patch_path_;
}

void PatchSession::set_current_patch_path(const std::filesystem::path &path) {
  if (path.empty()) {
    current_patch_path_.clear();
    return;
  }
  if (!path.is_absolute()) {
    current_patch_path_ = path.generic_string();
    return;
  }
  const auto relative = repository_->to_relative_path(path);
  current_patch_path_ = relative.generic_string();
}

PatchRepository &PatchSession::repository() { return *repository_; }

const PatchRepository &PatchSession::repository() const { return *repository_; }

bool PatchSession::is_modified() const {
  return original_patch_ != current_patch_;
}
void PatchSession::mark_as_clean() { original_patch_ = current_patch_; }

void PatchSession::initialize_patch_defaults() {
  const auto &paths = directories_.paths();
  const auto init_patch_path = paths.builtin_presets_root / "init.dmp";
  auto &vfs = directories_.file_system();

  if (!paths.builtin_presets_root.empty() && vfs.exists(init_patch_path)) {
    auto load_result = formats::load_patch_from_file(init_patch_path);
    if (load_result.status == formats::PatchLoadStatus::Success) {
      set_current_patch_path(init_patch_path);
      current_patch_ = std::move(load_result.patches[0]);
      mark_as_clean();
      return;
    }
  }
  current_patch_.name = "init";
  current_patch_.global = {
      .dac_enable = false,
      .lfo_enable = false,
      .lfo_frequency = 0,
  };

  current_patch_.channel = {
      .left_speaker = true,
      .right_speaker = true,
      .amplitude_modulation_sensitivity = 0,
      .frequency_modulation_sensitivity = 0,
  };

  current_patch_.instrument = {
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
  set_current_patch_path({});
  mark_as_clean();
}

void PatchSession::sync_workspace() {
  repository_->set_show_builtin_presets(preferences_.show_builtin_presets());
  repository_->sync_workspace();
}

void PatchSession::set_current_patch(const ym2612::Patch &patch,
                                     const std::filesystem::path &source_path) {
  current_patch_ = patch;
  set_current_patch_path(source_path);
  mark_as_clean(); // New patch loaded, not modified yet
  apply_patch_to_audio();
#if defined(MEGATOY_PLATFORM_WEB)
  platform::web::patch_url::sync_patch_to_url_if_needed(current_patch_);
#endif
}

void PatchSession::apply_patch_to_audio() {
  // Patch edits take the same route as notes so that a slider drag cannot
  // rewrite registers underneath the renderer.
  audio_.submit(audio::AudioCommand::apply_patch(current_patch_.global,
                                                 current_patch_.channel,
                                                 current_patch_.instrument));
}

namespace {

std::string lowercase_extension(const std::filesystem::path &path) {
  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return ext;
}

} // namespace

bool PatchSession::can_overwrite_in_place(
    const std::filesystem::path &path) const {
  const auto extension = lowercase_extension(path);

  // .ginpkg is megatoy's own container: saving appends a version rather than
  // replacing the file, so it is always safe to write back.
  if (extension == ".ginpkg") {
    return true;
  }

  const auto format = formats::adapter::format_for_extension(extension);
  if (!format) {
    return false;
  }
  // A bank holds other instruments; writing one patch over it would throw
  // them away.
  if (formats::adapter::is_multi_patch(*format)) {
    return false;
  }
  for (const auto &info : formats::adapter::known_formats()) {
    if (info.format == *format) {
      return info.can_write;
    }
  }
  return false;
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
PatchSession::save_current_patch(bool force_overwrite,
                                 std::string_view preferred_extension) {
  (void)force_overwrite;

  // Overwrite in place only when the source is a single-patch, writable
  // format sitting in a folder we may write to. Everything else -- read-only
  // formats, instrument banks, the built-in presets -- goes through Save As.
  if (const auto folder = writable_source_folder()) {
    const auto absolute = repository_->to_absolute_path(current_patch_path_);
    if (can_overwrite_in_place(absolute)) {
      if (write_patch_to(absolute)) {
        mark_as_clean();
        repository_->refresh();
        return SaveResult::success(absolute);
      }
      return SaveResult::error("Failed to write " + absolute.string());
    }
  }

  return save_current_patch_as(preferred_extension);
}

bool PatchSession::write_patch_to(const std::filesystem::path &path) {
  const auto extension = lowercase_extension(path);

  if (extension == ".ginpkg") {
    return formats::ginpkg::save_patch(path.parent_path(), current_patch_,
                                       path.stem().string())
        .has_value();
  }

  auto &registry = formats::PatchRegistry::instance();
  if (auto format = find_export_format(extension); format && format->is_text) {
    return registry.write_text(extension, current_patch_, path);
  }
  return registry.write(extension, current_patch_, path);
}

SaveResult
PatchSession::save_current_patch_as(std::string_view preferred_extension) {
  const std::string sanitized_name = sanitize_filename(
      current_patch_.name.empty() ? "patch" : current_patch_.name);

  std::string extension(preferred_extension);
  if (extension.empty()) {
    extension = ".ginpkg";
  }
  if (extension.front() != '.') {
    extension = "." + extension;
  }

  if (megatoy::platform::is_web()) {
    auto result = repository_->save_patch(current_patch_, current_patch_.name,
                                          /*overwrite=*/true, extension);
    if (result.status == SavePatchResult::Status::Success) {
      mark_as_clean();
      return SaveResult::success(result.path);
    }
    if (repository_->download_patch(current_patch_, current_patch_.name,
                                    ".dmp")) {
      mark_as_clean();
      return SaveResult::success(std::filesystem::path("download"));
    }
    return SaveResult::error("Saving patches is unsupported on this platform");
  }

  // Start in the folder the patch came from when that folder is writable,
  // otherwise wherever the user last saved something.
  const auto start_directory =
      writable_source_folder().value_or(preferences_.last_save_directory());

  std::vector<platform::file_dialog::FileFilter> filters;
  filters.push_back({"megatoy patch", {"ginpkg", "gin"}});
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

  if (selected.extension().empty()) {
    selected.replace_extension(extension);
  }
  if (!write_patch_to(selected)) {
    return SaveResult::error("Failed to write " + selected.string());
  }

  preferences_.set_last_save_directory(selected.parent_path());
  set_current_patch_path(selected);
  mark_as_clean();
  repository_->refresh();
  return SaveResult::success(selected);
}

SaveResult PatchSession::export_current_patch_as(
    const ExportFormatInfo &format) {
  const auto default_dir = preferences_.last_save_directory();
  const std::string sanitized_name = sanitize_filename(
      current_patch_.name.empty() ? "patch" : current_patch_.name);

  const bool is_web = megatoy::platform::is_web();
  const std::string ext = format.extension.empty() ? "" : format.extension;

  auto download_or_error = [&](const std::string &hint) -> SaveResult {
    if (repository_->download_patch(current_patch_, current_patch_.name,
                                    hint)) {
      return SaveResult::success(std::filesystem::path("download"));
    }
    return SaveResult::error("Failed to export patch");
  };

  if (is_web) {
    return download_or_error(ext.empty() ? ".dmp" : ext);
  }

  std::filesystem::path selected_path;
  const std::string default_name =
      sanitized_name + (ext.empty() ? "" : ext.front() == '.' ? ext : "." + ext);
  std::vector<platform::file_dialog::FileFilter> filters;
  std::string trimmed_ext = ext;
  if (!trimmed_ext.empty() && trimmed_ext.front() == '.') {
    trimmed_ext = trimmed_ext.substr(1);
  }
  if (!trimmed_ext.empty()) {
    filters.push_back({format.label, {trimmed_ext}});
  }
  filters.push_back({"All Files", {"*"}});

  auto result = platform::file_dialog::save_file(default_dir, default_name,
                                                 filters, selected_path);

  if (result == platform::file_dialog::DialogResult::Ok) {
    if (selected_path.extension().empty() && !ext.empty()) {
      selected_path.replace_extension(ext);
    }

    bool ok = false;
    if (format.is_text) {
      ok = formats::PatchRegistry::instance().write_text(
          selected_path.extension().string(), current_patch_, selected_path);
    } else {
      ok = formats::PatchRegistry::instance().write(
          selected_path.extension().string(), current_patch_, selected_path);
    }

    if (ok) {
      preferences_.set_last_save_directory(selected_path.parent_path());
      return SaveResult::success(selected_path);
    }
    return SaveResult::error("Failed to export file: " +
                             selected_path.string());
  }
  if (result == platform::file_dialog::DialogResult::Cancelled) {
    return SaveResult::cancelled();
  }
  return SaveResult::error("Failed to export patch");
}

std::optional<ExportFormatInfo>
PatchSession::find_export_format(const std::string &extension) const {
  auto formats = formats::PatchRegistry::instance().export_formats();
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

std::vector<ExportFormatInfo> PatchSession::export_formats() const {
  return formats::PatchRegistry::instance().export_formats();
}

bool PatchSession::note_on(ym2612::Note note, uint8_t velocity,
                           const PreferenceManager::UIPreferences &prefs) {
  // Handed to the audio thread rather than written here: the chip has exactly
  // one writer, and the note starts on the next audio callback instead of
  // waiting for whenever this frame happens to finish.
  audio_.set_note_options(prefs.use_velocity, prefs.steal_oldest_note_when_full);
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

PatchSession::PatchSnapshot PatchSession::capture_snapshot() const {
  PatchSnapshot snapshot;
  snapshot.original_patch = original_patch_;
  snapshot.patch = current_patch_;
  snapshot.path = current_patch_path_;
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
  apply_patch_to_audio();
}

bool PatchSession::can_save_in_place() const {
  const auto folder = writable_source_folder();
  if (!folder) {
    return false;
  }
  return can_overwrite_in_place(
      repository_->to_absolute_path(current_patch_path_));
}

bool PatchSession::current_patch_is_user_patch() const {
  if (current_patch_path_.empty()) {
    return false;
  }

  const bool is_local_storage =
      current_patch_path_.rfind("localStorage/", 0) == 0 ||
      current_patch_path_.rfind("localStorage://", 0) == 0;

  return (can_save_in_place() || is_local_storage) &&
         original_patch_.name == current_patch_.name;
}

const char *PatchSession::save_label_for(bool is_user_patch) const {
  if (current_patch_path_.rfind("localStorage/", 0) == 0) {
    return is_user_patch ? "Overwrite" : "Save to 'localStorage'";
  }
  if (!is_user_patch) {
    return "Save As...";
  }
  // Saving a .ginpkg adds a version rather than replacing the file.
  return current_patch_path_.ends_with(".ginpkg") ? "Save version"
                                                  : "Overwrite";
}

} // namespace patches
