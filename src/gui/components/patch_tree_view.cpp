#include "patch_tree_view.hpp"

#include "common.hpp"
#include "gui/styles/megatoy_style.hpp"
#include "patch_selector_shared.hpp"

#include <imgui.h>

namespace ui::selector_detail {

namespace {

constexpr float kDepthIndent = 4.0f;

bool directory_has_visible_children(const patches::PatchEntry &directory,
                                    const std::string &query_lower,
                                    int min_star_rating) {
  for (const auto &child : directory.children) {
    if (child.is_directory) {
      if (directory_has_visible_children(child, query_lower, min_star_rating)) {
        return true;
      }
    } else if (entry_passes_star_filter(child, min_star_rating) &&
               entry_matches_query(child, query_lower)) {
      return true;
    }
  }
  return false;
}

bool render_subtree(const std::vector<patches::PatchEntry> &tree,
                    PatchSelectorContext &context,
                    const std::string &query_lower, int min_star_rating,
                    int depth) {
  bool any_rendered = false;

  for (const auto &item : tree) {
    if (item.is_directory) {
      const bool has_children =
          directory_has_visible_children(item, query_lower, min_star_rating);
      const bool matches_self =
          !query_lower.empty() && entry_matches_query(item, query_lower);
      if (!has_children && !matches_self) {
        continue;
      }

      ImGui::PushID(item.relative_path.c_str());
      std::string display_name = item.name;
      if (item.relative_path == kBuiltinPresetRoot) {
        display_name = std::string(kBuiltinPresetDisplayName);
      }
      const bool open = ImGui::TreeNodeEx(display_name.c_str(),
                                          ImGuiTreeNodeFlags_SpanFullWidth);
      entry_context_menu(context, item);
      if (open) {
        render_subtree(item.children, context, query_lower, min_star_rating,
                       depth + 1);
        ImGui::TreePop();
      }
      ImGui::PopID();
      any_rendered = true;
      continue;
    }

    if (!entry_passes_star_filter(item, min_star_rating) ||
        !entry_matches_query(item, query_lower)) {
      continue;
    }

    ImGui::PushID(item.relative_path.c_str());
    ImGui::Indent(kDepthIndent * depth);

    const std::string &selection_path = item.source_relative_path.empty()
                                            ? item.relative_path
                                            : item.source_relative_path;
    const bool is_current =
        selection_path == context.session.current_patch_path();
    if (is_current) {
      ImGui::PushStyleColor(ImGuiCol_Text,
                            styles::color(styles::MegatoyCol::TextHighlight));
    }

    // Name and dimmed format label, either of which loads the patch. The
    // context menu and tooltip attach to whichever was drawn last, so both
    // are registered after each selectable.
    const bool name_clicked = ImGui::Selectable(item.name.c_str(), false);
    entry_context_menu(context, item);
    show_patch_tooltip(item);

    ImGui::SameLine();
    ImGui::PushStyleColor(
        ImGuiCol_Text,
        color_with_alpha_vec4(ImGui::GetStyleColorVec4(ImGuiCol_Text), 0.5f));
    const bool format_clicked = ImGui::Selectable(item.format.c_str(), false);
    ImGui::PopStyleColor();
    entry_context_menu(context, item);
    show_patch_tooltip(item);

    if (is_current) {
      ImGui::PopStyleColor();
    }

    if ((name_clicked || format_clicked) && context.safe_load_patch) {
      context.safe_load_patch(item);
    }

    ImGui::Unindent(kDepthIndent * depth);
    ImGui::PopID();
    any_rendered = true;
  }

  return any_rendered;
}

} // namespace

bool render_patch_tree(const std::vector<patches::PatchEntry> &tree,
                       PatchSelectorContext &context,
                       const std::string &query_lower, int min_star_rating) {
  return render_subtree(tree, context, query_lower, min_star_rating, 0);
}

} // namespace ui::selector_detail
