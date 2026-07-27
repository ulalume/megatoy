#pragma once

#include <cstddef>

namespace audio {

/**
 * Pick where a scope window should start so the trace holds still.
 *
 * Without this the display simply shows the most recent N samples, so the
 * waveform slides sideways by however many samples arrived since the last
 * frame. Starting each window on a rising zero crossing locks it to the
 * signal's own period instead.
 *
 * Returns an index in [0, count - window_size]; falls back to the most recent
 * window when the signal is too quiet or has no usable crossing.
 */
std::size_t find_trigger(const float *samples, std::size_t count,
                         std::size_t window_size);

} // namespace audio
