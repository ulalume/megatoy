#include "audio/audio_engine.hpp"

#include <algorithm>
#include <cstring>
#include <thread>

#include "audio/performance.hpp"
#include "ym2612/channel.hpp"
#include "ym2612/note.hpp"

namespace {

constexpr uint32_t kFallbackSampleRate = 44100;

// DC blocker pole: first-order high-pass with a cutoff around 3 Hz at
// 44.1 kHz -- far below anything audible, and fast enough that the offset is
// gone within a fraction of a second of startup.
constexpr float kDcBlockerPole = 0.9995f;
// How long a note-off will wait for queue space before being abandoned.
constexpr int kFullQueueRetries = 1000;
constexpr uint32_t kDefaultFrameSize = sizeof(int16_t) * 2; // stereo s16
constexpr size_t kReservedMixFrames = 8192;

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
  mix_buffer_.reserve(kReservedMixFrames * 2);
  scope_buffer_.clear();
  midi_release_recovery_pending_.store(false, std::memory_order_relaxed);
  dc_x_[0] = dc_x_[1] = 0.0f;
  dc_y_[0] = dc_y_[1] = 0.0f;
  bend_semitones_ = 0.0f;
  mod_wheel_ = 0;
  device_.init(sample_rate_);
  running_ = device_.is_initialized();
  return running_;
}

void AudioEngine::shutdown() {
  running_ = false;
  midi_release_recovery_pending_.store(false, std::memory_order_relaxed);
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

  // The YM2612's DAC is discontinuous around zero and ymfm reproduces that
  // faithfully, so even an idle chip emits a constant offset (about 500 LSB
  // at 16 bits). Real hardware never lets that reach the speaker -- the
  // console AC-couples its audio output -- but shipping it to a modern DAC
  // means the "silent" signal is a nonzero constant. Amplifiers that gate on
  // signal energy then drop in and out of standby, and every transition is
  // an audible pop; users reported exactly that, a tick every several
  // seconds while the app sat idle. Blocking DC restores the coupling the
  // hardware had: idle output decays to true digital silence.
  remove_dc(mix_buffer_.data(), frames);

  scope_buffer_.write(mix_buffer_.data(), frames);

  auto *pcm = static_cast<int16_t *>(data);
  for (size_t i = 0; i < required; ++i) {
    pcm[i] = to_pcm16(mix_buffer_[i]);
  }

  return frames * frame_size_;
}

void AudioEngine::remove_dc(float *interleaved, uint32_t frames) {
  for (uint32_t i = 0; i < frames; ++i) {
    for (int ch = 0; ch < 2; ++ch) {
      const float x = interleaved[i * 2 + ch];
      const float y = x - dc_x_[ch] + kDcBlockerPole * dc_y_[ch];
      dc_x_[ch] = x;
      dc_y_[ch] = y;
      interleaved[i * 2 + ch] = y;
    }
  }
}

void AudioEngine::set_note_options(bool use_velocity,
                                   uint8_t velocity_sensitivity_depth,
                                   bool steal_oldest) {
  use_velocity_.store(use_velocity, std::memory_order_relaxed);
  velocity_sensitivity_depth_.store(
      std::min<uint8_t>(velocity_sensitivity_depth, 100),
      std::memory_order_relaxed);
  steal_oldest_.store(steal_oldest, std::memory_order_relaxed);
}

void AudioEngine::set_performance_options(bool pitch_bend, bool mod_wheel) {
  pitch_bend_enabled_.store(pitch_bend, std::memory_order_relaxed);
  mod_wheel_enabled_.store(mod_wheel, std::memory_order_relaxed);
}

bool AudioEngine::submit_from_midi(const audio::AudioCommand &command) {
  if (!running_.load(std::memory_order_acquire)) {
    // This runs on the MIDI driver's thread. With no audio thread draining
    // the queue, applying here would race the UI thread's own inline apply
    // (see submit); the note cannot sound anyway, so drop it.
    return false;
  }

  const bool is_release =
      command.type == audio::AudioCommand::Type::NoteOff ||
      command.type == audio::AudioCommand::Type::AllNotesOff;
  bool queue_full = false;
  if (try_push_midi_command(command, queue_full)) {
    return true;
  }
  if (!queue_full) {
    return false;
  }

  // The queue is full, which needs a burst far beyond what a MIDI cable can
  // physically carry. Even so the two failures are not equivalent: losing a
  // note-on drops a note, while losing a note-off leaves one sounding
  // forever. So a release waits for room; a note-on gives up.
  if (!is_release) {
    return false;
  }
  for (int attempt = 0; attempt < kFullQueueRetries; ++attempt) {
    std::this_thread::yield();
    if (try_push_midi_command(command, queue_full)) {
      return true;
    }
    if (!queue_full) {
      return false;
    }
  }
  if (is_release) {
    const std::lock_guard<std::mutex> guard(midi_push_mutex_);
    midi_release_recovery_pending_.store(true, std::memory_order_release);
    return true;
  }
  return false;
}

bool AudioEngine::try_push_midi_command(const audio::AudioCommand &command,
                                        bool &queue_full) {
  const std::lock_guard<std::mutex> guard(midi_push_mutex_);
  queue_full = false;
  const bool is_release =
      command.type == audio::AudioCommand::Type::NoteOff ||
      command.type == audio::AudioCommand::Type::AllNotesOff;
  if (midi_release_recovery_pending_.load(std::memory_order_acquire)) {
    if (command.type == audio::AudioCommand::Type::NoteOn) {
      return false;
    }
    if (is_release) {
      // The pending all-notes-off already subsumes this release.
      return true;
    }
  }
  if (midi_commands_.push(command)) {
    return true;
  }
  queue_full = true;
  return false;
}

bool AudioEngine::submit(const audio::AudioCommand &command) {
  if (!running_.load(std::memory_order_acquire)) {
    // Nothing is rendering, so no other thread can be touching the chip:
    // apply directly. This keeps note state consistent when the device failed
    // to open, and lets tests drive a session without a sound card.
    //
    // The transport is always stopped before running_ is cleared (see
    // AudioManager::shutdown), so a callback can never be in flight here --
    // and submit_from_midi drops commands while not running, so the MIDI
    // driver thread cannot be applying either.
    apply(command);
    return true;
  }
  return commands_.push(command);
}

void AudioEngine::drain_commands() {
  drain(commands_);
  drain(midi_commands_);
  if (midi_release_recovery_pending_.exchange(false,
                                              std::memory_order_acq_rel)) {
    apply(audio::AudioCommand::all_notes_off());
  }
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
    current_global_ = command.global;
    current_channel_ = command.channel;
    current_instrument_ = command.instrument;
    const auto effective_global = audio::performance::compose_global_settings(
        current_global_, mod_wheel_);
    const auto effective_channel = audio::performance::compose_channel_settings(
        current_channel_, current_global_.lfo_enable, mod_wheel_);
    device_.write_settings(effective_global);
    const auto depth =
        velocity_sensitivity_depth_.load(std::memory_order_relaxed);
    for (ym2612::ChannelIndex index : ym2612::all_channel_indices) {
      auto channel = device_.channel(index);
      channel.write_settings(effective_channel);
      const auto velocity = allocator_.active_velocity(index);
      channel.write_instrument(
          velocity ? command.instrument.clone_with_velocity(*velocity, depth)
                   : command.instrument);
    }
    break;
  }

  case Type::NoteOn: {
    const bool steal = steal_oldest_.load(std::memory_order_relaxed);
    const uint8_t velocity = audio::performance::effective_velocity(
        use_velocity_.load(std::memory_order_relaxed), command.velocity);
    auto claim = allocator_.note_on(command.note, velocity, steal);
    if (!claim) {
      break;
    }
    if (claim->replaced_note) {
      device_.channel(claim->channel).write_key_off();
    }

    auto channel = device_.channel(claim->channel);
    channel.write_frequency(command.note, bend_semitones_);
    channel.write_settings(audio::performance::compose_channel_settings(
        current_channel_, current_global_.lfo_enable, mod_wheel_));
    const auto depth =
        velocity_sensitivity_depth_.load(std::memory_order_relaxed);
    const auto instrument =
        current_instrument_.clone_with_velocity(velocity, depth);
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

  case Type::PitchBend:
    bend_semitones_ =
        pitch_bend_enabled_.load(std::memory_order_relaxed)
            ? audio::performance::pitch_bend_semitones(command.pitch_bend_value)
            : 0.0f;
    for (ym2612::ChannelIndex index : ym2612::all_channel_indices) {
      if (const auto note = allocator_.active_note(index)) {
        device_.channel(index).write_frequency(*note, bend_semitones_);
      }
    }
    break;

  case Type::ModWheel: {
    mod_wheel_ = mod_wheel_enabled_.load(std::memory_order_relaxed)
                     ? std::min<uint8_t>(command.mod_wheel_value, 127)
                     : 0;
    device_.write_settings(audio::performance::compose_global_settings(
        current_global_, mod_wheel_));
    const auto channel_settings = audio::performance::compose_channel_settings(
        current_channel_, current_global_.lfo_enable, mod_wheel_);
    for (ym2612::ChannelIndex index : ym2612::all_channel_indices) {
      device_.channel(index).write_settings(channel_settings);
    }
    break;
  }

  case Type::SetChipType:
    device_.set_chip_type(command.chip_type);
    apply(audio::AudioCommand::apply_patch(current_global_, current_channel_,
                                           current_instrument_));
    apply(audio::AudioCommand::all_notes_off());
    break;
  }
}

void AudioEngine::apply_patch_to_all_channels(const ym2612::Patch &patch) {
  current_global_ = patch.global;
  current_channel_ = patch.channel;
  current_instrument_ = patch.instrument;
  device_.write_settings(
      audio::performance::compose_global_settings(current_global_, mod_wheel_));
  const auto channel_settings = audio::performance::compose_channel_settings(
      current_channel_, current_global_.lfo_enable, mod_wheel_);
  for (ym2612::ChannelIndex channel_index : ym2612::all_channel_indices) {
    auto channel = device_.channel(channel_index);
    channel.write_settings(channel_settings);
    channel.write_instrument(patch.instrument);
  }
}
