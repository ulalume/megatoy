#pragma once

#include "audio/audio_command.hpp"
#include "audio/audio_transport.hpp"
#include "audio/load_meter.hpp"
#include "audio/scope_buffer.hpp"
#include "channel_allocator.hpp"
#include "platform/platform_config.hpp"
#include "ym2612/note.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>
#if defined(MEGATOY_PLATFORM_WEB)
#include "platform/web/audio_worklet/worklet_protocol.hpp"
#else
#include "audio/audio_engine.hpp"
#endif

/**
 * AudioManager - audio system management
 *
 * On the desktop the engine runs here, rendered by the transport's audio
 * thread. On the web it runs in an audio worklet on the browser's audio
 * thread (src/platform/web/web_audio_worklet.cpp), out of the page's reach:
 * commands are posted to it, and what the UI reads -- the scope, the load,
 * the notes and voices -- is a copy it reports back every few milliseconds.
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

  /**
   * Close the device and open it again at the same size, for a browser
   * that stopped the old one for good.
   */
  bool reopen_device();

  /// Frames per callback the open device was asked for.
  int buffer_frames() const;

  /// Frames the device uses when the buffer preference is left unset; 0
  /// where the size is not the app's to choose.
  int default_buffer_frames() const;

  /**
   * Shutdown and cleanup complete audio system
   */
  void shutdown();

  /// Recent output, for the waveform.
  const audio::ScopeBuffer &scope_buffer() const;

  /// How much of each block's deadline rendering has been using.
  const audio::LoadMeter &load_meter() const;

  /**
   * Hand a chip write to the audio thread. See AudioEngine::submit.
   */
  bool submit(const audio::AudioCommand &command);

  /// Same, from a MIDI driver callback.
  bool submit_from_midi(const audio::AudioCommand &command);

  void set_note_options(bool use_velocity, int velocity_sensitivity_depth,
                        bool steal_oldest);

  /// Disabling one sends its neutral value, so a held bend or a raised wheel
  /// does not stay frozen.
  void set_performance_options(bool pitch_bend, bool mod_wheel);

  void set_chip_type(ym2612::ChipType type) {
    submit(audio::AudioCommand::set_chip_type(type));
  }

  void set_core_type(ym2612::CoreType type) {
    submit(audio::AudioCommand::set_core_type(type));
  }

  /// The notes sounding now, and whether one is: for the keyboard and the
  /// chord.
  std::vector<ym2612::Note> sounding_notes() const;
  bool note_sounding(const ym2612::Note &note) const;

  /// The voices and the chip's clock, for the envelope graph's live cursor.
  VoiceActivityFrame voice_activity() const;

  /**
   * Check if audio system is running
   */
  bool is_running() const;

  /**
   * Get current sample rate
   */
  uint32_t sample_rate() const;

#if defined(MEGATOY_PLATFORM_WEB)
  /// From the worklet: `frames` of output, interleaved stereo, and the
  /// engine's state after them; now and then also that the last
  /// `timed_frames` took `elapsed_ms` to render.
  void receive_report(const float *scope, uint32_t frames,
                      uint32_t timed_frames, double elapsed_ms,
                      const web_audio::Snapshot &snapshot);
  /// Sends the worklet started anew what the last one had been told.
  void resend_state();
#endif

private:
#if defined(MEGATOY_PLATFORM_WEB)
  void post(const audio::AudioCommand &command);

  audio::ScopeBuffer scope_;
  audio::LoadMeter load_;
  web_audio::Snapshot snapshot_{};
  uint32_t sample_rate_ = 0;
  bool running_ = false;
  // What sets the engine up rather than plays it: kept for a worklet started
  // anew, which begins with an engine of its own.
  std::optional<audio::AudioCommand> patch_;
  std::optional<audio::AudioCommand> chip_;
  std::optional<audio::AudioCommand> core_;
  struct NoteOptions {
    bool use_velocity;
    int depth;
    bool steal_oldest;
  };
  std::optional<NoteOptions> note_options_;
  std::optional<std::pair<bool, bool>> performance_options_;
#else
  /// Open the device on `buffer_frames`, recording what that resolves to.
  bool start_transport(int buffer_frames);
  int resolve_buffer_frames(int preference) const;

  AudioEngine engine_;
  std::unique_ptr<AudioTransport> transport_;
  int buffer_frames_ = 0;
#endif
};
