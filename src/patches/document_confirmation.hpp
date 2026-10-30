#pragma once

#include "patch_document.hpp"
#include <cstddef>
#include <string>

namespace patches {

class PatchSession;

struct ConfirmationState {
  bool show_unsaved_changes_dialog = false;
  std::string dialog_message;
  enum class Operation {
    Close,
    Exit,
  } operation = Operation::Exit;
  /// The document a Close asks about, or the one an Exit can save.
  patches::DocumentId pending_document = 0;
  /// An Exit with one changed document that saves to its own file.
  bool exit_can_save = false;

  static ConfirmationState close(patches::DocumentId document,
                                 const std::string &name) {
    ConfirmationState state;
    state.show_unsaved_changes_dialog = true;
    state.pending_document = document;
    state.dialog_message = "Save changes to \"" + name + "\" before closing?";
    state.operation = Operation::Close;
    return state;
  }
  static ConfirmationState exit(std::size_t modified_documents = 1) {
    ConfirmationState state;
    state.show_unsaved_changes_dialog = true;
    state.dialog_message =
        modified_documents > 1
            ? std::to_string(modified_documents) +
                  " patches have unsaved changes. Do you want to discard "
                  "them and exit?"
            : "You have unsaved changes. Do you want to discard them and "
              "exit?";
    state.operation = Operation::Exit;
    return state;
  }
  /// An Exit whose only changed document can be saved where it is.
  static ConfirmationState exit_saving(patches::DocumentId document,
                                       const std::string &name) {
    ConfirmationState state = exit();
    state.pending_document = document;
    state.exit_can_save = true;
    state.dialog_message = "Save changes to \"" + name + "\" before exiting?";
    return state;
  }
};

enum class CloseDocumentAction { Ignore, Close, Ask };

struct CloseDocumentDecision {
  CloseDocumentAction action = CloseDocumentAction::Ignore;
  ConfirmationState confirmation;
};

bool confirmation_offers_save(const ConfirmationState &state);
ConfirmationState exit_confirmation(const PatchSession &session);
CloseDocumentDecision close_document_decision(const PatchDocument *document);

} // namespace patches
