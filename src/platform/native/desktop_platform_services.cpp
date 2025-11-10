#include "platform/native/desktop_platform_services.hpp"

#include "audio/sdl_audio_transport.hpp"
#include "midi/rtmidi_backend.hpp"

DesktopPlatformServices::DesktopPlatformServices()
    : file_system_(),
      release_provider_(std::make_shared<update::CurlReleaseInfoProvider>()) {}

platform::VirtualFileSystem &DesktopPlatformServices::file_system() {
  return file_system_;
}

std::unique_ptr<AudioTransport>
DesktopPlatformServices::create_audio_transport() {
  return std::make_unique<SdlAudioTransport>();
}

std::unique_ptr<MidiBackend> DesktopPlatformServices::create_midi_backend() {
  return std::make_unique<RtMidiBackend>();
}

std::shared_ptr<update::ReleaseInfoProvider>
DesktopPlatformServices::release_info_provider() {
  return release_provider_;
}
