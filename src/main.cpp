#include "platform/platform_config.hpp"

#include "app_context.hpp"
#include "app_services.hpp"
#include "app_state.hpp"
#include "audio/audio_command.hpp"
#include "core/status.hpp"
#include "drop_actions.hpp"
#include "gui/envelope/envelope_curve.hpp"
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
#include <algorithm>
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
  // The envelope graphs are drawn at this note. Pushed in before the frame
  // rather than compared against the saved preferences after it, because the
  // frame that changes the setting is the last one an idle app draws: read
  // afterwards, the graph would keep the old note until something else moved.
  ui::envelope::set_reference_midi_note(
      app_state.ui_state().prefs.envelope_reference_midi_note);
  ui::render_all(*runtime.app_context);
  const auto &saved_prefs = services.preference_manager.ui_preferences();
  const auto &current_prefs = app_state.ui_state().prefs;
  if (current_prefs.ym2612_core != saved_prefs.ym2612_core) {
    services.audio_manager.set_core_type(static_cast<ym2612::CoreType>(
        std::clamp(current_prefs.ym2612_core, 0, 1)));
  }
  if (current_prefs.ym2612_chip_type != saved_prefs.ym2612_chip_type) {
    services.audio_manager.set_chip_type(static_cast<ym2612::ChipType>(
        std::clamp(current_prefs.ym2612_chip_type, 0, 1)));
  }
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
  // Reopening the device has to happen here rather than through the audio
  // command queue: that queue is drained by the device's own callback.
  if (current_prefs.audio_buffer_frames != saved_prefs.audio_buffer_frames &&
      !services.audio_manager.set_buffer_frames(
          current_prefs.audio_buffer_frames)) {
    megatoy::status::error(
        services.audio_manager.is_running()
            ? "Audio device would not reopen at that buffer size."
            : "Audio device unavailable -- megatoy is running without sound.");
    // Show the size the device is actually using.
    app_state.ui_state().prefs.audio_buffer_frames =
        services.audio_manager.buffer_frames();
  }
  services.preference_manager.set_ui_preferences(app_state.ui_state().prefs);
  services.gui_manager.end_frame();
  return true;
}

} // namespace

#if defined(MEGATOY_PLATFORM_WEB)
// The web build returns from main() with the loop still registered, so
// everything the frames touch must outlive main: page lifetime, never
// destroyed.
#define MEGATOY_APP_STORAGE static
#else
#define MEGATOY_APP_STORAGE
#endif

int main(int argc, char *argv[]) {
#if defined(MEGATOY_PLATFORM_WEB)
  MEGATOY_APP_STORAGE platform::web::WebPlatformServices platform_services;
#else
  DesktopPlatformServices platform_services;
#endif

  update::set_release_info_provider(platform_services.release_info_provider());

  MEGATOY_APP_STORAGE AppServices services(platform_services);
  MEGATOY_APP_STORAGE AppState app_state{};
  services.initialize_app(app_state);
  MEGATOY_APP_STORAGE AppContext app_context{services, app_state};

  services.gui_manager.set_drop_callback(&app_context, handle_file_drop);
  // Lambdas cannot capture statics on the web build; a reference with
  // automatic storage duration is capturable on both platforms.
  AppServices &services_ref = services;

#if defined(MEGATOY_PLATFORM_WEB)
  // Folders dragged onto the page are imported into persistent storage; SDL's
  // own drop handler cannot read directories.
  platform::web::set_drop_import_handler(
      [&services_ref](platform::web::FolderImportResult result) {
        auto &services = services_ref;
        if (result.cancelled) {
          return;
        }
        if (!result.ok) {
          megatoy::status::error("Folder drop failed: " + result.error);
          return;
        }
        if (services.preference_manager.add_workspace_folder(result.path)) {
          services.patch_session.sync_workspace();
          if (result.filtered_count == 0 &&
              result.validation_failures.empty()) {
            megatoy::status::success("Added \"" + result.folder_name + "\" (" +
                                     std::to_string(result.file_count) +
                                     " files)");
          }
        } else {
          megatoy::status::warning("\"" + result.folder_name +
                                   "\" is already in the workspace.");
        }
      });
  platform::web::install_drop_import(
      megatoy::system::PathService::web_storage_root());
#endif

  MEGATOY_APP_STORAGE MidiInputManager midi(
      platform_services.create_midi_backend());
  // Performance messages go straight from the driver's thread to the audio
  // thread, so their timing no longer depends on how fast the UI is drawing.
  midi.set_note_sink([&services_ref](const MidiMessage &message) {
    auto &services = services_ref;
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

  MEGATOY_APP_STORAGE RuntimeContext runtime{&app_context, &midi, true};

  platform::run_main_loop(runtime, run_frame);
#if defined(MEGATOY_PLATFORM_WEB)
  // The loop is registered and the app lives in static storage; the page
  // closing is the only shutdown.
  return 0;
#else
  services.shutdown_app();
  std::cout << "Goodbye!\n";
  return 0;
#endif
}
