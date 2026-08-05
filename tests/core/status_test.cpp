// The notification service: ordering, the bound, and dismissal.

#include "core/status.hpp"

#include "../test_check.hpp"
#include <iostream>
#include <string>

using megatoy::status::Severity;

int main() {
  megatoy::status::clear();
  CHECK(megatoy::status::entries().empty());

  megatoy::status::info("one");
  megatoy::status::success("two");
  megatoy::status::warning("three");
  megatoy::status::error("four");

  auto entries = megatoy::status::entries();
  CHECK(entries.size() == 4);
  CHECK(entries[0].message == "one");
  CHECK(entries[0].severity == Severity::Info);
  CHECK(entries[3].message == "four");
  CHECK(entries[3].severity == Severity::Error);

  // Ids are unique and increasing.
  CHECK(entries[0].id < entries[1].id);
  CHECK(entries[1].id < entries[2].id);

  // Dismissing removes exactly the targeted entry.
  megatoy::status::dismiss(entries[1].id);
  entries = megatoy::status::entries();
  CHECK(entries.size() == 3);
  CHECK(entries[0].message == "one");
  CHECK(entries[1].message == "three");

  // Dismissing an unknown id is a no-op.
  megatoy::status::dismiss(999999);
  CHECK(megatoy::status::entries().size() == 3);

  // Oldest entries fall off past the bound.
  megatoy::status::clear();
  for (int i = 0; i < 150; ++i) {
    megatoy::status::info("entry " + std::to_string(i));
  }
  entries = megatoy::status::entries();
  CHECK(entries.size() == 100);
  CHECK(entries.front().message == "entry 50");
  CHECK(entries.back().message == "entry 149");

  megatoy::status::clear();
  std::cout << "All status tests passed\n";
  return 0;
}
