#pragma once

#include "app_state.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <vector>

namespace input {

inline bool note_on(AppState &app_state, ym2612::Note note, uint8_t velocity) {
  auto &prefs = app_state.ui_state().prefs;
  const bool success =
      app_state.patch_session().note_on(note, velocity, prefs);
  if (success) {
    const uint8_t clamped_velocity =
        std::min<uint8_t>(velocity, static_cast<uint8_t>(127));
    const uint8_t effective_velocity =
        prefs.use_velocity ? clamped_velocity : static_cast<uint8_t>(127);
    std::cout << "Key ON - " << note << " (velocity "
              << static_cast<int>(effective_velocity) << ")\n"
              << std::flush;
  }
  return success;
}

inline bool note_off(AppState &app_state, ym2612::Note note) {
  if (app_state.patch_session().note_off(note)) {
    std::cout << "Key OFF - " << note << "\n" << std::flush;
    return true;
  }
  return false;
}

inline bool note_is_pressed(AppState &app_state, const ym2612::Note &note) {
  return app_state.patch_session().note_is_active(note);
}

inline std::vector<ym2612::Note> active_notes(AppState &app_state) {
  return app_state.patch_session().active_notes();
}

} // namespace input
