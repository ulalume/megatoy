// midi_input_manager.hpp
#pragma once

#include "midi/midi_backend.hpp"
#include <memory>
#include <string>
#include <vector>

struct AppContext;

class MidiInputManager {
public:
  struct StatusInfo {
    std::string message;
    bool show_enable_button = false;
    bool enable_button_disabled = false;
  };

  explicit MidiInputManager(std::unique_ptr<MidiBackend> backend);
  ~MidiInputManager();

  bool init();
  void shutdown();

  void poll();
  void dispatch(AppContext &context);

  StatusInfo status() const;
  void request_web_midi_access();

private:
  std::unique_ptr<MidiBackend> backend_;
  std::vector<MidiMessage> pending_events_;
  std::vector<std::string> available_ports_;
  bool ports_dirty_;
};
