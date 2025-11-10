#pragma once

#include "audio/audio_transport.hpp"
#include "midi/midi_backend.hpp"
#include "platform/virtual_file_system.hpp"
#include "update/release_provider.hpp"
#include <memory>

namespace platform {

class PlatformServicesProvider {
public:
  virtual ~PlatformServicesProvider() = default;

  virtual VirtualFileSystem &file_system() = 0;
  virtual std::unique_ptr<AudioTransport> create_audio_transport() = 0;
  virtual std::unique_ptr<MidiBackend> create_midi_backend() = 0;
  virtual std::shared_ptr<update::ReleaseInfoProvider>
  release_info_provider() = 0;
};

} // namespace platform
