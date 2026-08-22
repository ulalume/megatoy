#include "platform/web/web_folder_delete.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include "gui/components/common.hpp"
#include "gui/components/modal.hpp"
#include "platform/web/local_storage.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdint>
#include <emscripten.h>
#include <imgui.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <system_error>
#include <utility>

namespace platform::web {

EM_JS_DEPS(megatoy_folder_delete, "$stringToNewUTF8");

namespace {

constexpr const char *kPendingDeletionsKey = "megatoy.pendingFolderDeletions";
constexpr const char *kPopupId = "Deleting Folder###FolderDelete";

struct ActiveDelete {
  /// Identity handed to JavaScript. Deliberately NOT the slot address: a
  /// finished deletion frees the slot, and the next one can land on the very
  /// same heap address, so a stale JS callback would mistake the new flow for
  /// its own. A monotonic token can never be reused, and nothing ever
  /// dereferences it. (Same reasoning as PendingSelection in
  /// web_folder_import.cpp.)
  void *token = nullptr;
  std::filesystem::path path;
  std::string name;
  std::function<void(bool, std::string)> on_complete;
};

std::unique_ptr<ActiveDelete> g_active_delete;
std::uintptr_t g_next_delete_token = 1;
bool g_close_delete_popup = false;

std::vector<std::string> read_pending_list() {
  const auto stored = read_local_storage(kPendingDeletionsKey);
  if (!stored.has_value()) {
    return {};
  }
  // Non-throwing parse: a value mangled by another tool, an older build, or a
  // half-written quota failure must degrade to "no tombstones", never to an
  // exception thrown out of startup.
  const auto json = nlohmann::json::parse(*stored, nullptr, false);
  if (json.is_discarded() || !json.is_array()) {
    return {};
  }

  std::vector<std::string> paths;
  paths.reserve(json.size());
  for (const auto &entry : json) {
    if (!entry.is_string()) {
      continue;
    }
    auto value = entry.get<std::string>();
    if (!value.empty()) {
      paths.push_back(std::move(value));
    }
  }
  return paths;
}

void write_pending_list(const std::vector<std::string> &paths) {
  if (paths.empty()) {
    remove_local_storage(kPendingDeletionsKey);
    return;
  }
  write_local_storage(kPendingDeletionsKey, nlohmann::json(paths).dump());
}

void finish_active(bool ok, std::string error) {
  if (!g_active_delete) {
    return;
  }
  auto active = std::move(g_active_delete);
  g_close_delete_popup = true;
  if (ok) {
    clear_pending_deletion(active->path);
  }
  if (active->on_complete) {
    active->on_complete(ok, std::move(error));
  }
}

} // namespace

// clang-format off
EM_JS(void, megatoy_delete_sync_js, (void *token), {
  FS.syncfs(false, function(error) {
    var ptr = 0;
    if (error) ptr = stringToNewUTF8("" + error);
    Module["_megatoy_folder_delete_sync_done"](token, error ? 0 : 1, ptr);
    if (ptr) _free(ptr);
  });
});
// clang-format on

extern "C" {

/// An unknown token means the flow it belonged to is already settled, so the
/// late callback has nothing to report to.
EMSCRIPTEN_KEEPALIVE void megatoy_folder_delete_sync_done(void *token, int ok,
                                                          const char *error) {
  if (!g_active_delete || g_active_delete->token != token) {
    return;
  }
  // A failed flush keeps the tombstone: the next startup replays the deletion
  // rather than letting IndexedDB hand the folder back.
  finish_active(
      ok != 0,
      ok ? std::string{}
         : std::string(error != nullptr ? error : "storage sync failed"));
}

} // extern "C"

void add_pending_deletion(const std::filesystem::path &path) {
  auto value = path.string();
  if (value.empty()) {
    return;
  }
  auto paths = read_pending_list();
  if (std::find(paths.begin(), paths.end(), value) != paths.end()) {
    return;
  }
  paths.push_back(std::move(value));
  write_pending_list(paths);
}

void clear_pending_deletion(const std::filesystem::path &path) {
  const auto value = path.string();
  auto paths = read_pending_list();
  const auto tail = std::remove(paths.begin(), paths.end(), value);
  if (tail == paths.end()) {
    return;
  }
  paths.erase(tail, paths.end());
  write_pending_list(paths);
}

std::vector<std::filesystem::path> pending_deletions() {
  std::vector<std::filesystem::path> result;
  for (auto &value : read_pending_list()) {
    result.emplace_back(std::move(value));
  }
  return result;
}

void on_persist_succeeded() {
  auto paths = read_pending_list();
  if (paths.empty()) {
    return;
  }

  // The flush that just committed covered every MEMFS change made before it
  // started, so any recorded directory that is no longer there is now no
  // longer in IndexedDB either. One that still exists was not fully removed
  // (a replay is mid-flight, or removal failed); its tombstone stays.
  std::vector<std::string> remaining;
  for (auto &value : paths) {
    std::error_code ec;
    if (std::filesystem::exists(std::filesystem::path(value), ec) && !ec) {
      remaining.push_back(std::move(value));
    }
  }
  if (remaining.size() == paths.size()) {
    return;
  }
  write_pending_list(remaining);
}

bool begin_folder_delete(
    const std::filesystem::path &path,
    std::function<void(bool ok, std::string error)> on_complete) {
  if (g_active_delete) {
    return false;
  }

  auto active = std::make_unique<ActiveDelete>();
  active->token = reinterpret_cast<void *>(g_next_delete_token++);
  active->path = path;
  active->name = path.filename().string();
  if (active->name.empty()) {
    active->name = path.string();
  }
  active->on_complete = std::move(on_complete);

  // Tombstone first, and only then touch the filesystem: the whole point is
  // that a reload at any instant from here on finds a record of the deletion.
  add_pending_deletion(path);

  std::error_code error;
  std::filesystem::remove_all(path, error);
  if (error) {
    // Whatever was removed before the failure has left MEMFS but not
    // IndexedDB, so the tombstone stays and startup finishes the job.
    if (active->on_complete) {
      active->on_complete(false, error.message());
    }
    return true;
  }

  // MEMFS is already clear, so the UI can show the folder as gone; the flush
  // below is what makes that true after a reload. It is deliberately not the
  // debounced persist -- there is no completion to wait on there.
  auto *token = active->token;
  g_active_delete = std::move(active);
  megatoy_delete_sync_js(token);
  return true;
}

void render_folder_delete_ui() {
  if (!g_active_delete && g_close_delete_popup) {
    if (ImGui::IsPopupOpen(kPopupId) &&
        ui::begin_modal(kPopupId, ui::ModalDismiss::None).visible) {
      ImGui::CloseCurrentPopup();
      ui::end_modal();
    }
    g_close_delete_popup = false;
  }

  if (!g_active_delete) {
    return;
  }

  const auto &active = *g_active_delete;
  // Open on the edge only: re-opening an already-open popup every frame
  // resets popup state and can swallow clicks (see confirmation_dialog).
  if (!ImGui::IsPopupOpen(kPopupId)) {
    ImGui::OpenPopup(kPopupId);
  }
  if (ui::begin_modal(kPopupId, ui::ModalDismiss::None).visible) {
    ImGui::Text("Deleting \"%s\"...", active.name.c_str());
    ImGui::TextDisabled("Saving the change to browser storage.");
    // Indeterminate: IndexedDB reports no progress, only completion. No
    // Cancel either -- the files are already gone from MEMFS, and stopping
    // the flush would leave storage in exactly the state this fixes.
    ImGui::ProgressBar(static_cast<float>(-1.0 * ImGui::GetTime()),
                       ImVec2(-FLT_MIN, 0));
    ui::end_modal();
  }
}

} // namespace platform::web

#endif
