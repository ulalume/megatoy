#include "../test_check.hpp"
#include "gui/areas.hpp"
#include "gui/window_title.hpp"

#include <iostream>

namespace {

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
  test_every_view_has_its_area();

  std::cout << "All layout tests passed\n";
  return 0;
}
