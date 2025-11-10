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
  virtual ~MidiBackend() = default;

  virtual bool initialize() = 0;
  virtual void shutdown() = 0;

  virtual void poll(std::vector<MidiMessage> &events,
                    std::vector<std::string> &available_ports,
                    bool &ports_changed) = 0;
};
