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

/// The name used when there is none to start from.
inline constexpr std::string_view kFallbackSaveAsStem = "patch";

/// The name a copy of `stem` opens with.
std::string duplicate_stem(std::string_view stem);

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
save_as_folder_choices(const std::vector<megatoy::workspace::Folder> &folders);

/// Where Save writes, so the dialog can tell that the name is taken.
std::filesystem::path save_as_target_path(const std::filesystem::path &folder,
                                          std::string_view stem,
                                          std::string_view extension);

/// The folder the dialog opens on, limited to the ones it can offer.
std::filesystem::path
save_as_initial_folder(const std::vector<megatoy::workspace::Folder> &choices,
                       const std::filesystem::path &preferred);

/// Whether the folder is worth asking about.
bool save_as_shows_folder_choice(
    const std::vector<megatoy::workspace::Folder> &choices);

} // namespace ui
