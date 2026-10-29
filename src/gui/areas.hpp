#pragma once

#include <optional>
#include <string_view>

namespace ui::areas {

/**
 * The fixed regions of the window. Each view belongs to one: the views of
 * a side area share a single tab group there, while the main area can be
 * split between its views.
 */
enum class Area {
  Sidebar,
  Main,
  FooterLeft,
  FooterRight,
};

/// Whether a window, or the saved settings of one, is a patch editor's.
bool is_editor_window(std::string_view window_name);

/// The area of the view a window shows, by the window's name; empty for a
/// window that is not a view.
std::optional<Area> area_of(std::string_view window_name);

/**
 * Call right before Begin() of a view. A side area's tab group refuses other
 * windows, being split and having tabs pulled out of it. A main view left
 * floating, or docked outside the main area, goes back to `main_dock_id`,
 * or to the main area's central group when that is 0. `node_flags` are
 * added to the group the view is in.
 */
void keep_in_area(Area area, const char *window_name,
                  unsigned int node_flags = 0, unsigned int main_dock_id = 0);

/**
 * Call after the dockspace is submitted. A view dragged over the window then
 * offers only the groups of its own area: the window's outer edges, where a
 * new group would start beside every area, are not offered.
 */
void refuse_outer_docking(unsigned int dockspace_id);

/// Whether no tab group mixes areas and each side area's views share one,
/// judged from the saved arrangement just loaded.
bool layout_fits_areas();

} // namespace ui::areas
