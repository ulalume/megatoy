#include "patch_session.hpp"

#include <algorithm>
#include <system_error>
#include <utility>

namespace patches {

namespace {

bool selection_belongs_to_entry(const std::string &selection,
                                const PatchEntry &entry) {
  if (selection == entry.relative_path) {
    return true;
  }
  const std::string prefix = entry.relative_path + "/";
  return selection.rfind(prefix, 0) == 0;
}

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

template <typename Change>
bool PatchSession::update_entry_metadata(const PatchEntry &entry,
                                         Change &&change) {
  if (entry.is_directory || !repository_->can_edit_metadata(entry)) {
    return false;
  }
  auto metadata = entry.metadata.value_or(PatchMetadata{});
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

bool PatchSession::set_entry_icon(const PatchEntry &entry,
                                  const std::string &icon) {
  if (entry.metadata ? entry.metadata->icon == icon : icon.empty()) {
    return true;
  }
  return update_entry_metadata(
      entry, [&icon](PatchMetadata &metadata) { metadata.icon = icon; });
}

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

template <typename Remove>
auto PatchSession::detaching(const PatchEntry &entry,
                             std::vector<DetachedDocument> *detached,
                             Remove &&remove) {
  std::vector<DetachedDocument> showing;
  for (const auto &document : documents_) {
    if (selection_belongs_to_entry(document->selection_path(), entry)) {
      showing.push_back(
          {document->id(), document->path(), document->selection_path()});
    }
  }
  auto removed = remove();
  if (!removed) {
    return removed;
  }
  for (const auto &shown : showing) {
    if (auto *document = find_document(shown.id)) {
      set_document_path(*document, {});
    }
  }
  if (detached != nullptr) {
    *detached = std::move(showing);
  }
  return removed;
}

bool PatchSession::delete_patch(const PatchEntry &entry,
                                std::vector<DetachedDocument> *detached) {
  return detaching(entry, detached,
                   [&] { return repository_->delete_patch(entry); });
}

std::optional<DeletedEntry>
PatchSession::trash_patch(const PatchEntry &entry,
                          std::vector<DetachedDocument> *detached) {
  return detaching(entry, detached,
                   [&] { return repository_->trash_patch(entry); });
}

void PatchSession::reattach(const std::vector<DetachedDocument> &detached) {
  for (const auto &shown : detached) {
    auto *document = find_document(shown.id);
    if (document == nullptr || !document->path().empty()) {
      continue;
    }
    document->set_path(shown.path);
    document->set_selection_path(shown.selection_path);
    remember_disk_time(*document);
  }
}

bool PatchSession::remove_copies(
    const std::vector<std::filesystem::path> &copies) {
  std::vector<std::pair<DocumentId, std::filesystem::path>> showing;
  for (const auto &document : documents_) {
    if (document->path().empty()) {
      continue;
    }
    auto file = repository_->to_absolute_path(document->path());
    const bool copied =
        std::any_of(copies.begin(), copies.end(), [&](const auto &copy) {
          const auto relative = file.lexically_relative(copy);
          return !relative.empty() && *relative.begin() != "..";
        });
    if (copied) {
      showing.emplace_back(document->id(), std::move(file));
    }
  }
  const bool removed = repository_->remove_copies(copies);
  for (const auto &[id, file] : showing) {
    std::error_code ec;
    auto *document = find_document(id);
    if (document != nullptr && !std::filesystem::exists(file, ec)) {
      set_document_path(*document, {});
    }
  }
  return removed;
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

} // namespace patches
