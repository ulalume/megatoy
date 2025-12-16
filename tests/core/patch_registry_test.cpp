#include "formats/patch_registry.hpp"
#include <cassert>
#include <iostream>

int main() {
  const auto formats = formats::PatchRegistry::instance().export_formats();
  bool has_dmp = false;
  bool has_mml = false;
  for (const auto &fmt : formats) {
    if (fmt.extension == ".dmp") {
      has_dmp = true;
      assert(!fmt.is_text);
    }
    if (fmt.extension == ".mml") {
      has_mml = true;
      assert(fmt.is_text);
    }
  }
  assert(has_dmp && "DMP export format should be registered");
  assert(has_mml && "MML export format should be registered");
  std::cout << "patch_registry_test passed\n";
  return 0;
}
