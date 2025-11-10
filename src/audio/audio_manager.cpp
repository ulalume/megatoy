#include "audio/audio_manager.hpp"
#include "audio/sdl_audio_transport.hpp"
#include <iostream>
#include <memory>

namespace {

std::unique_ptr<AudioTransport> make_default_transport() {
  return std::make_unique<SdlAudioTransport>();
}

} // namespace

AudioManager::AudioManager()
    : AudioManager(make_default_transport()) {}

AudioManager::AudioManager(std::unique_ptr<AudioTransport> transport)
    : engine_(), transport_(std::move(transport)) {}

AudioManager::~AudioManager() { shutdown(); }

bool AudioManager::initialize(UINT32 sample_rate) {
  if (engine_.is_running()) {
    return true;
  }

  if (!transport_) {
    transport_ = make_default_transport();
  }

  if (!engine_.initialize(sample_rate)) {
    std::cerr << "Failed to initialize audio engine\n";
    return false;
  }

  AudioTransport::RenderCallback callback =
      [this](std::uint32_t buf_size, void *data) -> std::uint32_t {
    return engine_.render(buf_size, data);
  };

  if (!transport_->start(engine_.sample_rate(), std::move(callback))) {
    std::cerr << "Failed to start audio transport\n";
    engine_.shutdown();
    return false;
  }

  return true;
}

void AudioManager::shutdown() {
  if (transport_) {
    transport_->stop();
  }
  engine_.shutdown();
}

void AudioManager::apply_patch_to_all_channels(const ym2612::Patch &patch) {
  engine_.apply_patch_to_all_channels(patch);
}
