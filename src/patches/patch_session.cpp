#include "patch_session.hpp"
#include "audio/audio_manager.hpp"
#include "formats/ctrmml.hpp"
#include "formats/dmp.hpp"
#include "formats/gin.hpp"
#include "formats/patch_loader.hpp"
#include "platform/platform_config.hpp"
#include "platform/file_dialog.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_patch_store.hpp"
#include "platform/web/web_download.hpp"
#endif
#include "ym2612/channel.hpp"
#include "ym2612/types.hpp"
#include <algorithm>
#include <filesystem>
#include <utility>

namespace patches {

PatchSession::PatchSession(megatoy::system::PathService &directories,
                           AudioManager &audio)
    : directories_(directories), audio_(audio),
      repository_(std::make_unique<PatchRepository>(
          directories_.file_system(), directories_.paths().patches_root,
          directories_.paths().builtin_presets_root,
          directories_.paths().patch_metadata_db)),
      channel_allocator_() {}

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

void PatchSession::refresh_directories() {
  repository_ = std::make_unique<PatchRepository>(
      directories_.file_system(), directories_.paths().patches_root,
      directories_.paths().builtin_presets_root,
      directories_.paths().patch_metadata_db);
}

void PatchSession::set_current_patch(const ym2612::Patch &patch,
                                     const std::filesystem::path &source_path) {
  current_patch_ = patch;
  set_current_patch_path(source_path);
  mark_as_clean(); // New patch loaded, not modified yet
  apply_patch_to_audio();
}

void PatchSession::apply_patch_to_audio() {
  audio_.apply_patch_to_all_channels(current_patch_);
}

SaveResult PatchSession::save_current_patch(bool force_overwrite) {
#if defined(MEGATOY_PLATFORM_WEB)
  const std::string sanitized_name =
      sanitize_filename(current_patch_.name.empty() ? "patch"
                                                    : current_patch_.name);
  if (platform::web::patch_store::save(current_patch_, sanitized_name)) {
    mark_as_clean();
    repository_->refresh();
    return SaveResult::success(
        std::filesystem::path("localStorage/" + sanitized_name));
  }
  return SaveResult::error("Failed to save patch in browser storage");
#else
  // Check whether the file already exists
  auto patches_dir = directories_.paths().user_patches_root;
  auto patch_path =
      formats::gin::build_patch_path(patches_dir, current_patch_.name);

  if (std::filesystem::exists(patch_path) && !force_overwrite) {
    return SaveResult::duplicated();
  } else {
    // Save as new file
    auto result = formats::gin::save_patch(patches_dir, current_patch_,
                                           current_patch_.name);
    if (result.has_value()) {
      mark_as_clean(); // Patch saved, no longer modified
      return SaveResult::success(result.value());
    } else {
      return SaveResult::error("Failed to save patch");
    }
  }
#endif
}

SaveResult PatchSession::export_current_patch_as(ExportFormat format) {
  const auto &default_dir = directories_.paths().export_root;
  const std::string sanitized_name = sanitize_filename(
      current_patch_.name.empty() ? "patch" : current_patch_.name);

#if defined(MEGATOY_PLATFORM_WEB)
  switch (format) {
  case ExportFormat::DMP: {
    const std::string filename = sanitized_name + ".dmp";
    auto data = formats::dmp::serialize_patch(current_patch_);
    platform::web::download_binary(filename, data, "application/octet-stream");
    return SaveResult::success(filename);
  }
  case ExportFormat::MML: {
    const std::string filename = sanitized_name + ".mml";
    auto text = formats::ctrmml::patch_to_string(current_patch_);
    platform::web::download_text(filename, text, "text/plain");
    return SaveResult::success(filename);
  }
  }
  return SaveResult::error("Unknown export format");
#else
  switch (format) {
  case ExportFormat::DMP: {
    std::filesystem::path selected_path;
    auto result = platform::file_dialog::save_file(
        default_dir, sanitized_name + ".dmp",
        {{"DMP Files", {"dmp"}}, {"All Files", {"*"}}}, selected_path);

    if (result == platform::file_dialog::DialogResult::Ok) {
      if (selected_path.extension().empty()) {
        selected_path.replace_extension(".dmp");
      }

      if (formats::dmp::write_patch(current_patch_, selected_path)) {
        return SaveResult::success(selected_path);
      } else {
        return SaveResult::error("Failed to export DMP file: " +
                                 selected_path.string());
      }
    }
    return SaveResult::cancelled();
  }

  case ExportFormat::MML: {
    std::filesystem::path selected_path;
    auto result = platform::file_dialog::save_file(
        default_dir, sanitized_name + ".mml",
        {{"MML Files", {"mml"}}, {"All Files", {"*"}}}, selected_path);

    if (result == platform::file_dialog::DialogResult::Ok) {
      if (selected_path.extension().empty()) {
        selected_path.replace_extension(".mml");
      }

      if (formats::ctrmml::write_patch(current_patch_, selected_path)) {
        return SaveResult::success(selected_path);
      } else {
        return SaveResult::error("Failed to export MML file: " +
                                 selected_path.string());
      }
    }
    return SaveResult::cancelled();
  }
  }

  return SaveResult::error("Unknown export format");
#endif
}

bool PatchSession::note_on(ym2612::Note note, uint8_t velocity,
                           const PreferenceManager::UIPreferences &prefs) {
  const uint8_t clamped_velocity =
      std::min<uint8_t>(velocity, static_cast<uint8_t>(127));
  const uint8_t effective_velocity =
      prefs.use_velocity ? clamped_velocity : static_cast<uint8_t>(127);

  auto claim =
      channel_allocator_.note_on(note, prefs.steal_oldest_note_when_full);
  if (!claim) {
    return false;
  }

  if (claim->replaced_note) {
    audio_.device().channel(claim->channel).write_key_off();
  }

  auto ym_channel = audio_.device().channel(claim->channel);
  ym_channel.write_frequency(note);
  auto instrument =
      current_patch_.instrument.clone_with_velocity(effective_velocity);
  ym_channel.write_instrument(instrument);
  ym_channel.write_key_on(
      instrument.operators[static_cast<uint8_t>(ym2612::OperatorIndex::Op1)]
          .enable,
      instrument.operators[static_cast<uint8_t>(ym2612::OperatorIndex::Op2)]
          .enable,
      instrument.operators[static_cast<uint8_t>(ym2612::OperatorIndex::Op3)]
          .enable,
      instrument.operators[static_cast<uint8_t>(ym2612::OperatorIndex::Op4)]
          .enable);
  return true;
}

bool PatchSession::note_off(ym2612::Note note) {
  return channel_allocator_.note_off(note, audio_.device());
}

bool PatchSession::note_is_active(const ym2612::Note &note) const {
  return channel_allocator_.is_note_active(note);
}

void PatchSession::release_all_notes() {
  channel_allocator_.release_all(audio_.device());
}

const std::array<bool, 6> &PatchSession::active_channels() const {
  return channel_allocator_.channel_usage();
}

const std::vector<ym2612::Note> PatchSession::active_notes() const {
  const std::map<ym2612::Note, ym2612::ChannelIndex> &active_notes =
      channel_allocator_.active_notes();
  std::vector<ym2612::Note> notes;
  notes.reserve(active_notes.size());
  for (const auto &note : active_notes) {
    notes.push_back(note.first);
  }
  return notes;
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

bool PatchSession::current_patch_is_user_patch() const {
#if defined(MEGATOY_PLATFORM_WEB)
  const bool is_local_storage =
      !current_patch_path_.empty() &&
      current_patch_path_.rfind("localStorage/", 0) == 0;
#else
  constexpr bool is_local_storage = false;
#endif
  const bool is_user_directory =
      !current_patch_path_.empty() &&
      current_patch_path_.rfind("user/", 0) == 0 &&
      current_patch_path_.ends_with(".gin");
  return (is_user_directory || is_local_storage) &&
         original_patch_.name == current_patch_.name;
}

} // namespace patches
