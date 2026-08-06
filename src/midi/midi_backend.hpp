#pragma once

#include "ym2612/note.hpp"
#include <cstdint>
#include <functional>
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
  /**
   * Called the moment a note arrives, on whatever thread the driver delivers
   * it on -- not once per rendered frame.
   *
   * Polling for notes in the UI loop quantized their timing to the render
   * rate and made input jitter with drawing. Set this before initialize().
   */
  using NoteSink = std::function<void(const MidiMessage &)>;

  void set_note_sink(NoteSink sink) { note_sink_ = std::move(sink); }

  struct StatusInfo {
    std::string message;
    bool show_enable_button = false;
    bool enable_button_disabled = false;
  };

  virtual ~MidiBackend() = default;

  virtual bool initialize() = 0;
  virtual void shutdown() = 0;

  /// Port bookkeeping only; notes are delivered through the sink.
  virtual void poll(std::vector<MidiMessage> &events,
                    std::vector<std::string> &available_ports,
                    bool &ports_changed) = 0;

  // Optional status reporting (e.g., WebMIDI permission state).
  virtual StatusInfo status() const {
    return {"System MIDI backend active.", false, false};
  }

  // Optional permission request hook (WebMIDI).
  virtual void request_access() {}

protected:
  void emit(const MidiMessage &message) const {
    if (note_sink_) {
      note_sink_(message);
    }
  }

private:
  NoteSink note_sink_;
};
