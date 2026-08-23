#include "../test_check.hpp"
#include "gui/ui_scale.hpp"

#include <iostream>

namespace {

using namespace ui::scale;

void test_auto_follows_the_display() {
  CHECK(resolve(kAuto, 1.0f) == 1.0f);
  CHECK(resolve(kAuto, 1.5f) == 1.5f);
  CHECK(resolve(kAuto, 2.0f) == 2.0f);
}

void test_auto_falls_back_when_the_display_is_unknown() {
  CHECK(resolve(kAuto, 0.0f) == kMin);
  CHECK(resolve(kAuto, -1.0f) == kMin);
}

void test_an_explicit_choice_wins_over_the_display() {
  CHECK(resolve(1.0f, 2.0f) == 1.0f);
  CHECK(resolve(3.0f, 1.0f) == 3.0f);
}

void test_out_of_range_values_are_clamped() {
  CHECK(resolve(0.25f, 1.0f) == kMin);
  CHECK(resolve(10.0f, 1.0f) == kMax);
  CHECK(resolve(kAuto, 8.0f) == kMax);
}

void test_every_offered_choice_survives_resolution() {
  for (const float factor : kChoices) {
    CHECK(resolve(factor, 1.0f) == factor);
  }
}

} // namespace

int main() {
  test_auto_follows_the_display();
  test_auto_falls_back_when_the_display_is_unknown();
  test_an_explicit_choice_wins_over_the_display();
  test_out_of_range_values_are_clamped();
  test_every_offered_choice_survives_resolution();

  std::cout << "All UI scale tests passed\n";
  return 0;
}
