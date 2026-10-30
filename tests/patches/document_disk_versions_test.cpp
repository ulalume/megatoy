#include "../test_check.hpp"
#include "formats/patch_loader.hpp"
#include "patches/document_disk_versions.hpp"
#include "patches/patch_write.hpp"
#include "platform/std_file_system.hpp"

#include <chrono>
#include <filesystem>
#include <memory>

namespace fs = std::filesystem;

namespace {

struct Fixture {
  fs::path root;
  platform::StdFileSystem file_system;
  megatoy::workspace::Workspace workspace;
  patches::PatchRepository repository{file_system, workspace};
  patches::DocumentDiskVersions versions{repository};
  patches::DocumentDiskVersions::Documents documents;

  Fixture() {
    root = fs::temp_directory_path() / "megatoy_disk_versions_test";
    fs::remove_all(root);
    fs::create_directories(root / "workspace");
    root = fs::weakly_canonical(root);
    CHECK(workspace.add(root / "workspace"));
    repository.sync_workspace();
  }

  ~Fixture() { fs::remove_all(root); }

  patches::PatchDocument &open(const fs::path &path) {
    auto loaded = formats::load_patch_from_file(path);
    CHECK(loaded.status == formats::PatchLoadStatus::Success);
    CHECK(loaded.patches.size() == 1);
    auto document = std::make_unique<patches::PatchDocument>(
        static_cast<patches::DocumentId>(documents.size() + 1));
    document->patch() = loaded.patches.front();
    document->set_path(repository.to_relative_path(path).generic_string());
    document->set_selection_path(document->path());
    document->set_file_identity(path);
    document->mark_as_clean();
    documents.push_back(std::move(document));
    versions.remember(*documents.back());
    return *documents.back();
  }
};

void write(const fs::path &path, int algorithm) {
  ym2612::Patch patch;
  patch.instrument.algorithm = static_cast<uint8_t>(algorithm);
  CHECK(patches::write_patch(patch, path));
}

void rewrite(const fs::path &path, int algorithm) {
  const auto time = fs::last_write_time(path);
  write(path, algorithm);
  fs::last_write_time(path, time + std::chrono::seconds(1));
}

void test_workspace_and_outside_writes() {
  Fixture env;
  const auto inside = env.root / "workspace" / "inside.dmp";
  const auto outside = env.root / "outside.dmp";
  write(inside, 1);
  write(outside, 2);
  env.repository.refresh();
  auto &first = env.open(inside);
  auto &second = env.open(outside);
  first.patch().instrument.algorithm = 7;
  CHECK(env.versions.take(env.documents).empty());

  rewrite(inside, 3);
  env.repository.refresh();
  CHECK(!env.versions.outside_file_changed(env.documents));
  const auto versions = env.versions.take(env.documents);
  CHECK(versions.size() == 1);
  CHECK(versions[0].document == first.id());
  CHECK(versions[0].patch.instrument.algorithm == 3);
  CHECK(first.patch().instrument.algorithm == 7);
  CHECK(first.original_patch().instrument.algorithm == 1);
  CHECK(env.versions.take(env.documents).empty());

  rewrite(outside, 4);
  CHECK(env.versions.outside_file_changed(env.documents));
  const auto external = env.versions.take(env.documents);
  CHECK(external.size() == 1);
  CHECK(external[0].document == second.id());
  CHECK(external[0].patch.name == "outside");
  CHECK(external[0].patch.instrument.algorithm == 4);
  CHECK(!env.versions.outside_file_changed(env.documents));

  second.set_original_patch(external[0].patch);
  rewrite(outside, 4);
  CHECK(env.versions.outside_file_changed(env.documents));
  CHECK(env.versions.take(env.documents).empty());
}

void test_remember_save_and_forget_unavailable_documents() {
  Fixture env;
  const auto path = env.root / "outside.dmp";
  write(path, 1);
  auto &document = env.open(path);
  rewrite(path, 2);
  env.versions.remember(document);
  CHECK(!env.versions.outside_file_changed(env.documents));
  CHECK(env.versions.take(env.documents).empty());

  document.set_path({});
  env.versions.remember(document);
  document.set_path(path.generic_string());
  rewrite(path, 3);
  CHECK(!env.versions.outside_file_changed(env.documents));
  CHECK(env.versions.take(env.documents).empty());

  env.versions.remember(document);
  fs::remove(path);
  env.versions.remember(document);
  write(path, 4);
  CHECK(env.versions.take(env.documents).empty());

  env.versions.remember(document);
  const auto id = document.id();
  const auto remembered = document.path();
  env.documents.clear();
  CHECK(env.versions.take(env.documents).empty());
  CHECK(!env.versions.outside_file_changed(env.documents));

  // Taking forgot the closed document, so its id has no time recorded.
  env.documents.push_back(std::make_unique<patches::PatchDocument>(id));
  env.documents.back()->set_path(remembered);
  rewrite(path, 5);
  CHECK(!env.versions.outside_file_changed(env.documents));
  CHECK(env.versions.take(env.documents).empty());
}

} // namespace

int main() {
  test_workspace_and_outside_writes();
  test_remember_save_and_forget_unavailable_documents();
}
