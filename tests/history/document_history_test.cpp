#include "../test_check.hpp"
#include "app_context.hpp"
#include "app_services.hpp"
#include "app_state.hpp"
#include "audio/audio_transport.hpp"
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
