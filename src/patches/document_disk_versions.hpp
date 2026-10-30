#pragma once

#include "patch_document.hpp"
#include "patch_repository.hpp"
#include <filesystem>
#include <map>
#include <memory>
#include <vector>

namespace patches {

/// Last observed writes and file contents for the open documents.
class DocumentDiskVersions {
public:
  using Documents = std::vector<std::unique_ptr<PatchDocument>>;

  struct DiskVersion {
    DocumentId document = 0;
    ym2612::Patch patch;
  };

  explicit DocumentDiskVersions(PatchRepository &repository)
      : repository_(repository) {}

  void remember(const PatchDocument &document);
  /// Report each write once, excluding the document's saved contents.
  std::vector<DiskVersion> take(const Documents &documents);
  bool outside_file_changed(const Documents &documents) const;

private:
  PatchRepository &repository_;
  std::map<DocumentId, std::filesystem::file_time_type> disk_times_;
};

} // namespace patches
