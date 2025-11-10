#include "midi/rtmidi_backend.hpp"

#include <algorithm>
#include <iostream>

RtMidiBackend::RtMidiBackend()
    : enumerator_(), connections_(), available_ports_(),
      waiting_for_ports_(false), ports_dirty_(false) {}

RtMidiBackend::~RtMidiBackend() { shutdown(); }

bool RtMidiBackend::initialize() {
  try {
    enumerator_ = std::make_unique<RtMidiIn>();
    available_ports_ = enumerate_ports();
    if (available_ports_.empty()) {
      waiting_for_ports_ = true;
      ports_dirty_ = true;
      std::cout
          << "No MIDI input ports found. Waiting for device connection...\n";
      return true;
    }

    sync_connections(available_ports_);
    ports_dirty_ = true;
    waiting_for_ports_ = false;
    return true;
  } catch (RtMidiError &error) {
    std::cerr << "RtMidi error: " << error.getMessage() << "\n";
    return false;
  }
}

void RtMidiBackend::shutdown() {
  for (auto &connection : connections_) {
    close_connection(connection);
  }
  connections_.clear();
  enumerator_.reset();
  available_ports_.clear();
  waiting_for_ports_ = false;
  ports_dirty_ = false;
}

void RtMidiBackend::poll(std::vector<MidiMessage> &events,
                         std::vector<std::string> &available_ports,
                         bool &ports_changed) {
  if (!enumerator_) {
    if (!available_ports.empty()) {
      available_ports.clear();
      ports_changed = true;
    }
    return;
  }

  try {
    handle_port_changes();
    collect_events(events);

    if (ports_dirty_) {
      available_ports = available_ports_;
      ports_changed = true;
      ports_dirty_ = false;
    }
  } catch (RtMidiError &error) {
    std::cerr << "RtMidi error while polling MIDI inputs: "
              << error.getMessage() << "\n";
  }
}

std::vector<std::string> RtMidiBackend::enumerate_ports() {
  std::vector<std::string> names;
  if (!enumerator_) {
    return names;
  }

  const unsigned int port_count = enumerator_->getPortCount();
  names.reserve(port_count);
  for (unsigned int i = 0; i < port_count; ++i) {
    try {
      names.emplace_back(enumerator_->getPortName(i));
    } catch (RtMidiError &error) {
      std::cerr << "RtMidi error while enumerating port " << i << ": "
                << error.getMessage() << "\n";
    }
  }
  return names;
}

void RtMidiBackend::close_connection(Connection &connection) {
  if (connection.midi_in && connection.midi_in->isPortOpen()) {
    std::cout << "Closing MIDI input port: " << connection.port_name << "\n";
    connection.midi_in->closePort();
  }
}

bool RtMidiBackend::open_connection(const std::string &port_name) {
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
    connections_.push_back(Connection{std::move(midi), {}, port_name});
    std::cout << "Opened MIDI input port: " << port_name << "\n";
    return true;
  } catch (RtMidiError &error) {
    std::cerr << "RtMidi error while opening port '" << port_name
              << "': " << error.getMessage() << "\n";
    return false;
  }
}

void RtMidiBackend::sync_connections(const std::vector<std::string> &ports) {
  for (auto it = connections_.begin(); it != connections_.end();) {
    if (std::find(ports.begin(), ports.end(), it->port_name) == ports.end()) {
      std::cout << "MIDI input disconnected: " << it->port_name << "\n";
      close_connection(*it);
      it = connections_.erase(it);
    } else {
      ++it;
    }
  }

  for (const auto &port : ports) {
    const bool already_open = std::any_of(
        connections_.begin(), connections_.end(),
        [&](const Connection &conn) { return conn.port_name == port; });

    if (!already_open) {
      std::cout << "MIDI input connected: " << port << "\n";
      open_connection(port);
    }
  }
}

void RtMidiBackend::handle_port_changes() {
  auto ports = enumerate_ports();

  const bool had_ports = !available_ports_.empty();
  available_ports_ = std::move(ports);
  ports_dirty_ = true;

  if (available_ports_.empty()) {
    if (had_ports) {
      for (auto &connection : connections_) {
        std::cout << "MIDI input disconnected: " << connection.port_name
                  << "\n";
        close_connection(connection);
      }
      connections_.clear();
    }

    if (!waiting_for_ports_) {
      std::cout << "No MIDI input ports available. Waiting for device...\n";
      waiting_for_ports_ = true;
    }
    return;
  }

  waiting_for_ports_ = false;
  sync_connections(available_ports_);
}

void RtMidiBackend::collect_events(std::vector<MidiMessage> &events) {
  if (connections_.empty()) {
    return;
  }

  for (auto &connection : connections_) {
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

      const std::uint8_t status = connection.message[0];
      const std::uint8_t status_type = status & 0xF0;
      const std::uint8_t midi_note_value = connection.message[1];
      const std::uint8_t velocity =
          (connection.message.size() >= 3) ? connection.message[2] : 0;

      const auto note = ym2612::Note::from_midi_note(midi_note_value);

      const bool is_note_off =
          status_type == 0x80 || (status_type == 0x90 && velocity == 0);
      const bool is_note_on = status_type == 0x90 && velocity > 0;

      if (is_note_on) {
        events.push_back(
            {MidiMessage::Type::NoteOn, note, velocity, connection.port_name});
      } else if (is_note_off) {
        events.push_back(
            {MidiMessage::Type::NoteOff, note, velocity, connection.port_name});
      }
    }
  }
}
