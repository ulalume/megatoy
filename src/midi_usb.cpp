// midi.cpp
#include "midi_usb.hpp"
#include "types.hpp"
#include "ym2612/types.hpp"
#include "ym2612/channel.hpp"
#include "ym2612/device.hpp"
#include <RtMidi.h>
#include <iostream>
#include <mutex>
#include <vector>

struct MidiInputManager::Impl {
  std::unique_ptr<RtMidiIn> midi_in;
  std::vector<unsigned char> message;
  std::mutex message_mutex;

  bool init() {
    try {
      midi_in = std::make_unique<RtMidiIn>();

      unsigned int ports = midi_in->getPortCount();
      if (ports == 0) {
        std::cout << "No MIDI input ports found.\n";
        return false;
      }

      std::cout << "Opening MIDI input port 0: "
                << midi_in->getPortName(0) << "\n";

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
    if ((status & 0xF0) == 0x90 && message[2] > 0) {
        // Note On
        int midi_note = message[1];
        uint8_t octave = static_cast<uint8_t>((midi_note / 12) - 1);
        Key key_enum = static_cast<Key>(static_cast<uint8_t>(midi_note % 12));

        ym2612::Note noteFreq(octave, key_enum);

        app_state.device()
            .channel(ym2612::ChannelIndex::Fm1)
            .write_frequency(noteFreq);

        app_state.device()
            .channel(ym2612::ChannelIndex::Fm1)
            .write_key_on();

    } else if ((status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && message[2] == 0)) {
        // Note Off
        int midi_note = message[1];
        std::cout << "Note OFF: " << midi_note << "\n";

        app_state.device()
            .channel(ym2612::ChannelIndex::Fm1)
            .write_key_off();
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

bool MidiInputManager::init() {
  return impl_->init();
}

void MidiInputManager::shutdown() {
  impl_->shutdown();
}

void MidiInputManager::poll(AppState &app_state) {
  impl_->poll(app_state);
}
