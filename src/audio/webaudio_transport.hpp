#pragma once

#include "audio/audio_transport.hpp"
#include <SDL3/SDL.h>
#include <cstdint>
#include <vector>

class WebAudioTransport : public AudioTransport {
public:
  WebAudioTransport();
  ~WebAudioTransport() override;

  bool start(std::uint32_t sample_rate, RenderCallback callback) override;
  void stop() override;
  bool is_active() const override { return initialized_; }

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
};
