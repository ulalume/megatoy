#include "audio/audio_manager.hpp"
#include "audio/sdl_audio_transport.hpp"
#include <iostream>
#include <memory>

namespace {

std::unique_ptr<AudioTransport> make_default_transport() {
  return std::make_unique<SdlAudioTransport>();
}

} // namespace

AudioManager::AudioManager() : AudioManager(make_default_transport()) {}

AudioManager::AudioManager(std::unique_ptr<AudioTransport> transport)
    : engine_(), transport_(std::move(transport)) {}

AudioManager::~AudioManager() { shutdown(); }

bool AudioManager::initialize(uint32_t sample_rate, int buffer_frames) {
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

  if (!start_transport(buffer_frames)) {
    engine_.shutdown();
    return false;
  }

  return true;
}

bool AudioManager::set_buffer_frames(int buffer_frames) {
  if (!transport_ || !engine_.is_running()) {
    return true;
  }
  if (resolve_buffer_frames(buffer_frames) == buffer_frames_) {
    return true;
  }

  const int previous = buffer_frames_;

  // Closing the device first is what makes the rest of this safe to run here:
  // no callback is in flight, so suspend() and resume() can write the chip
  // directly.
  transport_->stop();
  engine_.suspend();
  // Rendering has to be live again before the device starts pulling, or its
  // first callbacks come back empty.
  engine_.resume();

  if (start_transport(buffer_frames)) {
    return true;
  }

  // A device that refuses the new size must not cost the app its sound.
  if (!start_transport(previous)) {
    engine_.suspend();
  }
  return false;
}

bool AudioManager::start_transport(int buffer_frames) {
  transport_->set_buffer_frames(buffer_frames);

  AudioTransport::RenderCallback callback =
      [this](std::uint32_t buf_size, void *data) -> std::uint32_t {
    return engine_.render(buf_size, data);
  };

  if (!transport_->start(engine_.sample_rate(), std::move(callback))) {
    std::cerr << "Failed to start audio transport\n";
    return false;
  }

  buffer_frames_ = resolve_buffer_frames(buffer_frames);
  return true;
}

int AudioManager::resolve_buffer_frames(int preference) const {
  if (preference > 0) {
    return preference;
  }
  return transport_ ? transport_->default_buffer_frames() : 0;
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
