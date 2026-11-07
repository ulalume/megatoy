#include "../test_check.hpp"
#include "gui/instrument_icons.hpp"
#include <cstring>
#include <iostream>
#include <set>
#include <string>

using namespace ui::instrument_icons;

void test_glyph_lookup() {
  CHECK(std::strcmp(glyph(""), none_glyph()) == 0);
  CHECK(std::strcmp(glyph("Bright lead"), none_glyph()) == 0);
  CHECK(std::strcmp(glyph("grand-piano"), none_glyph()) != 0);
  CHECK(std::strcmp(glyph("triangle"), glyph("triangle-wave")) != 0);
  std::cout << "test_glyph_lookup passed" << std::endl;
}

void test_every_icon_has_its_own_glyph() {
  std::set<std::string> ids;
  std::set<std::string> glyphs{none_glyph()};
  std::size_t count = 0;
  for (const auto &group : groups()) {
    CHECK(!group.icons.empty());
    for (const auto &icon : group.icons) {
      ids.emplace(icon.id);
      glyphs.emplace(icon.glyph);
      CHECK(std::strcmp(glyph(icon.id), icon.glyph) == 0);
      ++count;
    }
  }
  CHECK(count == 86);
  CHECK(ids.size() == count);
  CHECK(glyphs.size() == count + 1);
  CHECK(groups().front().name == "Piano");
  CHECK(groups().back().name == "Sound Effects");
  std::cout << "test_every_icon_has_its_own_glyph passed" << std::endl;
}

void test_filter() {
  CHECK(matches("grand-piano", ""));
  CHECK(matches("grand-piano", "PIANO"));
  CHECK(matches("grand-piano", "d-p"));
  CHECK(!matches("grand-piano", "guitar"));
  CHECK(!matches("bell", "bells"));
  CHECK(matches("", ""));
  std::cout << "test_filter passed" << std::endl;
}

int main() {
  test_glyph_lookup();
  test_every_icon_has_its_own_glyph();
  test_filter();
  return 0;
}
