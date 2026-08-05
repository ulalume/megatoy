#pragma once

// Format-driven rules and file writing for saving a patch, kept free of
// session state so they can be tested without one.

#include "ym2612/patch.hpp"

#include <filesystem>
#include <string>

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

} // namespace patches
