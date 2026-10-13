#pragma once

#include "preferences/preferences_data.hpp"
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ui::layouts {

/**
 * A layout is a preset over the one shared set of windows: which of them
 * show, and how they are docked. Each layout keeps its own saved
 * arrangement.
 */
enum class LayoutId {
  Sound,
};

/// The windows the View menu toggles.
struct WindowSet {
  bool patch_browser = true;
  bool patch_editor = true;
  bool patch_lab = true;
  bool soft_keyboard = true;
  bool mml_console = true;
  bool waveform = true;
  bool preferences = true;
};

struct LayoutDefinition {
  LayoutId id;
  /// Shown to the user, persisted as the current layout, and the source of
  /// the layout's storage names.
  std::string_view name;
  WindowSet windows;
  /// Docks the windows into the empty dockspace `dockspace_id`.
  void (*build)(unsigned int dockspace_id);
};

std::span<const LayoutDefinition> builtin_layouts();
const LayoutDefinition &definition(LayoutId id);
std::optional<LayoutId> find_layout(std::string_view name);
/// The layout a stored name refers to; unknown or empty names fall back to
/// the default layout.
LayoutId resolve_layout(std::string_view name);
constexpr LayoutId kDefaultLayout = LayoutId::Sound;

void apply_windows(const WindowSet &windows, UIPreferences &prefs);

/**
 * Where a layout's arrangement is stored. The default layout keeps the
 * single file or key used before layouts existed, so an arrangement saved
 * then still belongs to it; any other layout appends its lowercased name.
 */
std::filesystem::path ini_file_path(const std::filesystem::path &base_file,
                                    std::string_view layout_name);
std::string ini_storage_key(std::string_view base_key,
                            std::string_view layout_name);

} // namespace ui::layouts
