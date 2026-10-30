#pragma once

#include "ym2612/patch.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace patches {

using DocumentId = std::uint32_t;

/**
 * One patch open in the editor: its contents, the contents it was loaded or
 * last saved with, and where it came from.
 *
 * Paths are stored as PatchSession normalises them (relative to the
 * repository where possible); setting them goes through the session, which
 * owns the repository.
 */
class PatchDocument {
public:
  struct Snapshot {
    ym2612::Patch original_patch;
    ym2612::Patch patch;
    std::string path;
    std::string selection_path;
    bool operator==(const Snapshot &other) const {
      return patch == other.patch && path == other.path &&
             selection_path == other.selection_path;
    }
  };

  explicit PatchDocument(DocumentId id) : id_(id) {}

  DocumentId id() const { return id_; }

  ym2612::Patch &patch() { return patch_; }
  const ym2612::Patch &patch() const { return patch_; }
  const ym2612::Patch &original_patch() const { return original_patch_; }

  bool is_modified() const { return original_patch_ != patch_; }
  void mark_as_clean() { original_patch_ = patch_; }
  /// Take `patch` as what the file holds, leaving the contents as they are.
  void set_original_patch(const ym2612::Patch &patch) {
    original_patch_ = patch;
  }

  /// The file Save targets.
  const std::string &path() const { return path_; }
  /// The repository entry shown as selected; a version inside a package has
  /// one of its own.
  const std::string &selection_path() const { return selection_path_; }
  void set_path(std::string path) { path_ = std::move(path); }
  void set_selection_path(std::string path) {
    selection_path_ = std::move(path);
  }

  /// False while the document is a preview that the next one opened in its
  /// place replaces.
  bool pinned() const { return pinned_; }
  void set_pinned(bool pinned) { pinned_ = pinned; }

  /// Name the patch after its file, in both the contents and the clean copy,
  /// so taking the name from the file does not count as an edit.
  void set_file_identity(const std::filesystem::path &path);

  Snapshot capture_snapshot() const;
  /// Contents only; the paths are restored through the session.
  void restore_contents(const Snapshot &snapshot);

private:
  DocumentId id_;
  ym2612::Patch patch_;
  ym2612::Patch original_patch_;
  std::string path_;
  std::string selection_path_;
  bool pinned_ = true;
};

inline std::string patch_identity(std::string_view current_patch_path) {
  if (current_patch_path.empty()) {
    return "Untitled";
  }
  return std::filesystem::path(current_patch_path).stem().string();
}

/// What a document is called on its tab: the patch's name, which is its
/// file's own for a single-patch file and the instrument's inside a bank.
inline std::string document_label(const patches::PatchDocument &document) {
  if (!document.patch().name.empty()) {
    return document.patch().name;
  }
  return patch_identity(document.path());
}

} // namespace patches
