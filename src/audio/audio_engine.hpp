#pragma once

#include "audio/audio_command.hpp"
#include "audio/scope_buffer.hpp"
#include "channel_allocator.hpp"
#include "ym2612/device.hpp"
#include "ym2612/patch.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

class AudioEngine {
public:
  AudioEngine();
  ~AudioEngine() = default;

  AudioEngine(const AudioEngine &) = delete;
  AudioEngine &operator=(const AudioEngine &) = delete;
  AudioEngine(AudioEngine &&) = delete;
  AudioEngine &operator=(AudioEngine &&) = delete;

  bool initialize(uint32_t sample_rate);
  void shutdown();

  /// Fill `data` with up to `buf_size` bytes of interleaved stereo s16 audio.
  /// Returns the number of bytes written.
  uint32_t render(uint32_t buf_size, void *data);

  /**
   * Hand work to the audio thread.
   *
   * Every chip write -- notes and patch edits alike -- goes through here, so
   * the audio thread is the only writer. Note timing then depends on the
   * audio buffer rather than on how fast the UI happens to be drawing.
   *
   * Returns false only if the queue is full, which needs a whole buffer's
   * worth of unprocessed input to happen.
   */
  bool submit(const audio::AudioCommand &command);

  /**
   * Same, from a MIDI driver callback.
   *
   * A separate queue from the UI's so each has a single producer. RtMidi runs
   * one thread per open port, so pushes are serialized by a mutex -- held
   * only by producers, never by the audio thread, which keeps the consumer
   * side lock-free.
   */
  bool submit_from_midi(const audio::AudioCommand &command);

  /// How incoming notes are treated. Read by the audio thread.
  void set_note_options(bool use_velocity, bool steal_oldest);

  /// Apply a patch immediately. Only safe while the device is stopped.
  void apply_patch_to_all_channels(const ym2612::Patch &patch);

  /// Note state, safe to read from the UI thread.
  const ChannelAllocator &notes() const { return allocator_; }

  ym2612::Device &device() { return device_; }
  const ym2612::Device &device() const { return device_; }

  audio::ScopeBuffer &scope_buffer() { return scope_buffer_; }
  const audio::ScopeBuffer &scope_buffer() const { return scope_buffer_; }

  uint32_t sample_rate() const { return sample_rate_; }
  uint32_t frame_size() const { return frame_size_; }
  bool is_running() const { return running_; }

private:
  uint32_t sample_rate_;
  uint32_t frame_size_;

  void drain_commands();
  void drain(audio::AudioCommandQueue &queue);
  void apply(const audio::AudioCommand &command);

  std::vector<float> mix_buffer_; // interleaved stereo, [-1, 1]
  ym2612::Device device_;
  audio::ScopeBuffer scope_buffer_;
  audio::AudioCommandQueue commands_;
  audio::AudioCommandQueue midi_commands_;
  std::mutex midi_push_mutex_;
  ChannelAllocator allocator_;
  // The instrument notes are played with, kept here so a note command does
  // not have to carry one and MIDI never reads the UI's patch.
  ym2612::ChannelInstrument current_instrument_{};
  std::atomic<bool> use_velocity_{true};
  std::atomic<bool> steal_oldest_{true};
  bool running_;
};
