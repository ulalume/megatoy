// midi.cpp
#include "midi_usb.hpp"
#include "channel_allocator.hpp"
#include "types.hpp"
#include "ym2612/channel.hpp"
#include "ym2612/device.hpp"
#include "ym2612/types.hpp"
#include <RtMidi.h>
#include <iostream>
#include <mutex>
#include <vector>

struct MidiInputManager::Impl {
  std::unique_ptr<RtMidiIn> midi_in;
  std::vector<unsigned char> message;
  std::mutex message_mutex;
  ChannelAllocator channel_allocator;

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

    double stamp;
    message.clear();

    stamp = midi_in->getMessage(&message);
    if (message.empty())
      return;

    unsigned char status = message[0];
    int midi_note = message[1];

    uint8_t octave = static_cast<uint8_t>((midi_note / 12) - 1);
    Key key_enum = static_cast<Key>(static_cast<uint8_t>(midi_note % 12));
    ym2612::Note noteFreq(octave, key_enum);

    if ((status & 0xF0) == 0x90 && message[2] > 0) {
      // Note On
      bool success = channel_allocator.note_on(noteFreq, app_state.device());
      if (!success) {
        std::cout << "No free channels available for note " << midi_note
                  << "\n";
      } else {
        std::cout << "Note ON " << midi_note << "\n";
      }
    } else if ((status & 0xF0) == 0x80 ||
               ((status & 0xF0) == 0x90 && message[2] == 0)) {
      // Note Off
      bool success = channel_allocator.note_off(noteFreq, app_state.device());
      if (success) {
        std::cout << "Note OFF " << midi_note << "\n";
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
