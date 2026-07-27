#include "formats/patch_registry.hpp"
#include "../test_check.hpp"
#include <iostream>

int main() {
  const auto formats = formats::PatchRegistry::instance().export_formats();
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
  CHECK(has_dmp && "DMP export format should be registered");
  CHECK(has_mml && "MML export format should be registered");
  std::cout << "patch_registry_test passed\n";
  return 0;
}
