#include "audio/sdl_audio_transport.hpp"
#include <algorithm>
#include <cstdint>
#include <iostream>

namespace {
constexpr std::uint32_t kFallbackSampleRate = 44100;
constexpr std::uint32_t kFallbackFrameSize = sizeof(std::int16_t) * 2;
constexpr std::size_t kReservedPeriods = 4;
} // namespace

SdlAudioTransport::SdlAudioTransport()
    : audio_stream_(nullptr), owns_audio_subsystem_(false), frame_size_(0),
      initialized_(false) {}

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

  // Without this hint SDL picks 1024-frame periods, and its macOS backend
  // queues three of them pre-filled with silence -- a permanent ~46 ms of
  // pipeline ahead of every rendered note. 384 keeps the period above the
  // 15 ms threshold where that backend doubles the buffer count (256 would
  // land in the six-buffer regime and be WORSE), cutting mean note latency
  // from ~58 ms to ~30 ms at 44.1 kHz. Plain SetHint keeps NORMAL priority,
  // so the SDL_AUDIO_DEVICE_SAMPLE_FRAMES environment variable still
  // overrides it for tuning.
  SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "384");

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
  SDL_AudioSpec device_format{};
  int sample_frames = 0;
  if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(audio_stream_),
                               &device_format, &sample_frames) &&
      sample_frames > 0) {
    stream_buffer_.reserve(static_cast<std::size_t>(sample_frames) *
                           kReservedPeriods * frame_size_);
  }

  // SDL's device thread pulls data through this callback as the device
  // drains, so rendering is paced by the hardware itself.
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

void SdlAudioTransport::stop() {
  if (!initialized_) {
    return;
  }

  if (audio_stream_ != nullptr) {
    SDL_PauseAudioStreamDevice(audio_stream_);
    // Destroying the stream detaches the callback; SDL guarantees it is not
    // running once this returns.
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

void SDLCALL SdlAudioTransport::stream_callback(void *userdata,
                                                SDL_AudioStream *stream,
                                                int additional_amount,
                                                int total_amount) {
  if (auto *self = static_cast<SdlAudioTransport *>(userdata)) {
    self->handle_stream_callback(stream, additional_amount, total_amount);
  }
}

void SdlAudioTransport::handle_stream_callback(SDL_AudioStream *stream,
                                               int additional_amount,
                                               int total_amount) {
  if (!callback_ || stream == nullptr || frame_size_ == 0) {
    return;
  }

  const int available = SDL_GetAudioStreamAvailable(stream);
  int required = total_amount - available;
  if (required < additional_amount) {
    required = additional_amount;
  }
  if (required <= 0) {
    return;
  }

  const std::uint32_t bytes =
      align_to_frame(static_cast<std::uint32_t>(required), frame_size_);
  stream_buffer_.resize(bytes);

  const std::uint32_t produced =
      callback_(bytes, static_cast<void *>(stream_buffer_.data()));
  if (produced == 0) {
    return;
  }

  if (!SDL_PutAudioStreamData(audio_stream_, stream_buffer_.data(),
                              static_cast<int>(produced))) {
    std::cerr << "SDL_PutAudioStreamData failed: " << SDL_GetError()
              << std::endl;
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
