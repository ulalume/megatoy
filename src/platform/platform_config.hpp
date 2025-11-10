#pragma once

namespace megatoy::platform {

enum class PlatformKind {
  Desktop,
  Web,
};

#if defined(__EMSCRIPTEN__)
constexpr PlatformKind kPlatform = PlatformKind::Web;
#define MEGATOY_PLATFORM_WEB 1
#else
constexpr PlatformKind kPlatform = PlatformKind::Desktop;
#define MEGATOY_PLATFORM_DESKTOP 1
#endif

constexpr bool is_desktop() { return kPlatform == PlatformKind::Desktop; }

constexpr bool is_web() { return kPlatform == PlatformKind::Web; }

} // namespace megatoy::platform
