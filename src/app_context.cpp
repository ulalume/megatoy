#include "app_context.hpp"
#include "drop_actions.hpp"
#include "patch_actions.hpp"

void AppContext::safe_load(const patches::PatchEntry &entry) {
  patch_actions::safe_load(*this, entry);
}

void AppContext::load_patch(const patches::PatchEntry &entry) {
  patch_actions::load(*this, entry);
}

void AppContext::load_dropped_patch(const ym2612::Patch &patch,
                                    const std::filesystem::path &path) {
  patch_actions::load_dropped_patch(*this, patch, path);
}

void AppContext::handle_drop(const std::filesystem::path &path) {
  drop_actions::handle_drop(*this, path);
}

void AppContext::apply_instrument_selection(size_t index) {
  drop_actions::apply_selection(*this, index);
}

void AppContext::cancel_instrument_selection() {
  drop_actions::cancel_selection(*this);
}
