#include "../patches/decision_test_environment.hpp"
#include "patches/patch_display.hpp"
#include "patches/patch_lab_sources.hpp"

#include <algorithm>

namespace {

void test_source_list() {
  decision_tests::Environment env("lab_sources");
  auto &session = env.session;
  session.repository().set_show_builtin_presets(false);
  CHECK(ui::build_entry_list(session.repository()).empty());
  CHECK(patches::write_patch({}, env.folder / "zebra.gin"));
  CHECK(patches::write_patch({}, env.folder / "Alpha.gin"));
  std::filesystem::create_directories(env.folder / "sub");
  CHECK(patches::write_patch({}, env.folder / "sub" / "deep.gin"));
  session.repository().refresh();
  const auto entries = ui::build_entry_list(session.repository());
  CHECK(entries.size() == 3);
  CHECK(entries[0].label == "patches/Alpha.gin");
  CHECK(entries[1].label == "patches/sub/deep.gin");
  CHECK(entries[2].label == "patches/zebra.gin");
  CHECK(std::all_of(entries.begin(), entries.end(), [](const auto &item) {
    return item.entry != nullptr && !item.entry->is_directory;
  }));

  auto &zebra = env.open(env.folder / "zebra.gin");
  auto &alpha = env.open(env.folder / "Alpha.gin");
  CHECK(session.activate(alpha.id()));
  const auto all = ui::patch_lab_sources(session, entries, "");
  CHECK(all.size() == 8);
  CHECK(all[0].kind == ui::SourceRowKind::Heading && all[0].text() == "Open");
  CHECK(all[1].kind == ui::SourceRowKind::Document);
  CHECK(all[1].text() == "Untitled");
  CHECK(all[2].document == &zebra && all[2].text() == "zebra");
  CHECK(all[3].document == &alpha && all[3].text() == "Alpha");
  CHECK(all[4].kind == ui::SourceRowKind::Heading);
  CHECK(all[4].text() == "Patches");
  CHECK(all[5].kind == ui::SourceRowKind::Entry);
  CHECK(all[5].entry->entry == entries[0].entry);
  CHECK(all[7].entry->entry == entries[2].entry);

  const auto filtered = ui::patch_lab_sources(session, entries, "aLPHa");
  CHECK(filtered.size() == 4);
  CHECK(filtered[0].kind == ui::SourceRowKind::Heading);
  CHECK(filtered[1].document == &alpha);
  CHECK(filtered[2].kind == ui::SourceRowKind::Heading);
  CHECK(filtered[3].entry->entry == entries[0].entry);
  const auto path = ui::patch_lab_sources(session, entries, "SUB/");
  CHECK(path.size() == 1);
  CHECK(path[0].kind == ui::SourceRowKind::Entry);
  CHECK(path[0].entry->entry == entries[1].entry);
  const auto open_only = ui::patch_lab_sources(session, entries, "Untitled");
  CHECK(open_only.size() == 3);
  CHECK(open_only[0].label == "Open");
  CHECK(open_only[1].label == "Untitled");
  CHECK(open_only[2].label == "Patches");
  const auto none = ui::patch_lab_sources(session, entries, "missing");
  CHECK(none.empty());
  const auto whitespace = ui::patch_lab_sources(session, entries, " Alpha ");
  CHECK(whitespace.empty());
  CHECK(session.close_document(zebra.id()));
  const auto closed = ui::patch_lab_sources(session, entries, "zebra");
  CHECK(closed.size() == 1);
  CHECK(closed[0].kind == ui::SourceRowKind::Entry);
}

void test_source_labels() {
  decision_tests::Environment env("lab_source_labels");
  const auto &repository = env.session.repository();
  patches::PatchEntry entry;
  CHECK(ui::patch_lab_entry_label(repository, entry) == "(unnamed patch)");
  entry.name = "Name only";
  CHECK(ui::patch_lab_entry_label(repository, entry) == "Name only");
  entry.relative_path = "presets/lead.dmp";
  CHECK(ui::patch_lab_entry_label(repository, entry) ==
        "Built-in Presets/lead.dmp");
  entry.relative_path = "presets_extra/lead.dmp";
  CHECK(ui::patch_lab_entry_label(repository, entry) ==
        "presets_extra/lead.dmp");
  CHECK(patches::display_preset_path(std::string("presets")) ==
        "Built-in Presets");
  CHECK(patches::display_preset_path(std::filesystem::path("presets/x")) ==
        "Built-in Presets/x");

  auto &document = env.open({});
  document.patch().name = "Instrument name";
  auto sources = ui::patch_lab_sources(env.session, {}, "Instrument");
  CHECK(sources.size() == 3);
  CHECK(sources[1].kind == ui::SourceRowKind::Document);
  CHECK(sources[1].label == "Instrument name");
  CHECK(sources[1].document == &document);
  document.patch().name.clear();
  document.set_path("folder/fallback.gin");
  sources = ui::patch_lab_sources(env.session, {}, "fallback");
  CHECK(sources.size() == 3);
  CHECK(sources[1].label == "fallback");
}

} // namespace

int main() {
  test_source_list();
  test_source_labels();
}
