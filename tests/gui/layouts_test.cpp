#include "../test_check.hpp"
#include "gui/areas.hpp"
#include "gui/layouts.hpp"
#include "gui/window_title.hpp"
#include "platform/native/native_file_system.hpp"
#include "preferences/preference_manager.hpp"
#include "system/path_service.hpp"

#include <filesystem>
#include <iostream>
#include <set>
#include <string>

namespace fs = std::filesystem;
using namespace ui::layouts;

namespace {

void test_every_builtin_layout_is_found_by_its_name() {
  CHECK(!builtin_layouts().empty());
  std::set<std::string_view> names;
  for (const auto &layout : builtin_layouts()) {
    CHECK(!layout.name.empty());
    CHECK(layout.build != nullptr);
    CHECK(names.insert(layout.name).second);
    CHECK(find_layout(layout.name) == layout.id);
    CHECK(definition(layout.id).name == layout.name);
  }
  CHECK(definition(LayoutId::Sound).name == "Sound");
}

void test_unknown_names_fall_back_to_the_default_layout() {
  CHECK(kDefaultLayout == LayoutId::Sound);
  CHECK(!find_layout("").has_value());
  CHECK(!find_layout("sound").has_value());
  CHECK(resolve_layout("") == kDefaultLayout);
  CHECK(resolve_layout("Gone") == kDefaultLayout);
  CHECK(resolve_layout("Sound") == LayoutId::Sound);
}

void test_sound_shows_every_window() {
  UIPreferences prefs;
  prefs.show_patch_selector = false;
  prefs.show_patch_editor = false;
  prefs.show_patch_lab = false;
  prefs.show_midi_keyboard = false;
  prefs.show_mml_console = false;
  prefs.show_waveform = false;
  prefs.show_preferences = false;
  prefs.show_audio_controls = false;
  prefs.ui_scale = 2.0f;

  apply_windows(definition(LayoutId::Sound).windows, prefs);
  CHECK(prefs.show_patch_selector);
  CHECK(prefs.show_patch_editor);
  CHECK(prefs.show_patch_lab);
  CHECK(prefs.show_midi_keyboard);
  CHECK(prefs.show_mml_console);
  CHECK(prefs.show_waveform);
  CHECK(prefs.show_preferences);
  // Settings outside the View menu are not the layout's.
  CHECK(!prefs.show_audio_controls);
  CHECK(prefs.ui_scale == 2.0f);
}

void test_the_default_layout_keeps_the_storage_that_predates_layouts() {
  const fs::path base = fs::path("config") / "imgui.ini";
  CHECK(ini_file_path(base, "Sound") == base);
  CHECK(ini_storage_key("megatoy_imgui_ini_v3", "Sound") ==
        "megatoy_imgui_ini_v3");
}

void test_other_layouts_store_beside_it() {
  const fs::path base = fs::path("config") / "imgui.ini";
  CHECK(ini_file_path(base, "Song") == fs::path("config") / "imgui-song.ini");
  CHECK(ini_storage_key("megatoy_imgui_ini_v3", "Song") ==
        "megatoy_imgui_ini_v3_song");
  CHECK(ini_file_path({}, "Song").empty());

  std::set<fs::path> files;
  std::set<std::string> keys;
  for (const auto &layout : builtin_layouts()) {
    CHECK(files.insert(ini_file_path(base, layout.name)).second);
    CHECK(keys.insert(ini_storage_key("key", layout.name)).second);
  }
}

void test_the_current_layout_name_survives_a_restart() {
  const auto root = fs::temp_directory_path() / "megatoy_layouts_test";
  fs::remove_all(root);
  fs::create_directories(root / "config");
  NativeFileSystem file_system;
  megatoy::system::PathService directories(file_system, root / "config");
  {
    PreferenceManager preferences(directories);
    CHECK(preferences.layout().empty());
    preferences.set_layout("Song");
    // Resetting the view keeps the layout it applies to.
    preferences.reset_ui_preferences();
  }
  {
    PreferenceManager preferences(directories);
    CHECK(preferences.layout() == "Song");
    preferences.set_layout("Sound");
  }
  {
    PreferenceManager preferences(directories);
    CHECK(preferences.layout() == "Sound");
  }
  fs::remove_all(root);
}

void test_every_view_has_its_area() {
  using ui::areas::Area;
  using ui::areas::area_of;
  CHECK(area_of(ui::PATCH_BROWSER_TITLE) == Area::Sidebar);
  CHECK(area_of(ui::PREFERENCES_TITLE) == Area::Main);
  CHECK(area_of(ui::patch_editor_window_title("bass", 1, false)) ==
        Area::Main);
  CHECK(area_of(ui::patch_editor_window_title("bass", 3, true)) ==
        Area::Main);
  CHECK(area_of(ui::WAVEFORM_TITLE) == Area::FooterLeft);
  CHECK(area_of(ui::SOFT_KEYBOARD_TITLE) == Area::FooterRight);
  CHECK(area_of(ui::MML_CONSOLE_TITLE) == Area::FooterRight);
  CHECK(area_of(ui::PATCH_LAB_TITLE) == Area::FooterRight);
  // Saved settings keep only the id after "###".
  CHECK(area_of("Patch Editor 2") == Area::Main);
  CHECK(!area_of("Patch Editor Help").has_value());
  CHECK(!area_of("Debug##Default").has_value());
}

} // namespace

int main() {
  test_every_builtin_layout_is_found_by_its_name();
  test_unknown_names_fall_back_to_the_default_layout();
  test_sound_shows_every_window();
  test_the_default_layout_keeps_the_storage_that_predates_layouts();
  test_other_layouts_store_beside_it();
  test_the_current_layout_name_survives_a_restart();
  test_every_view_has_its_area();

  std::cout << "All layout tests passed\n";
  return 0;
}
