#include "patch_session.hpp"

#include "audio/audio_command.hpp"
#include "audio/audio_manager.hpp"
#include "formats/patch_loader.hpp"
#include "formats/ym2612_format_adapter.hpp"
#include "patches/patch_write.hpp"
#include "platform/platform_config.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_patch_url.hpp"
#endif
#include <algorithm>
#include <filesystem>
#include <utility>

namespace patches {

PatchSession::PatchSession(megatoy::system::PathService &directories,
                           PreferenceManager &preferences, AudioManager &audio,
                           PersistentParseCache *persistent_cache)
    : directories_(directories), preferences_(preferences), audio_(audio),
      repository_(std::make_unique<PatchRepository>(
          directories_.file_system(), preferences_.workspace(),
          directories_.paths().builtin_presets_root,
          directories_.paths().builtin_presets_metadata, persistent_cache)),
      disk_versions_(*repository_) {
  repository_->set_show_builtin_presets(preferences_.show_builtin_presets());
  active_id_ = create_document(/*pinned=*/false).id();
  // A saved list can still hold a folder inside another.
  absorb_nested_folders();
}

const std::vector<std::unique_ptr<PatchDocument>> &
PatchSession::documents() const {
  return documents_;
}

PatchDocument &PatchSession::active_document() {
  return *find_document(active_id_);
}

const PatchDocument &PatchSession::active_document() const {
  return *find_document(active_id_);
}

DocumentId PatchSession::active_document_id() const { return active_id_; }

PatchDocument *PatchSession::find_document(DocumentId id) {
  for (auto &document : documents_) {
    if (document->id() == id) {
      return document.get();
    }
  }
  return nullptr;
}

const PatchDocument *PatchSession::find_document(DocumentId id) const {
  for (const auto &document : documents_) {
    if (document->id() == id) {
      return document.get();
    }
  }
  return nullptr;
}

bool PatchSession::activate(DocumentId id) {
  auto *document = find_document(id);
  if (document == nullptr) {
    return false;
  }
  if (id == active_id_) {
    return true;
  }
  active_id_ = id;
  preferences_.set_last_patch_path(document->path());
#if defined(MEGATOY_PLATFORM_WEB)
  platform::web::patch_url::sync_patch_to_url_if_needed(document->patch());
#endif
  return true;
}

bool PatchSession::any_document_modified() const {
  return modified_document_count() > 0;
}

std::size_t PatchSession::modified_document_count() const {
  return static_cast<std::size_t>(std::count_if(
      documents_.begin(), documents_.end(),
      [](const auto &document) { return document->is_modified(); }));
}

void PatchSession::mark_all_clean() {
  for (auto &document : documents_) {
    document->mark_as_clean();
  }
}

ym2612::Patch &PatchSession::current_patch() {
  return active_document().patch();
}

const ym2612::Patch &PatchSession::current_patch() const {
  return active_document().patch();
}

const std::string &PatchSession::current_patch_path() const {
  return active_document().path();
}

void PatchSession::set_current_patch_path(const std::filesystem::path &path,
                                          RememberPatchPath remember) {
  set_document_path(active_document(), path, remember);
}

const std::string &PatchSession::current_patch_selection_path() const {
  return active_document().selection_path();
}

void PatchSession::set_current_patch_selection_path(
    const std::filesystem::path &path) {
  set_document_selection_path(active_document(), path);
}

bool PatchSession::is_active(const PatchDocument &document) const {
  return document.id() == active_id_;
}

PatchDocument &PatchSession::create_document(bool pinned) {
  documents_.push_back(std::make_unique<PatchDocument>(next_id_++));
  documents_.back()->set_pinned(pinned);
  return *documents_.back();
}

void PatchSession::set_document_path(PatchDocument &document,
                                     const std::filesystem::path &path,
                                     RememberPatchPath remember) {
  if (path.empty()) {
    document.set_path({});
    document.set_selection_path({});
  } else {
    const auto stored =
        (path.is_absolute() ? repository_->to_relative_path(path) : path)
            .generic_string();
    auto selection = stored;
    if (lowercase_extension(path) == ".ginpkg") {
      selection += "/latest";
    }
    document.set_path(stored);
    document.set_selection_path(std::move(selection));
  }

  if (remember == RememberPatchPath::Yes && is_active(document)) {
    preferences_.set_last_patch_path(document.path());
  }
}

std::string
PatchSession::selection_key(const std::filesystem::path &path) const {
  if (path.empty()) {
    return {};
  }
  return (path.is_absolute() ? repository_->to_relative_path(path) : path)
      .generic_string();
}

void PatchSession::set_document_selection_path(
    PatchDocument &document, const std::filesystem::path &path) {
  document.set_selection_path(selection_key(path));
}

PatchRepository &PatchSession::repository() { return *repository_; }

const PatchRepository &PatchSession::repository() const { return *repository_; }

bool PatchSession::is_modified() const {
  return active_document().is_modified();
}
void PatchSession::mark_as_clean() { active_document().mark_as_clean(); }

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
  assign_default_patch(active_document());
}

void PatchSession::assign_default_patch(PatchDocument &document) {
  auto starting_point = default_patch_source();
  document.patch() = std::move(starting_point.patch);
  if (starting_point.source.empty()) {
    set_document_path(document, {}, RememberPatchPath::No);
    document.mark_as_clean();
    return;
  }
  set_document_path(document, starting_point.source, RememberPatchPath::No);
  document.set_file_identity(starting_point.source);
  document.mark_as_clean();
  remember_disk_time(document);
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

  const auto *entry = find_entry(relative_string);
  return entry != nullptr && load_patch_from_entry(*entry);
}

const PatchEntry *
PatchSession::find_entry(const std::string &relative_path) const {
  return repository_->find_entry(relative_path);
}

void PatchSession::set_current_patch(const ym2612::Patch &patch,
                                     const std::filesystem::path &source_path,
                                     RememberPatchPath remember) {
  assign_patch(active_document(), patch, source_path, remember);
  apply_patch_to_audio();
#if defined(MEGATOY_PLATFORM_WEB)
  platform::web::patch_url::sync_patch_to_url_if_needed(current_patch());
#endif
}

void PatchSession::assign_patch(PatchDocument &document,
                                const ym2612::Patch &patch,
                                const std::filesystem::path &source_path,
                                RememberPatchPath remember) {
  document.patch() = patch;
  set_document_path(document, source_path, remember);
  if (!source_path.empty()) {
    const auto format = formats::adapter::format_for_extension(
        lowercase_extension(source_path));
    if (!format || !formats::adapter::is_multi_patch(*format)) {
      document.set_file_identity(source_path);
    }
  }
  document.mark_as_clean(); // New patch loaded, not modified yet
  remember_disk_time(document);
}

void PatchSession::remember_disk_time(const PatchDocument &document) {
  disk_versions_.remember(document);
}

std::vector<PatchSession::DiskVersion> PatchSession::take_disk_versions() {
  return disk_versions_.take(documents_);
}

bool PatchSession::outside_file_changed() const {
  return disk_versions_.outside_file_changed(documents_);
}

void PatchSession::accept_disk_version(DocumentId id,
                                       const ym2612::Patch &patch,
                                       bool replace) {
  auto *document = find_document(id);
  if (document == nullptr) {
    return;
  }
  document->set_original_patch(patch);
  if (replace) {
    document->patch() = patch;
    if (is_active(*document)) {
      apply_patch_to_audio();
    }
  }
}

void PatchSession::apply_patch_to_audio() {
  // Patch edits take the same route as notes so that a slider drag cannot
  // rewrite registers underneath the renderer.
  const auto &patch = current_patch();
  if (audio_.submit(audio::AudioCommand::apply_patch(
          patch.global, patch.channel, patch.instrument))) {
    last_applied_ = patch;
    has_applied_patch_ = true;
  }
}

bool PatchSession::apply_patch_to_audio_if_changed() {
  const auto &patch = current_patch();
  const bool audio_settings_changed =
      !has_applied_patch_ || patch.global != last_applied_.global ||
      patch.channel != last_applied_.channel ||
      patch.instrument != last_applied_.instrument;
  if (!audio_settings_changed) {
    return false;
  }

  apply_patch_to_audio();
  return has_applied_patch_ && patch.global == last_applied_.global &&
         patch.channel == last_applied_.channel &&
         patch.instrument == last_applied_.instrument;
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

PatchSession::PatchSnapshot PatchSession::capture_snapshot() const {
  return active_document().capture_snapshot();
}

void PatchSession::restore_snapshot(const PatchSnapshot &snapshot) {
  restore_snapshot(active_id_, snapshot);
}

void PatchSession::restore_snapshot(DocumentId id,
                                    const PatchSnapshot &snapshot) {
  auto *document = find_document(id);
  if (document == nullptr) {
    return;
  }
  document->restore_contents(snapshot);
  set_document_path(*document, snapshot.path);
  set_document_selection_path(*document, snapshot.selection_path);
  if (is_active(*document)) {
    apply_patch_to_audio();
  }
}

} // namespace patches
