#include "ym2612/device.hpp"
#include "ym2612/channel.hpp"
#include "ym2612/nuked_chip.hpp"
#include "ym2612/ymfm_chip.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <vector>

extern "C" {
#include "audio/resampler/Resampler.h"
}

namespace ym2612 {

namespace {

// The resampler applies an 8.8 fixed-point gain, so a full-scale chip sample
// (Chip::kFullScale) comes out as kFullScale * kResamplerVolume.
constexpr uint16_t kResamplerVolume = 0x100;
constexpr float kInverseFullScale =
    1.0f / (static_cast<float>(Chip::kFullScale) * kResamplerVolume);

} // namespace

struct Device::Resampler {
  RESMPL_STATE state{};
  bool initialized = false;
  std::vector<WAVE_32BS> scratch;

  ~Resampler() { deinit(); }

  void deinit() {
    if (initialized) {
      Resmpl_Deinit(&state);
      initialized = false;
    }
    state = {};
  }

  void init(Chip &chip, uint32_t destination_rate) {
    deinit();
    state.smpRateSrc = chip.native_sample_rate();
    state.StreamUpdate = &Chip::stream_update;
    state.su_DataPtr = &chip;
    Resmpl_SetVals(&state, 0xFF, kResamplerVolume, destination_rate);
    Resmpl_Init(&state);
    initialized = true;
  }

  /// Point the callback at another core. Every core runs at the same native
  /// rate, so nothing the resampler was set up with changes.
  void rebind(Chip &chip) {
    assert(!initialized || state.smpRateSrc == chip.native_sample_rate());
    state.su_DataPtr = &chip;
  }
};

Device::Device() = default;
Device::~Device() { stop(); }

void Device::init(uint32_t sample_rate) {
  stop();
  if (sample_rate == 0) {
    return;
  }

  sample_rate_ = sample_rate;
  nuked_chip_ = std::make_unique<NukedChip>(kClock);
  ymfm_chip_ = std::make_unique<YmfmChip>(kClock);
  nuked_chip_->set_chip_type(chip_type_);
  ymfm_chip_->set_chip_type(chip_type_);
  chip_ = active_chip();
  resampler_ = std::make_unique<Resampler>();
  resampler_->init(*chip_, sample_rate_);
  lowpass_.init(sample_rate_);
}

void Device::stop() {
  resampler_.reset();
  chip_ = nullptr;
  ymfm_chip_.reset();
  nuked_chip_.reset();
  sample_rate_ = 0;
}

Chip *Device::active_chip() {
  return core_type_ == CoreType::Nuked ? nuked_chip_.get() : ymfm_chip_.get();
}

uint32_t Device::native_sample_rate() const {
  return chip_ ? chip_->native_sample_rate() : 0;
}

void Device::set_chip_type(ChipType type) {
  if (chip_ && chip_->chip_type() != type) {
    // An inactive chip must not retain keyed envelopes that can resume when
    // it is selected again.
    chip_->reset();
  }
  chip_type_ = type;
  // Both cores follow, so whichever is selected next is already on the
  // chip the caller asked for.
  if (nuked_chip_) {
    nuked_chip_->set_chip_type(type);
  }
  if (ymfm_chip_) {
    ymfm_chip_->set_chip_type(type);
  }
}

void Device::set_core_type(CoreType type) {
  if (chip_ && core_type_ != type) {
    // An inactive core must not retain keyed envelopes that can resume when
    // it is selected again, nor bus writes it never got to clock in.
    chip_->reset();
  }
  core_type_ = type;
  if (!chip_) {
    return;
  }
  chip_ = active_chip();
  if (resampler_) {
    resampler_->rebind(*chip_);
  }
}

void Device::write(uint8_t reg, uint8_t data, bool port) {
  if (!chip_) {
    return;
  }
  const uint8_t offset = static_cast<uint8_t>(static_cast<uint8_t>(port) << 1);
  chip_->write(offset, reg);                            // register address
  chip_->write(static_cast<uint8_t>(offset + 1), data); // data payload
}

void Device::write_settings(const GlobalSettings &settings) {
  write(0x2B, static_cast<uint8_t>(settings.dac_enable << 7));
  write(0x22, static_cast<uint8_t>(settings.lfo_enable << 3 |
                                   settings.lfo_frequency));
}

void Device::render(uint32_t frames, float *out) {
  if (out == nullptr || frames == 0) {
    return;
  }
  if (!chip_ || !resampler_ || !resampler_->initialized) {
    std::memset(out, 0, static_cast<size_t>(frames) * 2 * sizeof(float));
    return;
  }

  // The vendored libvgm resampler holds only one second of source samples
  // and its header carries an unresolved TODO about larger blocks; feeding a
  // bigger request in one Resmpl_Execute call overwrites the buffer end
  // (heap corruption -- aborts under glibc/MSVC, silent on macOS). Chunking
  // here keeps requests far below that limit for any sane rate ratio. The
  // low-pass filter is sequential per sample, so chunking is transparent.
  constexpr uint32_t kMaxChunkFrames = 4096;

  auto &scratch = resampler_->scratch;
  if (scratch.size() < kMaxChunkFrames) {
    scratch.resize(kMaxChunkFrames);
  }

  uint32_t done = 0;
  while (done < frames) {
    const uint32_t chunk = std::min(frames - done, kMaxChunkFrames);
    // Resmpl_Execute accumulates into the buffer, so it must start silent.
    std::memset(scratch.data(), 0,
                static_cast<size_t>(chunk) * sizeof(WAVE_32BS));

    Resmpl_Execute(&resampler_->state, chunk, scratch.data());

    for (uint32_t i = 0; i < chunk; ++i) {
      lowpass_.apply(scratch[i].L, scratch[i].R);
      out[(done + i) * 2 + 0] =
          static_cast<float>(scratch[i].L) * kInverseFullScale;
      out[(done + i) * 2 + 1] =
          static_cast<float>(scratch[i].R) * kInverseFullScale;
    }
    done += chunk;
  }
}

Channel Device::channel(ChannelIndex idx) { return Channel(*this, idx); }

} // namespace ym2612
