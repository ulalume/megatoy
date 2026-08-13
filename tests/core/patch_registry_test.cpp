#include "../test_check.hpp"
#include "formats/patch_registry.hpp"
#include <array>
#include <iostream>

int main() {
  const auto formats = formats::PatchRegistry::instance().save_formats();
  bool has_dmp = false;
  bool has_mml = false;
  for (const auto &fmt : formats) {
    if (fmt.extension == ".dmp") {
      has_dmp = true;
      CHECK(!fmt.is_text);
    }
    if (fmt.extension == ".mml") {
      has_mml = true;
      CHECK(fmt.is_text);
    }
  }
  CHECK(has_dmp && "DMP save format should be registered");
  CHECK(has_mml && "MML save format should be registered");

  static constexpr std::array<const char *, 7> expected_order = {
      ".gin", ".dmp", ".fui", ".eif", ".tfi", ".vgi", ".mml"};
  CHECK(formats.size() >= expected_order.size());
  for (std::size_t i = 0; i < expected_order.size(); ++i) {
    CHECK(formats[i].extension == expected_order[i]);
  }
  CHECK(formats.front().label == "megatoy");
  std::cout << "patch_registry_test passed\n";
  return 0;
}
