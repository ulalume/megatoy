#include "audio/audio_manager.hpp"
#include "ym2612/channel.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>

AudioManager::AudioManager()
    : aud_drv_(nullptr), driver_index_(0), smpl_size_(0), smpl_alloc_(0),
      sample_rate_(44100), device_(), wave_sampler_(), initialized_(false),
      running_(false) {}

AudioManager::~AudioManager() { shutdown(); }

bool AudioManager::initialize(UINT32 sample_rate) {
  if (initialized_) {
    return true;
  }

  sample_rate_ = sample_rate;

  // Initialize audio system
  UINT8 ret_val = Audio_Init();
  if (ret_val) {
    std::cerr << "Audio_Init failed: " << (int)ret_val << std::endl;
    return false;
  }

  // Find suitable driver candidates
  if (!find_suitable_driver()) {
    Audio_Deinit();
    return false;
  }

  AUDDRV_INFO *drv_info = nullptr;
  bool driver_initialized = false;
  for (UINT32 candidate : driver_order_) {
    ret_val = AudioDrv_Init(candidate, &aud_drv_);
    if (ret_val == 0) {
      driver_index_ = candidate;
      Audio_GetDriverInfo(driver_index_, &drv_info);
      driver_initialized = true;
      break;
    }
    std::cerr << "AudioDrv_Init failed for driver " << candidate << ": "
              << (int)ret_val << std::endl;
  }

  if (!driver_initialized || drv_info == nullptr) {
    Audio_Deinit();
    return false;
  }

  std::cout << "Using audio driver: " << drv_info->drvName << std::endl;

  // Configure audio options
  AUDIO_OPTS *opts = AudioDrv_GetOptions(aud_drv_);
  const bool allow_custom_sample_rate =
      !(drv_info->drvName != nullptr &&
        strstr(drv_info->drvName, "WASAPI") != nullptr);

  if (allow_custom_sample_rate && sample_rate != 0) {
    opts->sampleRate = sample_rate;
  }
  opts->numChannels = 2;
  opts->numBitsPerSmpl = 16;

  sample_rate_ = opts->sampleRate;
  smpl_size_ = opts->numChannels * opts->numBitsPerSmpl / 8;

  // Initialize YM2612 device
  wave_sampler_.clear();
  device_.stop();
  device_.init(sample_rate_);

  // Set up audio callback with YM2612 device integration
  AudioDrv_SetCallback(aud_drv_, fill_buffer_static, this);

  if (sample_rate_ != sample_rate) {
    std::cout << "Audio sample rate set to " << sample_rate_ << " Hz\n";
  }

  // Start audio streaming
  ret_val = AudioDrv_Start(aud_drv_, 0);
  if (ret_val) {
    std::cerr << "AudioDrv_Start failed: " << (int)ret_val << std::endl;
    if (aud_drv_) {
      AudioDrv_Deinit(&aud_drv_);
      aud_drv_ = nullptr;
    }
    Audio_Deinit();
    return false;
  }

  // Allocate sample buffers
  smpl_alloc_ = AudioDrv_GetBufferSize(aud_drv_) / smpl_size_;
  smpl_data_[0].resize(smpl_alloc_);
  smpl_data_[1].resize(smpl_alloc_);

  initialized_ = true;
  running_ = true;
  return true;
}

void AudioManager::shutdown() {
  if (!initialized_) {
    return;
  }

  if (running_ && aud_drv_) {
    AudioDrv_Stop(aud_drv_);
  }

  device_.stop();
  wave_sampler_.clear();

  if (aud_drv_) {
    AudioDrv_Deinit(&aud_drv_);
    aud_drv_ = nullptr;
  }

  Audio_Deinit();

  // Clear sample buffers
  smpl_data_[0].clear();
  smpl_data_[1].clear();

  driver_order_.clear();

  initialized_ = false;
  running_ = false;
}

void AudioManager::apply_patch_to_all_channels(const ym2612::Patch &patch) {
  device_.write_settings(patch.global);
  for (ym2612::ChannelIndex channel_index : ym2612::all_channel_indices) {
    auto channel = device_.channel(channel_index);
    channel.write_settings(patch.channel);
    channel.write_instrument(patch.instrument);
  }
}

UINT32 AudioManager::fill_buffer_static(void *drv_struct, void *user_param,
                                        UINT32 buf_size, void *data) {
  AudioManager *self = static_cast<AudioManager *>(user_param);
  return self->fill_buffer(buf_size, data);
}

UINT32 AudioManager::fill_buffer(UINT32 buf_size, void *data) {
  if (!running_) {
    memset(data, 0x00, buf_size);
    return buf_size;
  }

  UINT32 smpl_count = buf_size / smpl_size_;

  // Clear sample buffers
  std::fill_n(smpl_data_[0].data(), smpl_count, DEV_SMPL{});
  std::fill_n(smpl_data_[1].data(), smpl_count, DEV_SMPL{});

  // Generate audio samples from YM2612 device
  std::array<DEV_SMPL *, 2> outputs{smpl_data_[0].data(), smpl_data_[1].data()};
  device_.update(smpl_count, outputs);
  wave_sampler_.push_samples(outputs[0], outputs[1], smpl_count);

  // Convert to output format (16-bit stereo)
  INT16 *smpl_ptr_16 = (INT16 *)data;
  for (UINT32 cur_smpl = 0; cur_smpl < smpl_count;
       cur_smpl++, smpl_ptr_16 += 2) {
    smpl_ptr_16[0] = std::clamp(smpl_data_[0][cur_smpl], -32768, 32767);
    smpl_ptr_16[1] = std::clamp(smpl_data_[1][cur_smpl], -32768, 32767);
  }

  return buf_size;
}

bool AudioManager::find_suitable_driver() {
  UINT32 drv_count = Audio_GetDriverCount();
  if (!drv_count) {
    std::cerr << "No audio drivers available\n";
    return false;
  }

  AUDDRV_INFO *drv_info;
  driver_order_.clear();

  struct Candidate {
    int priority;
    UINT32 index;
  };

  auto compute_priority = [](const char *name) {
    if (name == nullptr) {
      return 50;
    }
    if (strstr(name, "CoreAudio") != nullptr ||
        strstr(name, "ALSA") != nullptr) {
      return 0;
    }
    if (strstr(name, "PulseAudio") != nullptr ||
        strstr(name, "PipeWire") != nullptr) {
      return 1;
    }
    if (strstr(name, "XAudio2") != nullptr) {
      return 1;
    }
    if (strstr(name, "WinMM") != nullptr) {
      return 2;
    }
    if (strstr(name, "DirectSound") != nullptr) {
      return 3;
    }
    if (strstr(name, "WASAPI") != nullptr) {
      return 4;
    }
    if (strstr(name, "WaveWrite") != nullptr) {
      return 100;
    }
    return 10;
  };

  std::vector<Candidate> candidates;
  candidates.reserve(drv_count);

  std::cout << "Available audio drivers:\n";
  for (UINT32 i = 0; i < drv_count; i++) {
    Audio_GetDriverInfo(i, &drv_info);
    std::cout << "  " << i << ": " << drv_info->drvName << std::endl;
    candidates.push_back({compute_priority(drv_info->drvName), i});
  }

  std::sort(candidates.begin(), candidates.end(),
            [](const Candidate &lhs, const Candidate &rhs) {
              if (lhs.priority == rhs.priority) {
                return lhs.index < rhs.index;
              }
              return lhs.priority < rhs.priority;
            });

  for (const Candidate &candidate : candidates) {
    driver_order_.push_back(candidate.index);
  }

  if (driver_order_.empty()) {
    return false;
  }

  driver_index_ = driver_order_.front();
  return true;
}
