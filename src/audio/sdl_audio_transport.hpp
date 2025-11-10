#pragma once

#include "audio/audio_transport.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

class SdlAudioTransport : public AudioTransport {
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

private:
  void audio_thread_func();
  void pump_audio_stream();

  static std::uint32_t align_to_frame(std::uint32_t bytes,
                                      std::uint32_t frame_size);

  SDL_AudioStream *audio_stream_;
  bool owns_audio_subsystem_;
  std::thread audio_thread_;
  std::atomic<bool> audio_thread_alive_{false};
  std::vector<std::uint8_t> stream_buffer_;

  RenderCallback callback_;
  std::uint32_t frame_size_;
  std::uint32_t target_buffer_bytes_;
  bool initialized_;
};
