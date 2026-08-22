#pragma once

#include "audio/audio_transport.hpp"
#include <SDL3/SDL.h>
#include <cstdint>
#include <vector>

/**
 * Desktop audio output.
 *
 * Pull model: SDL's device thread calls back whenever the stream needs more
 * data, and the render callback fills exactly that much. There is no feeder
 * thread of our own -- the previous implementation kept one that polled
 * SDL_GetAudioStreamAvailable in a tight loop, which pinned a full core for
 * the lifetime of the app.
 */
class SdlAudioTransport : public AudioTransport {
public:
  /// What the device is asked for when the preference is left at 0.
  static constexpr int kDefaultBufferFrames = 384;

public:
  SdlAudioTransport();
  ~SdlAudioTransport() override;

  SdlAudioTransport(const SdlAudioTransport &) = delete;
  SdlAudioTransport &operator=(const SdlAudioTransport &) = delete;
  SdlAudioTransport(SdlAudioTransport &&) = delete;
  SdlAudioTransport &operator=(SdlAudioTransport &&) = delete;

  bool start(std::uint32_t sample_rate, RenderCallback callback) override;
  void stop() override;
  bool is_active() const override { return initialized_; }
  void set_buffer_frames(int frames) override { buffer_frames_ = frames; }
  int default_buffer_frames() const override { return kDefaultBufferFrames; }

private:
  static void SDLCALL stream_callback(void *userdata, SDL_AudioStream *stream,
                                      int additional_amount, int total_amount);
  void handle_stream_callback(SDL_AudioStream *stream, int additional_amount,
                              int total_amount);

  static std::uint32_t align_to_frame(std::uint32_t bytes,
                                      std::uint32_t frame_size);

  SDL_AudioStream *audio_stream_;
  bool owns_audio_subsystem_;
  std::vector<std::uint8_t> stream_buffer_;

  RenderCallback callback_;
  std::uint32_t frame_size_;
  bool initialized_;
  int buffer_frames_ = 0;
};
