#include "about_dialog.hpp"

#include "gui/styles/megatoy_style.hpp"
#include "project_info.hpp"
#include "system/open_default_browser.hpp"
#include "update/update_checker.hpp"
#include <atomic>
#include <imgui.h>
#include <memory>
#include <sstream>
#include <string>
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

bool open_external_url(const std::string &url) {
  auto result = megatoy::system::open_default_browser(url);
  return result.success;
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
      std::ostringstream oss;
      oss << "New version available: " << state.latest_version;
      std::string label = oss.str();
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

} // namespace

void render_about_dialog() {
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

void open_about_dialog() { ImGui::OpenPopup("About megatoy"); }

} // namespace ui
