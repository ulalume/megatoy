#pragma once

#include "audio/audio_engine.hpp"
#include "audio/audio_transport.hpp"
#include "audio/performance.hpp"
#include "ym2612/patch.hpp"
#include <algorithm>
#include <cstdint>
#include <memory>

/**
 * AudioManager - audio system management
 */
class AudioManager {
public:
  AudioManager();
  explicit AudioManager(std::unique_ptr<AudioTransport> transport);
  ~AudioManager();

  // Non-copyable, non-movable
  AudioManager(const AudioManager &) = delete;
  AudioManager &operator=(const AudioManager &) = delete;
  AudioManager(AudioManager &&) = delete;
  AudioManager &operator=(AudioManager &&) = delete;

  /**
   * Initialize and start the complete audio system
   * @param sample_rate Target sample rate
   * @return true on success, false on failure
   */
  bool initialize(uint32_t sample_rate, int buffer_frames = 0);

  /**
   * Reopen the device on a different buffer size.
   *
   * A device cannot be reopened from inside its own callback, so this closes
   * the transport and does the work on the calling thread rather than through
   * the command queue, which that callback drains. The patch is carried
   * across; sounding notes are released and the load history, whose entries
   * would otherwise no longer all cover the same span of audio, is dropped. A
   * gap in the output while the device is closed is expected.
   *
   * `buffer_frames` is the preference, 0 meaning the platform default; only a
   * change in the frames it resolves to reopens anything. Does nothing while
   * no device is open: the preference is read again when one is.
   *
   * @return false if the device would not reopen. The previous size is put
   * back if it still works; is_running() says whether it did.
   */
  bool set_buffer_frames(int buffer_frames);

  /// Frames per callback the open device was asked for.
  int buffer_frames() const { return buffer_frames_; }

  /// Frames the device uses when the buffer preference is left unset.
  int default_buffer_frames() const {
    return transport_ ? transport_->default_buffer_frames() : 0;
  }

  /**
   * Shutdown and cleanup complete audio system
   */
  void shutdown();

  /**
   * Direct access to YM2612 device
   */
  ym2612::Device &device() { return engine_.device(); }
  const ym2612::Device &device() const { return engine_.device(); }

  /**
   * Direct access to the scope buffer holding recent output
   */
  audio::ScopeBuffer &scope_buffer() { return engine_.scope_buffer(); }
  const audio::ScopeBuffer &scope_buffer() const {
    return engine_.scope_buffer();
  }

  /// How much of each block's deadline rendering has been using.
  const audio::LoadMeter &load_meter() const { return engine_.load_meter(); }

  /**
   * Hand a chip write to the audio thread. See AudioEngine::submit.
   */
  bool submit(const audio::AudioCommand &command) {
    return engine_.submit(command);
  }

  /// Same, from a MIDI driver callback.
  bool submit_from_midi(const audio::AudioCommand &command) {
    return engine_.submit_from_midi(command);
  }

  void set_note_options(bool use_velocity, int velocity_sensitivity_depth,
                        bool steal_oldest) {
    engine_.set_note_options(
        use_velocity,
        static_cast<uint8_t>(std::clamp(velocity_sensitivity_depth, 0, 100)),
        steal_oldest);
  }

  void set_performance_options(bool pitch_bend, bool mod_wheel) {
    engine_.set_performance_options(pitch_bend, mod_wheel);
    // Disabling must not freeze a held bend or a raised wheel: push the
    // neutral value through the normal command path so the audio thread
    // rewrites the affected channels.
    if (!pitch_bend) {
      engine_.submit(audio::AudioCommand::pitch_bend(
          audio::performance::kPitchBendCenter));
    }
    if (!mod_wheel) {
      engine_.submit(audio::AudioCommand::mod_wheel(0));
    }
  }

  void set_chip_type(ym2612::ChipType type) {
    engine_.submit(audio::AudioCommand::set_chip_type(type));
  }

  void set_core_type(ym2612::CoreType type) {
    engine_.submit(audio::AudioCommand::set_core_type(type));
  }

  /// Note state, safe to read from the UI thread.
  const ChannelAllocator &notes() const { return engine_.notes(); }

  /// The voices and the chip's clock, for the envelope graph's live cursor.
  VoiceActivityFrame voice_activity() const { return engine_.voice_activity(); }

  /**
   * Apply patch settings to all channels. Only safe while stopped.
   */
  void apply_patch_to_all_channels(const ym2612::Patch &patch);

  /**
   * Check if audio system is running
   */
  bool is_running() const { return engine_.is_running(); }

  /**
   * Get current sample rate
   */
  uint32_t sample_rate() const { return engine_.sample_rate(); }

private:
  /// Open the device on `buffer_frames`, recording what that resolves to.
  bool start_transport(int buffer_frames);
  int resolve_buffer_frames(int preference) const;

  AudioEngine engine_;
  std::unique_ptr<AudioTransport> transport_;
  int buffer_frames_ = 0;
};
