#pragma once

#include "patch_editor.hpp"
#include "ym2612/patch.hpp"

namespace ui {

/// The Quick Edit and MML sections under the operators: side by side when the
/// window is wide, one above the other otherwise.
void render_patch_editor_tools(PatchEditorContext &context,
                               ym2612::Patch &patch);

} // namespace ui
