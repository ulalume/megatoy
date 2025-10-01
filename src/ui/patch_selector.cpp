#include "patch_selector.hpp"
#include "../patches/patch_repository.hpp"
#include "file_manager.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <imgui.h>
#include <string>
#include <vector>

#include "styles/megatoy_style.hpp"

namespace ui {
namespace {
#define INDENT (1.0f)

std::string to_lower(const std::string &value) {
  std::string lowered;
  lowered.reserve(value.size());
  for (unsigned char ch : value) {
    lowered.push_back(static_cast<char>(std::tolower(ch)));
  }
  return lowered;
}

bool contains_case_insensitive(const std::string &haystack,
                               const std::string &needle_lower) {
  if (needle_lower.empty()) {
    return true;
  }

  std::string haystack_lower = to_lower(haystack);
  return haystack_lower.find(needle_lower) != std::string::npos;
}

void collect_leaf_patches(const std::vector<patches::PatchEntry> &tree,
                          std::vector<const patches::PatchEntry *> &out) {
  for (const auto &item : tree) {
    if (item.is_directory) {
      collect_leaf_patches(item.children, out);
    } else {
      out.push_back(&item);
    }
  }
}

void render_patch_tree(const std::vector<patches::PatchEntry> &tree,
                       AppState &app_state, int depth = 0) {
  for (const auto &item : tree) {
    ImGui::PushID(item.relative_path.c_str());

    if (item.is_directory) {
      if (ImGui::TreeNode(item.name.c_str())) {

        if (ImGui::BeginPopupContextItem(nullptr)) {
          if (ImGui::MenuItem(ui::reveal_in_file_manager_label())) {
            ui::reveal_in_file_manager(app_state.patch_repository()
                                           .to_absolute_path(item.relative_path)
                                           .string());
          }
          ImGui::EndPopup();
        }

        render_patch_tree(item.children, app_state, depth + 1);
        ImGui::TreePop();
      }
    } else {
      ImGui::Indent(INDENT * depth);
      // ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "[%s]",
      //                    item.format.c_str());

      // ImGui::SameLine();

      bool is_current = (item.relative_path == app_state.current_patch_path());
      if (is_current) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              styles::color(styles::MegatoyCol::TextHighlight));
      }
      std::string name_and_format = "[" + item.format + "] " + item.name;
      if (ImGui::Selectable(name_and_format.c_str(), false)) {
        app_state.load_patch(item);
      }

      if (ImGui::BeginPopupContextItem(nullptr)) {
        if (ImGui::MenuItem(ui::reveal_in_file_manager_label())) {
          ui::reveal_in_file_manager(app_state.patch_repository()
                                         .to_absolute_path(item.relative_path)
                                         .string());
        }
        ImGui::EndPopup();
      }

      if (is_current) {
        ImGui::PopStyleColor();
      }

      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Format: %s\nPath: %s", item.format.c_str(),
                          item.relative_path.c_str());
      }

      ImGui::Unindent(INDENT * depth);
    }

    ImGui::PopID();
  }
}

} // namespace

void render_patch_selector(AppState &app_state) {
  const auto patches_dir =
      app_state.preference_manager().get_user_patches_directory();
  const auto builtin_dir =
      app_state.preference_manager().get_builtin_presets_directory();

  auto &ui_state = app_state.ui_state();
  if (!ui_state.prefs.show_patch_selector) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(50, 400), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(350, 500), ImGuiCond_FirstUseEver);

  if (ImGui::Begin("Patch Browser", &ui_state.prefs.show_patch_selector)) {
    if (ImGui::Button("Refresh")) {
      app_state.patch_repository().refresh();
    }

    auto &preset_repository = app_state.patch_repository();
    if (preset_repository.has_directory_changed()) {
      preset_repository.refresh();
    }

    ImGui::Spacing();

    char search_buffer[128];
    std::strncpy(search_buffer, ui_state.prefs.patch_search_query.c_str(),
                 sizeof(search_buffer));
    search_buffer[sizeof(search_buffer) - 1] = '\0';

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputTextWithHint("##Search", "Type to filter patches",
                                 search_buffer, sizeof(search_buffer))) {
      ui_state.prefs.patch_search_query = std::string(search_buffer);
    }
    if (ImGui::IsItemActive()) {
      app_state.input_state().text_input_focused = true;
    }

    auto preset_tree = preset_repository.tree();
    std::sort(preset_tree.begin(), preset_tree.end(),
              [](const patches::PatchEntry &a, const patches::PatchEntry &b) {
                if (a.name == "user") {
                  return true;
                }
                if (b.name == "user") {
                  return false;
                }

                if (a.name == "presets") {
                  return false;
                }
                if (b.name == "presets") {
                  return true;
                }
                return a.name < b.name;
              });

    bool has_query =
        std::any_of(ui_state.prefs.patch_search_query.begin(),
                    ui_state.prefs.patch_search_query.end(),
                    [](unsigned char ch) { return !std::isspace(ch); });

    if (preset_tree.empty()) {
      ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted),
                         "No patches found");
      ImGui::Text("User directory: %s", patches_dir.c_str());
      ImGui::Text("Factory directory: %s", builtin_dir.c_str());
    } else if (has_query) {
      std::vector<const patches::PatchEntry *> all_patches;
      collect_leaf_patches(preset_tree, all_patches);

      std::string query_lower = to_lower(ui_state.prefs.patch_search_query);

      if (ImGui::BeginChild("PresetSearchResults",
                            ImGui::GetContentRegionAvail(), true)) {
        int match_count = 0;
        for (const auto *entry : all_patches) {
          if (!contains_case_insensitive(entry->name, query_lower) &&
              !contains_case_insensitive(entry->format, query_lower) &&
              !contains_case_insensitive(entry->relative_path, query_lower)) {
            continue;
          }

          match_count++;
          ImGui::PushID(entry->relative_path.c_str());

          bool is_current =
              (entry->relative_path == app_state.current_patch_path());
          if (is_current) {
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                styles::color(styles::MegatoyCol::StatusSuccess));
          }

          std::string label = "[" + entry->format + "] " + entry->name + "##" +
                              entry->relative_path;
          if (ImGui::Selectable(label.c_str(), is_current)) {
            app_state.load_patch(*entry);
          }

          if (is_current) {
            ImGui::PopStyleColor();
          }

          if (ImGui::BeginPopupContextItem(nullptr)) {
            if (ImGui::MenuItem(reveal_in_file_manager_label())) {
              reveal_in_file_manager(app_state.patch_repository()
                                         .to_absolute_path(entry->relative_path)
                                         .string());
            }
            ImGui::EndPopup();
          }

          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Format: %s\nPath: %s", entry->format.c_str(),
                              entry->relative_path.c_str());
          }

          ImGui::PopID();
        }

        if (match_count == 0) {
          ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted),
                             "No results for '%s'",
                             ui_state.prefs.patch_search_query.c_str());
        }
      }
      ImGui::EndChild();
    } else {
      if (ImGui::BeginChild("PresetTree", ImGui::GetContentRegionAvail(),
                            true)) {
        render_patch_tree(preset_tree, app_state);
      }
      ImGui::EndChild();
    }
  }

  ImGui::End();
}

} // namespace ui
