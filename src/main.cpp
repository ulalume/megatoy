#include "app_state.hpp"
#include "midi/midi_input_manager.hpp"
#include "core/types.hpp"
#include "gui/components/keyboard_typing.hpp"
#include "gui/components/main_menu.hpp"
#include "gui/components/midi_keyboard.hpp"
#include "gui/components/mml_console.hpp"
#include "gui/components/patch_drop.hpp"
#include "gui/components/patch_editor.hpp"
#include "gui/components/patch_selector.hpp"
#include "gui/components/preferences.hpp"
#include "gui/components/waveform.hpp"
#include "ym2612/channel.hpp"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <imgui.h>
#include <iostream>

namespace {

void handle_history_shortcuts(AppState &app_state) {
  ImGuiIO &io = ImGui::GetIO();

  if (io.WantTextInput || app_state.input_state().text_input_focused) {
    return;
  }

  const bool primary_modifier = io.KeyCtrl || io.KeySuper;
  if (!primary_modifier) {
    return;
  }

  const bool shift = io.KeyShift;
  auto &history = app_state.history();

  if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
    if (shift) {
      if (history.can_redo()) {
        history.redo(app_state);
      }
    } else if (history.can_undo()) {
      history.undo(app_state);
    }
  } else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
    if (history.can_redo()) {
      history.redo(app_state);
    }
  }
}

void handle_file_drop(GLFWwindow *window, int count, const char **paths) {
  if (paths == nullptr || count <= 0) {
    return;
  }

  auto *state = static_cast<AppState *>(glfwGetWindowUserPointer(window));
  if (state == nullptr) {
    return;
  }

  for (int i = 0; i < count; ++i) {
    if (paths[i] != nullptr) {
      state->handle_patch_file_drop(std::filesystem::path(paths[i]));
    }
  }
}

} // namespace

int main(int argc, char *argv[]) {
  std::cout << "VGM Real-time Audio Test with Dear ImGui\n";

  AppState app_state;
  app_state.init();

  if (GLFWwindow *window = app_state.gui().manager().get_window()) {
    glfwSetWindowUserPointer(window, &app_state);
    glfwSetDropCallback(window, handle_file_drop);
  }

  // Set up frequency for C4 note
  app_state.audio()
      .device()
      .channel(ym2612::ChannelIndex::Fm1)
      .write_frequency({4, Key::C});

  // Init MIDI
  MidiInputManager midi;
  midi.init();

  std::cout
      << "GUI initialized. Use the button in the window to play C4 note.\n";

  // Main GUI loop
  while (!app_state.gui().manager().should_close()) {
    // Poll events
    app_state.gui().manager().poll_events();
    // Start new frame
    app_state.gui().manager().begin_frame();

    // Midi USB update
  midi.poll();
  midi.dispatch(app_state);

    // ImGui::ShowDemoWindow();

    ui::render_main_menu(app_state);
    handle_history_shortcuts(app_state);
    ui::render_patch_drop_feedback(app_state);
    // Render UI panels
    ui::render_patch_editor(app_state);

    ui::render_midi_keyboard(app_state);
    ui::render_preferences_window(app_state);
    ui::render_patch_selector(app_state);

    ui::render_keyboard_typing(app_state);
    ui::render_mml_console(app_state);
    ui::render_waveform(app_state);

    app_state.input_state().text_input_focused = false;

    app_state.preference_manager().set_ui_preferences(
        app_state.ui_state().prefs);

    // End frame and render
    app_state.gui().manager().end_frame();
  }

  app_state.shutdown();
  std::cout << "Goodbye!\n";
  return 0;
}
