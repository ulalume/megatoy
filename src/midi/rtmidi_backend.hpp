#pragma once

#include "midi/midi_backend.hpp"
#include <RtMidi.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
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
  void poll(std::vector<std::string> &available_ports,
            bool &ports_changed) override;

private:
  /**
   * One open port. RtMidi's thread is handed a pointer to it, and may still
   * be in the callback as the port is closed, so a Connection stays where it
   * is for as long as the backend does: closing one clears `live` and leaves
   * the rest to shutdown().
   */
  struct Connection {
    std::unique_ptr<RtMidiIn> midi_in;
    std::string port_name;
    /// Which of the ports of that name, in the order the system lists them.
    std::size_t ordinal = 0;
    std::uint8_t source = 1;
    std::atomic<bool> live{true};
    RtMidiBackend *backend = nullptr;
  };

  static void handle_message(double timestamp,
                             std::vector<unsigned char> *message,
                             void *user_data);

  std::vector<std::string> enumerate_ports();
  void close_connection(Connection &connection);
  /// Close every open port of that name, telling the sink each is gone.
  void drop_connections(const std::string &port_name);
  bool open_connection(const std::string &port_name, std::size_t ordinal);
  std::uint8_t unused_source() const;
  void sync_connections(const std::vector<std::string> &ports);
  void handle_port_changes();

  std::unique_ptr<RtMidiIn> enumerator_;
  std::chrono::steady_clock::time_point next_enumeration_{};
  std::vector<std::unique_ptr<Connection>> connections_;
  /// Closed, and kept for a callback that may still be reading one.
  std::vector<std::unique_ptr<Connection>> retired_;
  std::vector<std::string> available_ports_;
  /// The ports the connections were last matched to.
  std::vector<std::string> synced_ports_;
  bool waiting_for_ports_;
  bool ports_dirty_;
};
