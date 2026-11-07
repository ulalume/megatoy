// The engine as the web's audio worklet runs it: a module of its own, with no
// Emscripten JavaScript around it, driven by dist/web_audio_worklet.js on the
// browser's audio thread. The page talks to it only through messages, which
// that script turns into the calls below.

#include "audio/audio_engine.hpp"
#include "platform/web/audio_worklet/worklet_protocol.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

#define WORKLET_EXPORT(name) extern "C" __attribute__((export_name(#name)))

namespace {

AudioEngine *engine = nullptr;
audio::AudioCommand inbox;
web_audio::Snapshot snapshot;
std::vector<int16_t> pcm;
std::vector<float> planar;

} // namespace

WORKLET_EXPORT(mt_protocol_version) uint32_t mt_protocol_version() {
  return web_audio::kProtocolVersion;
}

WORKLET_EXPORT(mt_init) int mt_init(uint32_t sample_rate) {
  if (engine == nullptr) {
    engine = new AudioEngine();
  }
  return engine->initialize(sample_rate) ? 1 : 0;
}

/// Where the page's command bytes are copied before mt_submit().
WORKLET_EXPORT(mt_inbox) audio::AudioCommand *mt_inbox() { return &inbox; }

WORKLET_EXPORT(mt_submit) int mt_submit() {
  return engine != nullptr && engine->submit(inbox) ? 1 : 0;
}

WORKLET_EXPORT(mt_set_note_options)
void mt_set_note_options(int use_velocity, int depth, int steal_oldest) {
  if (engine != nullptr) {
    engine->set_note_options(use_velocity != 0, static_cast<uint8_t>(depth),
                             steal_oldest != 0);
  }
}

WORKLET_EXPORT(mt_set_performance_options)
void mt_set_performance_options(int pitch_bend, int mod_wheel) {
  if (engine != nullptr) {
    engine->set_performance_options(pitch_bend != 0, mod_wheel != 0);
  }
}

/// Renders `frames` and returns them planar: the left channel, then the right.
WORKLET_EXPORT(mt_render) float *mt_render(uint32_t frames) {
  if (pcm.size() < frames * 2) {
    pcm.resize(frames * 2);
    planar.resize(frames * 2);
  }
  if (engine == nullptr ||
      engine->render(frames * 2 * sizeof(int16_t), pcm.data()) == 0) {
    std::memset(planar.data(), 0, frames * 2 * sizeof(float));
    return planar.data();
  }
  for (uint32_t i = 0; i < frames; ++i) {
    planar[i] = static_cast<float>(pcm[i * 2]) / 32768.0f;
    planar[frames + i] = static_cast<float>(pcm[i * 2 + 1]) / 32768.0f;
  }
  return planar.data();
}

/// What the page shows of the engine, as of the last block rendered.
WORKLET_EXPORT(mt_snapshot) const web_audio::Snapshot *mt_snapshot() {
  snapshot = {};
  if (engine != nullptr) {
    snapshot.voices = engine->voice_activity();
    const auto notes = engine->notes().published_notes();
    snapshot.note_count =
        static_cast<uint32_t>(std::min<std::size_t>(notes.size(), 6));
    for (uint32_t i = 0; i < snapshot.note_count; ++i) {
      snapshot.notes[i] = notes[i];
    }
  }
  return &snapshot;
}

WORKLET_EXPORT(mt_snapshot_size) uint32_t mt_snapshot_size() {
  return sizeof(web_audio::Snapshot);
}

WORKLET_EXPORT(mt_command_size) uint32_t mt_command_size() {
  return sizeof(audio::AudioCommand);
}
