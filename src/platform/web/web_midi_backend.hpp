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

  struct StatusInfo {
    State state = State::Unavailable;
    std::string message;
  };

  WebMidiBackend() = default;
  ~WebMidiBackend() override = default;

  bool initialize() override;
  void shutdown() override;
  void poll(std::vector<MidiMessage> &events,
            std::vector<std::string> &available_ports,
            bool &ports_changed) override;

  StatusInfo status() const;
  void request_access();

private:
  void setup_js_state() const;
  StatusInfo read_status_from_js() const;
};

} // namespace platform::web
