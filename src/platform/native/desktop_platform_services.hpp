#pragma once

#include "platform/platform_services.hpp"

class DesktopPlatformServices : public platform::PlatformServicesProvider {
public:
  DesktopPlatformServices();
  ~DesktopPlatformServices() override = default;

  platform::StdFileSystem &file_system() override;
  std::unique_ptr<AudioTransport> create_audio_transport() override;
  std::unique_ptr<MidiBackend> create_midi_backend() override;
  std::shared_ptr<update::ReleaseInfoProvider> release_info_provider() override;

private:
  platform::StdFileSystem file_system_;
  std::shared_ptr<update::ReleaseInfoProvider> release_provider_;
};
