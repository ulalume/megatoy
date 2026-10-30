// midi_input_manager.hpp
#pragma once

#include "midi/midi_backend.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

class MidiInputManager {
public:
  using StatusInfo = MidiBackend::StatusInfo;

  explicit MidiInputManager(std::unique_ptr<MidiBackend> backend);
  ~MidiInputManager();

  /**
   * @param sink Receives notes as they arrive, on the driver's thread.
   *             Install before init() so no event is missed.
   */
  void set_note_sink(MidiBackend::NoteSink sink);

  bool init();
  void shutdown();

  void poll();
  /// The port list, when it has changed since the last call. The backend
  /// sends notes directly to the audio thread.
  std::optional<std::vector<std::string>> take_changed_ports();

  StatusInfo status() const;
  void request_web_midi_access();

private:
  std::unique_ptr<MidiBackend> backend_;
  std::vector<MidiMessage> pending_events_;
  std::vector<std::string> available_ports_;
  bool ports_dirty_;
};
