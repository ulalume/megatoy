#include "patch_selector.hpp"
#include "common.hpp"
#include "file_manager.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <imgui.h>
#include <string>
#include <vector>

#include "gui/styles/megatoy_style.hpp"

namespace ui {
namespace {
#define INDENT (4.0f)

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

void begin_popup_context(PatchSelectorContext &context,
                         const std::string &relative_path) {
  if (ImGui::BeginPopupContextItem(nullptr)) {
    if (ImGui::MenuItem(ui::reveal_in_file_manager_label())) {
      if (context.reveal_in_file_manager) {
        context.reveal_in_file_manager(
            context.repository.to_absolute_path(relative_path));
      }
    }
    ImGui::EndPopup();
  }
}
void is_item_hovered(const std::string &format,
                     const std::string &relative_path) {
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Format: %s\nPath: %s", format.c_str(),
                      relative_path.c_str());
  }
}

void render_patch_tree(const std::vector<patches::PatchEntry> &tree,
                       PatchSelectorContext &context, int depth = 0) {
  for (const auto &item : tree) {
    ImGui::PushID(item.relative_path.c_str());

    if (item.is_directory) {
      if (ImGui::TreeNode(item.name.c_str())) {
        begin_popup_context(context, item.relative_path);
        render_patch_tree(item.children, context, depth + 1);
        ImGui::TreePop();
      }
    } else {
      ImGui::Indent(INDENT * depth);

      bool is_current =
          (item.relative_path == context.session.current_patch_path());
      if (is_current) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              styles::color(styles::MegatoyCol::TextHighlight));
      }
      std::string name_string = item.name;
      auto name_string_selectable =
          ImGui::Selectable(name_string.c_str(), false);
      begin_popup_context(context, item.relative_path);
      is_item_hovered(item.format, item.relative_path);
      ImGui::SameLine();
      ImGui::PushStyleColor(
          ImGuiCol_Text,
          color_with_alpha_vec4(ImGui::GetStyleColorVec4(ImGuiCol_Text), 0.5f));
      auto format_string_selectable =
          ImGui::Selectable(item.format.c_str(), false);
      if (name_string_selectable || format_string_selectable) {
        if (context.safe_load_patch) {
          context.safe_load_patch(item);
        }
      }
      ImGui::PopStyleColor();
      begin_popup_context(context, item.relative_path);
      is_item_hovered(item.format, item.relative_path);
      if (is_current) {
        ImGui::PopStyleColor();
      }
      ImGui::Unindent(INDENT * depth);
    }
    ImGui::PopID();
  }
}

} // namespace

void render_patch_selector(const char *title, PatchSelectorContext &context) {
  auto &prefs = context.prefs;
  if (!prefs.show_patch_selector) {
    return;
  }

  ImGui::SetNextWindowPos(ImVec2(50, 400), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(350, 500), ImGuiCond_FirstUseEver);

  if (ImGui::Begin(title, &prefs.show_patch_selector)) {
    if (ImGui::Button("Refresh")) {
      context.repository.refresh();
    }

    auto &preset_repository = context.repository;
    if (preset_repository.has_directory_changed()) {
      preset_repository.refresh();
    }

    ImGui::Spacing();

    char search_buffer[128];
    std::strncpy(search_buffer, prefs.patch_search_query.c_str(),
                 sizeof(search_buffer));
    search_buffer[sizeof(search_buffer) - 1] = '\0';

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::InputTextWithHint("##Search", "Type to filter patches",
                                 search_buffer, sizeof(search_buffer))) {
      prefs.patch_search_query = std::string(search_buffer);
    }

    auto preset_tree = preset_repository.tree();
    bool has_query = std::any_of(
        prefs.patch_search_query.begin(), prefs.patch_search_query.end(),
        [](unsigned char ch) { return !std::isspace(ch); });

    if (preset_tree.empty()) {
      ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted),
                         "No patches found");
    } else if (has_query) {
      std::vector<const patches::PatchEntry *> all_patches;
      collect_leaf_patches(preset_tree, all_patches);

      std::string query_lower = to_lower(prefs.patch_search_query);

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
              (entry->relative_path == context.session.current_patch_path());
          if (is_current) {
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                styles::color(styles::MegatoyCol::StatusSuccess));
          }

          std::string label = "[" + entry->format + "] " + entry->name + "##" +
                              entry->relative_path;
          if (ImGui::Selectable(label.c_str(), is_current)) {
            if (context.safe_load_patch) {
              context.safe_load_patch(*entry);
            }
          }

          if (is_current) {
            ImGui::PopStyleColor();
          }

          begin_popup_context(context, entry->relative_path);
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Format: %s\nPath: %s", entry->format.c_str(),
                              entry->relative_path.c_str());
          }

          ImGui::PopID();
        }

        if (match_count == 0) {
          ImGui::TextColored(styles::color(styles::MegatoyCol::TextMuted),
                             "No results for '%s'",
                             prefs.patch_search_query.c_str());
        }
      }
      ImGui::EndChild();
    } else {
      if (ImGui::BeginChild("PresetTree", ImGui::GetContentRegionAvail(),
                            true)) {
        render_patch_tree(preset_tree, context);
      }
      ImGui::EndChild();
    }
  }

  ImGui::End();
}

} // namespace ui
