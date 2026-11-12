#include "audio/native_audio_transport.hpp"

#if defined(__APPLE__)
#include "audio/core_audio_transport.hpp"
#else
#include "audio/sdl_audio_transport.hpp"
#endif

std::unique_ptr<AudioTransport> make_native_audio_transport() {
#if defined(__APPLE__)
  return std::make_unique<CoreAudioTransport>();
#else
  return std::make_unique<SdlAudioTransport>();
#endif
}
