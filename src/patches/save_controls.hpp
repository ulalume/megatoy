#pragma once

#include <string>

namespace patches {

class PatchDocument;
class PatchSession;

struct SaveControls {
  bool saves_in_place = false;
  bool save_enabled = true;
  bool show_save_as = false;
  const char *save_label = nullptr;
  std::string save_tooltip;
};

SaveControls save_controls(const PatchSession &session,
                           const PatchDocument &document);

} // namespace patches
