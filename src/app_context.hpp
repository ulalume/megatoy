#pragma once

#include "app_state.hpp"

struct AppServices;

struct AppContext {
  AppServices &services;
  AppState &state;
  AppState &app_state() { return state; }
  const AppState &app_state() const { return state; }

  InputState &input_state() { return state.input_state(); }
  UIState &ui_state() { return state.ui_state(); }
};
