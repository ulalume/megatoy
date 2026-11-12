#pragma once

#include "drop_actions.hpp"
#include "gui/components/confirmation_dialog.hpp"
#include "gui/components/main_menu.hpp"
#include "gui/components/midi_keyboard.hpp"
#include "gui/components/patch_drop.hpp"
#include "gui/components/patch_editor.hpp"
#include "gui/components/patch_lab_window.hpp"
#include "gui/components/patch_selector.hpp"
#include "gui/components/preferences.hpp"
#include "gui/components/waveform.hpp"

struct AppContext;

namespace ui::renderer_detail {

/// Where a dropped file goes when no place took it: the editor.
drop_actions::Environment make_drop_environment(AppContext &ctx);

/// References and callbacks for windows, retained for one AppContext.
struct FrameContexts {
  explicit FrameContexts(AppContext &ctx);
  FrameContexts(const FrameContexts &) = delete;
  FrameContexts &operator=(const FrameContexts &) = delete;

  AppContext *owner;
  MainMenuContext main_menu;
  PatchDropContext patch_drop;
  ConfirmationDialogContext confirmation;
  PatchEditorContext patch_editor;
  PatchSelectorContext patch_selector;
  PreferencesContext preferences;
  MidiKeyboardContext midi_keyboard;
  PatchLabContext patch_lab;
  WaveformContext waveform;
};

} // namespace ui::renderer_detail
