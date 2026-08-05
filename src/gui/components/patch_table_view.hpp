#pragma once

// The patch browser's flat, sortable table with editable metadata. Internal
// to the selector.

#include "patch_selector.hpp"

namespace ui::selector_detail {

void render_patch_table(PatchSelectorContext &context);

} // namespace ui::selector_detail
