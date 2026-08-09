// A VGM file carries no instrument list -- it is a log of chip register
// writes, and ym2612_format reconstructs instruments from key-on snapshots
// (see vgm::parse). Loading one through the registry must therefore behave
// like any other bank format (.dmf/.fur/.opm/.mml): is_multi_patch() has to
// say so, and PatchRegistry::load() has to hand back
// PatchLoadStatus::MultiInstrument with every instrument found. VGZ is the
// same register log gzip-compressed, and must load identically even though
// ym2612_format's FormatInfo only advertises a single "vgm" extension.
//
// This synthesizes a minimal but spec-valid VGM byte stream in code --
// mirroring the make_vgm()/emit_op()/emit_test_patch() helpers in
// ym2612_format's own test/roundtrip_test.cpp -- rather than shipping a
// binary fixture, and gzip-wraps it with the same miniz-based framing as
// that file's gzip_wrap() helper for the .vgz half of the test.

#include "formats/patch_loader.hpp"
#include "formats/patch_registry.hpp"
#include "formats/ym2612_format_adapter.hpp"

#include "../test_check.hpp"
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <miniz.h>
#include <vector>

namespace {

/// Build a minimal v1.50 VGM around the given command stream (0x66
/// terminator appended automatically). Same shape as ym2612_format's
/// test/roundtrip_test.cpp make_vgm() helper.
std::vector<uint8_t> make_vgm(const std::vector<uint8_t> &commands) {
  std::vector<uint8_t> v(0x40, 0);
  v[0] = 'V';
  v[1] = 'g';
  v[2] = 'm';
  v[3] = ' ';
  auto put32 = [&v](size_t off, uint32_t val) {
    v[off] = val & 0xFF;
    v[off + 1] = (val >> 8) & 0xFF;
    v[off + 2] = (val >> 16) & 0xFF;
    v[off + 3] = (val >> 24) & 0xFF;
  };
  put32(0x08, 0x150);       // version 1.50
  put32(0x2C, 7670453);     // YM2612 clock
  put32(0x34, 0x40 - 0x34); // data offset -> 0x40
  v.insert(v.end(), commands.begin(), commands.end());
  v.push_back(0x66); // end of sound data
  // EOF offset: not read by the parser; kept so the fixture is a
  // spec-valid VGM.
  put32(0x04, static_cast<uint32_t>(v.size()) - 4);
  return v;
}

/// Append the YM2612 (port 0) writes for one operator slot of one channel.
void emit_op(std::vector<uint8_t> &c, int ch, int slot, uint8_t dtml,
            uint8_t tl, uint8_t ksar, uint8_t amdr, uint8_t sr,
            uint8_t slrr, uint8_t ssg) {
  uint8_t base = static_cast<uint8_t>(ch + slot * 4);
  c.insert(c.end(), {0x52, static_cast<uint8_t>(0x30 + base), dtml});
  c.insert(c.end(), {0x52, static_cast<uint8_t>(0x40 + base), tl});
  c.insert(c.end(), {0x52, static_cast<uint8_t>(0x50 + base), ksar});
  c.insert(c.end(), {0x52, static_cast<uint8_t>(0x60 + base), amdr});
  c.insert(c.end(), {0x52, static_cast<uint8_t>(0x70 + base), sr});
  c.insert(c.end(), {0x52, static_cast<uint8_t>(0x80 + base), slrr});
  c.insert(c.end(), {0x52, static_cast<uint8_t>(0x90 + base), ssg});
}

/// A full 4-op patch (algorithm 2, feedback 5) on the given channel, with
/// every operator given a real attack rate and an audible TL so the
/// extractor's is_silent() filter does not drop it. Same register values
/// as ym2612_format's emit_test_patch() helper.
void emit_test_patch(std::vector<uint8_t> &c, int ch) {
  emit_op(c, ch, 0, 0x12, 0x28, 0x5F, 0x8A, 0x05, 0x28, 0x0C);
  emit_op(c, ch, 1, 0x71, 0x00, 0x14, 0x00, 0x00, 0x06, 0x00);
  emit_op(c, ch, 2, 0x04, 0x10, 0x99, 0x03, 0x02, 0x17, 0x00);
  emit_op(c, ch, 3, 0x41, 0x08, 0x1C, 0x04, 0x01, 0x05, 0x00);
  c.insert(c.end(), {0x52, static_cast<uint8_t>(0xB0 + ch), 0x2A}); // fb=5 alg=2
  c.insert(c.end(), {0x52, static_cast<uint8_t>(0xB4 + ch), 0x92}); // pan/AMS/FMS
}

/// Two-instrument VGM: identical operator data on channels 0 and 1, but
/// channel 1's algorithm/feedback register is overwritten before its
/// key-on. vgm::same_instrument() compares algorithm/feedback first, so
/// that alone guarantees two distinct extracted patches -- no need to hand
/// craft two full operator sets.
std::vector<uint8_t> make_two_instrument_vgm() {
  std::vector<uint8_t> c;

  emit_test_patch(c, 0);
  c.insert(c.end(), {0x52, 0x28, 0xF0}); // key on channel 0 (all slots)

  emit_test_patch(c, 1);
  c.insert(c.end(), {0x52, 0xB1, 0x1B}); // overwrite: fb=3 alg=3
  c.insert(c.end(), {0x52, 0x28, 0xF1}); // key on channel 1 (all slots)

  return make_vgm(c);
}

/// Gzip-wrap a buffer (with an FNAME field to exercise header skipping) --
/// same RFC 1952 framing as ym2612_format's own test/roundtrip_test.cpp
/// gzip_wrap() helper: fixed 10-byte header (FNAME flag set), raw deflate
/// via miniz's tdefl, then the CRC32 + ISIZE trailer.
std::vector<uint8_t> gzip_wrap(const std::vector<uint8_t> &plain) {
  size_t def_len = 0;
  void *def = tdefl_compress_mem_to_heap(plain.data(), plain.size(), &def_len,
                                         128 /* max probes */);
  if (!def) {
    return {};
  }

  std::vector<uint8_t> gz = {0x1F, 0x8B, 0x08, 0x08, 0, 0, 0, 0, 0x00, 0xFF};
  const char *fname = "two_instruments.vgm";
  gz.insert(gz.end(), fname, fname + std::strlen(fname) + 1); // incl. NUL
  gz.insert(gz.end(), static_cast<uint8_t *>(def),
            static_cast<uint8_t *>(def) + def_len);
  mz_free(def);
  uint32_t crc = static_cast<uint32_t>(
      mz_crc32(MZ_CRC32_INIT, plain.data(), plain.size()));
  uint32_t isize = static_cast<uint32_t>(plain.size());
  for (uint32_t v32 : {crc, isize}) {
    for (int b = 0; b < 4; ++b) {
      gz.push_back(static_cast<uint8_t>((v32 >> (b * 8)) & 0xFF));
    }
  }
  return gz;
}

void write_bytes(const std::filesystem::path &path,
                 const std::vector<uint8_t> &bytes) {
  std::ofstream file(path, std::ios::binary);
  CHECK(static_cast<bool>(file));
  file.write(reinterpret_cast<const char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  CHECK(static_cast<bool>(file));
}

/// Shared assertions for both the .vgm and .vgz load paths: the registry
/// must report a bank load, with (at least) the two deliberately distinct
/// instruments make_two_instrument_vgm() encoded.
void check_two_distinct_instruments(const formats::PatchLoadResult &loaded,
                                    const char *label) {
  if (loaded.status != formats::PatchLoadStatus::MultiInstrument) {
    std::cerr << label << ": expected MultiInstrument, got status="
              << static_cast<int>(loaded.status)
              << " message=" << loaded.message << "\n";
    CHECK(false);
  }
  CHECK(loaded.patches.size() >= 2);
  if (loaded.patches[0].instrument.algorithm ==
      loaded.patches[1].instrument.algorithm) {
    std::cerr << label << ": both patches have the same algorithm\n";
    CHECK(false);
  }
}

} // namespace

int main() {
  // The browser relies on this to know a .vgm/.vgz load can hand back more
  // than one patch, the same way it does for .dmf/.fur/.opm/.mml banks.
  CHECK(formats::adapter::is_multi_patch(ym2612_format::Format::Vgm));

  const auto dir = std::filesystem::temp_directory_path() /
                   "megatoy_vgm_multi_instrument";
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
  std::filesystem::create_directories(dir, ec);

  const auto bytes = make_two_instrument_vgm();

  const auto vgm_path = dir / "two_instruments.vgm";
  write_bytes(vgm_path, bytes);
  const auto vgm_loaded = formats::PatchRegistry::instance().load(vgm_path);
  check_two_distinct_instruments(vgm_loaded, ".vgm");

  // .vgz is the same register log, gzip-compressed. FormatInfo only
  // advertises the "vgm" extension, so the registry has to special-case
  // ".vgz" onto the same handler (see patch_registry.cpp) for this to work
  // at all; vgm::parse() itself already gunzips transparently.
  const auto gz_bytes = gzip_wrap(bytes);
  CHECK(!gz_bytes.empty());
  const auto vgz_path = dir / "two_instruments.vgz";
  write_bytes(vgz_path, gz_bytes);
  const auto vgz_loaded = formats::PatchRegistry::instance().load(vgz_path);
  check_two_distinct_instruments(vgz_loaded, ".vgz");

  std::filesystem::remove_all(dir, ec);
  std::cout << "vgm_multi_instrument_test passed\n";
  return 0;
}
