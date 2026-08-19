#include "core/utf8_utils.hpp"

#include "../test_check.hpp"
#include <iostream>
#include <string>

int main() {
  using megatoy::utf8::trim_incomplete_suffix;
  using megatoy::utf8::truncate;

  CHECK(truncate("ASCII patch", 63) == "ASCII patch");

  std::string japanese = "A";
  std::string expected = "A";
  for (int index = 0; index < 21; ++index) {
    japanese += "名";
    if (index < 20) {
      expected += "名";
    }
  }
  CHECK(japanese.size() == 64);
  CHECK(expected.size() == 61);
  CHECK(truncate(japanese, 63) == expected);

  std::string corrupted = "有効";
  corrupted.push_back(static_cast<char>(0xe5));
  CHECK(trim_incomplete_suffix(corrupted) == "有効");

  std::cout << "All UTF-8 utility tests passed\n";
  return 0;
}
