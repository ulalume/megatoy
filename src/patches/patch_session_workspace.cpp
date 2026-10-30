#include "patch_session.hpp"

#include "core/status.hpp"
#include "patches/folder_metadata.hpp"
#include <algorithm>
#include <utility>

namespace patches {

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
    // A file outside the workspace stays known by its absolute path.
    set_document_path(*document, repository_->to_relative_path(*absolute));
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
      return static_cast<std::size_t>(std::find_if(folders.begin(),
                                                   folders.end(),
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
        return repository_->to_relative_path(*absolute).generic_string();
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

} // namespace patches
