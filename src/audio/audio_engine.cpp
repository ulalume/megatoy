#include "audio/audio_engine.hpp"

#include <algorithm>
#include <cstring>
#include <thread>

#include "ym2612/channel.hpp"
#include "ym2612/note.hpp"

namespace {

constexpr uint32_t kFallbackSampleRate = 44100;
// How long a note-off will wait for queue space before being abandoned.
constexpr int kFullQueueRetries = 1000;
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

  // Notes land here rather than in the UI frame loop, so their timing follows
  // the audio buffer instead of the render rate.
  drain_commands();

  device_.render(frames, mix_buffer_.data());
  scope_buffer_.write(mix_buffer_.data(), frames);

  auto *pcm = static_cast<int16_t *>(data);
  for (size_t i = 0; i < required; ++i) {
    pcm[i] = to_pcm16(mix_buffer_[i]);
  }

  return frames * frame_size_;
}

void AudioEngine::set_note_options(bool use_velocity, bool steal_oldest) {
  use_velocity_.store(use_velocity, std::memory_order_relaxed);
  steal_oldest_.store(steal_oldest, std::memory_order_relaxed);
}

bool AudioEngine::submit_from_midi(const audio::AudioCommand &command) {
  if (!running_) {
    apply(command);
    return true;
  }

  const std::lock_guard<std::mutex> guard(midi_push_mutex_);
  if (midi_commands_.push(command)) {
    return true;
  }

  // The queue is full, which needs a burst far beyond what a MIDI cable can
  // physically carry. Even so the two failures are not equivalent: losing a
  // note-on drops a note, while losing a note-off leaves one sounding
  // forever. So a release waits for room; a note-on gives up.
  if (command.type == audio::AudioCommand::Type::NoteOn) {
    return false;
  }
  for (int attempt = 0; attempt < kFullQueueRetries; ++attempt) {
    std::this_thread::yield();
    if (midi_commands_.push(command)) {
      return true;
    }
  }
  return false;
}

bool AudioEngine::submit(const audio::AudioCommand &command) {
  if (!running_) {
    // Nothing is rendering, so no other thread can be touching the chip:
    // apply directly. This keeps note state consistent when the device failed
    // to open, and lets tests drive a session without a sound card.
    //
    // The transport is always stopped before running_ is cleared (see
    // AudioManager::shutdown), so a callback can never be in flight here.
    apply(command);
    return true;
  }
  return commands_.push(command);
}

void AudioEngine::drain_commands() {
  drain(commands_);
  drain(midi_commands_);
}

void AudioEngine::drain(audio::AudioCommandQueue &queue) {
  audio::AudioCommand command;
  while (queue.pop(command)) {
    apply(command);
  }
}

void AudioEngine::apply(const audio::AudioCommand &command) {
  using Type = audio::AudioCommand::Type;

  switch (command.type) {
  case Type::ApplyPatch: {
    current_instrument_ = command.instrument;
    device_.write_settings(command.global);
    for (ym2612::ChannelIndex index : ym2612::all_channel_indices) {
      auto channel = device_.channel(index);
      channel.write_settings(command.channel);
      channel.write_instrument(command.instrument);
    }
    break;
  }

  case Type::NoteOn: {
    const bool steal = steal_oldest_.load(std::memory_order_relaxed);
    auto claim = allocator_.note_on(command.note, steal);
    if (!claim) {
      break;
    }
    if (claim->replaced_note) {
      device_.channel(claim->channel).write_key_off();
    }

    auto channel = device_.channel(claim->channel);
    channel.write_frequency(command.note);
    const uint8_t velocity =
        use_velocity_.load(std::memory_order_relaxed) ? command.velocity : 127;
    const auto instrument = current_instrument_.clone_with_velocity(velocity);
    channel.write_instrument(instrument);
    channel.write_key_on(
        instrument.operators[static_cast<uint8_t>(ym2612::OperatorIndex::Op1)]
            .enable,
        instrument.operators[static_cast<uint8_t>(ym2612::OperatorIndex::Op2)]
            .enable,
        instrument.operators[static_cast<uint8_t>(ym2612::OperatorIndex::Op3)]
            .enable,
        instrument.operators[static_cast<uint8_t>(ym2612::OperatorIndex::Op4)]
            .enable);
    break;
  }

  case Type::NoteOff:
    allocator_.note_off(command.note, device_);
    break;

  case Type::AllNotesOff:
    allocator_.release_all(device_);
    break;
  }
}

void AudioEngine::apply_patch_to_all_channels(const ym2612::Patch &patch) {
  device_.write_settings(patch.global);
  for (ym2612::ChannelIndex channel_index : ym2612::all_channel_indices) {
    auto channel = device_.channel(channel_index);
    channel.write_settings(patch.channel);
    channel.write_instrument(patch.instrument);
  }
}
