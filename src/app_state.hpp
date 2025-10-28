#pragma once

#include "app_services.hpp"
#include "input_state.hpp"
#include "ym2612/patch.hpp"
#include <filesystem>
#include <string>
#include <vector>

struct UIState {
  PreferenceManager::UIPreferences prefs;
  bool open_directory_dialog = false;

  struct DropState {
    bool show_error_popup = false;
    std::string error_message;
    bool show_picker_for_multiple_instruments = false;
    std::filesystem::path pending_instruments_path;
    std::vector<ym2612::Patch> instruments;
    int selected_instrument = 0;
    bool show_drop_confirmation = false;
    ym2612::Patch pending_dropped_patch;
    std::filesystem::path pending_dropped_path;
  } drop_state;

  struct EnvelopeState {
    enum class SliderState {
      None,
      Hover,
      Active,
    };
    SliderState total_level = SliderState::None;
    SliderState attack_rate = SliderState::None;
    SliderState decay_rate = SliderState::None;
    SliderState sustain_level = SliderState::None;
    SliderState sustain_rate = SliderState::None;
    SliderState release_rate = SliderState::None;
  } envelope_states[4];

  struct ConfirmationState {
    bool show_unsaved_changes_dialog = false;
    patches::PatchEntry pending_patch_entry;
    std::string dialog_message;
    enum class Operation {
      Load,
      Drop,
      Exit,
    } operation;

    static ConfirmationState load(patches::PatchEntry patch_entry) {
      ConfirmationState state;
      state.show_unsaved_changes_dialog = true;
      state.pending_patch_entry = patch_entry;
      state.dialog_message =
          "You have unsaved changes. Loading a new patch will discard "
          "them.\n\nContinue?";
      state.operation = UIState::ConfirmationState::Operation::Load;
      return state;
    }
    static ConfirmationState drop() {
      ConfirmationState state;
      state.show_unsaved_changes_dialog = true;
      state.dialog_message =
          "You have unsaved changes. Dropping a new patch will discard "
          "them.\n\nContinue?";
      state.operation = UIState::ConfirmationState::Operation::Drop;
      return state;
    }
    static ConfirmationState exit() {
      ConfirmationState state;
      state.show_unsaved_changes_dialog = true;
      state.dialog_message =
          "You have unsaved changes. Do you want to discard them and exit?";
      state.operation = UIState::ConfirmationState::Operation::Exit;
      return state;
    }

  } confirmation_state;
};

class AppState {
public:
  explicit AppState(AppServices &services);

  void init();
  void shutdown();

  InputState &input_state() { return input_state_; }
  const InputState &input_state() const { return input_state_; }

  UIState &ui_state() { return ui_state_; }
  const UIState &ui_state() const { return ui_state_; }

  const std::vector<std::string> &connected_midi_inputs() const {
    return connected_midi_inputs_;
  }
  void set_connected_midi_inputs(std::vector<std::string> devices);

private:
  AppServices &services_;
  InputState input_state_;
  UIState ui_state_;
  std::vector<std::string> connected_midi_inputs_;
};
