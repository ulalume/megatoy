#include "main_menu.hpp"

#include "gui/styles/megatoy_style.hpp"
#include "gui/window_title.hpp"
#include "project_info.hpp"
#include "update/update_checker.hpp"
#include <atomic>
#include <cstdlib>
#include <format>
#include <imgui.h>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace ui {
namespace {

enum class UpdateStatus {
  Idle,
  Checking,
  UpdateAvailable,
  UpToDate,
  Error,
};

struct UpdateCheckRequest {
  std::atomic<bool> completed{false};
  update::UpdateCheckResult result;
};

struct AboutModalState {
  UpdateStatus status = UpdateStatus::Idle;
  std::string latest_version;
  std::string release_url;
  std::string error_message;
  std::shared_ptr<UpdateCheckRequest> pending_request;

  void reset() {
    status = UpdateStatus::Idle;
    latest_version.clear();
    release_url.clear();
    error_message.clear();
    pending_request.reset();
  }
};

AboutModalState &about_modal_state() {
  static AboutModalState state;
  return state;
}

void open_external_url(const std::string &url) {
#if defined(_WIN32)
  std::string command = "start \"\" \"" + url + "\"";
#elif defined(__APPLE__)
  std::string command = "open \"" + url + "\"";
#elif defined(__linux__)
  std::string command = "xdg-open \"" + url + "\"";
#else
  std::string command;
#endif
  if (!command.empty()) {
    std::system(command.c_str());
  }
}

void poll_update_request(AboutModalState &state) {
  if (!state.pending_request) {
    return;
  }
  auto &request = state.pending_request;
  if (!request->completed.load(std::memory_order_acquire)) {
    return;
  }

  const auto &result = request->result;
  if (!result.success) {
    state.status = UpdateStatus::Error;
    state.error_message = result.error_message;
    state.latest_version.clear();
    state.release_url.clear();
  } else if (result.update_available) {
    state.status = UpdateStatus::UpdateAvailable;
    state.latest_version = result.latest_version;
    state.release_url = result.release_url;
    state.error_message.clear();
  } else {
    state.status = UpdateStatus::UpToDate;
    state.error_message.clear();
    state.release_url = result.release_url;
    state.latest_version.clear();
  }

  state.pending_request.reset();
}

void start_update_check(AboutModalState &state) {
  auto request = std::make_shared<UpdateCheckRequest>();
  state.status = UpdateStatus::Checking;
  state.latest_version.clear();
  state.release_url.clear();
  state.error_message.clear();
  state.pending_request = request;

  std::thread([weak_request = std::weak_ptr<UpdateCheckRequest>(request)]() {
    if (auto shared = weak_request.lock()) {
      shared->result = update::check_for_updates(megatoy::kVersionTag);
      shared->completed.store(true, std::memory_order_release);
    }
  }).detach();
}

void render_update_status(const AboutModalState &state) {
  switch (state.status) {
  case UpdateStatus::Idle:
    break;
  case UpdateStatus::Checking:
    ImGui::SameLine();
    ImGui::TextUnformatted("Checking...");
    break;
  case UpdateStatus::UpdateAvailable:
    ImGui::Spacing();
    if (!state.release_url.empty()) {
      auto label = std::format("New version available: {}",
                               state.latest_version.c_str());
      if (ImGui::TextLink(label.c_str())) {
        open_external_url(state.release_url);
      }
    }
    break;
  case UpdateStatus::UpToDate:
    ImGui::Spacing();
    ImGui::Text("There are currently no updates available.");
    break;
  case UpdateStatus::Error:
    ImGui::Spacing();
    ImGui::TextColored(styles::color(styles::MegatoyCol::StatusError),
                       "Update check failed: %s", state.error_message.c_str());
    break;
  }
}

void render_about_modal() {
  auto &state = about_modal_state();
  poll_update_request(state);
  ImGui::SetNextWindowSize(ImVec2(350, -1));
  if (!ImGui::BeginPopupModal("About megatoy", nullptr,
                              ImGuiWindowFlags_AlwaysAutoResize)) {
    if (!ImGui::IsPopupOpen("About megatoy")) {
      state.reset();
    }
    return;
  }

  if (ImGui::IsWindowAppearing()) {
    state.reset();
  }

  ImGui::Text("Version: %s", megatoy::kVersionTag);

  ImGui::Spacing();
  bool is_checking = state.status == UpdateStatus::Checking;
  ImGui::BeginDisabled(is_checking);
  if (ImGui::Button("Check for updates")) {
    start_update_check(state);
  }
  ImGui::EndDisabled();

  render_update_status(state);

  ImGui::Separator();
  if (ImGui::Button("OK", ImVec2(120.0f, 0.0f))) {
    state.reset();
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}

} // namespace

void render_main_menu(MainMenuContext &context) {
  bool open_about = false;
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("megatoy")) {
      if (ImGui::MenuItem("About megatoy")) {
        open_about = true;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Quit")) {
        context.gui.set_should_close(true);
      }
      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
      auto &history = context.history;
      const ImGuiIO &io = ImGui::GetIO();
      const bool mac_behavior = io.ConfigMacOSXBehaviors;
      const char *undo_shortcut = mac_behavior ? "Cmd+Z" : "Ctrl+Z";
      const char *redo_shortcut = mac_behavior ? "Cmd+Shift+Z" : "Ctrl+Shift+Z";

      std::string undo_label = "Undo";
      if (history.can_undo()) {
        std::string_view change = history.undo_label();
        if (!change.empty()) {
          undo_label.append(" ");
          undo_label.append(change);
        }
      }

      if (ImGui::MenuItem(undo_label.c_str(), undo_shortcut, false,
                          history.can_undo())) {
        if (context.undo) {
          context.undo();
        }
      }

      std::string redo_label = "Redo";
      if (history.can_redo()) {
        std::string_view change = history.redo_label();
        if (!change.empty()) {
          redo_label.append(" ");
          redo_label.append(change);
        }
      }

      if (ImGui::MenuItem(redo_label.c_str(), redo_shortcut, false,
                          history.can_redo())) {
        if (context.redo) {
          context.redo();
        }
      }

      ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
      auto &ui_prefs = context.ui_prefs;

      bool fullscreen = context.gui.is_fullscreen();
      if (ImGui::MenuItem("Fullscreen", nullptr, fullscreen)) {
        context.gui.set_fullscreen(!fullscreen);
      }

      ImGui::Separator();

      ImGui::MenuItem(PATCH_EDITOR_TITLE, nullptr, &ui_prefs.show_patch_editor);
      ImGui::MenuItem(SOFT_KEYBOARD_TITLE, nullptr,
                      &ui_prefs.show_midi_keyboard);
      ImGui::MenuItem(PATCH_BROWSER_TITLE, nullptr,
                      &ui_prefs.show_patch_selector);
      ImGui::MenuItem(PATCH_LAB_TITLE, nullptr, &ui_prefs.show_patch_lab);
      ImGui::MenuItem(WAVEFORM_TITLE, nullptr, &ui_prefs.show_waveform);
      ImGui::MenuItem(MML_CONSOLE_TITLE, nullptr, &ui_prefs.show_mml_console);
      ImGui::MenuItem(PREFERENCES_TITLE, nullptr, &ui_prefs.show_preferences);

      ImGui::Separator();

      // Reset buttons
      if (ImGui::MenuItem("Reset to Default View")) {
        context.preferences.reset_ui_preferences();
        context.ui_prefs = context.preferences.ui_preferences();
        context.open_directory_dialog = false;
        context.gui.reset_layout();
        context.gui.set_theme(ui::styles::ThemeId::MegatoyDark);
      }
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }

  if (open_about) {
    ImGui::OpenPopup("About megatoy");
  }

  render_about_modal();
}

} // namespace ui
