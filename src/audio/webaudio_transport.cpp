#include "audio/webaudio_transport.hpp"
#include <algorithm>
#include <iostream>

WebAudioTransport::WebAudioTransport()
    : audio_stream_(nullptr), owns_audio_subsystem_(false), callback_(),
      temp_buffer_(), initialized_(false), frame_size_(0), sample_rate_(0) {}

WebAudioTransport::~WebAudioTransport() { stop(); }

bool WebAudioTransport::start(std::uint32_t sample_rate,
                              RenderCallback callback) {
  if (initialized_) {
    return true;
  }
  if (!callback) {
    std::cerr << "Audio callback must not be empty\n";
    return false;
  }

  callback_ = std::move(callback);

  if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
    if (!SDL_Init(SDL_INIT_AUDIO)) {
      std::cerr << "Failed to initialize SDL audio: " << SDL_GetError()
                << std::endl;
      return false;
    }
    owns_audio_subsystem_ = true;
  }

  SDL_AudioSpec desired{};
  desired.freq = static_cast<int>(sample_rate);
  desired.channels = 2;
  desired.format = SDL_AUDIO_F32;

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

  frame_size_ = SDL_AUDIO_FRAMESIZE(desired);
  sample_rate_ = sample_rate != 0 ? sample_rate : desired.freq;

  if (!SDL_SetAudioStreamGetCallback(audio_stream_, stream_callback, this)) {
    std::cerr << "Failed to set audio stream callback: " << SDL_GetError()
              << std::endl;
    SDL_DestroyAudioStream(audio_stream_);
    audio_stream_ = nullptr;
    if (owns_audio_subsystem_) {
      SDL_QuitSubSystem(SDL_INIT_AUDIO);
      owns_audio_subsystem_ = false;
    }
    return false;
  }

  if (!SDL_ResumeAudioStreamDevice(audio_stream_)) {
    std::cerr << "Failed to start SDL audio device: " << SDL_GetError()
              << std::endl;
  }

  initialized_ = true;
  return true;
}

void WebAudioTransport::stop() {
  if (!initialized_) {
    return;
  }

  if (audio_stream_ != nullptr) {
    SDL_SetAudioStreamGetCallback(audio_stream_, nullptr, nullptr);
    SDL_PauseAudioStreamDevice(audio_stream_);
    SDL_DestroyAudioStream(audio_stream_);
    audio_stream_ = nullptr;
  }

  if (owns_audio_subsystem_) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    owns_audio_subsystem_ = false;
  }

  callback_ = nullptr;
  temp_buffer_.clear();
  initialized_ = false;
}

void SDLCALL WebAudioTransport::stream_callback(void *userdata,
                                                SDL_AudioStream *stream,
                                                int additional_amount,
                                                int total_amount) {
  if (auto *self = static_cast<WebAudioTransport *>(userdata)) {
    self->handle_stream_callback(stream, additional_amount, total_amount);
  }
}

void WebAudioTransport::handle_stream_callback(SDL_AudioStream *stream,
                                               int additional_amount,
                                               int total_amount) {
  if (!callback_ || stream == nullptr || frame_size_ == 0) {
    return;
  }

  int available = SDL_GetAudioStreamAvailable(stream);
  int required = total_amount - available;
  if (required < additional_amount) {
    required = additional_amount;
  }
  if (required <= 0) {
    return;
  }

  if (int_buffer_.size() < static_cast<size_t>(required / sizeof(int16_t))) {
    int_buffer_.resize(static_cast<size_t>(required / sizeof(int16_t)));
  }

  std::uint32_t produced = callback_(static_cast<std::uint32_t>(required),
                                     static_cast<void *>(int_buffer_.data()));
  if (produced == 0) {
    return;
  }

  std::size_t frames =
      static_cast<std::size_t>(produced) / (sizeof(int16_t) * 2);
  temp_buffer_.resize(frames * sizeof(float) * 2);
  float *out = reinterpret_cast<float *>(temp_buffer_.data());
  const int16_t *in = int_buffer_.data();
  for (std::size_t i = 0; i < frames * 2; ++i) {
    out[i] = static_cast<float>(in[i]) / 32768.0f;
  }

  SDL_PutAudioStreamData(stream, temp_buffer_.data(),
                         static_cast<int>(frames * sizeof(float) * 2));
}
