#pragma once

// What the app asks of whatever it runs in, the desktop or a browser page,
// where the two differ and the caller should not have to know which.

#include "ym2612/patch.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace megatoy::workspace {
class Workspace;
}

namespace platform::host {

/// Files in the workspace were written. A host whose storage is only
/// flushed on request flushes it.
void files_written();

/// `patch` is the one being edited. A host that shows it outside the app,
/// as the page's address does, follows.
void current_patch_changed(const ym2612::Patch &patch);

/// Gives the workspace the folders this host starts it with. True when the
/// workspace changed.
bool seed_workspace(megatoy::workspace::Workspace &workspace);

/// The folder the workspace always keeps, or empty where there is none.
std::filesystem::path required_workspace_folder();

/// True when the host keeps the settings itself instead of in a file.
bool keeps_settings();
std::optional<std::string> read_settings();
bool write_settings(const std::string &text);

/// A key of the keyboard's layout: its place by the name browsers give it
/// ("KeyA"), and the character it types.
struct LayoutKey {
  std::string place;
  char32_t typed = 0;
};

/// True when SDL has the keyboard's layout from the system. Where it has
/// not, the layout is come to know of key by key.
bool has_keyboard_layout();

/// The keys of the keyboard's layout the host has come to know of since the
/// last call. A host that leaves the layout to SDL gives none.
std::vector<LayoutKey> take_keyboard_layout();

} // namespace platform::host
