#include "platform/host.hpp"

namespace platform::host {

void files_written() {}

void current_patch_changed(const ym2612::Patch &) {}

bool seed_workspace(megatoy::workspace::Workspace &) { return false; }

std::filesystem::path required_workspace_folder() { return {}; }

bool keeps_settings() { return false; }

std::optional<std::string> read_settings() { return std::nullopt; }

bool write_settings(const std::string &) { return false; }

bool has_keyboard_layout() { return true; }

std::vector<LayoutKey> take_keyboard_layout() { return {}; }

} // namespace platform::host
