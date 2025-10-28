// midi_input_manager.hpp
#pragma once

#include <memory>

struct AppContext;

class MidiInputManager {
public:
  MidiInputManager();
  ~MidiInputManager();

  bool init();
  void shutdown();

  void poll();
  void dispatch(AppContext &context);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
