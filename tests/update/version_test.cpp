#include "update/version.hpp"

#include "../test_check.hpp"
#include <iostream>

namespace {

void test_numeric_versions() {
  CHECK(update::is_version_newer("v0.8.0", "v0.7.0") == true);
  CHECK(update::is_version_newer("v0.7.0", "0.7.0") == false);
  CHECK(update::is_version_newer("v0.6.0", "v0.7.0") == false);
  CHECK(update::is_version_newer("0.10.0", "0.9.0") == true);
  CHECK(update::is_version_newer("2.0.0", "10.0.0") == false);
}

void test_prerelease_versions() {
  CHECK(update::is_version_newer("0.7.0", "0.7.0-rc.1") == true);
  CHECK(update::is_version_newer("0.7.0-rc.2", "0.7.0-rc.1") == true);
  CHECK(update::is_version_newer("0.7.0-rc.1", "0.7.0") == false);
  CHECK(update::is_version_newer("0.7.0+server", "0.7.0+local") == false);
}

void test_malformed_versions_are_rejected() {
  CHECK(!update::is_version_newer("latest", "0.7.0").has_value());
  CHECK(!update::is_version_newer("0.8", "0.7.0").has_value());
  CHECK(!update::is_version_newer("0.8.0", "dev").has_value());
  CHECK(!update::is_version_newer("0.8.0-", "0.7.0").has_value());
}

} // namespace

int main() {
  test_numeric_versions();
  test_prerelease_versions();
  test_malformed_versions_are_rejected();
  std::cout << "All version tests passed\n";
  return 0;
}
