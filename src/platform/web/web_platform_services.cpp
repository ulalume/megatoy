#include "platform/web/web_platform_services.hpp"

#include "audio/webaudio_transport.hpp"
#include "platform/web/web_midi_backend.hpp"
#include "update/release_provider.hpp"
#include <future>

namespace {

class WebReleaseInfoProvider : public update::ReleaseInfoProvider {
public:
  std::future<update::UpdateCheckResult>
  check_for_updates_async(std::string_view current_version_tag) override {
    update::UpdateCheckResult result{};
    result.success = true;
    result.latest_version = std::string(current_version_tag);
    result.update_available = false;
    return std::async(std::launch::deferred,
                      [result]() mutable { return result; });
  }
};

} // namespace

namespace platform::web {

WebPlatformServices::WebPlatformServices()
    : file_system_(),
      release_provider_(std::make_shared<WebReleaseInfoProvider>()) {}

platform::VirtualFileSystem &WebPlatformServices::file_system() {
  return file_system_;
}

std::unique_ptr<AudioTransport> WebPlatformServices::create_audio_transport() {
  return std::make_unique<WebAudioTransport>();
}

std::unique_ptr<MidiBackend> WebPlatformServices::create_midi_backend() {
  return std::make_unique<WebMidiBackend>();
}

std::shared_ptr<update::ReleaseInfoProvider>
WebPlatformServices::release_info_provider() {
  return release_provider_;
}

} // namespace platform::web
