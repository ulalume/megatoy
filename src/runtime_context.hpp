#pragma once

#include "platform/frame_scheduler.hpp"

struct AppContext;
class MidiInputManager;

struct RuntimeContext {
  AppContext *app_context = nullptr;
  MidiInputManager *midi = nullptr;
  bool running = true;
  platform::FrameScheduler frame_scheduler;
};
