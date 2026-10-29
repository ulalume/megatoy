#pragma once

#include "formats/patch_registry.hpp"
#include "gui/components/operator_commands.hpp"
#include "gui/patch_save_dialog.hpp"
#include "gui/save_export_state.hpp"
#include "input_state.hpp"
#include "patches/patch_document.hpp"
#include "patches/patch_repository.hpp"
#include "preferences/preference_manager.hpp"
#include "ym2612/patch.hpp"
#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct UIState {
  PreferenceManager::UIPreferences prefs;
  bool open_add_folder_dialog = false;
  /// Raised to bring up Preferences on its Sound tab.
  bool open_sound_preferences = false;

  struct DropState {
    bool show_picker_for_multiple_instruments = false;
    std::filesystem::path pending_instruments_path;
    std::vector<ym2612::Patch> instruments;
    int selected_instrument = 0;
  } drop_state;

  struct EnvelopeState {
    enum class SliderState {
      None,
      Hover,
      Active,
    };
    SliderState total_level = SliderState::None;
    SliderState attack_rate = SliderState::None;
    SliderState decay_rate = SliderState::None;
    SliderState sustain_level = SliderState::None;
    SliderState sustain_rate = SliderState::None;
    SliderState release_rate = SliderState::None;
    /// The same six as the graph's handles light them. Kept apart from the
    /// sliders' own, which are written and read at different points of the
    /// frame: the graph and the sliders each take the stronger of the two.
    struct HandleLit {
      SliderState total_level = SliderState::None;
      SliderState attack_rate = SliderState::None;
      SliderState decay_rate = SliderState::None;
      SliderState sustain_level = SliderState::None;
      SliderState sustain_rate = SliderState::None;
      SliderState release_rate = SliderState::None;
    } handles;
    /// A handle on the graph is being dragged, which holds the time axis
    /// still.
    bool handle_active = false;
  };

  /// What the patch editor keeps for each open document.
  struct DocumentUi {
    EnvelopeState envelope_states[4];
    ui::OperatorEditState operator_edit;
    /// The editor window showing the document; 0 until it has one.
    int window_slot = 0;
    /// Whether that window had the focus last frame.
    bool window_focused = false;
    /// The window has been placed for this document.
    bool window_placed = false;
    /// A dock node to move the window into next frame; 0 for none.
    unsigned int dock_target = 0;
    /// What the file holds after it was written outside megatoy while the
    /// document had unsaved changes, until the user picks one.
    std::optional<ym2612::Patch> disk_version;
  };
  std::map<patches::DocumentId, DocumentUi> documents;
  /// The document whose editor window is brought to the front next frame.
  std::optional<patches::DocumentId> reveal_document;
  /// The dock node the active document's window was last drawn in; new
  /// documents' windows open there.
  unsigned int editor_dock_id = 0;
  /// Shared by every document, so an operator can be carried across.
  ym2612::OperatorClipboard operator_clipboard;

  DocumentUi &document_ui(patches::DocumentId id) { return documents[id]; }
  /// Drop the entries of documents `is_open` no longer accepts.
  template <typename IsOpen> void prune_documents(IsOpen &&is_open) {
    std::erase_if(documents, [&is_open](const auto &entry) {
      return !is_open(entry.first);
    });
  }

  struct ConfirmationState {
    bool show_unsaved_changes_dialog = false;
    std::string dialog_message;
    enum class Operation {
      Close,
      Exit,
    } operation = Operation::Exit;
    /// The document a Close asks about, or the one an Exit can save.
    patches::DocumentId pending_document = 0;
    /// An Exit with one changed document that saves to its own file.
    bool exit_can_save = false;

    static ConfirmationState close(patches::DocumentId document,
                                   const std::string &name) {
      ConfirmationState state;
      state.show_unsaved_changes_dialog = true;
      state.pending_document = document;
      state.dialog_message = "Save changes to \"" + name + "\" before closing?";
      state.operation = UIState::ConfirmationState::Operation::Close;
      return state;
    }
    static ConfirmationState exit(std::size_t modified_documents = 1) {
      ConfirmationState state;
      state.show_unsaved_changes_dialog = true;
      state.dialog_message =
          modified_documents > 1
              ? std::to_string(modified_documents) +
                    " patches have unsaved changes. Do you want to discard "
                    "them and exit?"
              : "You have unsaved changes. Do you want to discard them and "
                "exit?";
      state.operation = UIState::ConfirmationState::Operation::Exit;
      return state;
    }
    /// An Exit whose only changed document can be saved where it is.
    static ConfirmationState exit_saving(patches::DocumentId document,
                                         const std::string &name) {
      ConfirmationState state = exit();
      state.pending_document = document;
      state.exit_can_save = true;
      state.dialog_message = "Save changes to \"" + name + "\" before exiting?";
      return state;
    }

  } confirmation_state;

  struct DangerConfirmationState {
    bool requested = false;
    std::string title;
    std::string message;
    std::string confirm_label;
    std::function<void()> on_confirm;

    void request(std::string dialog_title, std::string dialog_message,
                 std::string button_label,
                 std::function<void()> confirm_action) {
      requested = true;
      title = std::move(dialog_title);
      message = std::move(dialog_message);
      confirm_label = std::move(button_label);
      on_confirm = std::move(confirm_action);
    }

    void clear() {
      requested = false;
      title.clear();
      message.clear();
      confirm_label.clear();
      on_confirm = {};
    }
  } danger_confirmation_state;

  struct TextPromptState {
    bool requested = false;
    std::string title;
    std::string label;
    std::string buffer;
    std::string confirm_label;
    std::function<void(const std::string &)> on_confirm;
    std::function<std::string(const std::string &)> validator;

    void
    request(std::string dialog_title, std::string input_label,
            std::string initial_value, std::string button_label,
            std::function<void(const std::string &)> confirm_action,
            std::function<std::string(const std::string &)> validate = {}) {
      requested = true;
      title = std::move(dialog_title);
      label = std::move(input_label);
      buffer = std::move(initial_value);
      confirm_label = std::move(button_label);
      on_confirm = std::move(confirm_action);
      validator = std::move(validate);
    }

    void clear() {
      requested = false;
      title.clear();
      label.clear();
      buffer.clear();
      confirm_label.clear();
      on_confirm = {};
      validator = {};
    }
  } text_prompt_state;

  /**
   * The "New Patch" prompt: a name and the format to write it in.
   *
   * Its own state rather than TextPromptState's, whose single control is the
   * text field. The formats are captured when the prompt is requested, from
   * the one list of writable formats the registry keeps.
   */
  struct NewPatchPromptState {
    bool requested = false;
    std::filesystem::path folder;
    std::string name;
    std::string extension;
    std::vector<formats::SaveFormatInfo> formats;
    std::function<void(const std::string &name, const std::string &extension)>
        on_confirm;

    void request(std::filesystem::path target_folder,
                 std::vector<formats::SaveFormatInfo> save_formats,
                 std::function<void(const std::string &, const std::string &)>
                     confirm_action) {
      requested = true;
      folder = std::move(target_folder);
      name.clear();
      formats = std::move(save_formats);
      extension = formats.empty() ? std::string{} : formats.front().extension;
      on_confirm = std::move(confirm_action);
    }

    void clear() {
      requested = false;
      folder.clear();
      name.clear();
      extension.clear();
      formats.clear();
      on_confirm = {};
    }
  } new_patch_prompt_state;

  /**
   * The tree's "Duplicate": the copy waits here for the dialog that names it.
   *
   * The patch is held rather than reloaded, so the copy is the one that was
   * clicked however the tree changes meanwhile. Unused where a native save
   * dialog does the asking.
   */
  struct PatchDuplicateState {
    ui::PatchSaveDialogState dialog;
    ym2612::Patch patch;
  } patch_duplicate_state;

  ui::SaveExportState save_export_state;
};

class AppState {
public:
  AppState() = default;

  InputState &input_state() { return input_state_; }
  const InputState &input_state() const { return input_state_; }

  UIState &ui_state() { return ui_state_; }
  const UIState &ui_state() const { return ui_state_; }

  const std::vector<std::string> &connected_midi_inputs() const {
    return connected_midi_inputs_;
  }
  void set_connected_midi_inputs(std::vector<std::string> devices);

private:
  InputState input_state_;
  UIState ui_state_;
  std::vector<std::string> connected_midi_inputs_;
};
