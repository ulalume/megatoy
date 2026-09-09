#pragma once

// What the Save As dialog starts with. Kept apart from the dialog itself so
// the choices can be checked without an ImGui frame.

#include "formats/patch_registry.hpp"
#include "workspace/workspace.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ui {

/// The format offered when the patch's own is not one megatoy can write.
inline constexpr std::string_view kFallbackSaveAsExtension = ".dmp";

/// The format the dialog opens on: the patch's own when it is writable.
std::string
default_save_as_extension(std::string_view current_extension,
                          const std::vector<formats::SaveFormatInfo> &formats);

/// The folder the dialog opens on: the patch's own when it can be written to.
std::filesystem::path default_save_as_folder(
    const std::optional<std::filesystem::path> &writable_source_folder,
    const std::filesystem::path &default_folder);

/// The folders Save can write into, in the order the browser lists them.
std::vector<megatoy::workspace::Folder>
save_as_folder_choices(const megatoy::workspace::Workspace &workspace);

} // namespace ui
