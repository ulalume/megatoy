#pragma once

#include "app_state.hpp"
#include "channel_allocator.hpp"
#include "ym2612/types.hpp"
#include <cstdint>
#include <imgui.h>

namespace ui {

/**
 * The notes the chip is sounding, as the envelope graph draws them: newest
 * first, timed against the audio thread's own clock.
 *
 * Gathered once a frame and handed to every operator, so all four graphs
 * agree about what instant they are drawing.
 */
struct EnvelopeVoices {
  struct Voice {
    /// The allocator's own key-on counter, which is what tells one note from
    /// the next on the same channel. Zero for a channel that never sounded.
    uint64_t sequence = 0;
    int midi_note = 0;
    double since_key_on_ms = 0.0;
    /// Negative while the key is still down.
    double since_key_off_ms = -1.0;
    /// 1 for the newest voice, progressively less for the ones before it.
    float recency = 1.0f;
  };
  int count = 0;
  Voice items[6]{};
};

/// Turn the audio thread's publication into elapsed times and a recency
/// order. Voices whose release finished long ago are dropped here.
EnvelopeVoices collect_envelope_voices(const VoiceActivityFrame &frame);

void render_envelope_image(const ym2612::OperatorSettings &op,
                           const UIState::EnvelopeState &state, ImVec2 size,
                           const EnvelopeVoices &voices);
} // namespace ui
