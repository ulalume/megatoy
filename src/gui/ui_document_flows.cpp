#include "ui_document_flows.hpp"

#include "app_services.hpp"
#include "gui/components/patch_editor.hpp"
#include "gui/save_export_actions.hpp"
#include "gui/window_title.hpp"
#include "patch_actions.hpp"
#include "platform/platform_config.hpp"
#include <imgui_internal.h>
#include <string_view>
#include <utility>

namespace ui::renderer_detail {

void close_document_now(AppContext &ctx, patches::DocumentId id) {
  auto &session = ctx.services.patch_session;
  if (session.find_document(id) == nullptr) {
    return;
  }
  const bool was_active = id == session.active_document_id();
  if (was_active) {
    ctx.services.history().cancel_transaction();
  }
  session.close_document(id);
  if (was_active) {
    patch_actions::reveal_document(ctx, session.active_document_id());
  }
}

/// Close a document, asking first when it has unsaved changes.
void request_close_document(AppContext &ctx, patches::DocumentId id) {
  if (!ctx.services.patch_session.can_close_document(id)) {
    return;
  }
  const auto decision = patches::close_document_decision(
      ctx.services.patch_session.find_document(id));
  switch (decision.action) {
  case patches::CloseDocumentAction::Ignore:
    return;
  case patches::CloseDocumentAction::Close:
    close_document_now(ctx, id);
    return;
  case patches::CloseDocumentAction::Ask:
    ctx.ui_state().confirmation_state = decision.confirmation;
    return;
  }
}

/**
 * Save a document and close it. One that has to be saved through Save As is
 * activated and the dialog started instead; it stays open, to be closed
 * again once saved.
 */
void save_and_close_document(AppContext &ctx, patches::DocumentId id) {
  auto &session = ctx.services.patch_session;
  if (session.find_document(id) == nullptr) {
    return;
  }
  patch_actions::activate_document(ctx, id);
  patch_actions::reveal_document(ctx, id);
  auto &save_state = ctx.ui_state().save_export_state;
  if (!session.can_save_in_place()) {
    request_save_as(save_state);
    return;
  }
  trigger_save(session, save_state);
  const auto *document = session.find_document(id);
  if (document != nullptr && !document->is_modified()) {
    close_document_now(ctx, id);
  }
}

/// Save the one changed document in place and exit once it is clean; a save
/// that does not finish, such as one waiting on an overwrite question,
/// leaves the app open.
void save_and_exit(AppContext &ctx, patches::DocumentId id) {
  auto &session = ctx.services.patch_session;
  if (session.find_document(id) == nullptr) {
    return;
  }
  patch_actions::activate_document(ctx, id);
  trigger_save(session, ctx.ui_state().save_export_state);
  if (session.modified_document_count() == 0) {
    ctx.services.gui_manager.set_should_close(true);
  }
}

/**
 * Pick up what changed outside megatoy: the browser's tree, and the open
 * documents whose file was written, in the workspace or, when
 * `check_outside_files`, beyond it. One without unsaved changes shows the
 * file's new contents; one with them keeps its own and asks.
 */
void follow_outside_changes(AppContext &ctx, bool check_outside_files) {
  auto &session = ctx.services.patch_session;
  auto &repository = session.repository();
  const bool outside = repository.take_outside_change();
  if (outside || repository.has_directory_changed()) {
    repository.refresh();
  } else if (!check_outside_files || !session.outside_file_changed()) {
    return;
  }
  for (auto &version : session.take_disk_versions()) {
    const auto *document = session.find_document(version.document);
    if (document == nullptr) {
      continue;
    }
    if (document->is_modified()) {
      ctx.ui_state().document_ui(version.document).disk_version =
          std::move(version.patch);
    } else {
      patch_actions::reload_from_disk(ctx, version.document, version.patch);
    }
  }
}

namespace {

/// The View menu's switch for a panel the keyboard can close.
bool *panel_switch(UIPreferences &prefs, std::string_view name) {
  if (name == PATCH_LAB_TITLE) {
    return &prefs.show_patch_lab;
  }
  if (name == SOFT_KEYBOARD_TITLE) {
    return &prefs.show_midi_keyboard;
  }
  if (name == MML_CONSOLE_TITLE) {
    return &prefs.show_mml_console;
  }
  if (name == WAVEFORM_TITLE) {
    return &prefs.show_waveform;
  }
  if (name == PREFERENCES_TITLE) {
    return &prefs.show_preferences;
  }
  return nullptr;
}

} // namespace

/// Cmd/Ctrl+W closes the panel that has the keyboard. From an editor window
/// or the browser it closes the active document, the one a click in the
/// browser opens. The browser keeps the key for its own tab.
void handle_close_shortcut(AppContext &ctx) {
#if defined(MEGATOY_PLATFORM_WEB)
  (void)ctx;
#else
  const ImGuiIO &io = ImGui::GetIO();
  if (io.WantTextInput || !(io.KeyCtrl || io.KeySuper) || io.KeyShift ||
      io.KeyAlt || !ImGui::IsKeyPressed(ImGuiKey_W, false)) {
    return;
  }
  const ImGuiWindow *focused = GImGui->NavWindow;
  if (focused == nullptr) {
    return;
  }
  const std::string_view name = window_id(focused->RootWindow->Name);
  if (bool *shown = panel_switch(ctx.ui_state().prefs, name)) {
    *shown = false;
    return;
  }
  if (name == PATCH_BROWSER_TITLE || patch_editor_has_focus()) {
    request_close_document(ctx,
                           ctx.services.patch_session.active_document_id());
  }
#endif
}

/// Cmd/Ctrl+N opens an untitled patch. The browser keeps the key for a new
/// window.
void handle_new_shortcut(AppContext &ctx) {
#if defined(MEGATOY_PLATFORM_WEB)
  (void)ctx;
#else
  const ImGuiIO &io = ImGui::GetIO();
  if (io.WantTextInput || !(io.KeyCtrl || io.KeySuper) || io.KeyShift ||
      io.KeyAlt || !ImGui::IsKeyPressed(ImGuiKey_N, false)) {
    return;
  }
  patch_actions::new_patch(ctx);
#endif
}

/// Drop what closed documents kept: their undo history and editor state.
void prune_closed_documents(AppContext &ctx) {
  const auto &session = ctx.services.patch_session;
  ctx.services.prune_histories();
  ctx.ui_state().prune_documents([&session](patches::DocumentId id) {
    return session.find_document(id) != nullptr;
  });
}

} // namespace ui::renderer_detail
