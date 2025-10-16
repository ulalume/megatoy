#include "audio/audio_manager.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>

AudioManager::AudioManager()
    : aud_drv(nullptr), driver_index(0), smpl_size(0), smpl_alloc(0),
      sample_rate(44100), callback(nullptr), initialized(false),
      running(false) {}

AudioManager::~AudioManager() { shutdown(); }

bool AudioManager::init(UINT32 sample_rate_param) {
  if (initialized) {
    return true;
  }

  sample_rate = sample_rate_param;

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
    ret_val = AudioDrv_Init(candidate, &aud_drv);
    if (ret_val == 0) {
      driver_index = candidate;
      Audio_GetDriverInfo(driver_index, &drv_info);
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
  AUDIO_OPTS *opts = AudioDrv_GetOptions(aud_drv);
  const bool allow_custom_sample_rate =
      !(drv_info->drvName != nullptr &&
        strstr(drv_info->drvName, "WASAPI") != nullptr);

  if (allow_custom_sample_rate && sample_rate_param != 0) {
    opts->sampleRate = sample_rate_param;
  }
  opts->numChannels = 2;
  opts->numBitsPerSmpl = 16;

  sample_rate = opts->sampleRate;
  smpl_size = opts->numChannels * opts->numBitsPerSmpl / 8;

  // Set up audio callback
  AudioDrv_SetCallback(aud_drv, fill_buffer_static, this);

  initialized = true;
  return true;
}

void AudioManager::shutdown() {
  if (!initialized) {
    return;
  }

  stop();

  if (aud_drv) {
    AudioDrv_Deinit(&aud_drv);
    aud_drv = nullptr;
  }

  Audio_Deinit();

  // Clear sample buffers
  smpl_data[0].clear();
  smpl_data[1].clear();

  driver_order_.clear();

  initialized = false;
}

bool AudioManager::start() {
  if (!initialized || running) {
    return running;
  }

  UINT8 ret_val = AudioDrv_Start(aud_drv, 0);
  if (ret_val) {
    std::cerr << "AudioDrv_Start failed: " << (int)ret_val << std::endl;
    return false;
  }

  // Allocate sample buffers
  smpl_alloc = AudioDrv_GetBufferSize(aud_drv) / smpl_size;
  smpl_data[0].resize(smpl_alloc);
  smpl_data[1].resize(smpl_alloc);

  running = true;
  return true;
}

void AudioManager::stop() {
  if (!running) {
    return;
  }

  if (aud_drv) {
    AudioDrv_Stop(aud_drv);
  }

  running = false;
}

void AudioManager::set_callback(AudioCallback callback_param) {
  callback = std::move(callback_param);
}

void AudioManager::clear_callback() { callback = nullptr; }

UINT32 AudioManager::fill_buffer_static(void *drv_struct, void *user_param,
                                        UINT32 buf_size, void *data) {
  AudioManager *self = static_cast<AudioManager *>(user_param);
  return self->fill_buffer(buf_size, data);
}

UINT32 AudioManager::fill_buffer(UINT32 buf_size, void *data) {
  if (!running || !callback) {
    memset(data, 0x00, buf_size);
    return buf_size;
  }

  UINT32 smpl_count = buf_size / smpl_size;

  // Clear sample buffers
  std::fill_n(smpl_data[0].data(), smpl_count, DEV_SMPL{});
  std::fill_n(smpl_data[1].data(), smpl_count, DEV_SMPL{});

  // Generate audio samples via callback
  std::array<DEV_SMPL *, 2> outs{smpl_data[0].data(), smpl_data[1].data()};
  callback(smpl_count, outs);

  // Convert to output format (16-bit stereo)
  INT16 *smpl_ptr_16 = (INT16 *)data;
  for (UINT32 cur_smpl = 0; cur_smpl < smpl_count;
       cur_smpl++, smpl_ptr_16 += 2) {
    smpl_ptr_16[0] = std::clamp(smpl_data[0][cur_smpl], -32768, 32767);
    smpl_ptr_16[1] = std::clamp(smpl_data[1][cur_smpl], -32768, 32767);
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

  driver_index = driver_order_.front();
  return true;
}
