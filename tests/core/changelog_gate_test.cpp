#include "../test_check.hpp"
#include "changelog_gate.hpp"

#include <iostream>

namespace {

using megatoy::ChangelogAction;
using megatoy::decide_changelog_action;

void test_a_fresh_install_is_announced() {
  CHECK(decide_changelog_action("", "v0.8.1") == ChangelogAction::Show);
}

// Someone upgrading from a build that predates the change log has no stored
// version either. Indistinguishable from a fresh install, and announced the
// same way.
void test_an_upgrade_is_announced() {
  CHECK(decide_changelog_action("v0.8.0", "v0.8.1") == ChangelogAction::Show);
  CHECK(decide_changelog_action("v0.6.0", "v0.8.1") == ChangelogAction::Show);
  CHECK(decide_changelog_action("0.8.0", "0.8.1") == ChangelogAction::Show);
}

void test_the_same_version_says_nothing() {
  CHECK(decide_changelog_action("v0.8.1", "v0.8.1") == ChangelogAction::Nothing);
}

// Running an older build after a newer one -- bisecting, or keeping two
// installs around -- must not claim the app was updated.
void test_a_downgrade_is_recorded_but_not_announced() {
  CHECK(decide_changelog_action("v0.8.1", "v0.8.0") ==
        ChangelogAction::RecordOnly);
}

void test_a_version_that_cannot_be_read_is_recorded_but_not_announced() {
  CHECK(decide_changelog_action("garbage", "v0.8.1") ==
        ChangelogAction::RecordOnly);
}

void test_no_current_version_does_nothing() {
  CHECK(decide_changelog_action("v0.8.0", "") == ChangelogAction::Nothing);
  CHECK(decide_changelog_action("", "") == ChangelogAction::Nothing);
}

// The leading v is optional on either side; the same version spelled two ways
// is still the same version.
void test_the_v_prefix_is_not_part_of_the_version() {
  CHECK(decide_changelog_action("0.8.1", "v0.8.1") ==
        ChangelogAction::RecordOnly);
  CHECK(decide_changelog_action("v0.8.0", "0.8.1") == ChangelogAction::Show);
}

// A prerelease is older than the release it leads to.
void test_a_prerelease_upgrading_to_its_release_is_announced() {
  CHECK(decide_changelog_action("v0.9.0-beta.1", "v0.9.0") ==
        ChangelogAction::Show);
  CHECK(decide_changelog_action("v0.9.0", "v0.9.0-beta.1") ==
        ChangelogAction::RecordOnly);
}

} // namespace

int main() {
  test_a_fresh_install_is_announced();
  test_an_upgrade_is_announced();
  test_the_same_version_says_nothing();
  test_a_downgrade_is_recorded_but_not_announced();
  test_a_version_that_cannot_be_read_is_recorded_but_not_announced();
  test_no_current_version_does_nothing();
  test_the_v_prefix_is_not_part_of_the_version();
  test_a_prerelease_upgrading_to_its_release_is_announced();

  std::cout << "All change log gate tests passed\n";
  return 0;
}
