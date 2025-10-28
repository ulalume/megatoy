#include "midi/midi_input_manager.hpp"
#include "app_context.hpp"
#include "ym2612/note.hpp"
#include <RtMidi.h>
#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

struct MidiEvent {
  enum class Type {
    NoteOn,
    NoteOff,
  } type;

  ym2612::Note note;
  uint8_t velocity = 0;
  std::string port_name;
};

} // namespace

struct MidiInputManager::Impl {
  struct Connection {
    std::unique_ptr<RtMidiIn> midi_in;
    std::vector<unsigned char> message;
    std::string port_name;
  };

  std::unique_ptr<RtMidiIn> enumerator;
  std::vector<Connection> connections;
  std::vector<std::string> available_ports;
  bool waiting_for_ports = false;
  bool ports_dirty = false;
  std::vector<MidiEvent> pending_events;

  std::vector<std::string> enumerate_ports() {
    std::vector<std::string> names;
    if (!enumerator) {
      return names;
    }

    const unsigned int port_count = enumerator->getPortCount();
    names.reserve(port_count);
    for (unsigned int i = 0; i < port_count; ++i) {
      try {
        names.emplace_back(enumerator->getPortName(i));
      } catch (RtMidiError &error) {
        std::cerr << "RtMidi error while enumerating port " << i << ": "
                  << error.getMessage() << "\n";
      }
    }
    return names;
  }

  void close_connection(Connection &connection) {
    if (connection.midi_in && connection.midi_in->isPortOpen()) {
      std::cout << "Closing MIDI input port: " << connection.port_name << "\n";
      connection.midi_in->closePort();
    }
  }

  bool open_connection(const std::string &port_name) {
    auto midi = std::make_unique<RtMidiIn>();

    const unsigned int port_count = midi->getPortCount();
    int matched_index = -1;

    for (unsigned int i = 0; i < port_count; ++i) {
      try {
        if (midi->getPortName(i) == port_name) {
          matched_index = static_cast<int>(i);
          break;
        }
      } catch (RtMidiError &error) {
        std::cerr << "RtMidi error while matching port '" << port_name
                  << "': " << error.getMessage() << "\n";
        return false;
      }
    }

    if (matched_index < 0) {
      std::cerr << "MIDI input port not found while opening: " << port_name
                << "\n";
      return false;
    }

    try {
      midi->openPort(static_cast<unsigned int>(matched_index));
      midi->ignoreTypes(false, false, false);
      connections.push_back(Connection{std::move(midi), {}, port_name});
      std::cout << "Opened MIDI input port: " << port_name << "\n";
      return true;
    } catch (RtMidiError &error) {
      std::cerr << "RtMidi error while opening port '" << port_name
                << "': " << error.getMessage() << "\n";
      return false;
    }
  }

  void sync_connections(const std::vector<std::string> &ports) {
    for (auto it = connections.begin(); it != connections.end();) {
      if (std::find(ports.begin(), ports.end(), it->port_name) == ports.end()) {
        std::cout << "MIDI input disconnected: " << it->port_name << "\n";
        close_connection(*it);
        it = connections.erase(it);
      } else {
        ++it;
      }
    }

    for (const auto &port : ports) {
      const bool already_open = std::any_of(
          connections.begin(), connections.end(),
          [&](const Connection &conn) { return conn.port_name == port; });

      if (!already_open) {
        std::cout << "MIDI input connected: " << port << "\n";
        open_connection(port);
      }
    }
  }

  void handle_port_changes() {
    if (!enumerator) {
      if (!available_ports.empty()) {
        available_ports.clear();
        ports_dirty = true;
      }
      return;
    }

    const auto ports = enumerate_ports();

    const bool had_ports = !available_ports.empty();
    available_ports = ports;
    ports_dirty = true;

    if (available_ports.empty()) {
      if (had_ports) {
        for (auto &connection : connections) {
          std::cout << "MIDI input disconnected: " << connection.port_name
                    << "\n";
          close_connection(connection);
        }
        connections.clear();
      }

      if (!waiting_for_ports) {
        std::cout << "No MIDI input ports available. Waiting for device...\n";
        waiting_for_ports = true;
      }
      return;
    }

    waiting_for_ports = false;
    sync_connections(available_ports);
  }

  void collect_events() {
    if (connections.empty()) {
      return;
    }

    for (auto &connection : connections) {
      while (true) {
        connection.message.clear();
        double stamp = connection.midi_in->getMessage(&connection.message);
        (void)stamp;

        if (connection.message.empty()) {
          break;
        }

        if (connection.message.size() < 2) {
          continue;
        }

        const uint8_t status = connection.message[0];
        const uint8_t status_type = status & 0xF0;
        const uint8_t midi_note_value = connection.message[1];
        const uint8_t velocity =
            (connection.message.size() >= 3) ? connection.message[2] : 0;

        const auto note = ym2612::Note::from_midi_note(midi_note_value);

        const bool is_note_off =
            status_type == 0x80 || (status_type == 0x90 && velocity == 0);
        const bool is_note_on = status_type == 0x90 && velocity > 0;

        if (is_note_on) {
          pending_events.push_back(
              {MidiEvent::Type::NoteOn, note, velocity, connection.port_name});
        } else if (is_note_off) {
          pending_events.push_back(
              {MidiEvent::Type::NoteOff, note, velocity, connection.port_name});
        }
      }
    }
  }

  bool init() {
    try {
      enumerator = std::make_unique<RtMidiIn>();

      available_ports = enumerate_ports();

      if (available_ports.empty()) {
        waiting_for_ports = true;
        std::cout
            << "No MIDI input ports found. Waiting for device connection...\n";
        ports_dirty = true;
        return true;
      }

      sync_connections(available_ports);
      ports_dirty = true;
      return true;
    } catch (RtMidiError &error) {
      std::cerr << "RtMidi error: " << error.getMessage() << "\n";
      return false;
    }
  }

  void poll() {
    if (!enumerator) {
      return;
    }

    try {
      handle_port_changes();
      collect_events();
    } catch (RtMidiError &error) {
      std::cerr << "RtMidi error while polling MIDI inputs: "
                << error.getMessage() << "\n";
    }
  }

  void dispatch(AppContext &context) {
    if (ports_dirty) {
      context.app_state().set_connected_midi_inputs(available_ports);
      ports_dirty = false;
    }

    for (const auto &event : pending_events) {
      switch (event.type) {
      case MidiEvent::Type::NoteOn:
        if (!context.note_on(event.note, event.velocity)) {
          std::clog
              << "MIDI note-on ignored (no free channel or already active): "
              << event.note << " velocity " << static_cast<int>(event.velocity)
              << " (" << event.port_name << ")\n";
        }
        break;
      case MidiEvent::Type::NoteOff:
        if (!context.note_off(event.note)) {
          std::clog << "MIDI note-off ignored (note not active): " << event.note
                    << " (" << event.port_name << ")\n";
        }
        break;
      }
    }

    pending_events.clear();
  }

  void shutdown() {
    for (auto &connection : connections) {
      close_connection(connection);
    }
    connections.clear();
    enumerator.reset();
    available_ports.clear();
    waiting_for_ports = false;
    ports_dirty = false;
    pending_events.clear();
  }
};

MidiInputManager::MidiInputManager() : impl_(std::make_unique<Impl>()) {}
MidiInputManager::~MidiInputManager() { impl_->shutdown(); }

bool MidiInputManager::init() { return impl_->init(); }

void MidiInputManager::shutdown() { impl_->shutdown(); }

void MidiInputManager::poll() { impl_->poll(); }

void MidiInputManager::dispatch(AppContext &context) {
  impl_->dispatch(context);
}
