#pragma once

#include "app_services.hpp"
#include "app_state.hpp"
#include <filesystem>
#include <vector>

struct AppContext {
  AppServices &services;
  AppState &state;
  AppServices &svc() { return services; }
  const AppServices &svc() const { return services; }
  AppState &app_state() { return state; }
  const AppState &app_state() const { return state; }

  InputState &input_state() { return state.input_state(); }
  UIState &ui_state() { return state.ui_state(); }

  bool note_on(ym2612::Note note, uint8_t velocity) {
    return services.patch_session.note_on(note, velocity,
                                          state.ui_state().prefs);
  }
  bool note_off(ym2612::Note note) {
    return services.patch_session.note_off(note);
  }
  bool note_is_active(const ym2612::Note &note) {
    return services.patch_session.note_is_active(note);
  }
  std::vector<ym2612::Note> active_notes() {
    return services.patch_session.active_notes();
  }

  void safe_load(const patches::PatchEntry &entry);
  void load_patch(const patches::PatchEntry &entry);
  void load_dropped_patch(const ym2612::Patch &patch,
                          const std::filesystem::path &path);

  void handle_drop(const std::filesystem::path &path);
  void apply_instrument_selection(size_t index);
  void cancel_instrument_selection();
};
