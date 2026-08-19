#include "platform/platform_config.hpp"

#include "app_context.hpp"
#include "app_services.hpp"
#include "app_state.hpp"
#include "audio/audio_command.hpp"
#include "core/status.hpp"
#include "drop_actions.hpp"
#include "gui/ui_renderer.hpp"
#include "midi/midi_input_manager.hpp"
#include "patch_actions.hpp"
#include "platform/run_loop.hpp"
#include "update/release_provider.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_folder_import.hpp"
#include "platform/web/web_platform_services.hpp"
#include "system/path_service.hpp"
#else
#include "platform/native/desktop_platform_services.hpp"
#endif
#include "runtime_context.hpp"
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

#if defined(MEGATOY_PLATFORM_WEB)
  services.gui_manager.poll_events();
#else
  constexpr std::uint64_t kSignalTailFrames = AppServices::SampleRate / 10;
  const bool waveform_is_live =
      app_state.ui_state().prefs.show_waveform &&
      services.audio_manager.scope_buffer().signal_within(kSignalTailFrames);
  const auto before_wait = platform::FrameScheduler::Clock::now();
  const auto wait = runtime.frame_scheduler.wait_before_frame(
      before_wait, services.gui_manager.is_minimized(), waveform_is_live);
  const bool received_event =
      services.gui_manager.poll_events(static_cast<int>(wait.count()));
  const auto frame_started = platform::FrameScheduler::Clock::now();
  if (received_event) {
    runtime.frame_scheduler.note_interaction(frame_started);
  }
  runtime.frame_scheduler.note_frame_started(frame_started);
#endif

  runtime.midi->poll();
  runtime.midi->dispatch(*runtime.app_context);

  services.gui_manager.begin_frame();
  services.history.handle_shortcuts(*runtime.app_context);
  ui::render_all(*runtime.app_context);
  const auto &saved_prefs = services.preference_manager.ui_preferences();
  const auto &current_prefs = app_state.ui_state().prefs;
  if (current_prefs.use_velocity != saved_prefs.use_velocity ||
      current_prefs.velocity_sensitivity_depth !=
          saved_prefs.velocity_sensitivity_depth ||
      current_prefs.steal_oldest_note_when_full !=
          saved_prefs.steal_oldest_note_when_full) {
    services.audio_manager.set_note_options(
        current_prefs.use_velocity, current_prefs.velocity_sensitivity_depth,
        current_prefs.steal_oldest_note_when_full);
  }
  if (current_prefs.use_pitch_bend != saved_prefs.use_pitch_bend ||
      current_prefs.use_mod_wheel != saved_prefs.use_mod_wheel) {
    services.audio_manager.set_performance_options(current_prefs.use_pitch_bend,
                                                   current_prefs.use_mod_wheel);
  }
  services.preference_manager.set_ui_preferences(app_state.ui_state().prefs);
  services.gui_manager.end_frame();
  return true;
}

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

#if defined(MEGATOY_PLATFORM_WEB)
  // Folders dragged onto the page are imported into persistent storage; SDL's
  // own drop handler cannot read directories.
  platform::web::set_drop_import_handler(
      [&services](platform::web::FolderImportResult result) {
        if (!result.ok) {
          megatoy::status::error("Folder drop failed: " + result.error);
          return;
        }
        if (services.preference_manager.add_workspace_folder(result.path)) {
          services.patch_session.sync_workspace();
          megatoy::status::success("Added \"" + result.folder_name + "\" (" +
                                   std::to_string(result.file_count) +
                                   " files)");
        } else {
          megatoy::status::warning("\"" + result.folder_name +
                                   "\" is already in the workspace.");
        }
      });
  platform::web::install_drop_import(
      megatoy::system::PathService::web_storage_root());
#endif

  MidiInputManager midi(platform_services.create_midi_backend());
  // Performance messages go straight from the driver's thread to the audio
  // thread, so their timing no longer depends on how fast the UI is drawing.
  midi.set_note_sink([&services](const MidiMessage &message) {
    switch (message.type) {
    case MidiMessage::Type::NoteOn:
      services.audio_manager.submit_from_midi(
          audio::AudioCommand::note_on(message.note, message.velocity));
      break;
    case MidiMessage::Type::NoteOff:
      services.audio_manager.submit_from_midi(
          audio::AudioCommand::note_off(message.note));
      break;
    case MidiMessage::Type::PitchBend:
      services.audio_manager.submit_from_midi(
          audio::AudioCommand::pitch_bend(message.pitch_bend));
      break;
    case MidiMessage::Type::ControlChange:
      if (message.controller == 1) {
        services.audio_manager.submit_from_midi(
            audio::AudioCommand::mod_wheel(message.controller_value));
      }
      break;
    }
  });
  midi.init();
  app_context.midi = &midi;

  RuntimeContext runtime{&app_context, &midi, true};

  platform::run_main_loop(runtime, run_frame);
  services.shutdown_app();
  std::cout << "Goodbye!\n";
  return 0;
}
