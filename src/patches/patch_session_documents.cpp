#include "patch_session.hpp"

#include "formats/patch_loader.hpp"
#include "platform/platform_config.hpp"
#include <algorithm>
#include <iterator>
#include <utility>

#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_patch_url.hpp"
#endif

namespace patches {

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

DocumentId
PatchSession::place_document(const ym2612::Patch &patch,
                             const std::filesystem::path &source_path,
                             const std::filesystem::path &selection_path,
                             bool pinned, bool reuse_preview) {
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
    const auto id = entry != nullptr
                        ? open_document(patch, source_of(*entry),
                                        entry->relative_path, pinned)
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

} // namespace patches
