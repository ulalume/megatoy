#include "midi/midi_input_manager.hpp"

#include "app_context.hpp"
#include "app_services.hpp"
#include "patches/patch_session.hpp"
#include <iostream>

MidiInputManager::MidiInputManager(std::unique_ptr<MidiBackend> backend)
    : backend_(std::move(backend)), pending_events_(), available_ports_(),
      ports_dirty_(false) {}

MidiInputManager::~MidiInputManager() { shutdown(); }

void MidiInputManager::set_note_sink(MidiBackend::NoteSink sink) {
  if (backend_) {
    backend_->set_note_sink(std::move(sink));
  }
}

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

  // Notes no longer come through here -- the backend hands them to the audio
  // thread the moment they arrive. This only keeps the port list current.
  pending_events_.clear();
}

MidiInputManager::StatusInfo MidiInputManager::status() const {
  if (!backend_) {
    return {"MIDI backend unavailable.", false, false};
  }
  return backend_->status();
}

void MidiInputManager::request_web_midi_access() {
  if (backend_) {
    backend_->request_access();
  }
}
