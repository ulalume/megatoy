#pragma once

#include "platform/host.hpp"

#include <vector>

namespace platform::web {

/// The keys the browser's layout map has given since the last call. The map
/// is asked for with the first call; only some browsers have one, and not
/// for a page inside another without leave.
std::vector<host::LayoutKey> take_keyboard_layout();

} // namespace platform::web
