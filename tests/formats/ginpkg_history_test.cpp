// The version history inside a .ginpkg: saving over a package archives the
// previous current patch, versions can be read back and deleted. This is the
// mechanism behind the Patch Versions panel, on desktop and web alike.

#include "formats/ginpkg.hpp"
#include "ym2612/patch.hpp"

#include "../test_check.hpp"
#include <filesystem>
#include <iostream>

namespace {

ym2612::Patch make_patch(const char *name, uint8_t algorithm) {
  ym2612::Patch patch;
  patch.name = name;
  patch.instrument.algorithm = algorithm;
  return patch;
}

} // namespace

int main() {
  const auto dir =
      std::filesystem::temp_directory_path() / "megatoy_ginpkg_history";
  std::filesystem::remove_all(dir);
  std::filesystem::create_directories(dir);

  // First save creates the package with no history.
  auto path = formats::ginpkg::save_patch(dir, make_patch("first", 1), "pkg");
  CHECK(path.has_value());
  {
    auto package = formats::ginpkg::load_package(*path);
    CHECK(package.has_value());
    CHECK(package->history().empty());
  }

  // Saving again archives the previous current patch as a version.
  CHECK(formats::ginpkg::save_patch(dir, make_patch("second", 2), "pkg")
            .has_value());
  std::string first_uuid;
  {
    auto package = formats::ginpkg::load_package(*path);
    CHECK(package.has_value());
    CHECK(package->history().size() == 1);
    first_uuid = package->history()[0].uuid;
    CHECK(!first_uuid.empty());
  }

  // The archived version still holds the older patch.
  auto archived = formats::ginpkg::read_version(*path, first_uuid);
  CHECK(archived.has_value());
  CHECK(archived->name == "first");
  CHECK(archived->instrument.algorithm == 1);

  // And the package's current patch is the newer one.
  {
    auto loaded = formats::ginpkg::read_file(*path);
    CHECK(loaded.size() == 1);
    CHECK(loaded[0].name == "second");
    CHECK(loaded[0].instrument.algorithm == 2);
  }

  // A third save stacks another version.
  CHECK(formats::ginpkg::save_patch(dir, make_patch("third", 3), "pkg")
            .has_value());
  {
    auto package = formats::ginpkg::load_package(*path);
    CHECK(package->history().size() == 2);
  }

  // Deleting a version removes it and keeps the rest intact.
  CHECK(formats::ginpkg::delete_version(*path, first_uuid));
  {
    auto package = formats::ginpkg::load_package(*path);
    CHECK(package->history().size() == 1);
    CHECK(package->history()[0].uuid != first_uuid);
    CHECK(!formats::ginpkg::read_version(*path, first_uuid).has_value());
  }

  std::filesystem::remove_all(dir);
  std::cout << "All ginpkg history tests passed\n";
  return 0;
}
