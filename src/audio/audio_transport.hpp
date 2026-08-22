#pragma once

#include <cstdint>
#include <functional>

class AudioTransport {
public:
  using RenderCallback = std::function<std::uint32_t(std::uint32_t, void *)>;

  virtual ~AudioTransport() = default;

  virtual bool start(std::uint32_t sample_rate, RenderCallback callback) = 0;
  virtual void stop() = 0;
  virtual bool is_active() const = 0;

  /**
   * Frames per callback to ask the device for; 0 keeps the platform default.
   *
   * Only read when the device opens, so it takes effect on the next start()
   * and not on the one already running.
   */
  virtual void set_buffer_frames(int /*frames*/) {}

  /// Frames the device uses when set_buffer_frames() is left at 0.
  virtual int default_buffer_frames() const { return 0; }
};
