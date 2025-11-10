#include "audio/audio_engine.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include "ym2612/channel.hpp"

namespace {

constexpr UINT32 kFallbackSampleRate = 44100;
constexpr UINT32 kDefaultFrameSize = sizeof(INT16) * 2; // stereo s16

} // namespace

AudioEngine::AudioEngine()
    : sample_rate_(kFallbackSampleRate), frame_size_(kDefaultFrameSize),
      sample_capacity_(0), running_(false) {}

bool AudioEngine::initialize(UINT32 sample_rate) {
  sample_rate_ = sample_rate != 0 ? sample_rate : kFallbackSampleRate;
  frame_size_ = kDefaultFrameSize;
  sample_capacity_ = 0;
  smpl_data_[0].clear();
  smpl_data_[1].clear();
  wave_sampler_.clear();
  device_.stop();
  device_.init(sample_rate_);
  running_ = true;
  return true;
}

void AudioEngine::shutdown() {
  running_ = false;
  wave_sampler_.clear();
  smpl_data_[0].clear();
  smpl_data_[1].clear();
  sample_capacity_ = 0;
  device_.stop();
}

UINT32 AudioEngine::render(UINT32 buf_size, void *data) {
  if (!running_ || frame_size_ == 0 || buf_size == 0) {
    if (data != nullptr && buf_size != 0) {
      std::memset(data, 0x00, buf_size);
    }
    return 0;
  }

  const UINT32 smpl_count = buf_size / frame_size_;
  if (smpl_count == 0) {
    return 0;
  }

  ensure_sample_storage(smpl_count);

  std::fill_n(smpl_data_[0].data(), smpl_count, DEV_SMPL{});
  std::fill_n(smpl_data_[1].data(), smpl_count, DEV_SMPL{});

  std::array<DEV_SMPL *, 2> outputs{smpl_data_[0].data(),
                                    smpl_data_[1].data()};
  device_.update(smpl_count, outputs);
  wave_sampler_.push_samples(outputs[0], outputs[1], smpl_count);

  auto *smpl_ptr_16 = static_cast<INT16 *>(data);
  for (UINT32 cur_smpl = 0; cur_smpl < smpl_count;
       cur_smpl++, smpl_ptr_16 += 2) {
    smpl_ptr_16[0] = std::clamp(smpl_data_[0][cur_smpl], -32768, 32767);
    smpl_ptr_16[1] = std::clamp(smpl_data_[1][cur_smpl], -32768, 32767);
  }

  return smpl_count * frame_size_;
}

void AudioEngine::apply_patch_to_all_channels(const ym2612::Patch &patch) {
  device_.write_settings(patch.global);
  for (ym2612::ChannelIndex channel_index : ym2612::all_channel_indices) {
    auto channel = device_.channel(channel_index);
    channel.write_settings(patch.channel);
    channel.write_instrument(patch.instrument);
  }
}

UINT32 AudioEngine::ensure_sample_storage(UINT32 smpl_count) {
  if (smpl_count > sample_capacity_) {
    sample_capacity_ = smpl_count;
    smpl_data_[0].resize(sample_capacity_);
    smpl_data_[1].resize(sample_capacity_);
  }
  return sample_capacity_;
}
