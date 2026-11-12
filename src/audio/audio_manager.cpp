#include "audio/audio_manager.hpp"

#if !defined(MEGATOY_PLATFORM_WEB)

#include "audio/native_audio_transport.hpp"
#include "audio/performance.hpp"
#include <algorithm>
#include <iostream>
#include <memory>

AudioManager::AudioManager() : AudioManager(make_native_audio_transport()) {}

AudioManager::AudioManager(std::unique_ptr<AudioTransport> transport)
    : engine_(), transport_(std::move(transport)) {}

AudioManager::~AudioManager() { shutdown(); }

bool AudioManager::initialize(uint32_t sample_rate, int buffer_frames) {
  if (engine_.is_running()) {
    return true;
  }

  if (!transport_) {
    transport_ = make_native_audio_transport();
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

bool AudioManager::reopen_device() {
  if (!transport_ || !engine_.is_running()) {
    return false;
  }
  transport_->stop();
  engine_.suspend();
  engine_.resume();
  return start_transport(buffer_frames_);
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

int AudioManager::buffer_frames() const { return buffer_frames_; }

int AudioManager::default_buffer_frames() const {
  return transport_ ? transport_->default_buffer_frames() : 0;
}

const audio::ScopeBuffer &AudioManager::scope_buffer() const {
  return engine_.scope_buffer();
}

const audio::LoadMeter &AudioManager::load_meter() const {
  return engine_.load_meter();
}

bool AudioManager::submit(const audio::AudioCommand &command) {
  return engine_.submit(command);
}

bool AudioManager::submit_from_midi(const audio::AudioCommand &command) {
  return engine_.submit_from_midi(command);
}

void AudioManager::set_note_options(bool use_velocity,
                                    int velocity_sensitivity_depth,
                                    bool steal_oldest) {
  engine_.set_note_options(
      use_velocity,
      static_cast<uint8_t>(std::clamp(velocity_sensitivity_depth, 0, 100)),
      steal_oldest);
}

void AudioManager::set_performance_options(bool pitch_bend, bool mod_wheel) {
  engine_.set_performance_options(pitch_bend, mod_wheel);
  // Through the normal command path, so the audio thread rewrites the
  // affected channels.
  if (!pitch_bend) {
    engine_.submit(
        audio::AudioCommand::pitch_bend(audio::performance::kPitchBendCenter));
  }
  if (!mod_wheel) {
    engine_.submit(audio::AudioCommand::mod_wheel(0));
  }
}

std::vector<ym2612::Note> AudioManager::sounding_notes() const {
  return engine_.notes().published_notes();
}

bool AudioManager::note_sounding(const ym2612::Note &note) const {
  return engine_.notes().published_contains(note);
}

VoiceActivityFrame AudioManager::voice_activity() const {
  return engine_.voice_activity();
}

bool AudioManager::is_running() const { return engine_.is_running(); }

uint32_t AudioManager::sample_rate() const { return engine_.sample_rate(); }

#endif
