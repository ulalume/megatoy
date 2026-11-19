#include "keyboard_typing.hpp"
#include "typing_keyboard_layout.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <utility>
#include <vector>

namespace ui {

namespace {

constexpr std::array<int, 7> white_key_offsets{0, 2, 4, 5, 7, 9, 11};

std::vector<size_t> non_chromatic_indices(size_t max_notes) {
  std::vector<size_t> indices;
  for (size_t octave = 0;; ++octave) {
    bool added = false;
    for (int offset : white_key_offsets) {
      const size_t index = octave * 12 + static_cast<size_t>(offset);
      if (index >= max_notes) {
        return indices;
      }
      indices.push_back(index);
      added = true;
    }
    if (!added) {
      break;
    }
  }
  return indices;
}

/// The places the scale's notes go on, in order: the keyboard's for the
/// chromatic scale, else those of its white notes, in either case up to the
/// first with no key.
TypingLayout places_for_scale(Scale scale, const TypingLayout &keyboard) {
  TypingLayout places;
  const auto take = [&](std::size_t index) {
    if (keyboard[index] == SDL_SCANCODE_UNKNOWN) {
      return false;
    }
    places.push_back(keyboard[index]);
    return true;
  };
  if (scale == Scale::CHROMATIC) {
    for (std::size_t index = 0; index < keyboard.size() && take(index);
         ++index) {
    }
    return places;
  }
  for (const auto index : non_chromatic_indices(keyboard.size())) {
    if (!take(index)) {
      break;
    }
  }
  return places;
}

/// The places held when typing was last checked.
std::map<SDL_Scancode, bool> key_pressed_last_frame;

/// Set by keep_typing_quiet() until typing is next checked.
bool quiet_at_next_check = false;

/// Moves `octave` a step, within 0..7, as either of its keys goes down. Kept
/// `quiet`, it only sees the keys go down.
void step_octave(const std::function<bool(SDL_Scancode)> &is_down,
                 SDL_Scancode down, SDL_Scancode up, uint8_t &octave,
                 bool quiet) {
  const auto went_down = [&is_down](SDL_Scancode place) {
    if (place == SDL_SCANCODE_UNKNOWN) {
      return false;
    }
    const bool pressed_now = is_down(place);
    const bool was_pressed =
        std::exchange(key_pressed_last_frame[place], pressed_now);
    return pressed_now && !was_pressed;
  };
  if (went_down(down) && !quiet && octave > 0) {
    octave--;
  }
  if (went_down(up) && !quiet && octave < 7) {
    octave++;
  }
}

bool held_in_sdl(SDL_Scancode place) {
  int count = 0;
  const bool *held = SDL_GetKeyboardState(&count);
  return place < count && held[place];
}

} // namespace

std::map<SDL_Scancode, ym2612::Note>
create_key_mappings(Scale scale, Key key, uint8_t selected_octave,
                    const TypingLayout &keyboard) {
  const auto places = places_for_scale(scale, keyboard);
  const auto notes = keys_from_scale_and_key(scale, key);

  std::map<SDL_Scancode, ym2612::Note> key_mappings;
  for (std::size_t i = 0; i < places.size(); ++i) {
    const Key note = notes[i % notes.size()];
    // The scale has come round to a lower note: the next octave begins.
    if (i != 0 && notes[(i - 1) % notes.size()] > note) {
      selected_octave++;
    }
    key_mappings.insert({places[i], {selected_octave, note}});
  }
  return key_mappings;
}

void keep_typing_quiet() { quiet_at_next_check = true; }

void check_keyboard_typing(
    KeyboardTypingContext &context,
    const std::map<SDL_Scancode, ym2612::Note> &key_mappings) {
  auto &input = context.input_state;
  auto &held = input.active_keyboard_notes;
  const std::function<bool(SDL_Scancode)> is_down =
      context.is_down ? context.is_down : held_in_sdl;

  const bool quiet = std::exchange(quiet_at_next_check, false);

  step_octave(is_down, context.octave_down_key, context.octave_up_key,
              input.keyboard_typing_octave, quiet);
  step_octave(is_down, context.upper_octave_down_key,
              context.upper_octave_up_key, input.keyboard_typing_upper_octave,
              quiet);

  const auto another_key_holds = [&held](const ym2612::Note &note) {
    return std::any_of(held.begin(), held.end(), [&note](const auto &entry) {
      return entry.second == note;
    });
  };

  for (auto it = held.begin(); it != held.end();) {
    const auto place = static_cast<SDL_Scancode>(it->first);
    if (is_down(place)) {
      ++it;
      continue;
    }
    const ym2612::Note note = it->second;
    key_pressed_last_frame[place] = false;
    it = held.erase(it);
    if (context.key_off && !another_key_holds(note)) {
      context.key_off(note);
    }
  }

  for (const auto &[place, note] : key_mappings) {
    const bool pressed_now = is_down(place);
    const bool was_pressed =
        std::exchange(key_pressed_last_frame[place], pressed_now);
    if (pressed_now && !was_pressed && !quiet) {
      if (context.key_on && !another_key_holds(note)) {
        context.key_on(note, 127);
      }
      held[static_cast<int>(place)] = note;
    }
  }
}

} // namespace ui
