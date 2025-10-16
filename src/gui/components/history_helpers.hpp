#pragma once

#include "app_state.hpp"
#include "history/snapshot_entry.hpp"
#include <imgui.h>
#include <string>

namespace ui {

inline void track_patch_history(AppState &app_state, const std::string &label,
                                const std::string &merge_key = {}) {
  const std::string key = merge_key.empty() ? label : merge_key;
  if (ImGui::IsItemActivated()) {
    auto before = app_state.patch();
    std::string label_copy = label;
    std::string key_copy = key;

    app_state.history().begin_transaction(
        label_copy, key_copy,
        [label = std::move(label_copy), key = std::move(key_copy),
         before = std::move(before)](AppState &state) mutable {
          auto entry = history::make_snapshot_entry<ym2612::Patch>(
              label, key, before, state.patch(),
              [](AppState &target, const ym2612::Patch &value) {
                target.patch() = value;
                target.apply_patch_to_device();
              });
          return entry;
        });
  }
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    app_state.history().commit_transaction(app_state);
  }
}

inline void track_patch_history(AppState &app_state, const char *label,
                                const char *merge_key = nullptr) {
  const std::string key = (merge_key && merge_key[0]) ? merge_key : label;
  track_patch_history(app_state, std::string(label), key);
}

} // namespace ui
