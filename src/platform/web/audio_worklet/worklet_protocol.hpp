#pragma once

#include "channel_allocator.hpp"
#include "ym2612/note.hpp"

#include <array>
#include <cstdint>
#include <type_traits>

/**
 * What the page and the web's audio worklet pass each other. Both modules are
 * built from these headers by the same compiler, so commands and snapshots
 * travel as their bytes; the sizes are checked against each other when the
 * worklet starts.
 */
namespace web_audio {

/// Bumped whenever a command or the snapshot changes shape.
inline constexpr uint32_t kProtocolVersion = 1;

/// Frames the worklet renders between two reports to the page.
inline constexpr uint32_t kReportFrames = 512;

/// Frames whose rendering time the worklet sums before reporting it: its
/// clock may tick only every millisecond, a twelfth of one report.
inline constexpr uint32_t kLoadFrames = 2048;

/// The engine's state the page draws from.
struct Snapshot {
  VoiceActivityFrame voices{};
  std::array<ym2612::Note, 6> notes{};
  uint32_t note_count = 0;
};

static_assert(std::is_trivially_copyable_v<Snapshot>);

} // namespace web_audio
