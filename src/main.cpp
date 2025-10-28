#include "app_context.hpp"
#include "app_services.hpp"
#include "app_state.hpp"
#include "drop_actions.hpp"
#include "gui/ui_renderer.hpp"
#include "midi/midi_input_manager.hpp"
#include <GLFW/glfw3.h>
#include <filesystem>
#include <imgui.h>
#include <iostream>

namespace {

void handle_file_drop(GLFWwindow *window, int count, const char **paths) {
  if (paths == nullptr || count <= 0) {
    return;
  }

  auto *context = static_cast<AppContext *>(glfwGetWindowUserPointer(window));
  if (context == nullptr) {
    return;
  }

  for (int i = 0; i < count; ++i) {
    if (paths[i] != nullptr) {
      drop_actions::handle_drop(*context, std::filesystem::path(paths[i]));
    }
  }
}

} // namespace

int main(int argc, char *argv[]) {
  AppServices services;
  AppState app_state{};
  services.initialize_app(app_state);
  AppContext app_context{services, app_state};

  // Setup window callbacks
  if (GLFWwindow *window = services.gui_manager.get_window()) {
    glfwSetWindowUserPointer(window, &app_context);
    glfwSetDropCallback(window, handle_file_drop);
  }

  // Initialize MIDI
  MidiInputManager midi;
  midi.init();

  // Main application loop
  while (true) {
    // Check if window close was requested
    if (services.gui_manager.get_should_close()) {
      // Check if there are unsaved changes
      if (services.patch_session.is_modified()) {
        app_state.ui_state().confirmation_state =
            UIState::ConfirmationState::exit();
        services.gui_manager.set_should_close(false);
      } else {
        break;
      }
    }

    // Poll events
    services.gui_manager.poll_events();

    // Update MIDI
    midi.poll();
    midi.dispatch(app_context);

    // Render UI
    services.gui_manager.begin_frame();
    services.history.handle_shortcuts(app_context);
    ui::render_all(app_context);

    // Update preferences from UI state
    services.preference_manager.set_ui_preferences(app_state.ui_state().prefs);

    services.gui_manager.end_frame();
  }

  // Shutdown
  services.shutdown_app();
  std::cout << "Goodbye!\n";
  return 0;
}
