#include "platform/platform_config.hpp"
#include "web_storage_dialogs.hpp"

#if defined(MEGATOY_PLATFORM_WEB)

#include "gui/components/modal.hpp"
#include "platform/web/web_storage_flush.hpp"
#include <imgui.h>

namespace ui {
namespace {

constexpr const char *kPopupId = "Saving###AwaitedStorageFlush";

} // namespace

using namespace platform::web;

void render_awaited_flush_ui() {
  // No Cancel: the first half of a flush never gives the frame back.
  const auto *active = awaited_flush_progress();
  progress_modal(kPopupId, take_awaited_flush_finished(), active != nullptr,
                 active != nullptr ? active->title.c_str() : "",
                 active != nullptr ? active->detail.c_str() : "");
}

} // namespace ui

#endif
