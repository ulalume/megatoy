#pragma once

// Format-driven rules and file writing for saving a patch, kept free of
// session state so they can be tested without one.

#include "formats/patch_registry.hpp"
#include "ym2612/patch.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace patches {

/// Extension of `path`, lowercased, with the leading dot.
std::string lowercase_extension(const std::filesystem::path &path);

/**
 * May Save write straight back to this file?
 *
 * True only for single-patch formats megatoy can serialize. A bank (.dmf,
 * .fur, .opm, ...) holds other instruments that one patch would overwrite,
 * and read-only formats (.rym2612) cannot be written at all -- those go
 * through Save As instead. .ginpkg is always safe: saving appends a version
 * rather than replacing the file.
 */
bool can_overwrite_in_place(const std::filesystem::path &path);

/**
 * Serialize `patch` to `path` in the format named by its extension.
 * Text formats are written as text; .ginpkg appends a version.
 */
bool write_patch(const ym2612::Patch &patch, const std::filesystem::path &path);

/// The save dialog's formats: each extension of `formats` once, `first`
/// leading so the dialog starts on it.
std::vector<formats::SaveFormatInfo>
save_dialog_formats(const std::vector<formats::SaveFormatInfo> &formats,
                    std::string_view first);

/// What a save dialog is asked: where it starts, the name it offers, and the
/// formats it lists, starting on `extension`.
struct SavePathRequest {
  std::filesystem::path start_directory;
  std::string stem;
  std::vector<formats::SaveFormatInfo> formats;
  std::string extension;
};

/// What the user answered. `path` is set only when one was chosen.
struct SavePathAnswer {
  enum class Status { Chosen, Cancelled, Failed };
  Status status = Status::Cancelled;
  std::filesystem::path path;
};

/// `path` as the save dialog returned it, given `fallback`'s extension when
/// its own is none of `formats`.
std::filesystem::path
with_save_extension(std::filesystem::path path,
                    const std::vector<formats::SaveFormatInfo> &formats,
                    std::string_view fallback);

} // namespace patches
