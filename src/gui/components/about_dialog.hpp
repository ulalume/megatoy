#pragma once

#include <functional>

namespace ui {

struct AboutDialogContext {
  std::function<void()> open_about_dialog;
};

void render_about_dialog();
void open_about_dialog();

} // namespace ui
