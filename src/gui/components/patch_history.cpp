#include "patch_history.hpp"

#include "formats/ginpkg.hpp"
#include <algorithm>
#include <filesystem>
#include <imgui.h>
#include <sstream>
#include <string_view>

namespace ui {

#if defined(MEGATOY_PLATFORM_WEB)
void render_patch_history(const char *, PatchHistoryContext &,
                          PatchHistoryState &) {
  // ginpkg is not supported on web builds.
}
#else
namespace {

bool is_ginpkg_path(const std::string &path) {
  std::string lower = path;
  std::transform(
      lower.begin(), lower.end(), lower.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return lower.ends_with(".ginpkg");
}

std::string make_label(const formats::ginpkg::HistoryEntry &entry) {
  std::ostringstream oss;
  oss << entry.timestamp;
  if (entry.comment && !entry.comment->empty()) {
    oss << " - " << *entry.comment;
  }
  return oss.str();
}

constexpr std::string_view kCurrentId = "__current__";

bool path_changed(const std::string &current, const std::string &loaded) {
  return current != loaded;
}

void maybe_refresh(PatchHistoryState &state,
                   const std::filesystem::path &package_path) {
  std::error_code ec;
  auto current_time = std::filesystem::last_write_time(package_path, ec);
  if (!ec) {
    if (!state.last_write_time || *state.last_write_time != current_time) {
      state.refresh_requested = true;
      state.last_write_time = current_time;
    }
  }
}

} // namespace

void render_patch_history(const char *title, PatchHistoryContext &context,
                          PatchHistoryState &state) {
  if (!context.prefs.show_patch_history) {
    return;
  }

  if (!ImGui::Begin(title, &context.prefs.show_patch_history)) {
    ImGui::End();
    return;
  }

  const std::string &path_str = context.patch_session.current_patch_path();
  const bool is_ginpkg = is_ginpkg_path(path_str);

  if (!is_ginpkg) {
    state.loaded_path = path_str;
    state.selected_uuid = std::string(kCurrentId);
    state.refresh_requested = false;
    ImGui::TextUnformatted(
        "Patch versions view is available for ginpkg packages only.");
    ImGui::End();
    return;
  }

  auto absolute_path =
      context.patch_session.repository().to_absolute_path(path_str);
  maybe_refresh(state, absolute_path);

  if (path_changed(path_str, state.loaded_path)) {
    state.refresh_requested = true;
    state.loaded_path = path_str;
    state.last_write_time.reset();
    state.selected_uuid = std::string(kCurrentId);
  }

  if (state.refresh_requested) {
    state.error_message.clear();
    auto package = formats::ginpkg::load_package(absolute_path);
    if (package) {
      state.versions = package->history();
      state.current_data = package->current_data();
      state.current_timestamp = package->current_timestamp();
      state.selected_uuid = std::string(kCurrentId);
    } else {
      state.error_message = "Failed to read ginpkg.";
      state.versions.clear();
      state.current_data.clear();
      state.current_timestamp.clear();
    }
    state.refresh_requested = false;
  }

  if (ImGui::SmallButton("Refresh")) {
    state.refresh_requested = true;
  }
  ImGui::SameLine();
  ImGui::Text("Package: %s", path_str.c_str());

  if (!state.error_message.empty()) {
    ImGui::Text("%s", state.error_message.c_str());
  }

  ImGui::Separator();
  if (ImGui::BeginChild("##ginpkg_history", ImVec2(0, 0), true)) {
    if (ImGui::BeginTable("##ginpkg_history_table", 2,
                          ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersInnerV)) {
      ImGui::TableSetupColumn("Version");
      ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 80);
      ImGui::TableHeadersRow();

      // History entries
      for (const auto &version : state.versions) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        const std::string label = make_label(version);
        const std::string selectable_id = label + "##" + version.uuid;
        bool selected = state.selected_uuid == version.uuid;
        if (ImGui::Selectable(selectable_id.c_str(), selected)) {
          state.selected_uuid = version.uuid;
          auto patch =
              formats::ginpkg::read_version(absolute_path, version.uuid);
          if (patch) {
            context.patch_session.set_current_patch(*patch, absolute_path);
          } else {
            state.error_message = "Failed to load the selected version.";
          }
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::PushID(version.uuid.c_str());
        if (ImGui::SmallButton("Delete")) {
          if (formats::ginpkg::delete_version(absolute_path, version.uuid)) {
            state.refresh_requested = true;
            state.selected_uuid.clear();
          } else {
            state.error_message = "Failed to delete version.";
          }
        }
        ImGui::PopID();
      }
      // Current version (latest) shown last
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      {
        const std::string label = state.current_timestamp.empty()
                                       ? "Latest"
                                       : state.current_timestamp + " (Latest)";
        bool selected = state.selected_uuid == kCurrentId;
        if (ImGui::Selectable((label + "##current").c_str(), selected)) {
          state.selected_uuid = std::string(kCurrentId);
          try {
            if (!state.current_data.empty()) {
              auto json = nlohmann::json::parse(state.current_data);
              auto patch = json.get<ym2612::Patch>();
              context.patch_session.set_current_patch(patch, absolute_path);
            }
          } catch (const std::exception &) {
            state.error_message = "Failed to load current version.";
          }
        }
      }
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted("-");
      ImGui::EndTable();
    }
    ImGui::EndChild();
  }

  ImGui::End();
}
#endif

} // namespace ui
