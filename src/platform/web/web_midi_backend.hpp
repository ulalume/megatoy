#pragma once

#include "midi/midi_backend.hpp"

namespace platform::web {

class WebMidiBackend : public MidiBackend {
public:
  WebMidiBackend() = default;
  ~WebMidiBackend() override = default;

  bool initialize() override { return true; }
  void shutdown() override {}
  void poll(std::vector<MidiMessage> &events,
            std::vector<std::string> &available_ports,
            bool &ports_changed) override {
    (void)events;
    if (!available_ports.empty()) {
      available_ports.clear();
      ports_changed = true;
    }
  }
};

} // namespace platform::web
