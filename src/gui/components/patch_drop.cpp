#include "patch_drop.hpp"
#include <algorithm>
#include <imgui.h>

namespace ui {
namespace {
constexpr const char *kInstrumentPopupTitle = "Select Instrument";
constexpr const char *kFallbackErrorMessage = "Unsupported file format.";
} // namespace

// Load failures are announced as status toasts; only the instrument picker
// -- an actual decision -- still opens a dialog.
void render_patch_drop_feedback(PatchDropContext &context) {
  auto &drop = context.drop_state;

  if (drop.show_picker_for_multiple_instruments) {
    ImGui::OpenPopup(kInstrumentPopupTitle);
    drop.show_picker_for_multiple_instruments = false;
  }

  if (ImGui::BeginPopupModal(kInstrumentPopupTitle, nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (!drop.instruments.empty()) {
      drop.selected_instrument =
          std::clamp(drop.selected_instrument, 0,
                     static_cast<int>(drop.instruments.size() - 1));
      const auto &current =
          drop.instruments[static_cast<size_t>(drop.selected_instrument)];
      const char *preview =
          current.name.empty() ? "(No name)" : current.name.c_str();
      if (ImGui::BeginCombo("Instrument", preview)) {
        for (size_t i = 0; i < drop.instruments.size(); ++i) {
          bool selected = (drop.selected_instrument == static_cast<int>(i));
          const char *label = drop.instruments[i].name.empty()
                                  ? "(No name)"
                                  : drop.instruments[i].name.c_str();
          if (ImGui::Selectable(label, selected)) {
            drop.selected_instrument = static_cast<int>(i);
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        if (context.cancel_selection) {
          context.cancel_selection();
        }
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Load", ImVec2(120, 0))) {
        if (context.apply_selection) {
          context.apply_selection(
              static_cast<size_t>(drop.selected_instrument));
        }
        ImGui::CloseCurrentPopup();
      }
    } else {
      ImGui::TextUnformatted(kFallbackErrorMessage);
      if (ImGui::Button("OK", ImVec2(120, 0))) {
        if (context.cancel_selection) {
          context.cancel_selection();
        }
        ImGui::CloseCurrentPopup();
      }
    }

    ImGui::EndPopup();
  } else if (!drop.instruments.empty() &&
             !ImGui::IsPopupOpen(kInstrumentPopupTitle)) {
    if (context.cancel_selection) {
      context.cancel_selection();
    }
  }
}

} // namespace ui
