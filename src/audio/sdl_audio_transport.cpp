#include "audio/sdl_audio_transport.hpp"
#include <algorithm>
#include <emu/EmuStructs.h>
#include <iostream>

namespace {
constexpr std::uint32_t kFallbackSampleRate = 44100;
constexpr std::uint32_t kFallbackFrameSize = sizeof(INT16) * 2;
} // namespace

SdlAudioTransport::SdlAudioTransport()
    : audio_stream_(nullptr), owns_audio_subsystem_(false), frame_size_(0),
      target_buffer_bytes_(0), initialized_(false) {}

SdlAudioTransport::~SdlAudioTransport() { stop(); }

bool SdlAudioTransport::start(std::uint32_t sample_rate,
                              RenderCallback callback) {
  if (initialized_) {
    return true;
  }

  if (!callback) {
    std::cerr << "Audio callback must not be empty\n";
    return false;
  }

  callback_ = std::move(callback);
  const std::uint32_t effective_sample_rate =
      sample_rate != 0 ? sample_rate : kFallbackSampleRate;

  if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
    if (!SDL_Init(SDL_INIT_AUDIO)) {
      std::cerr << "Failed to initialize SDL audio: " << SDL_GetError()
                << std::endl;
      return false;
    }
    owns_audio_subsystem_ = true;
  }

  SDL_AudioSpec desired{};
  desired.freq = static_cast<int>(effective_sample_rate);
  desired.channels = 2;
  desired.format = SDL_AUDIO_S16;

  audio_stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                            &desired, nullptr, nullptr);
  if (audio_stream_ == nullptr) {
    std::cerr << "Failed to open SDL audio stream: " << SDL_GetError()
              << std::endl;
    if (owns_audio_subsystem_) {
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
      owns_audio_subsystem_ = false;
    }
    return false;
  }

  SDL_AudioSpec obtained{};
  if (SDL_GetAudioStreamFormat(audio_stream_, nullptr, &obtained)) {
    std::cout << "SDL audio device output: " << obtained.freq << " Hz, "
              << obtained.channels << " channels\n";
  }

  frame_size_ = SDL_AUDIO_FRAMESIZE(desired);
  if (frame_size_ == 0) {
    frame_size_ = kFallbackFrameSize;
  }

  target_buffer_bytes_ = std::max<std::uint32_t>(
      frame_size_ * (effective_sample_rate / 40), frame_size_ * 8);

  if (!SDL_ResumeAudioStreamDevice(audio_stream_)) {
    std::cerr << "Failed to start SDL audio device: " << SDL_GetError()
              << std::endl;
  }

  audio_thread_alive_ = true;
  audio_thread_ = std::thread([this]() { audio_thread_func(); });

  initialized_ = true;
  return true;
}

void SdlAudioTransport::stop() {
  if (!initialized_) {
    return;
  }

  audio_thread_alive_ = false;
  if (audio_thread_.joinable()) {
    audio_thread_.join();
  }

  if (audio_stream_ != nullptr) {
    SDL_PauseAudioStreamDevice(audio_stream_);
    SDL_DestroyAudioStream(audio_stream_);
    audio_stream_ = nullptr;
  }

  if (owns_audio_subsystem_) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    owns_audio_subsystem_ = false;
  }

  callback_ = nullptr;
  stream_buffer_.clear();
  initialized_ = false;
}

void SdlAudioTransport::audio_thread_func() {
  while (audio_thread_alive_) {
    if (audio_stream_ != nullptr) {
      pump_audio_stream();
    } else {
      SDL_Delay(5);
    }
  }
}

void SdlAudioTransport::pump_audio_stream() {
  if (!callback_ || audio_stream_ == nullptr || frame_size_ == 0) {
    return;
  }

  while (true) {
    const int available = SDL_GetAudioStreamAvailable(audio_stream_);
    if (available < 0) {
      return;
    }

    if (available >= static_cast<int>(target_buffer_bytes_)) {
      return;
    }

    std::uint32_t deficit =
        target_buffer_bytes_ - static_cast<std::uint32_t>(available);
    deficit = align_to_frame(deficit, frame_size_);
    if (deficit == 0) {
      deficit = frame_size_;
    }

    stream_buffer_.resize(deficit);
    const std::uint32_t produced =
        callback_(deficit, static_cast<void *>(stream_buffer_.data()));
    if (produced == 0) {
      return;
    }

    if (!SDL_PutAudioStreamData(audio_stream_, stream_buffer_.data(),
                                static_cast<int>(produced))) {
      std::cerr << "SDL_PutAudioStreamData failed: " << SDL_GetError()
                << std::endl;
      return;
    }
  }
}

std::uint32_t SdlAudioTransport::align_to_frame(std::uint32_t bytes,
                                                std::uint32_t frame_size) {
  if (frame_size == 0) {
    return bytes;
  }
  const std::uint32_t remainder = bytes % frame_size;
  if (remainder == 0) {
    return bytes;
  }
  return bytes + (frame_size - remainder);
}
