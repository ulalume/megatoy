#pragma once

#include "audio/audio_command.hpp"
#include "audio/load_meter.hpp"
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

  /**
   * Take rendering down and back up around a device reopen, keeping the
   * patch and the chip's registers.
   *
   * Both must run with the transport stopped, because both write the chip
   * from the calling thread. suspend() releases every sounding note -- the
   * queue has no consumer once it returns, so a release could not be
   * delivered later -- and drops the load history, whose slot duration is
   * about to change. resume() rewrites the chip from the current patch.
   */
  void suspend();
  void resume();

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
  void set_note_options(bool use_velocity, uint8_t velocity_sensitivity_depth,
                        bool steal_oldest);

  /// Whether pitch-bend / mod-wheel messages take effect. Read by the audio
  /// thread; disabled commands are applied as their neutral value.
  void set_performance_options(bool pitch_bend, bool mod_wheel);

  /// Apply a patch immediately. Only safe while the device is stopped.
  void apply_patch_to_all_channels(const ym2612::Patch &patch);

  /// Note state, safe to read from the UI thread.
  const ChannelAllocator &notes() const { return allocator_; }

  /**
   * Output frames rendered since initialize(), which is the clock every
   * key-on and key-off is stamped with. Safe to read from any thread.
   */
  uint64_t rendered_samples() const {
    return rendered_samples_.load(std::memory_order_acquire);
  }

  /// The voices and the clock, for the envelope graph. Records first, clock
  /// second, so a note-on that lands between the two can never be timed from
  /// the future.
  VoiceActivityFrame voice_activity() const;

  ym2612::Device &device() { return device_; }
  const ym2612::Device &device() const { return device_; }

  audio::ScopeBuffer &scope_buffer() { return scope_buffer_; }
  const audio::ScopeBuffer &scope_buffer() const { return scope_buffer_; }

  /// How much of each block's deadline render() has been using. Safe to read
  /// from any thread.
  const audio::LoadMeter &load_meter() const { return load_meter_; }

  uint32_t sample_rate() const { return sample_rate_; }
  uint32_t frame_size() const { return frame_size_; }
  bool is_running() const { return running_; }

private:
  uint32_t sample_rate_;
  uint32_t frame_size_;

  void drain_commands();
  void drain(audio::AudioCommandQueue &queue);
  void apply(const audio::AudioCommand &command);
  /// Put the chip into the state the current patch describes, with nothing
  /// keyed on.
  void restore_chip_state();
  void remove_dc(float *interleaved, uint32_t frames);
  bool try_push_midi_command(const audio::AudioCommand &command,
                             bool &queue_full);

  std::vector<float> mix_buffer_; // interleaved stereo, [-1, 1]
  // DC blocker state, one x/y pair per channel.
  float dc_x_[2] = {0.0f, 0.0f};
  float dc_y_[2] = {0.0f, 0.0f};
  ym2612::Device device_;
  audio::ScopeBuffer scope_buffer_;
  // Written once per render() from the audio thread.
  audio::LoadMeter load_meter_;
  audio::AudioCommandQueue commands_;
  audio::AudioCommandQueue midi_commands_;
  std::mutex midi_push_mutex_;
  // Set when an overflowing MIDI queue cannot preserve a release command.
  // The audio thread responds by releasing every channel; while it is pending,
  // producers refuse new note-ons. Overload may drop a note but cannot leave
  // one stuck.
  std::atomic<bool> midi_release_recovery_pending_{false};
  ChannelAllocator allocator_;
  // The chip's own clock. render() advances it once per block, after the
  // block has been rendered; every command drained at the top of that block
  // is stamped with the position the block starts at, which is exactly when
  // its chip writes take effect.
  std::atomic<uint64_t> rendered_samples_{0};
  uint64_t command_sample_ = 0;
  // The instrument notes are played with, kept here so a note command does
  // not have to carry one and MIDI never reads the UI's patch.
  ym2612::ChannelInstrument current_instrument_{};
  ym2612::GlobalSettings current_global_{};
  ym2612::ChannelSettings current_channel_{};
  float bend_semitones_ = 0.0f;
  uint8_t mod_wheel_ = 0;
  std::atomic<bool> use_velocity_{true};
  std::atomic<bool> pitch_bend_enabled_{true};
  std::atomic<bool> mod_wheel_enabled_{false};
  std::atomic<uint8_t> velocity_sensitivity_depth_{100};
  std::atomic<bool> steal_oldest_{true};
  // Read by the UI, audio, and MIDI driver threads.
  std::atomic<bool> running_;
};
