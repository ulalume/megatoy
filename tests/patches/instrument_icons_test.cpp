#include "../test_check.hpp"
#include "patches/instrument_icons.hpp"
#include <cstring>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

using namespace patches::instrument_icons;

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
  CHECK(matches("grand-piano", "Grand Piano"));
  CHECK(matches("kick", "drums"));
  CHECK(matches("xylophone", "chromatic"));
  CHECK(!matches("kick", "piano"));
  std::cout << "test_filter passed" << std::endl;
}

void test_names() {
  CHECK(known("grand-piano"));
  CHECK(!known(""));
  CHECK(!known("Bright lead"));
  CHECK(display_name("grand-piano") == "Grand Piano");
  CHECK(display_name("triangle-wave") == "Triangle Wave");
  CHECK(display_name("fx") == "FX");
  CHECK(display_name("ufo") == "UFO");
  CHECK(group_name("kick") == "Drums");
  CHECK(group_name("Bright lead").empty());
  std::cout << "test_names passed" << std::endl;
}

void test_groups_follow_the_library_vocabulary() {
  std::ifstream file(YM2612_PATCHES_VOCABULARY);
  CHECK(file.good());
  const auto vocabulary = nlohmann::json::parse(file);
  const auto &library = vocabulary.at("icon");
  CHECK(library.size() == groups().size());
  std::size_t index = 0;
  for (const auto &group : groups()) {
    const auto &expected = library.at(index++);
    CHECK(expected.at("group").get<std::string>() == group.name);
    const auto &ids = expected.at("ids");
    CHECK(ids.size() == group.icons.size());
    for (std::size_t i = 0; i < ids.size(); ++i) {
      CHECK(ids.at(i).get<std::string>() == group.icons[i].id);
    }
  }
  std::cout << "test_groups_follow_the_library_vocabulary passed" << std::endl;
}

int main() {
  test_glyph_lookup();
  test_every_icon_has_its_own_glyph();
  test_filter();
  test_names();
  test_groups_follow_the_library_vocabulary();
  return 0;
}
