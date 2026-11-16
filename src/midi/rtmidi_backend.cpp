#include "midi/rtmidi_backend.hpp"

#include "core/status.hpp"
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
    close_connection(*connection);
  }
  connections_.clear();
  retired_.clear();
  enumerator_.reset();
  available_ports_.clear();
  synced_ports_.clear();
  waiting_for_ports_ = false;
  ports_dirty_ = false;
}

void RtMidiBackend::poll(std::vector<std::string> &available_ports,
                         bool &ports_changed) {
  if (!enumerator_) {
    if (!available_ports.empty()) {
      available_ports.clear();
      ports_changed = true;
    }
    return;
  }

  try {
    // Asking the OS for the port list is not free (CoreMIDI and WinMM both
    // allocate), and hotplug is a human-timescale event -- once a second is
    // plenty. Notes are unaffected; they arrive through per-port callbacks.
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_enumeration_) {
      next_enumeration_ = now + std::chrono::seconds(1);
      handle_port_changes();
    }

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
  connection.live.store(false, std::memory_order_release);
  if (connection.midi_in && connection.midi_in->isPortOpen()) {
    std::cout << "Closing MIDI input port: " << connection.port_name << "\n";
    connection.midi_in->cancelCallback();
    connection.midi_in->closePort();
  }
  connection.midi_in.reset();
}

void RtMidiBackend::drop_connections(const std::string &port_name) {
  for (auto it = connections_.begin(); it != connections_.end();) {
    if ((*it)->port_name != port_name) {
      ++it;
      continue;
    }
    std::cout << "MIDI input disconnected: " << port_name << "\n";
    close_connection(**it);
    MidiMessage closed;
    closed.type = MidiMessage::Type::SourceClosed;
    closed.source = (*it)->source;
    emit(closed);
    retired_.push_back(std::move(*it));
    it = connections_.erase(it);
  }
}

std::uint8_t RtMidiBackend::unused_source() const {
  for (std::uint8_t source = 1; source < kLastSource; ++source) {
    const bool taken =
        std::any_of(connections_.begin(), connections_.end(),
                    [source](const std::unique_ptr<Connection> &connection) {
                      return connection->source == source;
                    });
    if (!taken) {
      return source;
    }
  }
  return kLastSource;
}

bool RtMidiBackend::open_connection(const std::string &port_name,
                                    std::size_t ordinal) {
  auto midi = std::make_unique<RtMidiIn>();

  const unsigned int port_count = midi->getPortCount();
  int matched_index = -1;
  std::size_t seen = 0;

  for (unsigned int i = 0; i < port_count; ++i) {
    try {
      if (midi->getPortName(i) == port_name && seen++ == ordinal) {
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
    midi->ignoreTypes(true, true, true);
    // The Connection has to be in place before the callback can fire.
    auto connection = std::make_unique<Connection>();
    connection->port_name = port_name;
    connection->ordinal = ordinal;
    connection->source = unused_source();
    connection->backend = this;
    connection->midi_in = std::move(midi);
    connection->midi_in->setCallback(&RtMidiBackend::handle_message,
                                     connection.get());
    connections_.push_back(std::move(connection));
    std::cout << "Opened MIDI input port: " << port_name << "\n";
    return true;
  } catch (RtMidiError &error) {
    std::cerr << "RtMidi error while opening port '" << port_name
              << "': " << error.getMessage() << "\n";
    return false;
  }
}

/**
 * Ports are told apart by name and, among those of one name, by their place
 * in the list. A name whose count has changed has its ports closed and opened
 * again, since which of them stayed cannot be told; the others are left as
 * they are, a port that would not open included, until the list changes.
 */
void RtMidiBackend::sync_connections(const std::vector<std::string> &ports) {
  const auto count = [](const std::vector<std::string> &list,
                        const std::string &name) {
    return static_cast<std::size_t>(std::count(list.begin(), list.end(), name));
  };

  std::vector<std::string> changed;
  const auto note_change = [&](const std::string &name) {
    if (count(ports, name) != count(synced_ports_, name) &&
        std::find(changed.begin(), changed.end(), name) == changed.end()) {
      changed.push_back(name);
    }
  };
  for (const auto &name : synced_ports_) {
    note_change(name);
  }
  for (const auto &name : ports) {
    note_change(name);
  }

  for (const auto &name : changed) {
    drop_connections(name);
    const std::size_t wanted = count(ports, name);
    for (std::size_t ordinal = 0; ordinal < wanted; ++ordinal) {
      std::cout << "MIDI input connected: " << name << "\n";
      if (!open_connection(name, ordinal)) {
        megatoy::status::warning("Could not open MIDI input \"" + name +
                                 "\".");
      }
    }
  }
  synced_ports_ = ports;
}

void RtMidiBackend::handle_port_changes() {
  auto ports = enumerate_ports();
  if (ports == available_ports_) {
    return;
  }
  available_ports_ = std::move(ports);
  ports_dirty_ = true;
  sync_connections(available_ports_);

  if (available_ports_.empty() && !waiting_for_ports_) {
    std::cout << "No MIDI input ports available. Waiting for device...\n";
  }
  waiting_for_ports_ = available_ports_.empty();
}

void RtMidiBackend::handle_message(double, std::vector<unsigned char> *message,
                                   void *user_data) {
  // Runs on RtMidi's own thread, one per open port. Notes go straight to the
  // sink instead of waiting to be picked up by the next rendered frame.
  auto *connection = static_cast<Connection *>(user_data);
  if (connection == nullptr || message == nullptr || message->size() < 2 ||
      !connection->live.load(std::memory_order_acquire)) {
    return;
  }

  MidiMessage event;
  event.source = connection->source;
  if (parse_midi_message((*message)[0], (*message)[1],
                         message->size() >= 3 ? (*message)[2] : 0, event)) {
    connection->backend->emit(event);
  }
}
