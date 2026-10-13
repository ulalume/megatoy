#include "patch_document.hpp"

namespace patches {

void PatchDocument::set_file_identity(const std::filesystem::path &path) {
  const auto stem = path.stem().string();
  patch_.name = stem;
  original_patch_.name = stem;
}

PatchDocument::Snapshot PatchDocument::capture_snapshot() const {
  Snapshot snapshot;
  snapshot.original_patch = original_patch_;
  snapshot.patch = patch_;
  snapshot.path = path_;
  snapshot.selection_path = selection_path_;
  return snapshot;
}

void PatchDocument::restore_contents(const Snapshot &snapshot) {
  patch_ = snapshot.patch;
  original_patch_ = snapshot.original_patch;
}

} // namespace patches
