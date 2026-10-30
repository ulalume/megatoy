#pragma once

#include "patches/patch_session.hpp"

#include <string>
#include <vector>

namespace ui {

struct EntryDisplay {
  const patches::PatchEntry *entry = nullptr;
  std::string label;
};

enum class SourceRowKind { Heading, Document, Entry };

/// One line of the list, built each frame while it is open. An entry's row
/// points at its label in the list it came from, which outlives the row.
struct PatchLabSourceRow {
  SourceRowKind kind;
  std::string label;
  const patches::PatchDocument *document = nullptr;
  const EntryDisplay *entry = nullptr;

  const std::string &text() const { return entry ? entry->label : label; }
};

std::string patch_lab_entry_label(const patches::PatchRepository &repository,
                                  const patches::PatchEntry &entry);
std::vector<EntryDisplay>
build_entry_list(const patches::PatchRepository &repository);
std::vector<PatchLabSourceRow>
patch_lab_sources(const patches::PatchSession &session,
                  const std::vector<EntryDisplay> &entries,
                  const std::string &filter);

} // namespace ui
