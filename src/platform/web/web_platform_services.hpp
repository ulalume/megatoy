#pragma once

#include "platform/platform_services.hpp"
#include "platform/web/web_file_system.hpp"

namespace platform::web {

class WebPlatformServices : public platform::PlatformServicesProvider {
public:
  WebPlatformServices();
  ~WebPlatformServices() override = default;

  platform::VirtualFileSystem &file_system() override;
  std::unique_ptr<AudioTransport> create_audio_transport() override;
  std::unique_ptr<MidiBackend> create_midi_backend() override;
  std::shared_ptr<update::ReleaseInfoProvider> release_info_provider() override;

private:
  WebFileSystem file_system_;
  std::shared_ptr<update::ReleaseInfoProvider> release_provider_;
};

} // namespace platform::web
