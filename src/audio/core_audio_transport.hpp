#pragma once

#include "audio/audio_transport.hpp"

#include <AudioToolbox/AudioToolbox.h>

/**
 * macOS audio output, straight to Core Audio's default output unit.
 *
 * SDL's macOS backend goes through Audio Queue Services, which keeps at
 * least 15 ms of buffers queued ahead of the device; this renders into the
 * device's own I/O cycle instead, so a note is as late as one buffer and the
 * device itself. The unit follows the default output device as it changes,
 * and converts the sample rate to the device's.
 */
class CoreAudioTransport : public AudioTransport {
public:
  /// The device's I/O buffer when the preference is left at 0.
  static constexpr int kDefaultBufferFrames = 256;

  CoreAudioTransport() = default;
  ~CoreAudioTransport() override;

  CoreAudioTransport(const CoreAudioTransport &) = delete;
  CoreAudioTransport &operator=(const CoreAudioTransport &) = delete;

  bool start(std::uint32_t sample_rate, RenderCallback callback) override;
  void stop() override;
  bool is_active() const override { return unit_ != nullptr; }
  void set_buffer_frames(int frames) override { buffer_frames_ = frames; }
  int default_buffer_frames() const override { return kDefaultBufferFrames; }

private:
  static OSStatus render(void *self, AudioUnitRenderActionFlags *flags,
                         const AudioTimeStamp *time, UInt32 bus, UInt32 frames,
                         AudioBufferList *data);
  /// Asks the unit's device for `frames` per I/O cycle, and reports what the
  /// device then runs at.
  void use_buffer_frames(UInt32 frames) const;

  AudioComponentInstance unit_ = nullptr;
  RenderCallback callback_;
  int buffer_frames_ = 0;
};
