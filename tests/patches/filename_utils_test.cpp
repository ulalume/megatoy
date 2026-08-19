#include "patches/filename_utils.hpp"

#include "../test_check.hpp"
#include <iostream>

int main() {
  using patches::append_extension_if_missing;

  CHECK(append_extension_if_missing("Bass 2.0", ".gin") == "Bass 2.0.gin");
  CHECK(append_extension_if_missing("bass.GIN", ".gin") == "bass.GIN");
  CHECK(append_extension_if_missing("bass.gIn", "GIN") == "bass.gIn");
  CHECK(append_extension_if_missing("bass", "") == "bass");
  CHECK(append_extension_if_missing("bass", ".gin") == "bass.gin");

  std::cout << "All filename utility tests passed\n";
  return 0;
}
