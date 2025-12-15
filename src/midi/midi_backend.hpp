#pragma once

#include "ym2612/note.hpp"
#include <cstdint>
#include <string>
#include <vector>

struct MidiMessage {
  enum class Type {
    NoteOn,
    NoteOff,
  } type;

  ym2612::Note note;
  std::uint8_t velocity = 0;
  std::string port_name;
};

class MidiBackend {
public:
  struct StatusInfo {
    std::string message;
    bool show_enable_button = false;
    bool enable_button_disabled = false;
  };

  virtual ~MidiBackend() = default;

  virtual bool initialize() = 0;
  virtual void shutdown() = 0;

  virtual void poll(std::vector<MidiMessage> &events,
                    std::vector<std::string> &available_ports,
                    bool &ports_changed) = 0;

   // Optional status reporting (e.g., WebMIDI permission state).
  virtual StatusInfo status() const {
    return {"System MIDI backend active.", false, false};
  }

  // Optional permission request hook (WebMIDI).
  virtual void request_access() {}
};
