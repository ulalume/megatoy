// midi_input_manager.hpp
#pragma once

#include <memory>

class AppState;

class MidiInputManager {
public:
  MidiInputManager();
  ~MidiInputManager();

  bool init();
  void shutdown();

  void poll();
  void dispatch(AppState &app_state);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
