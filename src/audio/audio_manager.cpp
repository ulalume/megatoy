#include "audio/audio_manager.hpp"
#include "ym2612/channel.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>

namespace {

constexpr Uint32 kFallbackSampleRate = 44100;
constexpr Uint32 kFallbackFrameSize = sizeof(INT16) * 2; // stereo s16

Uint32 align_to_frame(Uint32 bytes, Uint32 frame_size) {
  if (frame_size == 0) {
    return bytes;
  }
  const Uint32 remainder = bytes % frame_size;
  if (remainder == 0) {
    return bytes;
  }
  return bytes + (frame_size - remainder);
}

} // namespace

AudioManager::AudioManager()
    : audio_stream_(nullptr), owns_audio_subsystem_(false), smpl_size_(0),
      smpl_alloc_(0), sample_rate_(kFallbackSampleRate), device_(),
      wave_sampler_(), initialized_(false), running_(false) {}

AudioManager::~AudioManager() { shutdown(); }

bool AudioManager::initialize(UINT32 sample_rate) {
  if (initialized_) {
    return true;
  }

  sample_rate_ = sample_rate != 0 ? sample_rate : kFallbackSampleRate;

  if ((SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) == 0) {
    if (!SDL_Init(SDL_INIT_AUDIO)) {
      std::cerr << "Failed to initialize SDL audio: " << SDL_GetError()
                << std::endl;
      return false;
    }
    owns_audio_subsystem_ = true;
  }

  SDL_AudioSpec desired{};
  desired.freq = static_cast<int>(sample_rate_);
  desired.channels = 2;
  desired.format = SDL_AUDIO_S16;

  audio_stream_ = SDL_OpenAudioDeviceStream(
      SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, nullptr, nullptr);
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

  smpl_size_ = SDL_AUDIO_FRAMESIZE(desired);
  if (smpl_size_ == 0) {
    smpl_size_ = kFallbackFrameSize;
  }

  smpl_alloc_ = 0;
  target_buffer_bytes_ =
      std::max<Uint32>(smpl_size_ * (sample_rate_ / 40), smpl_size_ * 8);

  wave_sampler_.clear();
  device_.stop();
  device_.init(sample_rate_);

  running_ = true;
  initialized_ = true;

  if (!SDL_ResumeAudioStreamDevice(audio_stream_)) {
    std::cerr << "Failed to start SDL audio device: " << SDL_GetError()
              << std::endl;
  }

  audio_thread_alive_ = true;
  audio_thread_ = std::jthread([this](std::stop_token token) {
    audio_thread_func(token);
  });

  return true;
}

void AudioManager::shutdown() {
  if (!initialized_) {
    return;
  }

  running_ = false;

  audio_thread_alive_ = false;
  if (audio_thread_.joinable()) {
    audio_thread_.request_stop();
    audio_thread_.join();
  }

  if (audio_stream_ != nullptr) {
    SDL_PauseAudioStreamDevice(audio_stream_);
    SDL_DestroyAudioStream(audio_stream_);
    audio_stream_ = nullptr;
  }

  device_.stop();
  wave_sampler_.clear();

  smpl_data_[0].clear();
  smpl_data_[1].clear();
  stream_buffer_.clear();
  smpl_alloc_ = 0;

  if (owns_audio_subsystem_) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    owns_audio_subsystem_ = false;
  }

  initialized_ = false;
}

void AudioManager::apply_patch_to_all_channels(const ym2612::Patch &patch) {
  device_.write_settings(patch.global);
  for (ym2612::ChannelIndex channel_index : ym2612::all_channel_indices) {
    auto channel = device_.channel(channel_index);
    channel.write_settings(patch.channel);
    channel.write_instrument(patch.instrument);
  }
}

void AudioManager::audio_thread_func(std::stop_token stop_token) {
  while (!stop_token.stop_requested()) {
    if (running_ && audio_stream_ != nullptr) {
      pump_audio_stream();
    } else {
      SDL_Delay(5);
    }
  }
}

void AudioManager::pump_audio_stream() {
  if (!running_ || audio_stream_ == nullptr || smpl_size_ == 0) {
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

    Uint32 deficit =
        target_buffer_bytes_ - static_cast<Uint32>(available);
    deficit = align_to_frame(deficit, smpl_size_);
    if (deficit == 0) {
      deficit = smpl_size_;
    }

    const size_t samples_needed = deficit / sizeof(INT16);
    stream_buffer_.resize(samples_needed);

    const UINT32 produced =
        fill_buffer(deficit, static_cast<void *>(stream_buffer_.data()));
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

UINT32 AudioManager::fill_buffer(UINT32 buf_size, void *data) {
  if (smpl_size_ == 0) {
    return 0;
  }

  if (!running_) {
    std::memset(data, 0x00, buf_size);
    return buf_size;
  }

  const UINT32 smpl_count = buf_size / smpl_size_;
  if (smpl_count == 0) {
    return 0;
  }

  if (smpl_count > smpl_alloc_) {
    smpl_alloc_ = smpl_count;
    smpl_data_[0].resize(smpl_alloc_);
    smpl_data_[1].resize(smpl_alloc_);
  }

  // Clear sample buffers
  std::fill_n(smpl_data_[0].data(), smpl_count, DEV_SMPL{});
  std::fill_n(smpl_data_[1].data(), smpl_count, DEV_SMPL{});

  // Generate audio samples from YM2612 device
  std::array<DEV_SMPL *, 2> outputs{smpl_data_[0].data(), smpl_data_[1].data()};
  device_.update(smpl_count, outputs);
  wave_sampler_.push_samples(outputs[0], outputs[1], smpl_count);

  // Convert to output format (16-bit stereo)
  auto *smpl_ptr_16 = static_cast<INT16 *>(data);
  for (UINT32 cur_smpl = 0; cur_smpl < smpl_count;
       cur_smpl++, smpl_ptr_16 += 2) {
    smpl_ptr_16[0] = std::clamp(smpl_data_[0][cur_smpl], -32768, 32767);
    smpl_ptr_16[1] = std::clamp(smpl_data_[1][cur_smpl], -32768, 32767);
  }

  return smpl_count * smpl_size_;
}
