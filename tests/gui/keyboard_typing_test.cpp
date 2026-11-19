#include "../test_check.hpp"
#include "gui/input/key_name_utils.hpp"
#include "gui/input/key_positions.hpp"
#include "gui/input/keyboard_typing.hpp"

#include <iostream>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace {

using Mapping = std::map<SDL_Scancode, ym2612::Note>;
using ui::TypingKeyboardLayout;

/// Typing fed from keys held here rather than on a keyboard.
struct TestEnvironment {
  InputState input;
  std::set<SDL_Scancode> held;
  std::vector<std::pair<ym2612::Note, uint8_t>> on;
  std::vector<ym2612::Note> off;
  ui::KeyboardTypingContext context{
      input, SDL_SCANCODE_COMMA, SDL_SCANCODE_PERIOD,
      [this](ym2612::Note note, uint8_t velocity) {
        on.emplace_back(note, velocity);
      },
      [this](ym2612::Note note) { off.push_back(note); }};

  TestEnvironment() {
    context.is_down = [this](SDL_Scancode place) {
      return held.contains(place);
    };
  }

  /// Every key is let go, for the next test to start with none held.
  ~TestEnvironment() {
    context.octave_down_key = SDL_SCANCODE_COMMA;
    context.octave_up_key = SDL_SCANCODE_PERIOD;
    Mapping all;
    for (const auto place : held) {
      all[place] = {4, Key::C};
    }
    held.clear();
    frame(all);
  }

  void frame(const Mapping &mapping) {
    ui::check_keyboard_typing(context, mapping);
  }

  void press(SDL_Scancode place, bool down, const Mapping &mapping = {}) {
    if (down) {
      held.insert(place);
    } else {
      held.erase(place);
    }
    frame(mapping);
  }
};

void test_auto_layout_maps_semitones_and_octave_rollover() {
  const std::vector<ym2612::Note> expected{
      {3, Key::C},       {3, Key::C_SHARP}, {3, Key::D},
      {3, Key::D_SHARP}, {3, Key::E},       {3, Key::F},
      {3, Key::F_SHARP}, {3, Key::G},       {3, Key::G_SHARP},
      {3, Key::A},       {3, Key::A_SHARP}, {3, Key::B},
      {4, Key::C},       {4, Key::C_SHARP}, {4, Key::D},
      {4, Key::D_SHARP}, {4, Key::E},       {4, Key::F},
      {4, Key::F_SHARP}, {4, Key::G}};
  const auto mapping = ui::create_key_mappings(
      Scale::CHROMATIC, Key::C, 3, ui::layout_of(ui::typing_key_places));
  const auto keys = ui::layout_of(ui::typing_key_places);
  CHECK(keys.size() == expected.size());
  CHECK(mapping.size() == expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    CHECK(mapping.at(keys[i]) == expected[i]);
  }
  CHECK(!mapping.contains(SDL_SCANCODE_UNKNOWN));
  CHECK(keys[0] == SDL_SCANCODE_A && keys[1] == SDL_SCANCODE_W);
  CHECK(keys[16] == SDL_SCANCODE_SEMICOLON &&
        keys[19] == SDL_SCANCODE_BACKSLASH);
}

void test_non_chromatic_scales_use_white_key_positions() {
  const auto major = ui::create_key_mappings(
      Scale::MAJOR, Key::G, 2, ui::layout_of(ui::typing_key_places));
  const std::vector<SDL_Scancode> keys{
      SDL_SCANCODE_A,         SDL_SCANCODE_S,          SDL_SCANCODE_D,
      SDL_SCANCODE_F,         SDL_SCANCODE_G,          SDL_SCANCODE_H,
      SDL_SCANCODE_J,         SDL_SCANCODE_K,          SDL_SCANCODE_L,
      SDL_SCANCODE_SEMICOLON, SDL_SCANCODE_APOSTROPHE, SDL_SCANCODE_BACKSLASH};
  const std::vector<ym2612::Note> expected{
      {2, Key::G}, {2, Key::A}, {2, Key::B},       {3, Key::C},
      {3, Key::D}, {3, Key::E}, {3, Key::F_SHARP}, {3, Key::G},
      {3, Key::A}, {3, Key::B}, {4, Key::C},       {4, Key::D}};
  CHECK(major.size() == expected.size());
  for (std::size_t i = 0; i < keys.size(); ++i) {
    CHECK(major.at(keys[i]) == expected[i]);
  }
  CHECK(!major.contains(SDL_SCANCODE_W));
  CHECK(!major.contains(SDL_SCANCODE_O));
}

void test_custom_layouts_allow_empty_short_and_unterminated_input() {
  for (const auto scale : {Scale::CHROMATIC, Scale::MAJOR}) {
    CHECK(ui::create_key_mappings(scale, Key::C, 4, {}).empty());
    CHECK(ui::create_key_mappings(scale, Key::C, 4, {SDL_SCANCODE_UNKNOWN})
              .empty());
    const auto terminated =
        ui::create_key_mappings(scale, Key::C, 4,
                                {SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C,
                                 SDL_SCANCODE_V, SDL_SCANCODE_UNKNOWN});
    const auto unterminated = ui::create_key_mappings(
        scale, Key::C, 4,
        {SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V});
    CHECK(terminated == unterminated);
    CHECK((terminated.at(SDL_SCANCODE_Z) == ym2612::Note{4, Key::C}));
    CHECK(terminated.size() == (scale == Scale::CHROMATIC ? 4 : 2));
  }
  const auto stopped = ui::create_key_mappings(
      Scale::CHROMATIC, Key::C, 4,
      {SDL_SCANCODE_Z, SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_X});
  CHECK(stopped.size() == 1);
  CHECK(stopped.contains(SDL_SCANCODE_Z));
}

void test_two_keyboards_each_start_on_c() {
  const auto keyboards =
      ui::standard_typing_keys(TypingKeyboardLayout::TwoKeyboards);
  CHECK(keyboards.size() == 2);
  const auto lower =
      ui::create_key_mappings(Scale::CHROMATIC, Key::C, 3, keyboards[0].places);
  const auto upper =
      ui::create_key_mappings(Scale::CHROMATIC, Key::C, 4, keyboards[1].places);
  // The bottom row and the row over it are the white notes, C to E.
  CHECK(lower.size() == 17);
  CHECK((lower.at(SDL_SCANCODE_Z) == ym2612::Note{3, Key::C}));
  CHECK((lower.at(SDL_SCANCODE_S) == ym2612::Note{3, Key::C_SHARP}));
  CHECK((lower.at(SDL_SCANCODE_M) == ym2612::Note{3, Key::B}));
  CHECK((lower.at(SDL_SCANCODE_COMMA) == ym2612::Note{4, Key::C}));
  CHECK((lower.at(SDL_SCANCODE_SEMICOLON) == ym2612::Note{4, Key::D_SHARP}));
  CHECK((lower.at(SDL_SCANCODE_SLASH) == ym2612::Note{4, Key::E}));
  // The upper keyboard's, C to G.
  CHECK(upper.size() == 20);
  CHECK((upper.at(SDL_SCANCODE_Q) == ym2612::Note{4, Key::C}));
  CHECK((upper.at(SDL_SCANCODE_2) == ym2612::Note{4, Key::C_SHARP}));
  CHECK((upper.at(SDL_SCANCODE_U) == ym2612::Note{4, Key::B}));
  CHECK((upper.at(SDL_SCANCODE_I) == ym2612::Note{5, Key::C}));
  CHECK((upper.at(SDL_SCANCODE_EQUALS) == ym2612::Note{5, Key::F_SHARP}));
  CHECK((upper.at(SDL_SCANCODE_RIGHTBRACKET) == ym2612::Note{5, Key::G}));
  // No key is in both, and none is an octave key.
  for (const auto &[key, note] : lower) {
    CHECK(!upper.contains(key));
  }
  for (const auto key : {SDL_SCANCODE_A, SDL_SCANCODE_APOSTROPHE,
                         SDL_SCANCODE_1, SDL_SCANCODE_MINUS}) {
    CHECK(!lower.contains(key) && !upper.contains(key));
  }
  CHECK((keyboards[0].octave ==
         ui::OctavePlaces{SDL_SCANCODE_A, SDL_SCANCODE_APOSTROPHE}));
  CHECK((keyboards[1].octave ==
         ui::OctavePlaces{SDL_SCANCODE_1, SDL_SCANCODE_MINUS}));
}

void test_two_keyboards_each_start_on_the_scale_s_first_note() {
  const auto keyboards =
      ui::standard_typing_keys(TypingKeyboardLayout::TwoKeyboards);
  // Five notes to the octave: a row's first key is the first note all the
  // same.
  const auto lower = ui::create_key_mappings(Scale::MAJAR_PENTATONIC, Key::G, 2,
                                             keyboards[0].places);
  const auto upper = ui::create_key_mappings(Scale::MAJAR_PENTATONIC, Key::G, 5,
                                             keyboards[1].places);
  CHECK(lower.size() == 10);
  CHECK(upper.size() == 12);
  CHECK((lower.at(SDL_SCANCODE_Z) == ym2612::Note{2, Key::G}));
  CHECK((lower.at(SDL_SCANCODE_N) == ym2612::Note{3, Key::G}));
  CHECK((upper.at(SDL_SCANCODE_Q) == ym2612::Note{5, Key::G}));
  CHECK((upper.at(SDL_SCANCODE_Y) == ym2612::Note{6, Key::G}));
  CHECK(!lower.contains(SDL_SCANCODE_S));
  CHECK(!upper.contains(SDL_SCANCODE_2));
}

void test_the_layout_says_how_many_keyboards_are_played() {
  UIPreferences prefs;
  CHECK(ui::typing_keyboards(prefs) ==
        ui::standard_typing_keys(TypingKeyboardLayout::OneKeyboard));
  CHECK(ui::typing_keyboards(prefs).size() == 1);
  CHECK(ui::typing_keyboards(prefs)[0].octave == ui::typing_octave_places);

  prefs.midi_keyboard_layout =
      static_cast<int>(TypingKeyboardLayout::TwoKeyboards);
  CHECK(ui::typing_keyboards(prefs) ==
        ui::standard_typing_keys(TypingKeyboardLayout::TwoKeyboards));

  // A number that is no layout's is One Keyboard's.
  CHECK(ui::clamp_layout_pref(3) == TypingKeyboardLayout::TwoKeyboards);
  CHECK(ui::clamp_layout_pref(2) == TypingKeyboardLayout::OneKeyboard);
  CHECK(ui::clamp_layout_pref(1) == TypingKeyboardLayout::OneKeyboard);
  prefs.midi_keyboard_layout = 7;
  CHECK(ui::typing_keyboards(prefs).size() == 1);
  CHECK(prefs.midi_keyboard_layout == 0);
}

void test_custom_keys_start_as_the_layout_s_own() {
  for (const auto layout : {TypingKeyboardLayout::OneKeyboard,
                            TypingKeyboardLayout::TwoKeyboards}) {
    UIPreferences prefs;
    prefs.midi_keyboard_layout = static_cast<int>(layout);
    prefs.midi_keyboard_custom_keys = true;
    CHECK(ui::typing_keyboards(prefs) == ui::standard_typing_keys(layout));
    // The preferences now hold them, for the user to change.
    const auto standard = ui::standard_typing_keys(layout);
    const auto custom = ui::custom_typing_keys(prefs, layout);
    CHECK(custom.size() == standard.size());
    for (std::size_t i = 0; i < custom.size(); ++i) {
      CHECK(custom[i]->places.size() == standard[i].places.size());
      CHECK(custom[i]->octave_down == standard[i].octave.first);
      CHECK(custom[i]->octave_up == standard[i].octave.second);
    }
  }
}

void test_each_layout_keeps_custom_keys_of_its_own() {
  UIPreferences prefs;
  prefs.midi_keyboard_custom_keys = true;
  prefs.custom_typing_keys = {
      {SDL_SCANCODE_Z, SDL_SCANCODE_X}, SDL_SCANCODE_F1, SDL_SCANCODE_F2};
  prefs.custom_typing_lower_keys = {
      {SDL_SCANCODE_1, SDL_SCANCODE_2}, SDL_SCANCODE_F3, 0};
  prefs.custom_typing_upper_keys = {
      {SDL_SCANCODE_9, 0, SDL_SCANCODE_0}, 9999, SDL_SCANCODE_F4};

  const auto one = ui::typing_keyboards(prefs);
  CHECK(one.size() == 1);
  // A custom keyboard has twelve keys at the least, the rest without a place.
  CHECK(one[0].places.size() == ui::typing_layout_min_custom_slots);
  CHECK(one[0].places[0] == SDL_SCANCODE_Z);
  CHECK(one[0].places[1] == SDL_SCANCODE_X);
  CHECK(one[0].places[2] == SDL_SCANCODE_UNKNOWN);
  CHECK((one[0].octave == ui::OctavePlaces{SDL_SCANCODE_F1, SDL_SCANCODE_F2}));

  prefs.midi_keyboard_layout =
      static_cast<int>(TypingKeyboardLayout::TwoKeyboards);
  const auto two = ui::typing_keyboards(prefs);
  CHECK(two.size() == 2);
  CHECK(two[0].places[0] == SDL_SCANCODE_1);
  CHECK(two[1].places[0] == SDL_SCANCODE_9);
  CHECK(two[1].places[1] == SDL_SCANCODE_UNKNOWN);
  // An octave key without a place is the layout's.
  CHECK((two[0].octave ==
         ui::OctavePlaces{SDL_SCANCODE_F3, SDL_SCANCODE_APOSTROPHE}));
  CHECK((two[1].octave == ui::OctavePlaces{SDL_SCANCODE_1, SDL_SCANCODE_F4}));
  // One Keyboard's are as they were.
  CHECK(prefs.custom_typing_keys.places[0] == SDL_SCANCODE_Z);

  // Without custom keys the layout's own are played, and the user's kept.
  prefs.midi_keyboard_custom_keys = false;
  CHECK(ui::typing_keyboards(prefs) ==
        ui::standard_typing_keys(TypingKeyboardLayout::TwoKeyboards));
  CHECK(prefs.custom_typing_lower_keys.places[0] == SDL_SCANCODE_1);

  // No more keys than a custom keyboard may have.
  prefs.custom_typing_keys.places.assign(40, SDL_SCANCODE_A);
  prefs.midi_keyboard_custom_keys = true;
  prefs.midi_keyboard_layout = 0;
  CHECK(ui::typing_keyboards(prefs)[0].places.size() ==
        ui::typing_layout_max_custom_slots);
}

void test_a_note_two_keys_play_sounds_from_the_first_down_to_the_last_up() {
  TestEnvironment env;
  const ym2612::Note shared{4, Key::C};
  const Mapping mapping{{SDL_SCANCODE_A, shared}, {SDL_SCANCODE_S, shared}};
  env.press(SDL_SCANCODE_A, true, mapping);
  env.press(SDL_SCANCODE_S, true, mapping);
  CHECK(env.on.size() == 1);
  CHECK(env.input.active_keyboard_notes.size() == 2);
  env.press(SDL_SCANCODE_A, false, mapping);
  CHECK(env.off.empty());
  env.press(SDL_SCANCODE_A, true, mapping);
  CHECK(env.on.size() == 1);
  env.press(SDL_SCANCODE_S, false, mapping);
  CHECK(env.off.empty());
  env.press(SDL_SCANCODE_A, false, mapping);
  CHECK(env.off.size() == 1);
  CHECK(env.off.front() == shared);
  CHECK(env.input.active_keyboard_notes.empty());
  env.press(SDL_SCANCODE_S, true, mapping);
  CHECK(env.on.size() == 2);
  env.press(SDL_SCANCODE_S, false, mapping);
  CHECK(env.off.size() == 2);
}

void test_the_upper_keyboard_has_octave_keys_of_its_own() {
  TestEnvironment env;
  env.context.upper_octave_down_key = SDL_SCANCODE_1;
  env.context.upper_octave_up_key = SDL_SCANCODE_MINUS;
  env.input.keyboard_typing_octave = 3;
  env.input.keyboard_typing_upper_octave = 6;
  env.press(SDL_SCANCODE_MINUS, true);
  CHECK(env.input.keyboard_typing_upper_octave == 7);
  CHECK(env.input.keyboard_typing_octave == 3);
  env.press(SDL_SCANCODE_MINUS, false);
  env.press(SDL_SCANCODE_MINUS, true);
  CHECK(env.input.keyboard_typing_upper_octave == 7);
  env.press(SDL_SCANCODE_MINUS, false);
  env.press(SDL_SCANCODE_1, true);
  CHECK(env.input.keyboard_typing_upper_octave == 6);
  env.press(SDL_SCANCODE_1, false);
  env.press(SDL_SCANCODE_COMMA, true);
  CHECK(env.input.keyboard_typing_octave == 2);
  CHECK(env.input.keyboard_typing_upper_octave == 6);
  env.press(SDL_SCANCODE_COMMA, false);
  CHECK(env.on.empty());
  CHECK(env.off.empty());
}

void test_press_hold_and_release_emit_one_pair_of_events() {
  TestEnvironment env;
  const Mapping mapping{{SDL_SCANCODE_A, {4, Key::C}},
                        {SDL_SCANCODE_S, {4, Key::E}}};
  env.press(SDL_SCANCODE_A, true, mapping);
  CHECK(env.on.size() == 1);
  CHECK((env.on.front().first == ym2612::Note{4, Key::C}));
  CHECK(env.on.front().second == 127);
  CHECK(env.input.active_keyboard_notes.at(SDL_SCANCODE_A) ==
        mapping.at(SDL_SCANCODE_A));
  env.frame(mapping);
  env.frame(mapping);
  CHECK(env.on.size() == 1);
  env.press(SDL_SCANCODE_S, true, mapping);
  CHECK(env.on.size() == 2);
  CHECK(env.input.active_keyboard_notes.size() == 2);
  env.press(SDL_SCANCODE_A, false, mapping);
  CHECK(env.off.size() == 1);
  CHECK(env.off.front() == mapping.at(SDL_SCANCODE_A));
  CHECK(!env.input.active_keyboard_notes.contains(SDL_SCANCODE_A));
  CHECK(env.input.active_keyboard_notes.contains(SDL_SCANCODE_S));
  env.press(SDL_SCANCODE_S, false, mapping);
  env.frame(mapping);
  CHECK(env.off.size() == 2);
  CHECK(env.input.active_keyboard_notes.empty());
}

void test_release_uses_the_note_started_before_a_remap() {
  TestEnvironment env;
  const Mapping original{{SDL_SCANCODE_A, {3, Key::B}}};
  const Mapping remapped{{SDL_SCANCODE_A, {5, Key::D}}};
  env.press(SDL_SCANCODE_A, true, original);
  env.frame(remapped);
  CHECK(env.on.size() == 1);
  env.press(SDL_SCANCODE_A, false, remapped);
  CHECK(env.off.size() == 1);
  CHECK(env.off.front() == original.at(SDL_SCANCODE_A));
  CHECK(env.input.active_keyboard_notes.empty());
  env.press(SDL_SCANCODE_A, true, remapped);
  CHECK(env.on.size() == 2);
  CHECK(env.on.back().first == remapped.at(SDL_SCANCODE_A));
}

void test_release_after_key_leaves_mapping() {
  TestEnvironment env;
  const Mapping mapping{{SDL_SCANCODE_A, {4, Key::C}}};
  env.press(SDL_SCANCODE_A, true, mapping);
  env.frame({});
  CHECK(env.on.size() == 1);
  CHECK(env.off.empty());
  CHECK(env.input.active_keyboard_notes.contains(SDL_SCANCODE_A));
  env.press(SDL_SCANCODE_A, false, {});
  CHECK(env.off.size() == 1);
  CHECK(env.off.front() == mapping.at(SDL_SCANCODE_A));
  CHECK(env.input.active_keyboard_notes.empty());
  env.frame({});
  CHECK(env.off.size() == 1);
  env.press(SDL_SCANCODE_A, true, mapping);
  CHECK(env.on.size() == 2);
  CHECK(env.on.back().first == mapping.at(SDL_SCANCODE_A));
  env.press(SDL_SCANCODE_A, false, mapping);
  CHECK(env.off.size() == 2);
}

void test_octave_keys_are_edge_triggered_and_bounded() {
  TestEnvironment env;
  env.input.keyboard_typing_octave = 0;
  env.press(SDL_SCANCODE_COMMA, true);
  CHECK(env.input.keyboard_typing_octave == 0);
  env.press(SDL_SCANCODE_COMMA, false);
  env.press(SDL_SCANCODE_PERIOD, true);
  CHECK(env.input.keyboard_typing_octave == 1);
  env.frame({});
  CHECK(env.input.keyboard_typing_octave == 1);
  env.press(SDL_SCANCODE_PERIOD, false);
  env.press(SDL_SCANCODE_COMMA, true);
  CHECK(env.input.keyboard_typing_octave == 0);
  env.press(SDL_SCANCODE_COMMA, false);
  env.input.keyboard_typing_octave = 7;
  env.press(SDL_SCANCODE_PERIOD, true);
  CHECK(env.input.keyboard_typing_octave == 7);
  CHECK(env.on.empty());
  CHECK(env.off.empty());
}

void test_a_key_given_while_typing_is_kept_quiet_plays_nothing() {
  TestEnvironment env;
  const Mapping mapping{{SDL_SCANCODE_A, {4, Key::C}},
                        {SDL_SCANCODE_S, {4, Key::D}}};
  env.input.keyboard_typing_octave = 4;

  // A note that sounds already ends as its key comes up.
  env.press(SDL_SCANCODE_S, true, mapping);
  CHECK(env.on.size() == 1);
  ui::keep_typing_quiet();
  env.press(SDL_SCANCODE_S, false, mapping);
  CHECK(env.off.size() == 1);

  // A note's key and an octave's go down for an answer.
  ui::keep_typing_quiet();
  env.held.insert(SDL_SCANCODE_A);
  env.press(SDL_SCANCODE_PERIOD, true, mapping);
  CHECK(env.on.size() == 1);
  CHECK(env.input.keyboard_typing_octave == 4);
  CHECK(env.input.active_keyboard_notes.empty());

  // Still down once typing is no longer kept quiet, they stay silent.
  env.frame(mapping);
  CHECK(env.on.size() == 1);
  CHECK(env.input.keyboard_typing_octave == 4);

  // Let go and pressed again, they play.
  env.held.erase(SDL_SCANCODE_A);
  env.press(SDL_SCANCODE_PERIOD, false, mapping);
  CHECK(env.off.size() == 1);
  env.press(SDL_SCANCODE_A, true, mapping);
  CHECK(env.on.size() == 2);
  env.press(SDL_SCANCODE_PERIOD, true, mapping);
  CHECK(env.input.keyboard_typing_octave == 5);
}

void test_disabled_octave_keys_and_absent_callbacks_are_supported() {
  TestEnvironment env;
  env.context.octave_down_key = SDL_SCANCODE_UNKNOWN;
  env.context.octave_up_key = SDL_SCANCODE_UNKNOWN;
  env.context.key_on = {};
  env.context.key_off = {};
  const Mapping mapping{{SDL_SCANCODE_A, {4, Key::C}}};
  env.press(SDL_SCANCODE_PERIOD, true, mapping);
  CHECK(env.input.keyboard_typing_octave == 4);
  env.press(SDL_SCANCODE_A, true, mapping);
  CHECK(env.input.active_keyboard_notes.size() == 1);
  env.press(SDL_SCANCODE_A, false, mapping);
  CHECK(env.input.active_keyboard_notes.empty());
  CHECK(env.on.empty());
  CHECK(env.off.empty());
}

void test_a_place_is_found_by_the_name_browsers_give_it() {
  CHECK(ui::place_named("KeyA") == SDL_SCANCODE_A);
  CHECK(ui::place_named("KeyZ") == SDL_SCANCODE_Z);
  CHECK(ui::place_named("Digit1") == SDL_SCANCODE_1);
  CHECK(ui::place_named("Digit9") == SDL_SCANCODE_9);
  CHECK(ui::place_named("Digit0") == SDL_SCANCODE_0);
  CHECK(ui::place_named("Quote") == SDL_SCANCODE_APOSTROPHE);
  CHECK(ui::place_named("Backslash") == SDL_SCANCODE_BACKSLASH);
  CHECK(ui::place_named("IntlRo") == SDL_SCANCODE_INTERNATIONAL1);
  CHECK(ui::place_named("Enter") == SDL_SCANCODE_UNKNOWN);
  CHECK(ui::place_named("Keys") == SDL_SCANCODE_UNKNOWN);
  CHECK(ui::place_named("Numpad1") == SDL_SCANCODE_UNKNOWN);
  CHECK(ui::place_named("") == SDL_SCANCODE_UNKNOWN);
}

void test_a_learned_character_names_its_key_and_moves_no_note() {
  ui::TypedCharacters characters;
  const unsigned before = characters.revision();
  CHECK(characters.at(SDL_SCANCODE_A) == SDLK_A);
  CHECK(ui::place_name(SDL_SCANCODE_A, characters) == "A");

  // A French keyboard: the key where a US one has A types q.
  CHECK(characters.learn(SDL_SCANCODE_A, SDLK_Q));
  CHECK(characters.revision() != before);
  CHECK(characters.at(SDL_SCANCODE_A) == SDLK_Q);
  CHECK(ui::place_name(SDL_SCANCODE_A, characters) == "Q");

  const unsigned learned = characters.revision();
  CHECK(!characters.learn(SDL_SCANCODE_A, SDLK_Q));
  CHECK(characters.revision() == learned);

  // The note is the place's, whatever its key types.
  const auto chromatic = ui::create_key_mappings(
      Scale::CHROMATIC, Key::C, 4, ui::layout_of(ui::typing_key_places));
  CHECK((chromatic.at(SDL_SCANCODE_A) == ym2612::Note{4, Key::C}));

  characters.forget();
  CHECK(characters.revision() != learned);
  CHECK(characters.at(SDL_SCANCODE_A) == SDLK_A);
  CHECK(ui::place_name(SDL_SCANCODE_A, characters) == "A");
}

void test_only_the_character_of_a_character_key_is_learned() {
  ui::TypedCharacters characters;

  // The keypad's 1 is not the 1 over the letters.
  CHECK(!characters.learn(SDL_SCANCODE_KP_1, SDLK_1));
  CHECK(!characters.learn(SDL_SCANCODE_S, SDLK_UNKNOWN));
  CHECK(!characters.learn(SDL_SCANCODE_S, SDLK_RETURN));
  CHECK(!characters.learn(SDL_SCANCODE_S, SDLK_F1));
  CHECK(!characters.learn(SDL_SCANCODE_UNKNOWN, SDLK_S));
  CHECK(characters.at(SDL_SCANCODE_S) == SDLK_S);
  CHECK(characters.at(SDL_SCANCODE_UNKNOWN) == SDLK_UNKNOWN);

  // A press that leaves SDL with what it had keeps what was learned.
  characters.learn(SDL_SCANCODE_S, SDLK_O);
  SDL_KeyboardEvent press{};
  press.scancode = SDL_SCANCODE_S;
  press.key = SDLK_S;
  press.down = true;
  CHECK(!characters.learn(press));
  press.down = false;
  CHECK(!characters.learn(press));
  CHECK(characters.at(SDL_SCANCODE_S) == SDLK_O);
}

void test_key_names_follow_what_is_learned() {
  ui::TypedCharacters characters;
  CHECK(ui::place_name(SDL_SCANCODE_BACKSLASH, characters) == "\\");
  CHECK(ui::place_name(SDL_SCANCODE_LEFTBRACKET, characters) == "[");

  // A Japanese keyboard's signs.
  characters.learn(SDL_SCANCODE_BACKSLASH, SDLK_RIGHTBRACKET);
  CHECK(ui::place_name(SDL_SCANCODE_BACKSLASH, characters) == "]");
  characters.learn(SDL_SCANCODE_LEFTBRACKET, SDLK_AT);
  CHECK(ui::place_name(SDL_SCANCODE_LEFTBRACKET, characters) == "@");
  characters.learn(SDL_SCANCODE_RIGHTBRACKET, SDLK_LEFTBRACKET);
  CHECK(ui::place_name(SDL_SCANCODE_RIGHTBRACKET, characters) == "[");
  characters.learn(SDL_SCANCODE_APOSTROPHE, SDLK_COLON);
  CHECK(ui::place_name(SDL_SCANCODE_APOSTROPHE, characters) == ":");
  CHECK(ui::place_name(SDL_SCANCODE_A, characters) == "A");

  characters.forget();
  CHECK(ui::place_name(SDL_SCANCODE_BACKSLASH, characters) == "\\");

  // A key that types no character has its place's name, cut short.
  CHECK(ui::place_name(SDL_SCANCODE_SPACE, characters) == "Sp");
  CHECK(ui::place_name(SDL_SCANCODE_F1, characters) == "F1");
  CHECK(ui::place_name(SDL_SCANCODE_LSHIFT, characters) == "Le");
  CHECK(ui::place_name(SDL_SCANCODE_UNKNOWN, characters).empty());
}

void test_a_sign_has_no_name_until_its_key_is_known() {
  ui::TypedCharacters characters;
  characters.start_from_a_guess();
  CHECK(!characters.knows(SDL_SCANCODE_LEFTBRACKET));
  // A letter's key and a digit's are taken to be where a US keyboard has
  // them; a sign's is not.
  CHECK(ui::place_name(SDL_SCANCODE_Q, characters) == "Q");
  CHECK(ui::place_name(SDL_SCANCODE_2, characters) == "2");
  CHECK(ui::place_name(SDL_SCANCODE_LEFTBRACKET, characters).empty());
  CHECK(ui::place_name(SDL_SCANCODE_SEMICOLON, characters).empty());
  CHECK(ui::place_name(SDL_SCANCODE_SPACE, characters) == "Sp");

  // The browser's layout map names it.
  const unsigned before = characters.revision();
  CHECK(characters.learn(SDL_SCANCODE_LEFTBRACKET, SDLK_AT));
  CHECK(characters.revision() != before);
  CHECK(characters.knows(SDL_SCANCODE_LEFTBRACKET));
  CHECK(ui::place_name(SDL_SCANCODE_LEFTBRACKET, characters) == "@");

  // So does its press with nothing else held, even where it types what a
  // US keyboard has there; a press with Shift held tells nothing.
  SDL_KeyboardEvent press{};
  press.scancode = SDL_SCANCODE_SEMICOLON;
  press.down = true;
  press.mod = SDL_KMOD_LSHIFT;
  CHECK(!characters.learn(press));
  CHECK(ui::place_name(SDL_SCANCODE_SEMICOLON, characters).empty());
  press.mod = SDL_KMOD_NONE;
  CHECK(characters.learn(press));
  CHECK(ui::place_name(SDL_SCANCODE_SEMICOLON, characters) == ";");
  CHECK(!characters.learn(press));

  // A change of layout leaves the signs unknown again.
  characters.forget();
  CHECK(ui::place_name(SDL_SCANCODE_LEFTBRACKET, characters).empty());

  // With the layout SDL has from the system, every key is known.
  ui::TypedCharacters from_system;
  CHECK(from_system.knows(SDL_SCANCODE_LEFTBRACKET));
  CHECK(ui::place_name(SDL_SCANCODE_LEFTBRACKET, from_system) == "[");
}

void test_an_older_file_s_keys_become_places() {
  // Without a keyboard to ask, SDL answers for a US one.
  CHECK(ui::place_of(ImGuiKey_A) == SDL_SCANCODE_A);
  CHECK(ui::place_of(ImGuiKey_Comma) == SDL_SCANCODE_COMMA);
  CHECK(ui::place_of(ImGuiKey_LeftBracket) == SDL_SCANCODE_LEFTBRACKET);
  CHECK(ui::place_of(ImGuiKey_Space) == SDL_SCANCODE_SPACE);
  CHECK(ui::place_of(ImGuiKey_F1) == SDL_SCANCODE_F1);
  CHECK(ui::place_of(ImGuiKey_None) == SDL_SCANCODE_UNKNOWN);

  UIPreferences prefs;
  prefs.legacy_custom_typing_keys =
      ui::LegacyTypingKeys{{ImGuiKey_Z, ImGuiKey_X, 0, ImGuiKey_Slash, 9999},
                           ImGuiKey_F1,
                           ImGuiKey_F2};
  ui::adopt_legacy_typing_keys(prefs);
  CHECK(!prefs.legacy_custom_typing_keys);
  CHECK((prefs.custom_typing_keys.places ==
         std::vector<int>{SDL_SCANCODE_Z, SDL_SCANCODE_X, 0, SDL_SCANCODE_SLASH,
                          0}));
  CHECK(prefs.custom_typing_keys.octave_down == SDL_SCANCODE_F1);
  CHECK(prefs.custom_typing_keys.octave_up == SDL_SCANCODE_F2);

  // With none to adopt, the places are left as they are.
  const auto kept = prefs;
  ui::adopt_legacy_typing_keys(prefs);
  CHECK(prefs == kept);

  // The older files' layout before the user changed it is none to adopt.
  ui::LegacyTypingKeys untouched;
  untouched.layout.assign(ui::default_typing_layout_keys.begin(),
                          ui::default_typing_layout_keys.end());
  CHECK(untouched.untouched());
  untouched.layout[0] = ImGuiKey_Z;
  CHECK(!untouched.untouched());
}

} // namespace

int main() {
  test_auto_layout_maps_semitones_and_octave_rollover();
  test_non_chromatic_scales_use_white_key_positions();
  test_custom_layouts_allow_empty_short_and_unterminated_input();
  test_a_place_is_found_by_the_name_browsers_give_it();
  test_a_learned_character_names_its_key_and_moves_no_note();
  test_only_the_character_of_a_character_key_is_learned();
  test_key_names_follow_what_is_learned();
  test_a_sign_has_no_name_until_its_key_is_known();
  test_an_older_file_s_keys_become_places();
  test_two_keyboards_each_start_on_c();
  test_two_keyboards_each_start_on_the_scale_s_first_note();
  test_the_layout_says_how_many_keyboards_are_played();
  test_custom_keys_start_as_the_layout_s_own();
  test_each_layout_keeps_custom_keys_of_its_own();
  test_press_hold_and_release_emit_one_pair_of_events();
  test_a_note_two_keys_play_sounds_from_the_first_down_to_the_last_up();
  test_the_upper_keyboard_has_octave_keys_of_its_own();
  test_release_uses_the_note_started_before_a_remap();
  test_release_after_key_leaves_mapping();
  test_octave_keys_are_edge_triggered_and_bounded();
  test_disabled_octave_keys_and_absent_callbacks_are_supported();
  test_a_key_given_while_typing_is_kept_quiet_plays_nothing();
  std::cout << "All keyboard typing tests passed\n";
  return 0;
}
