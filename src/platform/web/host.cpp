#include "platform/host.hpp"

#include "platform/web/local_storage.hpp"
#include "platform/web/web_keyboard_layout.hpp"
#include "platform/web/web_patch_url.hpp"
#include "platform/web/web_storage_bootstrap.hpp"
#include "platform/web/web_storage_persistence.hpp"
#include "system/path_service.hpp"

namespace platform::host {

namespace {
constexpr const char *kSettingsKey = "megatoy_preferences";
}

void files_written() { web::request_storage_persist(); }

void current_patch_changed(const ym2612::Patch &patch) {
  web::patch_url::sync_patch_to_url_if_needed(patch);
}

bool seed_workspace(megatoy::workspace::Workspace &workspace) {
  return web::bootstrap_workspace(
      workspace, megatoy::system::PathService::web_storage_root());
}

std::filesystem::path required_workspace_folder() {
  return web::default_workspace_folder();
}

bool keeps_settings() { return true; }

std::optional<std::string> read_settings() {
  return web::read_local_storage(kSettingsKey);
}

bool write_settings(const std::string &text) {
  return web::write_local_storage(kSettingsKey, text);
}

bool has_keyboard_layout() { return false; }

std::vector<LayoutKey> take_keyboard_layout() {
  return web::take_keyboard_layout();
}

} // namespace platform::host
