#include "decision_test_environment.hpp"
#include "patches/patch_entry_actions.hpp"

namespace {

patches::EntryMenuCapabilities all_actions() {
  return {.create_patch = true,
          .create_folder = true,
          .save_current = true,
          .download_folder = true,
          .download_current = true,
          .download_patch = true,
          .save_to_storage = true,
          .save_as = true,
          .duplicate = true,
          .rename = true,
          .delete_patch = true,
          .remove_folder = true,
          .reveal = true};
}

void test_current_and_other_patches() {
  decision_tests::Environment env("entry_actions");
  auto &session = env.session;
  const auto path = env.folder / "lead.gin";
  CHECK(patches::write_patch({}, path));
  auto entry = env.entry(path);
  auto &document = env.open(path);
  CHECK(session.activate(document.id()));
  auto available = all_actions();
  auto actions =
      patches::entry_menu_actions(session, entry, available, false, false);
  CHECK(actions.is_current);
  CHECK(!actions.create_patch && !actions.create_folder);
  CHECK(actions.save_current && !actions.save_enabled);
  CHECK(!actions.download_folder && actions.download_patch);
  CHECK(actions.save_to_storage && !actions.save_as);
  CHECK(!actions.duplicate);
  CHECK(actions.rate && actions.reveal);
  CHECK(actions.rename && actions.delete_patch);
  CHECK(!actions.remove_folder);

  document.patch().instrument.feedback = 7;
  available.save_to_storage = false;
  actions =
      patches::entry_menu_actions(session, entry, available, false, false);
  CHECK(actions.save_enabled && actions.save_current);
  CHECK(!actions.save_to_storage && actions.save_as);
  available.save_as = false;
  available.download_current = false;
  actions =
      patches::entry_menu_actions(session, entry, available, false, false);
  CHECK(!actions.save_to_storage && !actions.save_as);
  CHECK(!actions.download_patch);
  available.save_current = false;
  CHECK(!patches::entry_menu_actions(session, entry, available, false, false)
             .save_current);

  CHECK(session.activate(session.documents().front()->id()));
  actions =
      patches::entry_menu_actions(session, entry, all_actions(), false, false);
  CHECK(!actions.is_current);
  CHECK(!actions.save_current && !actions.save_as && !actions.save_to_storage);
  CHECK(actions.download_patch && actions.duplicate);
  available = all_actions();
  available.download_patch = false;
  available.duplicate = false;
  actions =
      patches::entry_menu_actions(session, entry, available, false, false);
  CHECK(!actions.download_patch && !actions.duplicate);

  const auto none =
      patches::entry_menu_actions(session, entry, {}, false, false);
  CHECK(!none.create_patch && !none.create_folder);
  CHECK(!none.save_current && !none.download_folder && !none.download_patch);
  CHECK(!none.save_to_storage && !none.save_as && !none.duplicate);
  CHECK(!none.rename && !none.delete_patch && !none.remove_folder);
  CHECK(!none.reveal);
  CHECK(none.rate);

  CHECK(!patches::entry_can_rename(session.repository(), entry, false, false));
  CHECK(!patches::entry_can_rename(session.repository(), entry, true, true));
  CHECK(!patches::entry_can_delete(session.repository(), entry, false));
}

void test_folders_and_containers() {
  decision_tests::Environment env("entry_folder_actions");
  auto &session = env.session;
  auto folder = env.entry(env.folder, true);
  auto available = all_actions();
  auto actions =
      patches::entry_menu_actions(session, folder, available, false, true);
  CHECK(!actions.is_current);
  CHECK(actions.create_patch && actions.create_folder);
  CHECK(actions.download_folder && !actions.download_patch);
  CHECK(!actions.save_current && !actions.save_to_storage && !actions.save_as);
  CHECK(!actions.duplicate && !actions.rate);
  CHECK(actions.remove_folder);
  CHECK(!actions.delete_patch);

  actions = patches::entry_menu_actions(session, folder, available, true, true);
  CHECK(!actions.remove_folder && !actions.rename);
  CHECK(actions.create_patch && actions.create_folder);
  CHECK(!patches::entry_menu_actions(session, folder, available, false, false)
             .remove_folder);
  available.create_patch = false;
  available.create_folder = false;
  available.download_folder = false;
  available.remove_folder = false;
  actions =
      patches::entry_menu_actions(session, folder, available, false, true);
  CHECK(!actions.create_patch && !actions.create_folder);
  CHECK(!actions.download_folder && !actions.remove_folder);

  auto bank = env.entry(env.folder / "bank.opm", true);
  bank.format = "OPM";
  actions =
      patches::entry_menu_actions(session, bank, all_actions(), false, false);
  CHECK(!actions.create_patch && !actions.create_folder);
  CHECK(actions.download_folder && !actions.download_patch);
  CHECK(!actions.duplicate && !actions.rate);

  auto instrument = env.entry(env.folder / "bank.opm");
  instrument.relative_path += "/instrument";
  instrument.source_relative_path = bank.relative_path;
  instrument.instrument_index = 1;
  auto &document = env.open(bank.full_path);
  document.set_selection_path(instrument.relative_path);
  CHECK(session.activate(document.id()));
  actions = patches::entry_menu_actions(session, instrument, all_actions(),
                                        false, false);
  CHECK(actions.is_current);
  CHECK(!actions.save_current && !actions.duplicate);
  CHECK(actions.download_patch && actions.save_to_storage);
  CHECK(!actions.rename && !actions.delete_patch);

  env.set_read_only();
  actions =
      patches::entry_menu_actions(session, folder, all_actions(), false, true);
  CHECK(!actions.create_patch && !actions.create_folder);
}

void test_read_only_and_outside() {
  decision_tests::Environment env("entry_read_only_actions");
  auto &session = env.session;
  for (const auto &path :
       {env.directories.paths().builtin_presets_root / "init.dmp",
        env.folder / "read.dmf", env.folder / "bank.opm"}) {
    auto entry = env.entry(path);
    auto &document = env.open(path);
    CHECK(session.activate(document.id()));
    const auto actions = patches::entry_menu_actions(
        session, entry, all_actions(), false, false);
    CHECK(actions.is_current);
    CHECK(!actions.save_current && !actions.duplicate);
    CHECK(actions.download_patch && actions.save_to_storage);
  }
  const auto outside = env.root / "outside.gin";
  auto entry = env.entry(outside);
  auto &document = env.open(outside);
  CHECK(session.activate(document.id()));
  auto actions =
      patches::entry_menu_actions(session, entry, all_actions(), false, false);
  CHECK(actions.save_current);
  CHECK(!actions.rename && !actions.delete_patch && !actions.rate);
  const auto writable = env.folder / "lead.gin";
  entry = env.entry(writable);
  auto &inside = env.open(writable);
  CHECK(session.activate(inside.id()));
  env.set_read_only();
  actions =
      patches::entry_menu_actions(session, entry, all_actions(), false, false);
  CHECK(actions.is_current && !actions.save_current);
}

void test_read_only_workspace() {
  decision_tests::Environment env("entry_read_only_workspace", true);
  auto &session = env.session;
  const auto path = env.folder / "lead.gin";
  CHECK(patches::write_patch({}, path));
  const auto entry = env.entry(path);
  auto &document = env.open(path);
  CHECK(session.activate(document.id()));
  document.patch().instrument.feedback = 3;
  auto actions =
      patches::entry_menu_actions(session, entry, all_actions(), false, false);
  CHECK(actions.is_current && !actions.save_current);
  CHECK(!actions.rename && !actions.delete_patch && !actions.rate);
  CHECK(actions.download_patch && actions.save_to_storage);
  const auto folder = env.entry(env.folder, true);
  actions =
      patches::entry_menu_actions(session, folder, all_actions(), false, true);
  CHECK(!actions.create_patch && !actions.create_folder);
  CHECK(actions.download_folder && actions.remove_folder);
}

} // namespace

int main() {
  test_current_and_other_patches();
  test_folders_and_containers();
  test_read_only_and_outside();
  test_read_only_workspace();
}
