#include "about_dialog.hpp"

#include "changelog_dialog.hpp"
#include "common.hpp"
#include "modal.hpp"

#include "gui/image/app_icon.hpp"
#include "gui/image/resource_manager.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "gui/ui_scale.hpp"
#include "platform/platform_config.hpp"
#include "project_info.hpp"
#include "system/open_default_browser.hpp"
#include "update/release_provider.hpp"
#include "update/update_checker.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include <emscripten.h>
#endif
#include <algorithm>
#include <cctype>
#include <chrono>
#include <future>
#include <imgui.h>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace ui {
namespace {

enum class UpdateStatus {
  Idle,
  Checking,
  UpdateAvailable,
  UpToDate,
  Error,
};

struct License {
  const char *name;
  const char *license;
  /// Under licenses/, or nullptr when there is no text to show.
  const char *file;
  /// Shown in place of the text when there is none.
  const char *note = nullptr;
};

constexpr License kLicenses[] = {
    {"ymfm", "BSD-3-Clause", "ymfm-BSD-3-Clause.txt"},
    {"Nuked-OPN2", "LGPL-2.1+", "nuked-opn2-LGPL-2.1.txt"},
    {"libvgm resampler", "-", nullptr,
     "megatoy carries a copy of libvgm's emu/Resampler and the headers it "
     "needs. The upstream repository declares no top-level license, and these "
     "files carry no license header.\n\nhttps://github.com/ValleyBell/libvgm"},
    {"Dear ImGui", "MIT", "imgui-MIT.txt"},
    {"SDL3", "zlib", "sdl3-zlib.txt"},
    {"nativefiledialog-extended", "zlib", "nativefiledialog-extended-zlib.txt"},
    {"RtMidi", "MIT", "rtmidi-MIT.txt"},
    {"nlohmann/json", "MIT", "nlohmann-json-MIT.txt"},
    {"stb", "MIT / PD", "stb-license.txt"},
    {"kissfft", "BSD-3-Clause", "kissfft-BSD-3-Clause.txt"},
    {"efsw", "MIT", "efsw-MIT.txt"},
    {"miniz", "MIT", "miniz-MIT.txt"},
    {"IconFontCppHeaders", "zlib", "iconfontcppheaders-zlib.txt"},
    {"Font Awesome Free", "CC BY / OFL / MIT", "font-awesome-license.txt"},
    {"chord_detector", "MIT", "chord-detector-MIT.txt"},
    {"ym2612_format", "MIT", "ym2612-format-MIT.txt"},
    {"ym2612_eg", "MIT", "ym2612-eg-MIT.txt"},
    {"Built-in presets", "CC0 1.0", "presets-CC0.txt"},
};

/**
 * A license file with its hard line breaks taken out where they only wrapped
 * a long line, so it can be wrapped to the pane instead. A break stays after
 * a short line, before an indented or bulleted one, and around a rule.
 */
std::string unwrap(std::string_view text) {
  const auto has_letter = [](std::string_view line) {
    return std::any_of(line.begin(), line.end(),
                       [](unsigned char c) { return std::isalpha(c) != 0; });
  };
  const auto starts_block = [](std::string_view line) {
    if (line.empty()) {
      return true;
    }
    const unsigned char c = static_cast<unsigned char>(line.front());
    return std::isspace(c) != 0 || c == '-' || c == '*' || c == '#' ||
           std::isdigit(c) != 0;
  };

  std::string out;
  std::string_view previous;
  bool first = true;
  while (!text.empty()) {
    const size_t end = text.find('\n');
    std::string_view line = text.substr(0, end);
    text = end == std::string_view::npos ? std::string_view{}
                                         : text.substr(end + 1);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    if (!first) {
      const bool joins = previous.size() >= 60 && has_letter(previous) &&
                         has_letter(line) && !starts_block(line);
      out += joins ? ' ' : '\n';
    }
    out += line;
    previous = line;
    first = false;
  }
  return out;
}

const std::string &license_text(int index) {
  static std::string texts[std::size(kLicenses)];
  static bool loaded[std::size(kLicenses)] = {};
  std::string &text = texts[index];
  if (!loaded[index]) {
    loaded[index] = true;
    const License &license = kLicenses[index];
    if (license.file == nullptr) {
      text = license.note;
    } else if (const auto *resource = ResourceManager::instance().get_resource(
                   std::string("licenses/") + license.file)) {
      text = unwrap(std::string_view(
          reinterpret_cast<const char *>(resource->data), resource->size));
    } else {
      text = "This license text is missing from the build.";
    }
  }
  return text;
}

struct AboutModalState {
  int selected_license = 0;
  UpdateStatus status = UpdateStatus::Idle;
  std::string latest_version;
  std::string release_url;
  std::string error_message;
  std::optional<std::future<update::UpdateCheckResult>> pending_request;

  void reset() {
    selected_license = 0;
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

#if defined(MEGATOY_PLATFORM_WEB)
void open_url_in_browser(const std::string &url) {
  EM_ASM(
      {
        var target = UTF8ToString($0);
        window.open(target, '_blank');
      },
      url.c_str());
}
#endif

void poll_update_request(AboutModalState &state) {
  if (!state.pending_request.has_value()) {
    return;
  }
  auto &future = state.pending_request.value();
  if (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
    return;
  }

  update::UpdateCheckResult result = future.get();
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
  state.status = UpdateStatus::Checking;
  state.latest_version.clear();
  state.release_url.clear();
  state.error_message.clear();
  state.pending_request =
      update::release_info_provider().check_for_updates_async(
          megatoy::kVersionTag);
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
    ImGui::SameLine();
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
    ImGui::SameLine();
    ImGui::TextUnformatted("Up to date.");
    break;
  case UpdateStatus::Error:
    ImGui::SameLine();
    ImGui::TextColored(styles::color(styles::MegatoyCol::StatusError),
                       "Update check failed: %s", state.error_message.c_str());
    break;
  }
}

void render_header() {
  const ImVec4 muted = styles::color(styles::MegatoyCol::TextMuted);
  const ImTextureID icon = app_icon();
  if (icon != ImTextureID{}) {
    const float size = ui::scale::px(48.0f);
    ImGui::Image(icon, ImVec2(size, size));
    ImGui::SameLine(0.0f, ui::scale::px(12.0f));
  }
  ImGui::BeginGroup();
  ImGui::PushFont(nullptr, ImGui::GetFontSize() * 1.5f);
  ImGui::TextUnformatted("megatoy");
  ImGui::PopFont();
  ImGui::TextColored(muted, "A YM2612 patch editor for Mega Drive / Genesis.");
  ImGui::EndGroup();
  ImGui::Spacing();

  const float label_width =
      ImGui::CalcTextSize("Version").x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
  ImGui::TextColored(muted, "Version");
  ImGui::SameLine(label_width);
  ImGui::TextUnformatted(megatoy::kVersionTag);
  ImGui::SameLine();
  if (ImGui::TextLink("Change log")) {
    about_modal_state().reset();
    ImGui::CloseCurrentPopup();
    open_changelog_dialog();
  }
  ImGui::TextColored(muted, "License");
  ImGui::SameLine(label_width);
  ImGui::TextUnformatted("MIT");
}

/// The components on the left, the chosen one's license text on the right.
void render_licenses(AboutModalState &state) {
  ImGui::SeparatorText("Third-Party Licenses");
  const ImGuiStyle &style = ImGui::GetStyle();
  const ImVec4 muted = styles::color(styles::MegatoyCol::TextMuted);

  float name_width = 0.0f;
  float license_width = 0.0f;
  for (const License &license : kLicenses) {
    name_width = std::max(name_width, ImGui::CalcTextSize(license.name).x);
    license_width =
        std::max(license_width, ImGui::CalcTextSize(license.license).x);
  }
  const float list_width = name_width + license_width +
                           style.ItemSpacing.x * 2.0f +
                           style.WindowPadding.x * 2.0f;
  const float height = ui::scale::px(240.0f);

  if (ImGui::BeginChild("##license_list", ImVec2(list_width, height),
                        ImGuiChildFlags_Borders)) {
    const float right = ImGui::GetContentRegionAvail().x;
    for (int i = 0; i < static_cast<int>(std::size(kLicenses)); ++i) {
      const License &license = kLicenses[i];
      ImGui::PushID(i);
      if (ImGui::Selectable(license.name, state.selected_license == i)) {
        state.selected_license = i;
      }
      ImGui::SameLine(right - ImGui::CalcTextSize(license.license).x);
      ImGui::TextColored(muted, "%s", license.license);
      ImGui::PopID();
    }
  }
  ImGui::EndChild();

  ImGui::SameLine();
  if (ImGui::BeginChild("##license_text", ImVec2(0.0f, height),
                        ImGuiChildFlags_Borders)) {
    if (ImGui::IsWindowAppearing()) {
      ImGui::SetScrollY(0.0f);
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(license_text(state.selected_license).c_str());
    ImGui::PopTextWrapPos();
  }
  ImGui::EndChild();
}

} // namespace

void render_about_dialog() {
  auto &state = about_modal_state();
  poll_update_request(state);
  // Nothing here is a decision, so anything that means "I am done looking"
  // closes it.
  auto scope =
      begin_modal("About megatoy", ModalDismiss::EscapeOrOutsideClick, 720.0f);
  if (!scope.visible) {
    if (!ImGui::IsPopupOpen("About megatoy")) {
      state.reset();
    }
    return;
  }
  if (scope.dismissed) {
    state.reset();
  }

  if (ImGui::IsWindowAppearing()) {
    state.reset();
  }

  render_header();
  ImGui::Spacing();
  render_licenses(state);

  ImGui::Spacing();
  ImGui::PushStyleColor(ImGuiCol_Text,
                        styles::color(styles::MegatoyCol::TextMuted));
  dialog_text("SEGA, Mega Drive, and Genesis are either registered trademarks "
              "or trademarks of SEGA Corporation. megatoy is an unofficial fan "
              "project and is not affiliated with or endorsed by SEGA.");
  ImGui::PopStyleColor();

  ImGui::Separator();
#if defined(MEGATOY_PLATFORM_WEB)
  if (ImGui::Button("Open releases page")) {
    open_url_in_browser(update::build_release_page_url());
  }
#else
  ImGui::BeginDisabled(state.status == UpdateStatus::Checking);
  if (ImGui::Button("Check for updates")) {
    start_update_check(state);
  }
  ImGui::EndDisabled();
  render_update_status(state);
#endif
  ImGui::SameLine();
  align_buttons_right({dialog_button_width()});
  if (ImGui::Button("Close", ImVec2(dialog_button_width(), 0.0f))) {
    state.reset();
    ImGui::CloseCurrentPopup();
  }

  end_modal();
}

void open_about_dialog() { ImGui::OpenPopup("About megatoy"); }

} // namespace ui
