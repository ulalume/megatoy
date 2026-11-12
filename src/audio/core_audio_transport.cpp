#include "audio/core_audio_transport.hpp"

#include <CoreAudio/CoreAudio.h>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>

namespace {

constexpr UInt32 kChannels = 2;
constexpr UInt32 kFrameBytes = sizeof(std::int16_t) * kChannels;

/// Interleaved signed 16-bit stereo, what the engine renders.
AudioStreamBasicDescription engine_format(std::uint32_t sample_rate) {
  AudioStreamBasicDescription format{};
  format.mSampleRate = sample_rate;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags =
      kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
  format.mChannelsPerFrame = kChannels;
  format.mBitsPerChannel = 16;
  format.mBytesPerFrame = kFrameBytes;
  format.mFramesPerPacket = 1;
  format.mBytesPerPacket = kFrameBytes;
  return format;
}

template <typename T>
T device_property(AudioObjectID device, AudioObjectPropertySelector selector,
                  AudioObjectPropertyScope scope) {
  const AudioObjectPropertyAddress address{selector, scope,
                                           kAudioObjectPropertyElementMain};
  T value{};
  UInt32 size = sizeof(value);
  AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &value);
  return value;
}

} // namespace

CoreAudioTransport::~CoreAudioTransport() { stop(); }

bool CoreAudioTransport::start(std::uint32_t sample_rate,
                               RenderCallback callback) {
  if (unit_ != nullptr) {
    return true;
  }
  if (!callback) {
    std::cerr << "Audio callback must not be empty\n";
    return false;
  }

  const AudioComponentDescription description{
      kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput,
      kAudioUnitManufacturer_Apple, 0, 0};
  AudioComponent component = AudioComponentFindNext(nullptr, &description);
  if (component == nullptr ||
      AudioComponentInstanceNew(component, &unit_) != noErr) {
    std::cerr << "No Core Audio output unit\n";
    unit_ = nullptr;
    return false;
  }

  callback_ = std::move(callback);
  const AudioStreamBasicDescription format = engine_format(sample_rate);
  const AURenderCallbackStruct render_callback{&CoreAudioTransport::render,
                                               this};
  const bool configured =
      AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat,
                           kAudioUnitScope_Input, 0, &format,
                           sizeof(format)) == noErr &&
      AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback,
                           kAudioUnitScope_Input, 0, &render_callback,
                           sizeof(render_callback)) == noErr &&
      AudioUnitInitialize(unit_) == noErr;
  if (!configured) {
    std::cerr << "Could not set up the Core Audio output unit\n";
    stop();
    return false;
  }

  use_buffer_frames();
  AudioUnitAddPropertyListener(unit_, kAudioOutputUnitProperty_CurrentDevice,
                               &CoreAudioTransport::device_changed, this);
  if (AudioOutputUnitStart(unit_) != noErr) {
    std::cerr << "Could not start the Core Audio output unit\n";
    stop();
    return false;
  }
  return true;
}

void CoreAudioTransport::stop() {
  if (unit_ == nullptr) {
    return;
  }
  // Once the unit is stopped and disposed of, the render callback is no
  // longer called, so the engine's callback can go.
  AudioUnitRemovePropertyListenerWithUserData(
      unit_, kAudioOutputUnitProperty_CurrentDevice,
      &CoreAudioTransport::device_changed, this);
  AudioOutputUnitStop(unit_);
  AudioUnitUninitialize(unit_);
  AudioComponentInstanceDispose(unit_);
  unit_ = nullptr;
  callback_ = nullptr;
}

OSStatus CoreAudioTransport::render(void *self, AudioUnitRenderActionFlags *,
                                    const AudioTimeStamp *, UInt32,
                                    UInt32 frames, AudioBufferList *data) {
  auto &transport = *static_cast<CoreAudioTransport *>(self);
  AudioBuffer &buffer = data->mBuffers[0];
  const std::uint32_t bytes = frames * kFrameBytes;
  const std::uint32_t produced =
      transport.callback_ ? transport.callback_(bytes, buffer.mData) : 0;
  // Silence where the engine had nothing, rather than what was there before.
  if (produced < bytes) {
    std::memset(static_cast<std::uint8_t *>(buffer.mData) + produced, 0,
                bytes - produced);
  }
  return noErr;
}

void CoreAudioTransport::device_changed(void *self, AudioUnit,
                                        AudioUnitPropertyID, AudioUnitScope,
                                        AudioUnitElement) {
  static_cast<const CoreAudioTransport *>(self)->use_buffer_frames();
}

void CoreAudioTransport::use_buffer_frames() const {
  const auto frames = static_cast<UInt32>(
      buffer_frames_ > 0 ? buffer_frames_ : kDefaultBufferFrames);
  AudioObjectID device = kAudioObjectUnknown;
  UInt32 size = sizeof(device);
  if (AudioUnitGetProperty(unit_, kAudioOutputUnitProperty_CurrentDevice,
                           kAudioUnitScope_Global, 0, &device,
                           &size) != noErr) {
    return;
  }
  const AudioObjectPropertyAddress buffer_size{
      kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal,
      kAudioObjectPropertyElementMain};
  AudioObjectSetPropertyData(device, &buffer_size, 0, nullptr, sizeof(frames),
                             &frames);

  // What a note waits for: the I/O buffer, and the device's own latency and
  // safety offset on top.
  const auto rate =
      device_property<Float64>(device, kAudioDevicePropertyNominalSampleRate,
                               kAudioObjectPropertyScopeGlobal);
  const auto running =
      device_property<UInt32>(device, kAudioDevicePropertyBufferFrameSize,
                              kAudioObjectPropertyScopeGlobal);
  const auto latency = device_property<UInt32>(
      device, kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput);
  const auto safety =
      device_property<UInt32>(device, kAudioDevicePropertySafetyOffset,
                              kAudioObjectPropertyScopeOutput);
  if (rate > 0.0) {
    std::cout << std::fixed << std::setprecision(1)
              << "Core Audio output: " << rate << " Hz, " << running
              << "-frame buffer, about "
              << (running + latency + safety) * 1000.0 / rate
              << " ms to the device\n";
  }
}
