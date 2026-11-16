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
    PitchBend,
    ControlChange,
    /// The input went away, and with it whatever it held.
    SourceClosed,
  } type = Type::NoteOff;

  /// The input it came from, 1 and up: 0 is the app's own keyboards.
  std::uint8_t source = 1;
  ym2612::Note note{};
  std::uint8_t velocity = 0;
  std::uint16_t pitch_bend = 8192;
  std::uint8_t controller = 0;
  std::uint8_t controller_value = 0;
};

/**
 * A channel voice message as megatoy takes it, from its status byte and the
 * data after it, 0 for a byte the message does not have. False for one it
 * has no use for. The channel is not kept: every channel plays the one patch.
 */
inline bool parse_midi_message(std::uint8_t status, std::uint8_t data1,
                               std::uint8_t data2, MidiMessage &message) {
  const std::uint8_t kind = status & 0xf0;
  data1 &= 0x7f;
  data2 &= 0x7f;
  switch (kind) {
  case 0x90:
    // A note-on at no velocity is a note-off.
    message.type =
        data2 > 0 ? MidiMessage::Type::NoteOn : MidiMessage::Type::NoteOff;
    message.note = ym2612::Note::from_midi_note(data1);
    message.velocity = data2;
    return true;
  case 0x80:
    message.type = MidiMessage::Type::NoteOff;
    message.note = ym2612::Note::from_midi_note(data1);
    message.velocity = data2;
    return true;
  case 0xe0:
    message.type = MidiMessage::Type::PitchBend;
    message.pitch_bend = static_cast<std::uint16_t>(
        data1 | (static_cast<std::uint16_t>(data2) << 7));
    return true;
  case 0xb0:
    message.type = MidiMessage::Type::ControlChange;
    message.controller = data1;
    message.controller_value = data2;
    return true;
  default:
    return false;
  }
}

class MidiBackend {
public:
  /**
   * Called the moment a note arrives, on whatever thread the driver delivers
   * it on -- not once per rendered frame.
   *
   * Polling for notes in the UI loop would quantize their timing to the
   * render rate and make input jitter with drawing. Set this before
   * initialize().
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
  virtual void poll(std::vector<std::string> &available_ports,
                    bool &ports_changed) = 0;

  // Optional status reporting (e.g., WebMIDI permission state).
  virtual StatusInfo status() const {
    return {"System MIDI backend active.", false, false};
  }

  // Optional permission request hook (WebMIDI).
  virtual void request_access() {}

  /// The sources inputs are numbered with: 1 up to this.
  static constexpr std::uint8_t kLastSource = 31;

protected:
  void emit(const MidiMessage &message) const {
    if (note_sink_) {
      note_sink_(message);
    }
  }

private:
  NoteSink note_sink_;
};
