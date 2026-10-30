#include "../test_check.hpp"
#include "app_context.hpp"
#include "app_services.hpp"
#include "app_state.hpp"
#include "audio/audio_transport.hpp"
#include "gui/components/operator_commands.hpp"
#include "history/snapshot_entry.hpp"
#include "patch_actions.hpp"
#include "platform/native/native_file_system.hpp"
#include "platform/platform_services.hpp"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace fs = std::filesystem;

namespace {

/// AppServices reads its configuration from the home directory; point that
/// somewhere disposable for the test's lifetime.
class ScopedHome {
public:
  explicit ScopedHome(const fs::path &home)
#if defined(_WIN32)
      : variable_("USERPROFILE")
#else
      : variable_("HOME")
#endif
  {
    if (const char *previous = std::getenv(variable_.c_str())) {
      previous_ = previous;
    }
#if defined(_WIN32)
    CHECK(_putenv_s(variable_.c_str(), home.string().c_str()) == 0);
#else
    CHECK(setenv(variable_.c_str(), home.string().c_str(), 1) == 0);
#endif
  }

  ~ScopedHome() {
#if defined(_WIN32)
    _putenv_s(variable_.c_str(), previous_.value_or("").c_str());
#else
    if (previous_) {
      setenv(variable_.c_str(), previous_->c_str(), 1);
    } else {
      unsetenv(variable_.c_str());
    }
#endif
  }

private:
  std::string variable_;
  std::optional<std::string> previous_;
};

class SilentTransport final : public AudioTransport {
public:
  bool start(std::uint32_t, RenderCallback) override { return false; }
  void stop() override {}
  bool is_active() const override { return false; }
};

class TestPlatform final : public platform::PlatformServicesProvider {
public:
  platform::VirtualFileSystem &file_system() override { return file_system_; }
  std::unique_ptr<AudioTransport> create_audio_transport() override {
    return std::make_unique<SilentTransport>();
  }
  std::unique_ptr<MidiBackend> create_midi_backend() override {
    return nullptr;
  }
  std::shared_ptr<update::ReleaseInfoProvider>
  release_info_provider() override {
    return nullptr;
  }

private:
  NativeFileSystem file_system_;
};

void edit(AppContext &context, const std::string &label,
          void (*apply)(ym2612::Patch &)) {
  patch_actions::begin_change(context, label, label);
  apply(context.services.patch_session.current_patch());
  patch_actions::commit_change(context);
}

void change_feedback(history::HistoryManager &manager, AppContext &context,
                     int value, const std::string &label,
                     const std::string &merge_key = {}) {
  auto &patch = context.services.patch_session.current_patch();
  const int before = patch.instrument.feedback;
  manager.begin_transaction(
      label, merge_key, [before, label, merge_key](AppContext &app) {
        const int after =
            app.services.patch_session.current_patch().instrument.feedback;
        return history::make_snapshot_entry<int>(
            label, merge_key, before, after,
            [](AppContext &target, const int &feedback) {
              target.services.patch_session.current_patch()
                  .instrument.feedback = static_cast<uint8_t>(feedback);
            });
      });
  patch.instrument.feedback = static_cast<uint8_t>(value);
  manager.commit_transaction(context);
}

void test_merge_undo_redo_and_new_edit(AppContext &context) {
  auto &feedback =
      context.services.patch_session.current_patch().instrument.feedback;
  feedback = 0;
  history::HistoryManager manager;
  CHECK(manager.undo_label().empty());
  CHECK(manager.redo_label().empty());
  manager.undo(context);
  manager.redo(context);
  CHECK(feedback == 0);
  change_feedback(manager, context, 1, "Feedback 1", "feedback");
  change_feedback(manager, context, 3, "Feedback 3", "feedback");
  CHECK(manager.undo_label() == "Feedback 3");
  manager.undo(context);
  CHECK(feedback == 0);
  CHECK(!manager.can_undo());
  CHECK(manager.redo_label() == "Feedback 3");
  manager.redo(context);
  CHECK(feedback == 3);
  manager.undo(context);
  change_feedback(manager, context, 5, "Feedback 5");
  CHECK(!manager.can_redo());
  manager.undo(context);
  CHECK(feedback == 0);
}

void test_empty_and_cancelled_transactions_preserve_redo(AppContext &context) {
  auto &feedback =
      context.services.patch_session.current_patch().instrument.feedback;
  feedback = 0;
  history::HistoryManager manager;
  change_feedback(manager, context, 2, "Feedback");
  manager.undo(context);
  change_feedback(manager, context, 0, "Unchanged");
  CHECK(!manager.can_undo());
  CHECK(manager.redo_label() == "Feedback");
  manager.begin_transaction("Empty", "", {});
  manager.commit_transaction(context);
  manager.commit_transaction(context);
  bool called = false;
  manager.begin_transaction(
      "Cancelled", "",
      [&called](AppContext &) -> std::unique_ptr<history::HistoryEntry> {
        called = true;
        return nullptr;
      });
  manager.cancel_transaction();
  manager.commit_transaction(context);
  CHECK(!called);
  CHECK(manager.redo_label() == "Feedback");
  manager.redo(context);
  CHECK(feedback == 2);
}

void test_history_capacity_keeps_the_most_recent_edits(AppContext &context) {
  auto &feedback =
      context.services.patch_session.current_patch().instrument.feedback;
  for (const std::size_t capacity : {0u, 2u}) {
    feedback = 0;
    history::HistoryManager manager(capacity);
    change_feedback(manager, context, 1, "One");
    change_feedback(manager, context, 2, "Two");
    change_feedback(manager, context, 3, "Three");
    manager.undo(context);
    CHECK(feedback == 2);
    if (capacity == 2) {
      CHECK(manager.undo_label() == "Two");
      manager.undo(context);
      CHECK(feedback == 1);
    }
    CHECK(!manager.can_undo());
    manager.undo(context);
    CHECK(feedback == (capacity == 2 ? 1 : 2));
    while (manager.can_redo()) {
      manager.redo(context);
    }
    CHECK(feedback == 3);
  }
}

void test_clear_and_reset_cancel_pending_work(AppContext &context) {
  history::HistoryManager manager;
  for (const bool reset : {false, true}) {
    change_feedback(manager, context, 1, "One");
    change_feedback(manager, context, 2, "Two");
    manager.undo(context);
    bool called = false;
    manager.begin_transaction(
        "Pending", "",
        [&called](AppContext &) -> std::unique_ptr<history::HistoryEntry> {
          called = true;
          return nullptr;
        });
    if (reset) {
      manager.reset();
    } else {
      manager.clear();
    }
    manager.commit_transaction(context);
    CHECK(!called);
    CHECK(!manager.can_undo());
    CHECK(!manager.can_redo());
    CHECK(manager.undo_label().empty());
    CHECK(manager.redo_label().empty());
  }
}

void test_rejected_operator_paste_preserves_undo_and_redo(AppContext &context) {
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  std::string text = "31 20 10 8 6 40 2 3 1 0\n";
  auto &platform = ImGui::GetPlatformIO();
  platform.Platform_ClipboardUserData = &text;
  platform.Platform_GetClipboardTextFn = [](ImGuiContext *) -> const char * {
    return static_cast<std::string *>(
               ImGui::GetPlatformIO().Platform_ClipboardUserData)
        ->c_str();
  };
  auto &session = context.services.patch_session;
  auto &manager = context.services.history();
  manager.clear();
  session.current_patch().instrument.operators[0].attack_rate = 1;
  session.mark_as_clean();
  const auto original = session.capture_snapshot();
  ui::OperatorEditState state;
  state.selection.select_only(0);
  ym2612::OperatorClipboard clipboard;
  ui::OperatorCommandContext command{
      session.current_patch().instrument, state, clipboard,
      [&context](const std::string &label) {
        patch_actions::begin_change(context, label, "");
      },
      [&context] { patch_actions::commit_change(context); }};
  ui::paste_operator_command(command);
  const auto pasted = session.capture_snapshot();
  CHECK(pasted != original);
  CHECK(manager.undo_label() == "Paste to OP1");
  text = "1 2 malformed";
  ui::paste_operator_command(command);
  CHECK(session.capture_snapshot() == pasted);
  manager.undo(context);
  CHECK(session.capture_snapshot() == original);
  CHECK(!session.is_modified());
  CHECK(!manager.can_undo());
  ui::paste_operator_command(command);
  CHECK(manager.redo_label() == "Paste to OP1");
  manager.redo(context);
  CHECK(session.capture_snapshot() == pasted);
  ImGui::DestroyContext();
}

void run(const fs::path &root) {
  ScopedHome home(root / "home");
  TestPlatform platform;
  AppServices services(platform);
  AppState state;
  AppContext context{services, state};
  auto &session = services.patch_session;

  const auto first = session.active_document_id();
  session.current_patch().instrument.feedback = 1;
  session.mark_as_clean();
  ym2612::Patch other;
  other.instrument.algorithm = 2;
  const auto second = session.open_document(other, {});

  edit(context, "Feedback",
       [](ym2612::Patch &patch) { patch.instrument.feedback = 4; });
  CHECK(services.history().can_undo());
  CHECK(services.history().undo_label() == "Feedback");

  CHECK(session.activate(second));
  CHECK(!services.history().can_undo());
  edit(context, "Algorithm",
       [](ym2612::Patch &patch) { patch.instrument.algorithm = 6; });
  CHECK(services.history().undo_label() == "Algorithm");

  services.history().undo(context);
  CHECK(session.find_document(second)->patch().instrument.algorithm == 2);
  CHECK(session.find_document(first)->patch().instrument.feedback == 4);
  CHECK(services.history().can_redo());

  CHECK(session.activate(first));
  CHECK(services.history().undo_label() == "Feedback");
  CHECK(!services.history().can_redo());
  services.history().undo(context);
  CHECK(session.find_document(first)->patch().instrument.feedback == 1);
  CHECK(!session.find_document(first)->is_modified());
  CHECK(session.find_document(second)->patch().instrument.algorithm == 2);

  CHECK(session.activate(second));
  services.history().redo(context);
  CHECK(session.find_document(second)->patch().instrument.algorithm == 6);
  CHECK(session.find_document(first)->patch().instrument.feedback == 1);

  // Editor state is kept per document too; the operator clipboard is not.
  auto &ui = state.ui_state();
  ui.document_ui(first).operator_edit.selection.select_only(1);
  ui.document_ui(second).operator_edit.selection.select_only(3);
  ui.document_ui(second).envelope_states[2].handle_active = true;
  CHECK(ui.document_ui(first).operator_edit.selection.primary == 1);
  CHECK(ui.document_ui(second).operator_edit.selection.primary == 3);
  CHECK(!ui.document_ui(first).envelope_states[2].handle_active);

  // What a closed document kept goes with it.
  CHECK(session.close_document(second));
  services.prune_histories();
  ui.prune_documents([&session](patches::DocumentId id) {
    return session.find_document(id) != nullptr;
  });
  CHECK(services.histories.size() == 1);
  CHECK(services.histories.count(first) == 1);
  CHECK(ui.documents.size() == 1);
  CHECK(ui.documents.count(first) == 1);

  test_merge_undo_redo_and_new_edit(context);
  test_empty_and_cancelled_transactions_preserve_redo(context);
  test_history_capacity_keeps_the_most_recent_edits(context);
  test_clear_and_reset_cancel_pending_work(context);
  test_rejected_operator_paste_preserves_undo_and_redo(context);
}

} // namespace

int main() {
  const auto root = fs::temp_directory_path() / "megatoy_document_history_test";
  fs::remove_all(root);
  fs::create_directories(root / "home");
  run(root);
  fs::remove_all(root);
  return 0;
}
