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
};
