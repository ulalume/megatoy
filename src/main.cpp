#include "app_context.hpp"
#include "app_services.hpp"
#include "app_state.hpp"
#include "drop_actions.hpp"
#include "gui/ui_renderer.hpp"
#include "midi/midi_input_manager.hpp"
#include "patch_actions.hpp"
#include "platform/platform_config.hpp"
#include "update/release_provider.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_platform_services.hpp"
#include <emscripten.h>
#else
#include "platform/native/desktop_platform_services.hpp"
#endif
#include <filesystem>
#include <imgui.h>
#include <iostream>

namespace {

void handle_file_drop(void *user_pointer, int count, const char **paths) {
  if (paths == nullptr || count <= 0) {
    return;
  }

  auto *context = static_cast<AppContext *>(user_pointer);
  if (context == nullptr) {
    return;
  }

  for (int i = 0; i < count; ++i) {
    if (paths[i] == nullptr) {
      continue;
    }

    drop_actions::Environment env{context->services, context->ui_state(),
                                  [context](const ym2612::Patch &patch,
                                            const std::filesystem::path &path) {
                                    patch_actions::load_dropped_patch(
                                        *context, patch, path);
                                  }};

    drop_actions::handle_drop(env, std::filesystem::path(paths[i]));
  }
}

struct RuntimeContext {
  AppContext *app_context = nullptr;
  MidiInputManager *midi = nullptr;
  bool running = true;
};

bool run_frame(RuntimeContext &runtime) {
  auto &services = runtime.app_context->services;
  auto &app_state = runtime.app_context->state;

  if (services.gui_manager.get_should_close()) {
    if (services.patch_session.is_modified()) {
      app_state.ui_state().confirmation_state =
          UIState::ConfirmationState::exit();
      services.gui_manager.set_should_close(false);
    } else {
      runtime.running = false;
      return false;
    }
  }

  services.gui_manager.poll_events();

  runtime.midi->poll();
  runtime.midi->dispatch(*runtime.app_context);

  services.gui_manager.begin_frame();
  services.history.handle_shortcuts(*runtime.app_context);
  ui::render_all(*runtime.app_context);
  services.preference_manager.set_ui_preferences(app_state.ui_state().prefs);
  services.gui_manager.end_frame();
  return true;
}

#if defined(MEGATOY_PLATFORM_WEB)
void web_main_loop(void *arg) {
  auto *runtime = static_cast<RuntimeContext *>(arg);
  if (!runtime->running) {
    emscripten_cancel_main_loop();
    return;
  }
  if (!run_frame(*runtime)) {
    emscripten_cancel_main_loop();
  }
}
#endif

} // namespace

int main(int argc, char *argv[]) {
#if defined(MEGATOY_PLATFORM_WEB)
  platform::web::WebPlatformServices platform_services;
#else
  DesktopPlatformServices platform_services;
#endif

  update::set_release_info_provider(platform_services.release_info_provider());

  AppServices services(platform_services);
  AppState app_state{};
  services.initialize_app(app_state);
  AppContext app_context{services, app_state};

  services.gui_manager.set_drop_callback(&app_context, handle_file_drop);

  MidiInputManager midi(platform_services.create_midi_backend());
  midi.init();
  app_context.midi = &midi;

  RuntimeContext runtime{&app_context, &midi, true};

#if defined(MEGATOY_PLATFORM_WEB)
  emscripten_set_main_loop_arg(web_main_loop, &runtime, 0, true);
  services.shutdown_app();
  std::cout << "Goodbye!\n";
#else
  while (run_frame(runtime)) {
  }
  services.shutdown_app();
  std::cout << "Goodbye!\n";
#endif
  return 0;
}
