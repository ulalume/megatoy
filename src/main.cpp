#include "app_state.hpp"
#include "gui/ui_renderer.hpp"
#include "midi/midi_input_manager.hpp"
#include "ym2612/fft_analyzer.hpp"
#include "drop_actions.hpp"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <imgui.h>
#include <iostream>

namespace {

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
      drop_actions::handle_drop(*state, std::filesystem::path(paths[i]));
    }
  }
}

} // namespace

int main(int argc, char *argv[]) {
  // Initialize application state
  AppState app_state;
  app_state.init();

  ym2612::FFTAnalyzer analyzer = ym2612::FFTAnalyzer(1024);

  // Setup window callbacks
  if (GLFWwindow *window = app_state.gui().get_window()) {
    glfwSetWindowUserPointer(window, &app_state);
    glfwSetDropCallback(window, handle_file_drop);
  }

  // Initialize MIDI
  MidiInputManager midi;
  midi.init();

  // Main application loop
  while (true) {
    // Check if window close was requested
    if (app_state.gui().get_should_close()) {
      // Check if there are unsaved changes
      if (app_state.patch_session().is_modified()) {
        app_state.ui_state().confirmation_state =
            UIState::ConfirmationState::exit();
        app_state.gui().set_should_close(false);
      } else {
        break;
      }
    }

    // Poll events
    app_state.gui().poll_events();

    // Update MIDI
    midi.poll();
    midi.dispatch(app_state);

    // Render UI
    app_state.gui().begin_frame();
    app_state.history().handle_shortcuts(app_state);
    ui::render_all(app_state, analyzer);

    // Update preferences from UI state
    app_state.preference_manager().set_ui_preferences(
        app_state.ui_state().prefs);

    app_state.gui().end_frame();
  }

  // Shutdown
  app_state.shutdown();
  std::cout << "Goodbye!\n";
  return 0;
}
