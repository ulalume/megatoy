#pragma once

#include "midi/midi_backend.hpp"
#include <string>

namespace platform::web {

class WebMidiBackend : public MidiBackend {
public:
  enum class State {
    Unavailable,
    NeedsPermission,
    Pending,
    Enabled,
    Error,
  };

  WebMidiBackend() = default;
  ~WebMidiBackend() override = default;

  bool initialize() override;
  void shutdown() override;
  void poll(std::vector<MidiMessage> &events,
            std::vector<std::string> &available_ports,
            bool &ports_changed) override;

  MidiBackend::StatusInfo status() const override;
  void request_access() override;

  /// Called from JS the moment a MIDI message arrives.
  void deliver(unsigned char status, unsigned char note,
               unsigned char velocity);

private:
  void setup_js_state() const;
  struct WebStatus {
    State state = State::Unavailable;
    std::string message;
  };
  WebStatus read_status_from_js() const;
};

} // namespace platform::web
