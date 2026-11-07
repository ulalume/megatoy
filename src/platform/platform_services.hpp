#pragma once

#include "audio/audio_transport.hpp"
#include "midi/midi_backend.hpp"
#include "platform/std_file_system.hpp"
#include "update/release_provider.hpp"
#include <memory>

namespace platform {

class PlatformServicesProvider {
public:
  virtual ~PlatformServicesProvider() = default;

  virtual StdFileSystem &file_system() = 0;
  /// The device the engine renders through; none on the web, where it runs
  /// in an audio worklet instead (see src/platform/web/web_audio_worklet.cpp).
  virtual std::unique_ptr<AudioTransport> create_audio_transport() {
    return nullptr;
  }
  virtual std::unique_ptr<MidiBackend> create_midi_backend() = 0;
  virtual std::shared_ptr<update::ReleaseInfoProvider>
  release_info_provider() = 0;
};

} // namespace platform
