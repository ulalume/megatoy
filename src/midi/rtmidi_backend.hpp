#pragma once

#include "midi/midi_backend.hpp"
#include <RtMidi.h>
#include <memory>
#include <vector>

class RtMidiBackend : public MidiBackend {
public:
  RtMidiBackend();
  ~RtMidiBackend() override;

  RtMidiBackend(const RtMidiBackend &) = delete;
  RtMidiBackend &operator=(const RtMidiBackend &) = delete;
  RtMidiBackend(RtMidiBackend &&) = delete;
  RtMidiBackend &operator=(RtMidiBackend &&) = delete;

  bool initialize() override;
  void shutdown() override;
  void poll(std::vector<MidiMessage> &events,
            std::vector<std::string> &available_ports,
            bool &ports_changed) override;

private:
  struct Connection {
    std::unique_ptr<RtMidiIn> midi_in;
    std::vector<unsigned char> message;
    std::string port_name;
  };

  std::vector<std::string> enumerate_ports();
  void close_connection(Connection &connection);
  bool open_connection(const std::string &port_name);
  void sync_connections(const std::vector<std::string> &ports);
  void handle_port_changes();
  void collect_events(std::vector<MidiMessage> &events);

  std::unique_ptr<RtMidiIn> enumerator_;
  std::vector<Connection> connections_;
  std::vector<std::string> available_ports_;
  bool waiting_for_ports_;
  bool ports_dirty_;
};
