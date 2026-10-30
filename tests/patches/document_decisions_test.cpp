#include "decision_test_environment.hpp"
#include "patches/document_confirmation.hpp"
#include "patches/save_controls.hpp"

#include <string>

namespace {

void check_document(decision_tests::Environment &env,
                    patches::PatchDocument &document, bool in_place,
                    const std::string &label) {
  auto &session = env.session;
  session.mark_all_clean();
  CHECK(session.can_save_in_place(document) == in_place);
  CHECK(session.is_user_patch(document) == in_place);
  CHECK(std::string(session.save_label_for(document, in_place)) == label);
  const auto clean = patches::save_controls(session, document);
  CHECK(clean.saves_in_place == in_place);
  CHECK(clean.show_save_as == in_place);
  CHECK(clean.save_enabled == !in_place);
  CHECK(std::string(clean.save_label) == label);
  if (in_place) {
    CHECK(clean.save_tooltip == "Patch is not modified");
  } else {
    CHECK(clean.save_tooltip == "Choose a filename and format");
  }
  CHECK(!patches::exit_confirmation(session).show_unsaved_changes_dialog);

  auto close = patches::close_document_decision(&document);
  CHECK(close.action == patches::CloseDocumentAction::Close);
  CHECK(!close.confirmation.show_unsaved_changes_dialog);

  document.patch().instrument.feedback ^= 1;
  CHECK(document.is_modified());
  const auto changed = patches::save_controls(session, document);
  CHECK(changed.saves_in_place == in_place);
  CHECK(changed.show_save_as == in_place);
  CHECK(changed.save_enabled);
  CHECK(std::string(changed.save_label) == label);
  if (in_place) {
    CHECK(changed.save_tooltip.empty());
  } else {
    CHECK(changed.save_tooltip == "Choose a filename and format");
  }

  const auto exit = patches::exit_confirmation(session);
  CHECK(exit.show_unsaved_changes_dialog);
  CHECK(exit.operation == patches::ConfirmationState::Operation::Exit);
  CHECK(exit.exit_can_save == in_place);
  CHECK(patches::confirmation_offers_save(exit) == in_place);
  CHECK(exit.pending_document == (in_place ? document.id() : 0));
  const auto name = patches::document_label(document);
  CHECK(exit.dialog_message ==
        (in_place ? "Save changes to \"" + name + "\" before exiting?"
                  : "You have unsaved changes. Do you want to discard them "
                    "and exit?"));

  close = patches::close_document_decision(&document);
  CHECK(close.action == patches::CloseDocumentAction::Ask);
  CHECK(close.confirmation.show_unsaved_changes_dialog);
  CHECK(patches::confirmation_offers_save(close.confirmation));
  CHECK(close.confirmation.operation ==
        patches::ConfirmationState::Operation::Close);
  CHECK(close.confirmation.pending_document == document.id());
  CHECK(close.confirmation.dialog_message ==
        "Save changes to \"" + name + "\" before closing?");
  CHECK(session.find_document(document.id()) == &document);
  CHECK(document.is_modified());
}

void test_document_flows() {
  decision_tests::Environment env("document_decisions");
  auto &session = env.session;
  CHECK(patches::close_document_decision(nullptr).action ==
        patches::CloseDocumentAction::Ignore);
  CHECK(!patches::exit_confirmation(session).show_unsaved_changes_dialog);
  CHECK(session.current_patch_path().empty());
  CHECK(!session.can_save_in_place());
  CHECK(!session.current_patch_is_user_patch());
  CHECK(std::string(session.save_label_for(false)) == "Save As...");

  auto &untitled = env.open({});
  CHECK(patches::document_label(untitled) == "Untitled");
  check_document(env, untitled, false, "Save As...");
  auto &writable = env.open(env.folder / "lead.gin");
  CHECK(patches::document_label(writable) == "lead");
  check_document(env, writable, true, "Overwrite");
  // The only changed document need not be the active one.
  CHECK(session.active_document_id() != writable.id());
  CHECK(session.activate(writable.id()));
  CHECK(session.can_save_in_place());
  CHECK(session.current_patch_is_user_patch());
  CHECK(std::string(session.save_label_for(true)) == "Overwrite");

  auto &package = env.open(env.folder / "versions.ginpkg");
  check_document(env, package, true, "Save Version");
  auto &upper_package = env.open(env.folder / "VERSIONS.GINPKG");
  check_document(env, upper_package, true, "Save Version");
  CHECK(package.selection_path().ends_with("/latest"));
  CHECK(upper_package.selection_path().ends_with("/latest"));
  auto &preset =
      env.open(env.directories.paths().builtin_presets_root / "init.dmp");
  check_document(env, preset, false, "Save As...");
  for (const auto *filename :
       {"read.dmf", "read.fur", "read.rym2612", "bank.opm", "unknown.xyz"}) {
    auto &document = env.open(env.folder / filename);
    check_document(env, document, false, "Save As...");
  }
  auto &bank = env.open(env.folder / "bank.opm");
  bank.set_selection_path("bank.opm/instrument");
  bank.patch().name = "Bank instrument";
  bank.mark_as_clean();
  CHECK(patches::document_label(bank) == "Bank instrument");
  check_document(env, bank, false, "Save As...");

  auto &outside = env.open(env.root / "outside.gin");
  check_document(env, outside, true, "Overwrite");
  env.set_read_only();
  check_document(env, writable, false, "Save As...");
  check_document(env, package, false, "Save As...");
  check_document(env, outside, true, "Overwrite");

  writable.patch().instrument.feedback = 6;
  const auto several = patches::exit_confirmation(session);
  CHECK(several.show_unsaved_changes_dialog);
  CHECK(!several.exit_can_save);
  CHECK(several.pending_document == 0);
  CHECK(several.dialog_message ==
        "2 patches have unsaved changes. Do you want to discard them and "
        "exit?");
  bank.patch().instrument.feedback = 5;
  CHECK(patches::exit_confirmation(session).dialog_message ==
        "3 patches have unsaved changes. Do you want to discard them and "
        "exit?");
  session.mark_all_clean();
  CHECK(!patches::exit_confirmation(session).show_unsaved_changes_dialog);
}

void test_labels() {
  patches::PatchDocument document(42);
  CHECK(patches::document_label(document) == "Untitled");
  document.set_path("folder/fallback.dmp");
  CHECK(patches::document_label(document) == "fallback");
  document.patch().name = "Name \"with quotes\"";
  CHECK(
      patches::close_document_decision(&document).confirmation.dialog_message ==
      "Save changes to \"Name \"with quotes\"\" before closing?");
}

} // namespace

int main() {
  test_document_flows();
  test_labels();
}
