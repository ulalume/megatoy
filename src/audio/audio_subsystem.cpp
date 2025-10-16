#include "audio_subsystem.hpp"

#include "ym2612/channel.hpp"

#include <array>

AudioSubsystem::AudioSubsystem()
    : audio_manager_(), device_(), wave_sampler_(),
      runtime_(audio_manager_, device_, wave_sampler_) {}

bool AudioSubsystem::initialize(UINT32 sample_rate) {
  return runtime_.start(sample_rate);
}

void AudioSubsystem::shutdown() { runtime_.stop(); }

void AudioSubsystem::apply_patch_to_all_channels(const ym2612::Patch &patch) {
  device_.write_settings(patch.global);
  for (ym2612::ChannelIndex channel_index : ym2612::all_channel_indices) {
    auto channel = device_.channel(channel_index);
    channel.write_settings(patch.channel);
    channel.write_instrument(patch.instrument);
  }
}
