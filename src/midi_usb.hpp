// midi_usb.hpp
#pragma once

#include <memory>

class AppState;

class MidiInputManager {
public:
  MidiInputManager();
  ~MidiInputManager();

  bool init();
  void shutdown();

  void poll(AppState &app_state); // Call each frame

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
