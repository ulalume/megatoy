#include "audio_manager.hpp"
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

  // Find suitable driver
  if (!find_suitable_driver()) {
    Audio_Deinit();
    return false;
  }

  // Initialize the selected driver
  ret_val = AudioDrv_Init(driver_index, &aud_drv);
  if (ret_val) {
    std::cerr << "AudioDrv_Init failed: " << (int)ret_val << std::endl;
    Audio_Deinit();
    return false;
  }

  // Configure audio options
  AUDIO_OPTS *opts = AudioDrv_GetOptions(aud_drv);
  opts->sampleRate = sample_rate;
  opts->numChannels = 2;
  opts->numBitsPerSmpl = 16;
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
    smpl_ptr_16[0] = smpl_data[0][cur_smpl];
    smpl_ptr_16[1] = smpl_data[1][cur_smpl];
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
  bool found_suitable_driver = false;

  std::cout << "Available audio drivers:\n";
  for (UINT32 i = 0; i < drv_count; i++) {
    Audio_GetDriverInfo(i, &drv_info);
    std::cout << "  " << i << ": " << drv_info->drvName << std::endl;

    // Prefer CoreAudio on macOS, avoid WaveWrite for real-time
    if (!found_suitable_driver) {
      if (strstr(drv_info->drvName, "CoreAudio") != nullptr ||
          strstr(drv_info->drvName, "ALSA") != nullptr ||
          strstr(drv_info->drvName, "PulseAudio") != nullptr) {
        driver_index = i;
        found_suitable_driver = true;
      } else if (strstr(drv_info->drvName, "WaveWrite") == nullptr) {
        // Use any driver except WaveWrite if we haven't found a preferred one
        driver_index = i;
      }
    }
  }

  Audio_GetDriverInfo(driver_index, &drv_info);
  std::cout << "Using audio driver: " << drv_info->drvName << std::endl;

  return true;
}
