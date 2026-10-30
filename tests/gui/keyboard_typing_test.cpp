#include "../test_check.hpp"
#include "gui/input/keyboard_typing.hpp"

#include <iostream>
#include <map>
#include <utility>
#include <vector>

namespace {

using Mapping = std::map<ImGuiKey, ym2612::Note>;
using ui::TypingKeyboardLayout;

struct TestEnvironment {
  InputState input;
  std::vector<std::pair<ym2612::Note, uint8_t>> on;
  std::vector<ym2612::Note> off;
  ui::KeyboardTypingContext context{
      input, ImGuiKey_Comma, ImGuiKey_Period,
      [this](ym2612::Note note, uint8_t velocity) {
        on.emplace_back(note, velocity);
      },
      [this](ym2612::Note note) { off.push_back(note); }};

  TestEnvironment() {
    ImGui::CreateContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(640, 480);
    io.DeltaTime = 1.0f / 60.0f;
    io.ConfigInputTrickleEventQueue = false;
    unsigned char *pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    frame({{ImGuiKey_A, {4, Key::C}}, {ImGuiKey_S, {4, Key::D}}});
  }

  ~TestEnvironment() {
    context.octave_down_key = ImGuiKey_Comma;
    context.octave_up_key = ImGuiKey_Period;
    ImGui::GetIO().AddKeyEvent(ImGuiKey_A, false);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_S, false);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Comma, false);
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Period, false);
    frame({{ImGuiKey_A, {4, Key::C}}, {ImGuiKey_S, {4, Key::D}}});
    ImGui::DestroyContext();
  }

  void frame(const Mapping &mapping) {
    ImGui::NewFrame();
    ui::check_keyboard_typing(context, mapping);
    ImGui::EndFrame();
  }

  void press(ImGuiKey key, bool down, const Mapping &mapping = {}) {
    ImGui::GetIO().AddKeyEvent(key, down);
    frame(mapping);
  }
};

void test_builtin_layouts_map_semitones_and_octave_rollover() {
  const std::vector<ym2612::Note> expected{
      {3, Key::C},       {3, Key::C_SHARP}, {3, Key::D},
      {3, Key::D_SHARP}, {3, Key::E},       {3, Key::F},
      {3, Key::F_SHARP}, {3, Key::G},       {3, Key::G_SHARP},
      {3, Key::A},       {3, Key::A_SHARP}, {3, Key::B},
      {4, Key::C},       {4, Key::C_SHARP}, {4, Key::D},
      {4, Key::D_SHARP}, {4, Key::E}};
  for (const auto layout :
       {TypingKeyboardLayout::Qwerty, TypingKeyboardLayout::Azerty}) {
    const auto mapping =
        ui::create_key_mappings(Scale::CHROMATIC, Key::C, 3, layout, {});
    const auto keys = ui::builtin_typing_layout(layout);
    CHECK(mapping.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
      CHECK(mapping.at(keys[i]) == expected[i]);
    }
    CHECK(!mapping.contains(ImGuiKey_None));
  }
}

void test_non_chromatic_scales_use_white_key_positions() {
  const auto major = ui::create_key_mappings(Scale::MAJOR, Key::G, 2,
                                             TypingKeyboardLayout::Qwerty, {});
  const std::vector<ImGuiKey> keys{
      ImGuiKey_A, ImGuiKey_S, ImGuiKey_D, ImGuiKey_F, ImGuiKey_G,
      ImGuiKey_H, ImGuiKey_J, ImGuiKey_K, ImGuiKey_L, ImGuiKey_Semicolon};
  const std::vector<ym2612::Note> expected{
      {2, Key::G}, {2, Key::A},       {2, Key::B}, {3, Key::C}, {3, Key::D},
      {3, Key::E}, {3, Key::F_SHARP}, {3, Key::G}, {3, Key::A}, {3, Key::B}};
  CHECK(major.size() == expected.size());
  for (std::size_t i = 0; i < keys.size(); ++i) {
    CHECK(major.at(keys[i]) == expected[i]);
  }
  CHECK(!major.contains(ImGuiKey_W));
  CHECK(!major.contains(ImGuiKey_O));
}

void test_custom_layouts_allow_empty_short_and_unterminated_input() {
  for (const auto scale : {Scale::CHROMATIC, Scale::MAJOR}) {
    CHECK(ui::create_key_mappings(scale, Key::C, 4,
                                  TypingKeyboardLayout::Custom, {})
              .empty());
    CHECK(ui::create_key_mappings(scale, Key::C, 4,
                                  TypingKeyboardLayout::Custom, {ImGuiKey_None})
              .empty());
    const auto terminated = ui::create_key_mappings(
        scale, Key::C, 4, TypingKeyboardLayout::Custom,
        {ImGuiKey_Z, ImGuiKey_X, ImGuiKey_C, ImGuiKey_V, ImGuiKey_None});
    const auto unterminated = ui::create_key_mappings(
        scale, Key::C, 4, TypingKeyboardLayout::Custom,
        {ImGuiKey_Z, ImGuiKey_X, ImGuiKey_C, ImGuiKey_V});
    CHECK(terminated == unterminated);
    CHECK((terminated.at(ImGuiKey_Z) == ym2612::Note{4, Key::C}));
    CHECK(terminated.size() == (scale == Scale::CHROMATIC ? 4 : 2));
  }
  const auto stopped = ui::create_key_mappings(
      Scale::CHROMATIC, Key::C, 4, TypingKeyboardLayout::Custom,
      {ImGuiKey_Z, ImGuiKey_None, ImGuiKey_X});
  CHECK(stopped.size() == 1);
  CHECK(stopped.contains(ImGuiKey_Z));
}

void test_press_hold_and_release_emit_one_pair_of_events() {
  TestEnvironment env;
  const Mapping mapping{{ImGuiKey_A, {4, Key::C}}, {ImGuiKey_S, {4, Key::E}}};
  env.press(ImGuiKey_A, true, mapping);
  CHECK(env.on.size() == 1);
  CHECK((env.on.front().first == ym2612::Note{4, Key::C}));
  CHECK(env.on.front().second == 127);
  CHECK(env.input.active_keyboard_notes.at(ImGuiKey_A) ==
        mapping.at(ImGuiKey_A));
  env.frame(mapping);
  env.frame(mapping);
  CHECK(env.on.size() == 1);
  env.press(ImGuiKey_S, true, mapping);
  CHECK(env.on.size() == 2);
  CHECK(env.input.active_keyboard_notes.size() == 2);
  env.press(ImGuiKey_A, false, mapping);
  CHECK(env.off.size() == 1);
  CHECK(env.off.front() == mapping.at(ImGuiKey_A));
  CHECK(!env.input.active_keyboard_notes.contains(ImGuiKey_A));
  CHECK(env.input.active_keyboard_notes.contains(ImGuiKey_S));
  env.press(ImGuiKey_S, false, mapping);
  env.frame(mapping);
  CHECK(env.off.size() == 2);
  CHECK(env.input.active_keyboard_notes.empty());
}

void test_release_uses_the_note_started_before_a_remap() {
  TestEnvironment env;
  const Mapping original{{ImGuiKey_A, {3, Key::B}}};
  const Mapping remapped{{ImGuiKey_A, {5, Key::D}}};
  env.press(ImGuiKey_A, true, original);
  env.frame(remapped);
  CHECK(env.on.size() == 1);
  env.press(ImGuiKey_A, false, remapped);
  CHECK(env.off.size() == 1);
  CHECK(env.off.front() == original.at(ImGuiKey_A));
  CHECK(env.input.active_keyboard_notes.empty());
  env.press(ImGuiKey_A, true, remapped);
  CHECK(env.on.size() == 2);
  CHECK(env.on.back().first == remapped.at(ImGuiKey_A));
}

void test_octave_keys_are_edge_triggered_and_bounded() {
  TestEnvironment env;
  env.input.keyboard_typing_octave = 0;
  env.press(ImGuiKey_Comma, true);
  CHECK(env.input.keyboard_typing_octave == 0);
  env.press(ImGuiKey_Comma, false);
  env.press(ImGuiKey_Period, true);
  CHECK(env.input.keyboard_typing_octave == 1);
  env.frame({});
  CHECK(env.input.keyboard_typing_octave == 1);
  env.press(ImGuiKey_Period, false);
  env.press(ImGuiKey_Comma, true);
  CHECK(env.input.keyboard_typing_octave == 0);
  env.press(ImGuiKey_Comma, false);
  env.input.keyboard_typing_octave = 7;
  env.press(ImGuiKey_Period, true);
  CHECK(env.input.keyboard_typing_octave == 7);
  CHECK(env.on.empty());
  CHECK(env.off.empty());
}

void test_disabled_octave_keys_and_absent_callbacks_are_supported() {
  TestEnvironment env;
  env.context.octave_down_key = ImGuiKey_None;
  env.context.octave_up_key = ImGuiKey_None;
  env.context.key_on = {};
  env.context.key_off = {};
  const Mapping mapping{{ImGuiKey_A, {4, Key::C}}};
  env.press(ImGuiKey_Period, true, mapping);
  CHECK(env.input.keyboard_typing_octave == 4);
  env.press(ImGuiKey_A, true, mapping);
  CHECK(env.input.active_keyboard_notes.size() == 1);
  env.press(ImGuiKey_A, false, mapping);
  CHECK(env.input.active_keyboard_notes.empty());
  CHECK(env.on.empty());
  CHECK(env.off.empty());
}

} // namespace

int main() {
  test_builtin_layouts_map_semitones_and_octave_rollover();
  test_non_chromatic_scales_use_white_key_positions();
  test_custom_layouts_allow_empty_short_and_unterminated_input();
  test_press_hold_and_release_emit_one_pair_of_events();
  test_release_uses_the_note_started_before_a_remap();
  test_octave_keys_are_edge_triggered_and_bounded();
  test_disabled_octave_keys_and_absent_callbacks_are_supported();
  std::cout << "All keyboard typing tests passed\n";
  return 0;
}
