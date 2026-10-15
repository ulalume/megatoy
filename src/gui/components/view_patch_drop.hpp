#pragma once

#include <optional>
#include <string>

namespace ui {

/// A browser patch dropped on a view, to open as a tab in the view's dock
/// node.
struct ViewPatchDrop {
  std::string entry_path;
  /// 0 when the view is not docked.
  unsigned int dock_id = 0;
};

/// Lets a patch dragged from the browser drop anywhere on the current
/// window. A smaller target inside it takes the drop first. Call before
/// End().
void view_patch_drop_target();

/// The drop since the last call, if any.
std::optional<ViewPatchDrop> take_view_patch_drop();

} // namespace ui
