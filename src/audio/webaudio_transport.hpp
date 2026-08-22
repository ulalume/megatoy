#pragma once

#include "audio/audio_transport.hpp"
#include <SDL3/SDL.h>
#include <cstdint>
#include <vector>

class WebAudioTransport : public AudioTransport {
public:
  /// Frames the device ends up with when the preference is left at 0. SDL's
  /// Emscripten backend doubles what it is asked for, so the request is half.
  static constexpr int kDefaultBufferFrames = 1024;

  WebAudioTransport();
  ~WebAudioTransport() override;

  bool start(std::uint32_t sample_rate, RenderCallback callback) override;
  void stop() override;
  bool is_active() const override { return initialized_; }
  void set_buffer_frames(int frames) override { buffer_frames_ = frames; }

private:
  static void SDLCALL stream_callback(void *userdata, SDL_AudioStream *stream,
                                      int additional_amount, int total_amount);
  void handle_stream_callback(SDL_AudioStream *stream, int additional_amount,
                              int total_amount);

  SDL_AudioStream *audio_stream_;
  bool owns_audio_subsystem_;
  RenderCallback callback_;
  std::vector<std::uint8_t> temp_buffer_;
  std::vector<int16_t> int_buffer_;
  bool initialized_;
  std::uint32_t frame_size_;
  std::uint32_t sample_rate_;
  int buffer_frames_ = 0;
};
