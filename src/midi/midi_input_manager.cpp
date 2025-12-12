#include "midi/midi_input_manager.hpp"

#include "app_context.hpp"
#include "app_services.hpp"
#include "platform/platform_config.hpp"
#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/web_midi_backend.hpp"
#endif
#include "patches/patch_session.hpp"
#include <iostream>

MidiInputManager::MidiInputManager(std::unique_ptr<MidiBackend> backend)
    : backend_(std::move(backend)), pending_events_(), available_ports_(),
      ports_dirty_(false) {}

MidiInputManager::~MidiInputManager() { shutdown(); }

bool MidiInputManager::init() {
  if (!backend_) {
    std::cerr << "No MIDI backend available\n";
    return false;
  }
  return backend_->initialize();
}

void MidiInputManager::shutdown() {
  if (backend_) {
    backend_->shutdown();
  }
  pending_events_.clear();
  available_ports_.clear();
  ports_dirty_ = false;
}

void MidiInputManager::poll() {
  if (!backend_) {
    return;
  }

  pending_events_.clear();
  bool ports_changed = false;
  backend_->poll(pending_events_, available_ports_, ports_changed);
  ports_dirty_ = ports_dirty_ || ports_changed;
}

void MidiInputManager::dispatch(AppContext &context) {
  if (ports_dirty_) {
    context.app_state().set_connected_midi_inputs(available_ports_);
    ports_dirty_ = false;
  }

  for (const auto &event : pending_events_) {
    switch (event.type) {
    case MidiMessage::Type::NoteOn:
      if (!context.services.patch_session.note_on(event.note, event.velocity,
                                                  context.ui_state().prefs)) {
        std::clog
            << "MIDI note-on ignored (no free channel or already active): "
            << event.note << " velocity " << static_cast<int>(event.velocity)
            << " (" << event.port_name << ")\n";
      }
      break;
    case MidiMessage::Type::NoteOff:
      if (!context.services.patch_session.note_off(event.note)) {
        std::clog << "MIDI note-off ignored (note not active): " << event.note
                  << " (" << event.port_name << ")\n";
      }
      break;
    }
  }

  pending_events_.clear();
}

MidiInputManager::StatusInfo MidiInputManager::status() const {
  StatusInfo info;
#if defined(MEGATOY_PLATFORM_WEB)
  if (auto *web =
          dynamic_cast<platform::web::WebMidiBackend *>(backend_.get())) {
    auto backend_status = web->status();
    info.message = backend_status.message;
    info.show_enable_button =
        backend_status.state ==
            platform::web::WebMidiBackend::State::NeedsPermission ||
        backend_status.state == platform::web::WebMidiBackend::State::Error;
    info.enable_button_disabled =
        backend_status.state == platform::web::WebMidiBackend::State::Pending;
    return info;
  }
#endif
  info.message =
      backend_ ? "System MIDI backend active." : "MIDI backend unavailable.";
  return info;
}

void MidiInputManager::request_web_midi_access() {
#if defined(MEGATOY_PLATFORM_WEB)
  if (auto *web =
          dynamic_cast<platform::web::WebMidiBackend *>(backend_.get())) {
    web->request_access();
  }
#endif
}
