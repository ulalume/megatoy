#include "formats/ginpkg.hpp"
#include "formats/ym2612_format_adapter.hpp"
#include "patches/filesystem_patch_storage.hpp"
#include "patches/patch_repository.hpp"
#include "platform/import_pipeline.hpp"
#include "platform/std_file_system.hpp"
#include "ym2612/patch.hpp"

#include "../test_check.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <miniz.h>
#include <string>
#include <vector>

namespace {

void write_bytes(const std::filesystem::path &path,
                 const std::vector<std::uint8_t> &bytes) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  CHECK(static_cast<bool>(output));
  output.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  CHECK(static_cast<bool>(output));
}

void write_text(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  CHECK(static_cast<bool>(output));
  output << text;
  CHECK(static_cast<bool>(output));
}

ym2612::Patch sample_patch() {
  ym2612::Patch patch;
  patch.name = "Import validation";
  patch.instrument.algorithm = 3;
  patch.instrument.feedback = 5;
  for (std::size_t index = 0; index < 4; ++index) {
    auto &op = patch.instrument.operators[index];
    op.attack_rate = static_cast<std::uint8_t>(20 + index);
    op.decay_rate = static_cast<std::uint8_t>(8 + index);
    op.sustain_rate = static_cast<std::uint8_t>(4 + index);
    op.release_rate = static_cast<std::uint8_t>(3 + index);
    op.sustain_level = static_cast<std::uint8_t>(5 + index);
    op.total_level = static_cast<std::uint8_t>(16 + index);
    op.multiple = static_cast<std::uint8_t>(index + 1);
  }
  return patch;
}

void put_u32(std::vector<std::uint8_t> &bytes, std::size_t offset,
             std::uint32_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value);
  bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
  bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16);
  bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

void append_u32(std::vector<std::uint8_t> &bytes, std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    bytes.push_back(static_cast<std::uint8_t>(value >> shift));
  }
}

std::vector<std::uint8_t> make_dmf() {
  const char magic[] = ".DelekDefleMask.";
  std::vector<std::uint8_t> raw(magic, magic + 16);
  raw.push_back(0x18); // version
  raw.push_back(0x02); // Genesis
  raw.insert(raw.end(), {0, 0, 4, 16, 0, 0, 0, 0, 0, 0, 0, 0});
  append_u32(raw, 64); // rows per pattern
  raw.push_back(0);    // no pattern-matrix rows
  raw.push_back(1);    // one instrument
  raw.insert(raw.end(), {4, 'T', 'e', 's', 't', 1});
  raw.insert(raw.end(), {3, 5, 0, 0}); // alg/fb/fms/ams
  for (int op = 0; op < 4; ++op) {
    raw.insert(raw.end(), {0, 31, 8, 1, 5, 4, 20, 0, 0, 3, 4, 0});
  }
  mz_ulong compressed_size = mz_compressBound(raw.size());
  std::vector<std::uint8_t> compressed(compressed_size);
  CHECK(mz_compress2(compressed.data(), &compressed_size, raw.data(),
                     raw.size(), MZ_BEST_SPEED) == MZ_OK);
  compressed.resize(compressed_size);
  return compressed;
}

std::vector<std::uint8_t> make_fur(const ym2612::Patch &patch) {
  const auto fui =
      formats::adapter::serialize(ym2612_format::Format::Fui, patch);
  CHECK(fui.has_value());
  CHECK(fui->size() > 8);
  const char magic[] = "-Furnace module-";
  std::vector<std::uint8_t> bytes(magic, magic + 16);
  bytes.resize(32, 0);
  bytes[16] = 240;
  put_u32(bytes, 20, 32);
  bytes.insert(bytes.end(), {'I', 'N', 'F', '2'});
  append_u32(bytes, 0);
  bytes.insert(bytes.end(), 8, 0);         // eight empty metadata strings
  append_u32(bytes, 0);                    // tuning float
  bytes.push_back(0);                      // automatic system name
  append_u32(bytes, 0);                    // master-volume float
  bytes.insert(bytes.end(), {0, 0, 0, 0}); // channels/chips
  append_u32(bytes, 0);                    // patchbay count
  bytes.push_back(0);                      // automatic patchbay
  bytes.push_back(0x04);                   // instrument element list
  append_u32(bytes, 1);
  const auto pointer_offset = bytes.size();
  append_u32(bytes, 0);
  bytes.push_back(0); // element-list terminator
  put_u32(bytes, pointer_offset, static_cast<std::uint32_t>(bytes.size()));
  bytes.insert(bytes.end(), {'I', 'N', 'S', '2'});
  append_u32(bytes, static_cast<std::uint32_t>(fui->size() - 4));
  bytes.insert(bytes.end(), fui->begin() + 4, fui->end());
  return bytes;
}

std::vector<std::uint8_t> make_vgm() {
  std::vector<std::uint8_t> commands;
  for (int slot = 0; slot < 4; ++slot) {
    const auto base = static_cast<std::uint8_t>(slot * 4);
    const std::uint8_t writes[][2] = {
        {static_cast<std::uint8_t>(0x30 + base), 0x01},
        {static_cast<std::uint8_t>(0x40 + base), 0x10},
        {static_cast<std::uint8_t>(0x50 + base), 0x1f},
        {static_cast<std::uint8_t>(0x60 + base), 0x08},
        {static_cast<std::uint8_t>(0x70 + base), 0x04},
        {static_cast<std::uint8_t>(0x80 + base), 0x45},
        {static_cast<std::uint8_t>(0x90 + base), 0x00},
    };
    for (const auto &write : writes) {
      commands.insert(commands.end(), {0x52, write[0], write[1]});
    }
  }
  commands.insert(commands.end(), {0x52, 0xb0, 0x2b, 0x52, 0x28, 0xf0, 0x66});
  std::vector<std::uint8_t> bytes(0x40, 0);
  bytes[0] = 'V';
  bytes[1] = 'g';
  bytes[2] = 'm';
  bytes[3] = ' ';
  put_u32(bytes, 0x08, 0x150);
  put_u32(bytes, 0x2c, 7670453);
  put_u32(bytes, 0x34, 0x0c);
  bytes.insert(bytes.end(), commands.begin(), commands.end());
  put_u32(bytes, 0x04, static_cast<std::uint32_t>(bytes.size() - 4));
  return bytes;
}

std::vector<std::uint8_t> gzip_wrap(const std::vector<std::uint8_t> &plain) {
  std::size_t deflated_size = 0;
  void *deflated = tdefl_compress_mem_to_heap(plain.data(), plain.size(),
                                              &deflated_size, 128);
  CHECK(deflated != nullptr);
  std::vector<std::uint8_t> bytes = {0x1f, 0x8b, 0x08, 0, 0, 0, 0, 0, 0, 0xff};
  const auto *deflated_bytes = static_cast<const std::uint8_t *>(deflated);
  bytes.insert(bytes.end(), deflated_bytes, deflated_bytes + deflated_size);
  mz_free(deflated);
  append_u32(bytes, static_cast<std::uint32_t>(
                        mz_crc32(MZ_CRC32_INIT, plain.data(), plain.size())));
  append_u32(bytes, static_cast<std::uint32_t>(plain.size()));
  return bytes;
}

} // namespace

int main() {
  namespace fs = std::filesystem;
  namespace pipeline = platform::import_pipeline;

  const auto root = fs::temp_directory_path() / "megatoy_import_pipeline_test";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root, ec);
  CHECK(!ec);

  // The web filter and repository browser must never drift apart.
  const auto repository_extensions =
      patches::PatchRepository::supported_extensions();
  CHECK(repository_extensions == pipeline::supported_extensions());
  for (const auto &extension : repository_extensions) {
    CHECK(pipeline::supports_extension(extension));
    std::string uppercase = extension;
    std::transform(
        uppercase.begin(), uppercase.end(), uppercase.begin(),
        [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    CHECK(pipeline::supports_extension(uppercase));

    // Every dispatch branch rejects a missing/corrupted header with a useful
    // reason, including bank/container extensions that have no serializer.
    const auto corrupt = root / ("corrupt" + extension);
    write_bytes(corrupt, {0xff, 0x00, 0xff});
    const auto rejected = pipeline::validate_file(corrupt);
    CHECK(!rejected.valid);
    CHECK(!rejected.reason.empty());
  }
  CHECK(!pipeline::supports_extension(".txt"));
  CHECK(!pipeline::needs_confirmation(20, 2 * 1024 * 1024));
  CHECK(pipeline::needs_confirmation(21, 1));
  CHECK(pipeline::needs_confirmation(1, 2 * 1024 * 1024 + 1));

  // Every readable+writable library format supplies its own real sample.
  const auto patch = sample_patch();
  for (const auto &info : formats::adapter::known_formats()) {
    if (!info.can_read || !info.can_write ||
        info.format == ym2612_format::Format::Ginpkg) {
      continue;
    }
    const auto path =
        root / ("valid" + formats::adapter::extension_for(info.format));
    if (info.is_text) {
      auto text = formats::adapter::serialize_text(info.format, patch);
      CHECK(text.has_value());
      write_text(path, *text);
    } else {
      auto bytes = formats::adapter::serialize(info.format, patch);
      CHECK(bytes.has_value());
      write_bytes(path, *bytes);
    }
    const auto accepted = pipeline::validate_file(path);
    if (!accepted.valid) {
      std::cerr << path << ": " << accepted.reason << '\n';
      CHECK(false);
    }
  }

  // Read-only bank formats get synthesized genuine containers too.
  const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>
      read_only_samples = {
          {"valid.dmf", make_dmf()},
          {"valid.fur", make_fur(patch)},
          {"valid.opm",
           std::vector<std::uint8_t>{
               '/', '/',  'M', 'i', 'O', 'P',  'M', 'd',  'r', 'v',  '\n', '@',
               ':', '0',  ' ', 'T', 'e', 's',  't', '\n', 'C', 'H',  ':',  ' ',
               '1', '9',  '2', ' ', '0', ' ',  '0', ' ',  '3', ' ',  '5',  ' ',
               '1', '2',  '0', ' ', '0', '\n', 'M', '1',  ':', ' ',  '3',  '1',
               ' ', '0',  ' ', '0', ' ', '5',  ' ', '2',  ' ', '0',  ' ',  '0',
               ' ', '1',  '5', ' ', '3', ' ',  '0', ' ',  '0', '\n', 'C',  '1',
               ':', ' ',  '3', '1', ' ', '0',  ' ', '0',  ' ', '5',  ' ',  '2',
               ' ', '0',  ' ', '0', ' ', '1',  '5', ' ',  '3', ' ',  '0',  ' ',
               '0', '\n', 'M', '2', ':', ' ',  '3', '1',  ' ', '0',  ' ',  '0',
               ' ', '5',  ' ', '2', ' ', '0',  ' ', '0',  ' ', '1',  '5',  ' ',
               '3', ' ',  '0', ' ', '0', '\n', 'C', '2',  ':', ' ',  '3',  '1',
               ' ', '0',  ' ', '0', ' ', '5',  ' ', '2',  ' ', '0',  ' ',  '0',
               ' ', '1',  '5', ' ', '3', ' ',  '0', ' ',  '0', '\n'}},
          {"valid.rym2612",
           std::vector<std::uint8_t>{
               '<', 'P', 'A', 'R', 'A', 'M', ' ', 'i', 'd', '=', '"',
               'A', 'l', 'g', 'o', 'r', 'i', 't', 'h', 'm', '"', ' ',
               'v', 'a', 'l', 'u', 'e', '=', '"', '1', '"', '/', '>'}},
      };
  for (const auto &[name, bytes] : read_only_samples) {
    const auto path = root / name;
    write_bytes(path, bytes);
    const auto accepted = pipeline::validate_file(path);
    if (!accepted.valid) {
      std::cerr << name << ": " << accepted.reason << '\n';
      CHECK(false);
    }
  }
  const auto vgm = make_vgm();
  write_bytes(root / "valid.vgm", vgm);
  write_bytes(root / "valid.vgz", gzip_wrap(vgm));
  CHECK(pipeline::validate_file(root / "valid.vgm").valid);
  CHECK(pipeline::validate_file(root / "valid.vgz").valid);

  const auto package = formats::ginpkg::save_patch(root, patch, "valid_pkg");
  CHECK(package.has_value());
  const auto package_validation = pipeline::validate_file(*package);
  CHECK(package_validation.valid);
  CHECK(package_validation.warmed_container != nullptr);

  // The first repository scan consumes the validation parse instead of
  // opening a bank container a second time.
  const auto warm_root = root / "warm-cache";
  const auto warm_package =
      formats::ginpkg::save_patch(warm_root, patch, "warm_pkg");
  CHECK(warm_package.has_value());
  const auto warm_validation = pipeline::validate_file(*warm_package);
  CHECK(warm_validation.valid);
  CHECK(warm_validation.warmed_container != nullptr);
  pipeline::store_warmed_container(*warm_package,
                                   warm_validation.warmed_container);
  platform::StdFileSystem file_system;
  patches::FilesystemPatchStorage storage(file_system, warm_root, "warm", true,
                                          false);
  std::vector<patches::PatchEntry> tree;
  storage.append_entries(tree);
  CHECK(storage.container_parse_count_for_testing() == 0);
  CHECK(!tree.empty());

  // Lightweight MML validation checks text shape only; it does not compile.
  const auto loose_mml = root / "loose.mml";
  write_text(loose_mml, "@ broken-but-textual instrument definition\n");
  CHECK(pipeline::validate_file(loose_mml).valid);

  // Commit retains only validated relative paths and atomically renames the
  // stage. Abort removes staging while leaving an existing workspace alone.
  const auto stage = root / ".import-tmp-commit";
  const auto destination = root / "Imported";
  write_text(stage / "nested" / "valid.mml", "@ valid\n");
  write_text(stage / "nested" / "rejected.txt", "unrelated\n");
  {
    pipeline::ImportStager stager(stage, destination);
    std::string error;
    CHECK(stager.commit({"nested/valid.mml"}, error));
    CHECK(error.empty());
    CHECK(stager.committed());
  }
  CHECK(!fs::exists(stage));
  CHECK(fs::exists(destination / "nested" / "valid.mml"));
  CHECK(!fs::exists(destination / "nested" / "rejected.txt"));

  const auto abort_stage = root / ".import-tmp-abort";
  const auto untouched = root / "Existing Workspace";
  write_text(abort_stage / "pending.mml", "@ pending\n");
  write_text(untouched / "keep.txt", "keep\n");
  {
    pipeline::ImportStager stager(abort_stage, root / "Never Created");
    stager.abort();
  }
  CHECK(!fs::exists(abort_stage));
  CHECK(fs::exists(untouched / "keep.txt"));
  CHECK(!fs::exists(root / "Never Created"));

  // A post-commit sync failure can roll the rename back and remove it.
  const auto rollback_stage = root / ".import-tmp-rollback";
  const auto rollback_destination = root / "Rolled Back";
  write_text(rollback_stage / "valid.mml", "@ valid\n");
  {
    pipeline::ImportStager stager(rollback_stage, rollback_destination);
    std::string error;
    CHECK(stager.commit({"valid.mml"}, error));
    CHECK(stager.rollback_commit(error));
  }
  CHECK(!fs::exists(rollback_stage));
  CHECK(!fs::exists(rollback_destination));

  fs::remove_all(root, ec);
  std::cout << "import_pipeline_test passed\n";
  return 0;
}
