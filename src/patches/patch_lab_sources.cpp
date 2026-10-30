#include "patch_lab_sources.hpp"

#include "patches/patch_display.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace ui {
namespace {

void collect_entries(const patches::PatchRepository &repository,
                     const std::vector<patches::PatchEntry> &tree,
                     std::vector<EntryDisplay> &out) {
  for (const auto &entry : tree) {
    if (entry.is_directory) {
      collect_entries(repository, entry.children, out);
    } else {
      EntryDisplay display;
      display.entry = &entry;
      display.label = patch_lab_entry_label(repository, entry);
      out.push_back(std::move(display));
    }
  }
}

bool matches_filter(const std::string &label, const std::string &filter) {
  if (filter.empty()) {
    return true;
  }
  const auto same = [](unsigned char a, unsigned char b) {
    return std::tolower(a) == std::tolower(b);
  };
  const auto it = std::search(label.begin(), label.end(), filter.begin(),
                              filter.end(), same);
  return it != label.end();
}

} // namespace

std::string patch_lab_entry_label(const patches::PatchRepository &repository,
                                  const patches::PatchEntry &entry) {
  if (!entry.relative_path.empty()) {
    return patches::display_preset_path(
        repository.display_path(entry.relative_path));
  }
  return entry.name.empty() ? "(unnamed patch)" : entry.name;
}

std::vector<EntryDisplay>
build_entry_list(const patches::PatchRepository &repository) {
  std::vector<EntryDisplay> entries;
  collect_entries(repository, repository.tree(), entries);
  std::sort(entries.begin(), entries.end(),
            [](const EntryDisplay &lhs, const EntryDisplay &rhs) {
              return lhs.label < rhs.label;
            });
  return entries;
}

std::vector<PatchLabSourceRow>
patch_lab_sources(const patches::PatchSession &session,
                  const std::vector<EntryDisplay> &entries,
                  const std::string &filter) {
  std::vector<PatchLabSourceRow> sources;
  for (const auto &document : session.documents()) {
    auto label = patches::document_label(*document);
    if (matches_filter(label, filter)) {
      if (sources.empty()) {
        sources.push_back({SourceRowKind::Heading, "Open"});
      }
      sources.push_back(
          {SourceRowKind::Document, std::move(label), document.get()});
    }
  }
  if (!sources.empty()) {
    sources.push_back({SourceRowKind::Heading, "Patches"});
  }
  for (const auto &item : entries) {
    if (matches_filter(item.label, filter)) {
      sources.push_back({SourceRowKind::Entry, {}, nullptr, &item});
    }
  }
  return sources;
}

} // namespace ui
