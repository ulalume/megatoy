#include "save_controls.hpp"

#include "patch_session.hpp"

namespace patches {

SaveControls save_controls(const PatchSession &session,
                           const PatchDocument &document) {
  SaveControls controls;
  controls.saves_in_place = session.is_user_patch(document);
  controls.save_enabled = !controls.saves_in_place || document.is_modified();
  controls.show_save_as = controls.saves_in_place;
  controls.save_label =
      session.save_label_for(document, controls.saves_in_place);
  if (!controls.saves_in_place) {
    const auto &path = document.path();
    controls.save_tooltip = path.ends_with(".ginpkg")
                                ? "Save version to " + path
                                : "Choose a filename and format";
  } else if (!document.is_modified()) {
    controls.save_tooltip = "Patch is not modified";
  }
  return controls;
}

} // namespace patches
