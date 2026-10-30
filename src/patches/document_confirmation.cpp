#include "document_confirmation.hpp"

#include "patch_session.hpp"

namespace patches {

bool confirmation_offers_save(const ConfirmationState &state) {
  return state.operation == ConfirmationState::Operation::Close ||
         state.exit_can_save;
}

ConfirmationState exit_confirmation(const PatchSession &session) {
  const auto modified = session.modified_document_count();
  if (modified == 0) {
    return {};
  }
  if (modified == 1) {
    for (const auto &document : session.documents()) {
      if (document->is_modified() && session.can_save_in_place(*document)) {
        return ConfirmationState::exit_saving(document->id(),
                                              document_label(*document));
      }
    }
  }
  return ConfirmationState::exit(modified);
}

CloseDocumentDecision close_document_decision(const PatchDocument *document) {
  if (document == nullptr) {
    return {};
  }
  if (!document->is_modified()) {
    return {CloseDocumentAction::Close, {}};
  }
  return {CloseDocumentAction::Ask,
          ConfirmationState::close(document->id(), document_label(*document))};
}

} // namespace patches
