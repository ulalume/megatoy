#include "audio/audio_engine.hpp"

#include <algorithm>
#include <cstring>

#include "ym2612/channel.hpp"

namespace {

constexpr uint32_t kFallbackSampleRate = 44100;
constexpr uint32_t kDefaultFrameSize = sizeof(int16_t) * 2; // stereo s16

int16_t to_pcm16(float sample) {
  const float clamped = std::clamp(sample, -1.0f, 1.0f);
  return static_cast<int16_t>(clamped * 32767.0f);
}

} // namespace

AudioEngine::AudioEngine()
    : sample_rate_(kFallbackSampleRate), frame_size_(kDefaultFrameSize),
      running_(false) {}

bool AudioEngine::initialize(uint32_t sample_rate) {
  sample_rate_ = sample_rate != 0 ? sample_rate : kFallbackSampleRate;
  frame_size_ = kDefaultFrameSize;
  mix_buffer_.clear();
  scope_buffer_.clear();
  device_.init(sample_rate_);
  running_ = device_.is_initialized();
  return running_;
}

void AudioEngine::shutdown() {
  running_ = false;
  scope_buffer_.clear();
  mix_buffer_.clear();
  device_.stop();
}

uint32_t AudioEngine::render(uint32_t buf_size, void *data) {
  if (!running_ || frame_size_ == 0 || buf_size == 0) {
    if (data != nullptr && buf_size != 0) {
      std::memset(data, 0x00, buf_size);
    }
    return 0;
  }

  const uint32_t frames = buf_size / frame_size_;
  if (frames == 0) {
    return 0;
  }

  const size_t required = static_cast<size_t>(frames) * 2;
  if (mix_buffer_.size() < required) {
    mix_buffer_.resize(required);
  }

  device_.render(frames, mix_buffer_.data());
  scope_buffer_.write(mix_buffer_.data(), frames);

  auto *pcm = static_cast<int16_t *>(data);
  for (size_t i = 0; i < required; ++i) {
    pcm[i] = to_pcm16(mix_buffer_[i]);
  }

  return frames * frame_size_;
}

void AudioEngine::apply_patch_to_all_channels(const ym2612::Patch &patch) {
  device_.write_settings(patch.global);
  for (ym2612::ChannelIndex channel_index : ym2612::all_channel_indices) {
    auto channel = device_.channel(channel_index);
    channel.write_settings(patch.channel);
    channel.write_instrument(patch.instrument);
  }
}
