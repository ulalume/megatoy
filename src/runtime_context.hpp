#pragma once

struct AppContext;
class MidiInputManager;

struct RuntimeContext {
  AppContext *app_context = nullptr;
  MidiInputManager *midi = nullptr;
  bool running = true;
};
