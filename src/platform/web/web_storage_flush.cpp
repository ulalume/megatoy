#include "platform/web/web_storage_flush.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include "gui/components/modal.hpp"

#include <cfloat>
#include <cstdint>
#include <emscripten.h>
#include <imgui.h>
#include <memory>
#include <utility>

namespace platform::web {

EM_JS_DEPS(megatoy_storage_flush, "$stringToNewUTF8");

namespace {

constexpr const char *kPopupId = "Saving###AwaitedStorageFlush";

struct ActiveFlush {
  /// Identity handed to JavaScript. Not the slot address: a finished flush
  /// frees the slot and the next one can land on the same heap address, so a
  /// stale callback would mistake the new flow for its own. A monotonic
  /// token can never be reused, and nothing dereferences it.
  void *token = nullptr;
  std::string title;
  std::string detail;
  std::function<void(bool, std::string)> on_complete;
};

std::unique_ptr<ActiveFlush> g_active;
std::uintptr_t g_next_token = 1;
bool g_close_popup = false;

void finish_active(bool ok, std::string error) {
  if (!g_active) {
    return;
  }
  // Clear the slot before the callback runs: it is on the UI thread and may
  // start the next operation.
  auto active = std::move(g_active);
  g_active.reset();
  g_close_popup = true;
  if (active->on_complete) {
    active->on_complete(ok, std::move(error));
  }
}

} // namespace

extern "C" EMSCRIPTEN_KEEPALIVE void
megatoy_awaited_flush_done(void *token, int ok, const char *error) {
  if (!g_active || g_active->token != token) {
    return;
  }
  finish_active(ok != 0, ok ? std::string{}
                            : std::string(error != nullptr
                                              ? error
                                              : "storage sync failed"));
}

// clang-format off
EM_JS(void, megatoy_awaited_flush_js, (void *token), {
  FS.syncfs(false, function(error) {
    var ptr = 0;
    if (error) ptr = stringToNewUTF8("" + error);
    Module["_megatoy_awaited_flush_done"](token, error ? 0 : 1, ptr);
    if (ptr) _free(ptr);
  });
});
// clang-format on

bool begin_awaited_flush(
    std::string title, std::string detail,
    std::function<void(bool ok, std::string error)> on_complete) {
  if (g_active) {
    return false;
  }

  auto active = std::make_unique<ActiveFlush>();
  active->token = reinterpret_cast<void *>(g_next_token++);
  active->title = std::move(title);
  active->detail = std::move(detail);
  active->on_complete = std::move(on_complete);

  auto *token = active->token;
  g_active = std::move(active);
  megatoy_awaited_flush_js(token);
  return true;
}

void render_awaited_flush_ui() {
  if (!g_active && g_close_popup) {
    if (ImGui::IsPopupOpen(kPopupId) &&
        ui::begin_modal(kPopupId, ui::ModalDismiss::None).visible) {
      ImGui::CloseCurrentPopup();
      ui::end_modal();
    }
    g_close_popup = false;
  }

  if (!g_active) {
    return;
  }

  const auto &active = *g_active;
  // Open on the edge only: re-opening an already-open popup every frame
  // resets popup state and can swallow clicks.
  if (!ImGui::IsPopupOpen(kPopupId)) {
    ImGui::OpenPopup(kPopupId);
  }
  if (ui::begin_modal(kPopupId, ui::ModalDismiss::None).visible) {
    ImGui::TextUnformatted(active.title.c_str());
    ImGui::TextDisabled("%s", active.detail.c_str());
    // Indeterminate, and no Cancel: IndexedDB reports only completion, and
    // the first half of a flush runs without giving the frame back at all,
    // so this sits still rather than animating through the worst of it.
    ImGui::ProgressBar(static_cast<float>(-1.0 * ImGui::GetTime()),
                       ImVec2(-FLT_MIN, 0));
    ui::end_modal();
  }
}

} // namespace platform::web

#endif
