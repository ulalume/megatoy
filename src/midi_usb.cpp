// midi_usb.cpp
#include "midi_usb.hpp"
#include "app_state.hpp"
#include "ym2612/note.hpp"
#include <RtMidi.h>
#include <iostream>
#include <vector>

struct MidiInputManager::Impl {
  std::unique_ptr<RtMidiIn> midi_in;
  std::vector<unsigned char> message;

  bool init() {
    try {
      midi_in = std::make_unique<RtMidiIn>();

      unsigned int ports = midi_in->getPortCount();
      if (ports == 0) {
        std::cout << "No MIDI input ports found.\n";
        return false;
      }

      std::cout << "Opening MIDI input port 0: " << midi_in->getPortName(0)
                << "\n";

      midi_in->openPort(0);
      midi_in->ignoreTypes(false, false, false);
      return true;
    } catch (RtMidiError &error) {
      std::cerr << "RtMidi error: " << error.getMessage() << "\n";
      return false;
    }
  }

  void poll(AppState &app_state) {
    if (!midi_in)
      return;

    while (true) {
      message.clear();
      double stamp = midi_in->getMessage(&message);
      (void)stamp; // timestamp unused for now

      if (message.empty())
        break;

      if (message.size() < 2)
        continue;

      const uint8_t status = message[0];
      const uint8_t status_type = status & 0xF0;
      const uint8_t midi_note_value = message[1];
      const uint8_t velocity = (message.size() >= 3) ? message[2] : 0;

      const auto note = ym2612::Note::from_midi_note(midi_note_value);

      const bool is_note_off =
          status_type == 0x80 || (status_type == 0x90 && velocity == 0);
      const bool is_note_on = status_type == 0x90 && velocity > 0;

      if (is_note_on) {
        if (!app_state.key_on(note)) {
          std::clog
              << "MIDI note-on ignored (no free channel or already active): "
              << static_cast<int>(midi_note_value) << "\n";
        }
      } else if (is_note_off) {
        if (!app_state.key_off(note)) {
          std::clog << "MIDI note-off ignored (note not active): "
                    << static_cast<int>(midi_note_value) << "\n";
        }
      }
    }
  }

  void shutdown() {
    if (midi_in && midi_in->isPortOpen()) {
      midi_in->closePort();
    }
  }
};

MidiInputManager::MidiInputManager() : impl_(std::make_unique<Impl>()) {}
MidiInputManager::~MidiInputManager() { impl_->shutdown(); }

bool MidiInputManager::init() { return impl_->init(); }

void MidiInputManager::shutdown() { impl_->shutdown(); }

void MidiInputManager::poll(AppState &app_state) { impl_->poll(app_state); }
