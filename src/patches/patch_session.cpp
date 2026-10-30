#include "patch_session.hpp"

#include "audio/audio_command.hpp"
#include "audio/audio_manager.hpp"
#include "core/status.hpp"
#include "formats/patch_loader.hpp"
#include "formats/patch_registry.hpp"
#include "formats/ym2612_format_adapter.hpp"
#include "patches/folder_metadata.hpp"
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
#include <iterator>
#include <string_view>
#include <utility>

namespace patches {

PatchSession::PatchSession(megatoy::system::PathService &directories,
                           PreferenceManager &preferences, AudioManager &audio,
                           PersistentParseCache *persistent_cache)
    : directories_(directories), preferences_(preferences), audio_(audio),
      repository_(std::make_unique<PatchRepository>(
          directories_.file_system(), preferences_.workspace(),
          directories_.paths().builtin_presets_root,
          directories_.paths().builtin_presets_metadata, persistent_cache)) {
  repository_->set_show_builtin_presets(preferences_.show_builtin_presets());
  active_id_ = create_document(/*pinned=*/false).id();
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

std::string
PatchSession::file_identity(const std::string &relative_path) const {
  if (relative_path.empty()) {
    return {};
  }
  const auto absolute = repository_->to_absolute_path(relative_path);
  return absolute.is_absolute() ? absolute.lexically_normal().generic_string()
                                : relative_path;
}

std::optional<DocumentId> PatchSession::find_document_by_selection_path(
    const std::filesystem::path &path) const {
  const auto identity = file_identity(selection_key(path));
  if (identity.empty()) {
    return std::nullopt;
  }
  for (const auto &document : documents_) {
    if (file_identity(document->selection_path()) == identity) {
      return document->id();
    }
  }
  return std::nullopt;
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

DocumentId PatchSession::open_document(
    const ym2612::Patch &patch, const std::filesystem::path &source_path,
    const std::filesystem::path &selection_path, bool pinned) {
  auto &document = create_document(pinned);
  assign_patch(document, patch, source_path, RememberPatchPath::No);
  if (!selection_path.empty()) {
    set_document_selection_path(document, selection_path);
  }
  return document.id();
}

DocumentId PatchSession::open_default_document(bool pinned) {
  auto &document = create_document(pinned);
  assign_default_patch(document);
  return document.id();
}

DocumentId PatchSession::open_new_document() {
  auto patch = default_patch();
  patch.name.clear();
  return place_document(patch, {}, {}, /*pinned=*/true);
}

std::optional<DocumentId> PatchSession::open_entry(const PatchEntry &entry,
                                                   bool pinned) {
  ym2612::Patch patch;
  if (!repository_->load_patch(entry, patch)) {
    return std::nullopt;
  }
  return open_document(patch,
                       entry.source_relative_path.empty()
                           ? entry.relative_path
                           : entry.source_relative_path,
                       entry.relative_path, pinned);
}

namespace {

/// Two patches the chip would play the same way, whatever they are called.
bool same_sound(const ym2612::Patch &a, const ym2612::Patch &b) {
  return a.global == b.global && a.channel == b.channel &&
         a.instrument == b.instrument;
}

std::filesystem::path source_of(const PatchEntry &entry) {
  return entry.source_relative_path.empty() ? entry.relative_path
                                            : entry.source_relative_path;
}

} // namespace

std::optional<DocumentId> PatchSession::preview_document_id() const {
  for (const auto &document : documents_) {
    if (!document->pinned()) {
      return document->id();
    }
  }
  return std::nullopt;
}

bool PatchSession::pin(DocumentId id) {
  auto *document = find_document(id);
  if (document == nullptr) {
    return false;
  }
  document->set_pinned(true);
  return true;
}

void PatchSession::pin_modified_documents() {
  for (auto &document : documents_) {
    if (!document->pinned() && document->is_modified()) {
      document->set_pinned(true);
    }
  }
}

DocumentId PatchSession::place_document(
    const ym2612::Patch &patch, const std::filesystem::path &source_path,
    const std::filesystem::path &selection_path, bool pinned,
    bool reuse_preview) {
  PatchDocument *target = nullptr;
  if (const auto preview = preview_document_id(); preview && reuse_preview) {
    auto *document = find_document(*preview);
    if (document->is_modified()) {
      document->set_pinned(true);
    } else {
      target = document;
    }
  }
  if (target == nullptr) {
    target = &create_document(pinned);
  }
  target->set_pinned(pinned);
  activate(target->id());
  assign_patch(*target, patch, source_path, RememberPatchPath::Yes);
  if (!selection_path.empty()) {
    set_document_selection_path(*target, selection_path);
  }
#if defined(MEGATOY_PLATFORM_WEB)
  platform::web::patch_url::sync_patch_to_url_if_needed(target->patch());
#endif
  return target->id();
}

std::optional<DocumentId>
PatchSession::open_or_activate(const PatchEntry &entry, bool pinned,
                               bool reuse_preview) {
  if (const auto open = find_document_by_selection_path(entry.relative_path)) {
    activate(*open);
    if (pinned) {
      pin(*open);
    }
    return open;
  }
  ym2612::Patch patch;
  if (!repository_->load_patch(entry, patch)) {
    return std::nullopt;
  }
  return place_document(patch, source_of(entry), entry.relative_path, pinned,
                        reuse_preview);
}

DocumentId
PatchSession::open_or_activate_patch(const ym2612::Patch &patch,
                                     const std::filesystem::path &source_path,
                                     bool pinned) {
  const auto identity = file_identity(selection_key(source_path));
  if (!identity.empty()) {
    for (const auto &document : documents_) {
      if (file_identity(document->path()) == identity &&
          same_sound(document->original_patch(), patch)) {
        activate(document->id());
        if (pinned) {
          document->set_pinned(true);
        }
        return document->id();
      }
    }
  }
  return place_document(patch, source_path, {}, pinned);
}

std::optional<DocumentId>
PatchSession::find_document_with_sound(const ym2612::Patch &patch) const {
  if (same_sound(active_document().patch(), patch)) {
    return active_id_;
  }
  for (const auto &document : documents_) {
    if (same_sound(document->patch(), patch)) {
      return document->id();
    }
  }
  return std::nullopt;
}

std::optional<DocumentId>
PatchSession::open_path(const std::filesystem::path &path, bool pinned) {
  const auto *entry = find_entry(selection_key(path));
  if (entry == nullptr) {
    return std::nullopt;
  }
  return open_or_activate(*entry, pinned);
}

void PatchSession::remember_open_documents() {
  std::vector<OpenPatchRecord> records;
  int active = -1;
  for (const auto &document : documents_) {
    if (document->path().empty() || document->selection_path().empty()) {
      continue;
    }
    if (document->id() == active_id_) {
      active = static_cast<int>(records.size());
    }
    records.push_back({document->selection_path(), document->pinned()});
  }
  preferences_.set_open_patches(std::move(records), active);
}

bool PatchSession::restore_open_documents() {
  const auto records = preferences_.open_patches();
  const int active = preferences_.active_open_patch();
  const auto initial = active_id_;
  std::optional<DocumentId> first;
  std::optional<DocumentId> to_activate;
  bool have_preview = false;
  for (std::size_t i = 0; i < records.size(); ++i) {
    const auto *entry = find_entry(records[i].path);
    const std::filesystem::path recorded = records[i].path;
    ym2612::Patch patch;
    if (entry != nullptr) {
      if (!repository_->load_patch(*entry, patch)) {
        continue;
      }
    } else {
      // A file opened from outside the workspace is not in the tree.
      if (!recorded.is_absolute()) {
        continue;
      }
      auto loaded = formats::load_patch_from_file(recorded);
      if (loaded.status != formats::PatchLoadStatus::Success ||
          loaded.patches.empty()) {
        continue;
      }
      patch = std::move(loaded.patches[0]);
    }
    const bool pinned = records[i].pinned || have_preview;
    have_preview = have_preview || !pinned;
    const auto id =
        entry != nullptr
            ? open_document(patch, source_of(*entry), entry->relative_path,
                            pinned)
            : open_document(patch, recorded, {}, pinned);
    if (!first) {
      first = id;
    }
    if (static_cast<int>(i) == active) {
      to_activate = id;
    }
  }
  if (!first) {
    return false;
  }
  activate(to_activate.value_or(*first));
  const auto *active_document = find_document(active_id_);
  preferences_.set_last_patch_path(active_document->path());
#if defined(MEGATOY_PLATFORM_WEB)
  platform::web::patch_url::sync_patch_to_url_if_needed(
      active_document->patch());
#endif
  close_document(initial);
  return true;
}

bool PatchSession::close_document(DocumentId id) {
  const auto found =
      std::find_if(documents_.begin(), documents_.end(),
                   [id](const auto &document) { return document->id() == id; });
  if (found == documents_.end()) {
    return false;
  }
  if (documents_.size() == 1) {
    const auto replacement = open_default_document(/*pinned=*/false);
    activate(replacement);
  }
  const auto it =
      std::find_if(documents_.begin(), documents_.end(),
                   [id](const auto &document) { return document->id() == id; });
  if (id == active_id_) {
    const auto neighbour =
        std::next(it) != documents_.end() ? std::next(it) : std::prev(it);
    activate((*neighbour)->id());
  }
  documents_.erase(it);
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
    if (path.extension() == ".ginpkg") {
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

void PatchSession::sync_workspace() {
  // Resolved before the repository changes: a relabelled folder changes the
  // relative path a file is known by.
  std::vector<std::pair<PatchDocument *, std::optional<std::filesystem::path>>>
      resolved_paths;
  for (auto &document : documents_) {
    if (document->path().empty()) {
      continue;
    }
    std::optional<std::filesystem::path> absolute;
    auto resolved = repository_->to_absolute_path(document->path());
    if (resolved.is_absolute()) {
      absolute = std::move(resolved);
    }
    resolved_paths.emplace_back(document.get(), std::move(absolute));
  }

  repository_->set_show_builtin_presets(preferences_.show_builtin_presets());
  repository_->sync_workspace();

  for (auto &[document, absolute] : resolved_paths) {
    if (!absolute) {
      set_document_path(*document, {});
      continue;
    }
    const auto relative = repository_->to_relative_path(*absolute);
    if (relative.is_absolute()) {
      set_document_path(*document, {});
      continue;
    }
    set_document_path(*document, relative);
  }

  absorb_nested_folders();
}

void PatchSession::absorb_nested_folders() {
  auto &workspace = preferences_.workspace();
  struct Nesting {
    std::filesystem::path inner;
    std::filesystem::path outer;
    std::filesystem::path inner_sidecar;
    std::filesystem::path outer_sidecar;
  };
  std::vector<Nesting> nestings;
  for (const auto &folder : workspace.folders()) {
    const auto *outer = workspace.enclosing(folder.path);
    if (outer != nullptr &&
        !preferences_.workspace_folder_is_protected(folder.path)) {
      nestings.push_back({folder.path, outer->path, folder.metadata_path(),
                          outer->metadata_path()});
    }
  }
  if (nestings.empty()) {
    return;
  }

  // These name files by the inner folders, so they are resolved while those
  // are still in the list.
  using Resolved = std::optional<std::filesystem::path>;
  const auto resolve = [this](const std::string &relative) -> Resolved {
    if (relative.empty()) {
      return std::nullopt;
    }
    auto absolute = repository_->to_absolute_path(relative);
    if (!absolute.is_absolute()) {
      return std::nullopt;
    }
    return absolute;
  };
  struct DocumentPaths {
    PatchDocument *document;
    std::optional<std::filesystem::path> path;
    std::optional<std::filesystem::path> selection;
  };
  std::vector<DocumentPaths> document_paths;
  for (auto &document : documents_) {
    document_paths.push_back({document.get(), resolve(document->path()),
                              resolve(document->selection_path())});
  }
  const auto records = preferences_.open_patches();
  std::vector<std::optional<std::filesystem::path>> record_paths;
  for (const auto &record : records) {
    record_paths.push_back(resolve(record.path));
  }
  const auto last_patch = resolve(preferences_.last_patch_path());

  bool absorbed_any = false;
  for (const auto &nesting : nestings) {
    // Ratings that cannot be carried over keep their folder in the list.
    FolderMetadataStore inner_store(nesting.inner_sidecar);
    FolderMetadataStore outer_store(nesting.outer_sidecar);
    bool carried = inner_store.load() && outer_store.load();
    if (carried) {
      const auto prefix =
          nesting.inner.lexically_relative(nesting.outer).generic_string();
      auto entries = inner_store.entries();
      for (auto &entry : entries) {
        entry.path = prefix + "/" + entry.path;
      }
      std::size_t changed = 0;
      carried = outer_store.merge_newer(entries, changed);
    }
    if (!carried) {
      megatoy::status::warning("Could not move the ratings of \"" +
                               nesting.inner.filename().string() +
                               "\" into \"" +
                               nesting.outer.filename().string() + "\".");
      continue;
    }
    absorbed_any = true;

    // The outer folder takes the inner one's place in the list.
    const auto &folders = workspace.folders();
    const auto index_of = [&folders](const std::filesystem::path &path) {
      return static_cast<std::size_t>(
          std::find_if(folders.begin(), folders.end(),
                       [&path](const auto &folder) {
                         return folder.path == path;
                       }) -
          folders.begin());
    };
    const auto inner_index = index_of(nesting.inner);
    const auto outer_index = index_of(nesting.outer);
    if (outer_index < folders.size() && inner_index < outer_index) {
      preferences_.reorder_workspace_folder(outer_index, inner_index);
    }
    preferences_.remove_workspace_folder(nesting.inner);
    megatoy::status::info("\"" + nesting.inner.filename().string() +
                          "\" is now part of \"" +
                          nesting.outer.filename().string() + "\".");
  }

  if (!absorbed_any) {
    return;
  }
  repository_->sync_workspace();

  const auto relative_of =
      [this](const std::optional<std::filesystem::path> &absolute) {
        if (!absolute) {
          return std::string{};
        }
        const auto relative = repository_->to_relative_path(*absolute);
        return relative.is_absolute() ? std::string{}
                                      : relative.generic_string();
      };
  for (const auto &[document, path, selection] : document_paths) {
    if (path) {
      document->set_path(relative_of(path));
    }
    if (selection) {
      document->set_selection_path(relative_of(selection));
    }
  }
  std::vector<OpenPatchRecord> remapped;
  int active = -1;
  for (std::size_t i = 0; i < records.size(); ++i) {
    auto path = relative_of(record_paths[i]);
    if (path.empty()) {
      continue;
    }
    if (static_cast<int>(i) == preferences_.active_open_patch()) {
      active = static_cast<int>(remapped.size());
    }
    remapped.push_back({std::move(path), records[i].pinned});
  }
  preferences_.set_open_patches(std::move(remapped), active);
  if (last_patch) {
    preferences_.set_last_patch_path(relative_of(last_patch));
  }
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
  if (relative_path.empty()) {
    return nullptr;
  }
  const auto search = [&](const auto &self,
                          const std::vector<PatchEntry> &entries,
                          bool by_source) -> const PatchEntry * {
    for (const auto &entry : entries) {
      if (!entry.is_directory &&
          (by_source ? entry.source_relative_path == relative_path
                     : entry.relative_path == relative_path)) {
        return &entry;
      }
      if (const auto *found = self(self, entry.children, by_source)) {
        return found;
      }
    }
    return nullptr;
  };
  if (const auto *exact = search(search, repository_->tree(), false)) {
    return exact;
  }
  return search(search, repository_->tree(), true);
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

template <typename Change>
bool PatchSession::update_entry_metadata(const PatchEntry &entry,
                                         Change &&change) {
  if (entry.is_directory || !repository_->can_edit_metadata(entry)) {
    return false;
  }
  auto metadata = entry.metadata.value_or(PatchMetadata{});
  metadata.path = entry.relative_path;
  if (metadata.hash.empty()) {
    ym2612::Patch patch;
    if (repository_->load_patch(entry, patch)) {
      metadata.hash = patch.hash();
    }
  }
  change(metadata);
  if (!repository_->update_patch_metadata(entry.relative_path, metadata)) {
    return false;
  }
  repository_->refresh();
  return true;
}

bool PatchSession::set_entry_rating(const PatchEntry &entry, int stars) {
  stars = std::clamp(stars, 0, 5);
  if (entry.metadata && entry.metadata->star_rating == stars) {
    return true;
  }
  return update_entry_metadata(entry, [stars](PatchMetadata &metadata) {
    metadata.star_rating = stars;
  });
}

bool PatchSession::set_entry_category(const PatchEntry &entry,
                                      const std::string &category) {
  if (entry.metadata ? entry.metadata->category == category
                     : category.empty()) {
    return true;
  }
  return update_entry_metadata(entry, [&category](PatchMetadata &metadata) {
    metadata.category = category;
  });
}

void PatchSession::remember_disk_time(const PatchDocument &document) {
  if (document.path().empty()) {
    disk_times_.erase(document.id());
    return;
  }
  std::error_code error;
  const auto time = std::filesystem::last_write_time(
      repository_->to_absolute_path(document.path()), error);
  if (error) {
    disk_times_.erase(document.id());
    return;
  }
  disk_times_[document.id()] = time;
}

std::vector<PatchSession::DiskVersion> PatchSession::take_disk_versions() {
  std::erase_if(disk_times_, [this](const auto &entry) {
    return find_document(entry.first) == nullptr;
  });
  std::vector<DiskVersion> versions;
  for (const auto &document : documents_) {
    const auto recorded = disk_times_.find(document->id());
    if (recorded == disk_times_.end()) {
      continue;
    }
    const auto absolute = repository_->to_absolute_path(document->path());
    std::error_code error;
    const auto time = std::filesystem::last_write_time(absolute, error);
    if (error || time == recorded->second) {
      continue;
    }
    recorded->second = time;

    const auto &selection = document->selection_path().empty()
                                ? document->path()
                                : document->selection_path();
    const auto *entry = find_entry(selection);
    ym2612::Patch patch;
    if (entry == nullptr || !repository_->load_patch(*entry, patch)) {
      continue;
    }
    // Named as opening the file names it, so only a change in the file
    // itself tells it apart.
    const auto format = formats::adapter::format_for_extension(
        lowercase_extension(document->path()));
    if (!format || !formats::adapter::is_multi_patch(*format)) {
      patch.name = absolute.stem().string();
    }
    if (patch != document->original_patch()) {
      versions.push_back({document->id(), std::move(patch)});
    }
  }
  return versions;
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

std::optional<PatchSession::Relocation>
PatchSession::prepare_relocation(const PatchEntry &entry,
                                 std::filesystem::path to) const {
  Relocation relocation;
  relocation.from = entry.full_path;
  relocation.to = std::move(to);

  // A folder is registered in the workspace by path, so a relocated one has
  // to be followed there too or it comes back missing on the next launch.
  // Looked up before the change: once the directory is gone its old path no
  // longer resolves to what is stored.
  if (entry.is_directory) {
    if (const auto *folder = preferences_.workspace().find(relocation.from)) {
      if (preferences_.workspace_folder_is_protected(folder->path)) {
        return std::nullopt;
      }
      relocation.workspace_folder = folder->path;
    }
  }

  for (const auto &document : documents_) {
    const auto &path = document->path();
    if (path.empty()) {
      continue;
    }
    auto resolved = repository_->to_absolute_path(path);
    if (!resolved.is_absolute()) {
      continue;
    }
    Relocation::Follower follower{document->id(), std::move(resolved), {}};
    const auto &selection = document->selection_path();
    if (selection.rfind(path, 0) == 0) {
      follower.selection_suffix = selection.substr(path.size());
    }
    relocation.documents.push_back(std::move(follower));
  }
  return relocation;
}

void PatchSession::follow_relocation(const Relocation &relocation,
                                     bool renamed) {
  if (relocation.workspace_folder) {
    preferences_.rename_workspace_folder(*relocation.workspace_folder,
                                         relocation.to);
    repository_->sync_workspace();
  }

  // Follow each document to wherever it ended up, if it was inside what
  // changed at all. Anything else keeps the path it already had -- which
  // includes patches from outside the workspace entirely.
  for (const auto &follower : relocation.documents) {
    auto *document = find_document(follower.id);
    if (document == nullptr) {
      continue;
    }
    const auto moved =
        rebase(follower.absolute, relocation.from, relocation.to);
    if (moved == follower.absolute) {
      continue;
    }

    const auto relative = repository_->to_relative_path(moved);
    if (relative.is_absolute()) {
      set_document_path(*document, {});
      continue;
    }
    set_document_path(*document, relative);
    if (!follower.selection_suffix.empty()) {
      set_document_selection_path(*document, relative.generic_string() +
                                                 follower.selection_suffix);
    }
    if (renamed) {
      document->set_file_identity(moved);
    }
  }
}

bool PatchSession::rename_patch(const PatchEntry &entry,
                                const std::string &new_stem) {
  const auto &old_path = entry.full_path;
  auto relocation = prepare_relocation(
      entry,
      old_path.parent_path() /
          (entry.is_directory ? new_stem
                              : new_stem + old_path.extension().string()));
  if (!relocation || !repository_->rename_patch(entry, new_stem)) {
    return false;
  }
  follow_relocation(*relocation, /*renamed=*/true);
  return true;
}

namespace {

bool selection_belongs_to_entry(const std::string &selection,
                                const PatchEntry &entry) {
  if (selection == entry.relative_path) {
    return true;
  }
  const std::string prefix = entry.relative_path + "/";
  return selection.rfind(prefix, 0) == 0;
}

} // namespace

bool PatchSession::delete_patch(const PatchEntry &entry) {
  std::vector<DocumentId> showing;
  for (const auto &document : documents_) {
    if (selection_belongs_to_entry(document->selection_path(), entry)) {
      showing.push_back(document->id());
    }
  }
  if (!repository_->delete_patch(entry)) {
    return false;
  }
  for (const auto id : showing) {
    if (auto *document = find_document(id)) {
      set_document_path(*document, {});
    }
  }
  return true;
}

bool PatchSession::can_move_patch(const PatchEntry &entry,
                                  const PatchEntry &destination) const {
  if (entry.is_directory &&
      preferences_.workspace_folder_is_protected(entry.full_path)) {
    return false;
  }
  return repository_->can_move_patch(entry, destination);
}

bool PatchSession::move_patch(const PatchEntry &entry,
                              const PatchEntry &destination) {
  auto relocation = prepare_relocation(entry, destination.full_path /
                                                  entry.full_path.filename());
  if (!relocation || !repository_->move_patch(entry, destination)) {
    return false;
  }
  follow_relocation(*relocation, /*renamed=*/false);
  return true;
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
  return document.path().ends_with(".ginpkg") ? "Save Version" : "Overwrite";
}

} // namespace patches
