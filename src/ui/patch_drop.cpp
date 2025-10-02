#include "patch_drop.hpp"

#include "../app_state.hpp"
#include <imgui.h>

namespace ui {
namespace {
constexpr const char *kErrorPopupTitle = "Patch Load Error";
constexpr const char *kInstrumentPopupTitle = "Select Instrument";
constexpr const char *kFallbackErrorMessage = "Unsupported file format.";
}

void render_patch_drop_feedback(AppState &app_state) {
  auto &drop = app_state.ui_state().drop_state;

  if (drop.show_error_popup) {
    ImGui::OpenPopup(kErrorPopupTitle);
    drop.show_error_popup = false;
  }

  if (drop.show_picker_for_multiple_instruments) {
    ImGui::OpenPopup(kInstrumentPopupTitle);
    drop.show_picker_for_multiple_instruments = false;
  }

  if (ImGui::BeginPopupModal(kErrorPopupTitle, nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    const char *message = drop.error_message.empty()
                              ? kFallbackErrorMessage
                              : drop.error_message.c_str();
    ImGui::TextWrapped("%s", message);
    if (ImGui::Button("OK", ImVec2(120, 0))) {
      drop.error_message.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal(kInstrumentPopupTitle, nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (!drop.instruments.empty()) {
      drop.selected_instrument = std::clamp(
          drop.selected_instrument, 0,
          static_cast<int>(drop.instruments.size() - 1));
      const auto &current = drop.instruments[static_cast<size_t>(
          drop.selected_instrument)];
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
        app_state.cancel_instrument_selection();
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Load", ImVec2(120, 0))) {
        app_state.apply_mml_instrument_selection(
            static_cast<size_t>(drop.selected_instrument));
        ImGui::CloseCurrentPopup();
      }
    } else {
      ImGui::TextUnformatted(kFallbackErrorMessage);
      if (ImGui::Button("OK", ImVec2(120, 0))) {
        app_state.cancel_instrument_selection();
        ImGui::CloseCurrentPopup();
      }
    }

    ImGui::EndPopup();
  } else if (!drop.instruments.empty() &&
             !ImGui::IsPopupOpen(kInstrumentPopupTitle)) {
    app_state.cancel_instrument_selection();
  }
}

} // namespace ui
