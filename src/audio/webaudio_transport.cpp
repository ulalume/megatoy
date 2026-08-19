#include "audio/webaudio_transport.hpp"
#include <algorithm>
#include <iostream>

namespace {
constexpr std::size_t kReservedPeriods = 4;
constexpr std::size_t kEngineChannels = 2;
constexpr std::size_t kEngineFrameSize = kEngineChannels * sizeof(std::int16_t);
} // namespace

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

  // SDL picks 1024 frames for 44.1 kHz and then the Emscripten backend
  // doubles it, so the browser default is 2048 frames -- 46 ms per callback.
  // That is twice the desktop buffer, and it sets the rate at which the scope
  // can possibly update as well as the floor on note latency.
  //
  // Asking for 512 lands on 1024 after the doubling, matching desktop.
  // Going lower is tempting but riskier here than on desktop: Emscripten runs
  // the audio callback on the main thread, so it competes with rendering and
  // has less slack, not more.
  SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");

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
  SDL_AudioSpec device_format{};
  int sample_frames = 0;
  if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(audio_stream_),
                               &device_format, &sample_frames) &&
      sample_frames > 0) {
    const std::size_t reserved_frames =
        static_cast<std::size_t>(sample_frames) * kReservedPeriods;
    temp_buffer_.reserve(reserved_frames * frame_size_);
    int_buffer_.reserve(reserved_frames * kEngineChannels);
  }

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

  const std::size_t frames_requested =
      static_cast<std::size_t>(required) / frame_size_;
  if (frames_requested == 0) {
    return;
  }
  const std::size_t samples_requested = frames_requested * kEngineChannels;
  if (int_buffer_.size() < samples_requested) {
    int_buffer_.resize(samples_requested);
  }

  const std::uint32_t produced =
      callback_(static_cast<std::uint32_t>(frames_requested * kEngineFrameSize),
                static_cast<void *>(int_buffer_.data()));
  if (produced == 0) {
    return;
  }

  const std::size_t frames = std::min(
      static_cast<std::size_t>(produced) / kEngineFrameSize, frames_requested);
  if (frames == 0) {
    return;
  }

  const std::size_t produced_samples = frames * kEngineChannels;
  temp_buffer_.resize(produced_samples * sizeof(float));
  float *out = reinterpret_cast<float *>(temp_buffer_.data());
  const int16_t *in = int_buffer_.data();

  for (std::size_t i = 0; i < produced_samples; ++i) {
    out[i] = static_cast<float>(in[i]) / 32768.0f;
  }

  SDL_PutAudioStreamData(stream, temp_buffer_.data(),
                         static_cast<int>(produced_samples * sizeof(float)));
}
