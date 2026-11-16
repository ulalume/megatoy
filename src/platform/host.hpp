#pragma once

// What the app asks of whatever it runs in, the desktop or a browser page,
// where the two differ and the caller should not have to know which.

#include "ym2612/patch.hpp"

#include <filesystem>
#include <optional>
#include <string>

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

} // namespace platform::host
