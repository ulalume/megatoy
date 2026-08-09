#pragma once

#include "ym2612/patch.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <ym2612_format/converter.hpp>
#include <ym2612_format/detune.hpp>
#include <ym2612_format/format.hpp>
#include <ym2612_format/patch.hpp>

/**
 * Bridge between megatoy's ym2612::Patch and the ym2612_format library.
 *
 * megatoy groups a patch into global / channel / instrument sections because
 * that mirrors the chip's register map and the editor's layout; the library
 * keeps one flat struct. The two carry the same information, so conversion is
 * total in both directions -- except for the library's macro data, which the
 * editor has no concept of and therefore drops.
 */
namespace formats::adapter {

ym2612_format::Patch to_library(const ym2612::Patch &patch);
ym2612::Patch from_library(const ym2612_format::Patch &patch);

/// Extension including the leading dot, e.g. ".dmp".
std::string extension_for(ym2612_format::Format format);

std::optional<ym2612_format::Format>
format_for_extension(const std::string &extension);

/// Every format the library knows about.
const std::vector<ym2612_format::FormatInfo> &known_formats();

/// Readable extensions, with the leading dot, sorted.
const std::vector<std::string> &readable_extensions();

/**
 * True for formats that can hold more than one instrument, so the browser
 * expands them into a folder instead of showing a single entry.
 *
 * This is a fixed list rather than "parse it and count", because the browser
 * would otherwise have to parse every file in the library just to draw the
 * tree.
 */
bool is_multi_patch(ym2612_format::Format format);

/**
 * Detune conversion between the hardware register encoding (0-7) that
 * megatoy stores and the linear -3..+3 scale that editors and most file
 * formats present.
 *
 * Conversion delegates to the library. Since ym2612_format v0.2.1 it decodes
 * the linear zero point as register 0, so a save/reload of a detune-0 patch
 * stays a no-op.
 */
uint8_t detune_to_linear(uint8_t register_detune);
uint8_t detune_from_linear(int linear);

/// Read a file and convert every patch it contains. Empty on failure.
std::vector<ym2612::Patch> read_file(ym2612_format::Format format,
                                     const std::filesystem::path &path);

bool write_file(ym2612_format::Format format, const ym2612::Patch &patch,
                const std::filesystem::path &path);

/// Serialize without touching the filesystem, for callers that hand bytes to
/// a download or a clipboard.
std::optional<std::vector<uint8_t>> serialize(ym2612_format::Format format,
                                              const ym2612::Patch &patch);

std::optional<std::string> serialize_text(ym2612_format::Format format,
                                          const ym2612::Patch &patch);

} // namespace formats::adapter
