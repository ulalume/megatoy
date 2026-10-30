#include "ui_renderer.hpp"
#include "platform/platform_config.hpp"

#include "app_services.hpp"
#include "gui/components/folder_scan_dialog.hpp"
#include "gui/components/status_toasts.hpp"
#include "gui/components/view_patch_drop.hpp"
#include "gui/save_export_actions.hpp"
#include "gui/web_storage_dialogs.hpp"
#include "gui/window_title.hpp"
#include "midi/midi_input_manager.hpp"
#include "patch_actions.hpp"
#include "ui_document_flows.hpp"
#include "ui_file_requests.hpp"
#include "ui_frame_contexts.hpp"
#include <cassert>
#include <chrono>
#include <imgui.h>
#include <utility>

namespace ui {
namespace {

PatchLabState &patch_lab_state() {
  static PatchLabState state;
  return state;
}

void render_save_export_popup_host(AppContext &ctx) {
  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar;
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Pos);
  ImGui::SetNextWindowSize(ImVec2(0, 0));
  if (ImGui::Begin("##save_export_popup_host", nullptr, flags)) {
    render_save_export_popups(ctx.services.patch_session,
                              ctx.app_state().ui_state().save_export_state);
  }
  ImGui::End();
}

} // namespace

using namespace renderer_detail;

void render_all(AppContext &ctx) {
  static FrameContexts contexts(ctx);
  // The cache is only valid for the AppContext it captured.
  assert(contexts.owner == &ctx);

  prune_closed_documents(ctx);
  ctx.services.patch_session.pin_modified_documents();

  static auto next_workspace_refresh = std::chrono::steady_clock::time_point{};
  const auto now = std::chrono::steady_clock::now();
  const bool periodic_check = now >= next_workspace_refresh;
  if (periodic_check) {
    next_workspace_refresh = now + std::chrono::seconds(2);
    if (ctx.services.preference_manager.refresh_workspace_availability()) {
      ctx.services.patch_session.sync_workspace();
    }
  }

  follow_outside_changes(ctx, periodic_check);

  // Per-frame values; everything else in the contexts is stable references.
  contexts.patch_selector.workspace_is_empty =
      ctx.services.preference_manager.workspace().empty();
  {
    MidiInputManager::StatusInfo status =
        ctx.midi ? ctx.midi->status()
                 : MidiInputManager::StatusInfo{
                       .message = "MIDI backend unavailable."};
    contexts.preferences.midi_status_message = std::move(status.message);
    contexts.preferences.show_web_midi_button = status.show_enable_button;
    contexts.preferences.web_midi_button_disabled =
        status.enable_button_disabled;
  }

  render_main_menu(contexts.main_menu);
  render_patch_drop_feedback(contexts.patch_drop);
  render_confirmation_dialog(contexts.confirmation);
  render_patch_duplicate_dialog(ctx);
#if defined(MEGATOY_PLATFORM_WEB)
  render_folder_import_ui();
  render_folder_delete_ui();
  render_awaited_flush_ui();
#else
  render_folder_scan_dialog();
#endif
  render_patch_editors(contexts.patch_editor,
                       ctx.app_state().ui_state().save_export_state);
  render_patch_selector(PATCH_BROWSER_TITLE, contexts.patch_selector);
  render_preferences_window(PREFERENCES_TITLE, contexts.preferences);
  render_midi_keyboard(SOFT_KEYBOARD_TITLE, contexts.midi_keyboard);
  render_mml_console(MML_CONSOLE_TITLE, contexts.mml_console);
  render_patch_lab(PATCH_LAB_TITLE, contexts.patch_lab, patch_lab_state());
  render_waveform(WAVEFORM_TITLE, contexts.waveform);
  if (const auto drop = take_view_patch_drop()) {
    if (const auto *entry =
            ctx.services.patch_session.find_entry(drop->entry_path)) {
      patch_actions::open_entry_in_dock(ctx, *entry, drop->dock_id);
    }
  }

  render_save_export_popup_host(ctx);
  render_status_toasts();

  handle_close_shortcut(ctx);
  handle_new_shortcut(ctx);
  auto &session = ctx.services.patch_session;
  session.apply_patch_to_audio_if_changed();
  session.remember_open_documents();
  const auto &active = session.active_document();
  ctx.services.gui_manager.set_window_title(
      app_window_title(document_label(active), active.is_modified()));
}

} // namespace ui
